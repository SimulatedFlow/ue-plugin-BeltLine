// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "BeltTypes.h"
#include "BeltSubsystem.generated.h"

class ABeltActor;
class ABeltNode;
class UBeltItemType;
class UInstancedStaticMeshComponent;
class UStaticMesh;

namespace BeltLine
{
	/**
	 * Per-instance custom data the item batches carry:
	 *   0 - progress along the belt, 0..1
	 *   1 - jam flag, 1 while the item is being held up
	 *   2,3,4 - the item type's colour
	 *
	 * The jam flag is the one that matters. It lets one material tint a stalled item without the CPU
	 * touching a material instance or a component, which is how backpressure becomes something you can
	 * see in a video rather than a claim in a description.
	 */
	static constexpr int32 NumCustomDataFloats = 5;
	static constexpr int32 CustomDataIndex_Progress = 0;
	static constexpr int32 CustomDataIndex_Jammed = 1;
	static constexpr int32 CustomDataIndex_ColorR = 2;
	static constexpr int32 CustomDataIndex_ColorG = 3;
	static constexpr int32 CustomDataIndex_ColorB = 4;

	/** Scale a released instance slot is collapsed to. Small enough to vanish, never exactly zero. */
	static constexpr float HiddenInstanceScale = 1.0e-4f;
}

/**
 * One item type's instance set: the component that draws it, and the scratch it is written from.
 *
 * The scratch arrays are the reason the update allocates nothing in steady state. They are Reset every
 * update - which keeps the allocation and only moves a counter - and refilled, so after the first few
 * seconds the high-water mark is reached and no further memory is asked for however long the world runs.
 */
struct FBeltInstanceBatch
{
	/** The item type this set draws. */
	TWeakObjectPtr<UBeltItemType> ItemType;

	/** One component, one draw call, however many items of this type are in the world. */
	TObjectPtr<UInstancedStaticMeshComponent> Component;

	/** Transforms for this update, gathered across every belt. */
	TArray<FTransform> ScratchTransforms;

	/** Custom data for this update, NumCustomDataFloats per instance. */
	TArray<float> ScratchCustomData;

	/** Instances written last update, so shrinking sets get their leftover slots collapsed. */
	int32 LastWrittenCount = 0;

	/** Where the last live instance was, used to park collapsed slots without inflating the bounds. */
	FVector LastLivePosition = FVector::ZeroVector;

	// The item type's presentation, copied out once per update rather than read per item. Ten thousand
	// items would otherwise mean ten thousand trips through a UObject pointer for four values that are
	// the same every time.
	FQuat CachedMeshRotation = FQuat::Identity;
	FVector CachedMeshOffset = FVector::ZeroVector;
	FVector CachedMeshScale = FVector(1.0f, 1.0f, 1.0f);
	float CachedColor[3] = { 1.0f, 1.0f, 1.0f };
};

/**
 * Owns the belts, the nodes, the item types, the instance sets, the budget and the numbers for a world.
 *
 * One update is four passes and none of them is per-item-object:
 *
 *   1. Belts advance. One walk per belt over a flat float array - add the step, clamp against the item
 *      in front - and whatever reached the end is handed to a node, to the next belt, or consumed.
 *   2. Nodes drain into their output belts, after every belt has moved, so an item can never cross two
 *      belts in one update because of the order the actors happen to be in.
 *   3. Transforms are gathered. Each item's position comes from its belt's baked path table: two array
 *      lookups and a lerp, not a spline evaluation.
 *   4. Each item type's transforms go into its instance set in one bulk write with the render state
 *      left alone, so the instance data manager streams the deltas and no proxy is recreated.
 *
 * The whole thing is bounded by the item budget, which is a hard ceiling rather than a target: spawns
 * past it fail and are counted, and lowering it trims the world back down without giving up a single
 * instance slot - which is why the draw call count does not move when it does.
 */
UCLASS()
class BELTLINE_API UBeltSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// USubsystem interface
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	// FTickableGameObject interface
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableInEditor() const override;

	/** Convenience getter. Returns null outside a world that supports the subsystem. */
	static UBeltSubsystem* Get(const UObject* WorldContextObject);

	// ------------------------------------------------------------------------------------------------
	// Registration
	// ------------------------------------------------------------------------------------------------

	/** Called by the belt itself when its components register. Safe to call twice. */
	void RegisterBelt(ABeltActor* Belt);

	/** Called by the belt itself when its components unregister. Safe to call for an unknown belt. */
	void UnregisterBelt(ABeltActor* Belt);

	/** Called by the node itself when its components register. Safe to call twice. */
	void RegisterNode(ABeltNode* Node);

	/** Called by the node itself when its components unregister. Safe to call for an unknown node. */
	void UnregisterNode(ABeltNode* Node);

	/** Belts registered in this world. */
	UFUNCTION(BlueprintPure, Category = "BeltLine")
	int32 GetBeltCount() const { return Belts.Num(); }

	/** Nodes registered in this world. */
	UFUNCTION(BlueprintPure, Category = "BeltLine")
	int32 GetNodeCount() const { return Nodes.Num(); }

	/** Every belt in this world, in registration order. */
	UFUNCTION(BlueprintPure, Category = "BeltLine")
	TArray<ABeltActor*> GetBelts() const;

	// ------------------------------------------------------------------------------------------------
	// Item types
	// ------------------------------------------------------------------------------------------------

	/**
	 * Take an item type on and give it an index, creating its instance set the first time.
	 *
	 * Registration is what costs a draw call, so it happens when a type is first actually used and not
	 * before. Returns INDEX_NONE when the type is null or the world is already at MaxItemTypes.
	 */
	int32 RegisterItemType(UBeltItemType* ItemType);

	/** Index of an already registered type, or INDEX_NONE. */
	int32 FindItemTypeIndex(const UBeltItemType* ItemType) const;

	/** The type at an index, or null. */
	UFUNCTION(BlueprintPure, Category = "BeltLine|Items")
	UBeltItemType* GetItemTypeByIndex(int32 TypeIndex) const;

	/**
	 * One of the four code types, built on first use and kept for the lifetime of the world.
	 * A fresh project can put something on a belt with this and no content at all.
	 */
	UFUNCTION(BlueprintCallable, Category = "BeltLine|Items")
	UBeltItemType* GetBuiltInItemType(EBeltBuiltInItem Shape);

	/** Item types registered in this world. Equals the number of instance sets. */
	UFUNCTION(BlueprintPure, Category = "BeltLine|Items")
	int32 GetItemTypeCount() const { return ItemTypes.Num(); }

	// ------------------------------------------------------------------------------------------------
	// Spawning and taking
	// ------------------------------------------------------------------------------------------------

	/** Put one item on the start of a belt. False when the budget is full or the entry is occupied. */
	UFUNCTION(BlueprintCallable, Category = "BeltLine|Items")
	bool SpawnItem(ABeltActor* Belt, UBeltItemType* ItemType);

	/**
	 * Fill the free stretch of one belt with items and return how many went on. Stops at the belt's
	 * capacity, at the world budget, or when the belt has no more room at the spacing the items need.
	 */
	UFUNCTION(BlueprintCallable, Category = "BeltLine|Items")
	int32 SpawnItems(ABeltActor* Belt, UBeltItemType* ItemType, int32 Count);

	/**
	 * Spread items over every belt in the world, filling each in turn, and return how many went on.
	 * This is what a "+500 items" button calls; it needs no knowledge of the layout.
	 */
	UFUNCTION(BlueprintCallable, Category = "BeltLine|Items")
	int32 SpawnItemsAcrossBelts(UBeltItemType* ItemType, int32 Count);

	/** Take the item nearest the end off a belt. False when the belt is empty. */
	UFUNCTION(BlueprintCallable, Category = "BeltLine|Items")
	bool TryTakeItem(ABeltActor* Belt, UBeltItemType*& OutItemType);

	/** Take every item off every belt and out of every node. Frees nothing; nothing regrows afterwards. */
	UFUNCTION(BlueprintCallable, Category = "BeltLine|Items")
	void ClearAllItems();

	/** Items riding on belts right now. */
	UFUNCTION(BlueprintPure, Category = "BeltLine|Items")
	int32 GetItemCount() const { return TotalItems; }

	// ------------------------------------------------------------------------------------------------
	// Budget
	// ------------------------------------------------------------------------------------------------

	/**
	 * Change the world's item ceiling.
	 *
	 * Lowering it below the current count trims immediately, taking items off the back of belts so what
	 * is already on its way still arrives. Instance slots stay where they are, so the draw call count
	 * does not move and raising the budget again allocates nothing.
	 */
	UFUNCTION(BlueprintCallable, Category = "BeltLine|Budget")
	void SetItemBudget(int32 NewBudget);

	/** The world's item ceiling. */
	UFUNCTION(BlueprintPure, Category = "BeltLine|Budget")
	int32 GetItemBudget() const { return ItemBudget; }

	/** How many more items would fit before the ceiling. */
	UFUNCTION(BlueprintPure, Category = "BeltLine|Budget")
	int32 GetRemainingBudget() const { return FMath::Max(0, ItemBudget - TotalItems); }

	// ------------------------------------------------------------------------------------------------
	// Belts
	// ------------------------------------------------------------------------------------------------

	/** Set every belt in the world to one speed, in cm per second. */
	UFUNCTION(BlueprintCallable, Category = "BeltLine|Belts")
	void SetAllBeltSpeeds(float NewSpeed);

	/** Multiply every belt's speed. This is what a half-speed or double-speed button calls. */
	UFUNCTION(BlueprintCallable, Category = "BeltLine|Belts")
	void ScaleAllBeltSpeeds(float Scale);

	/** Hold, or release, every belt whose end feeds nothing - the world's outputs. */
	UFUNCTION(BlueprintCallable, Category = "BeltLine|Belts")
	int32 SetAllOutputsBlocked(bool bNewBlocked);

	/** Feed every output on every node, or only the first. This is what a merge/split button calls. */
	UFUNCTION(BlueprintCallable, Category = "BeltLine|Belts")
	int32 SetAllNodesSplitEnabled(bool bNewSplitEnabled);

	/** Average speed across the belts, in cm per second. */
	UFUNCTION(BlueprintPure, Category = "BeltLine|Belts")
	float GetAverageBeltSpeed() const;

	// ------------------------------------------------------------------------------------------------
	// Update rate
	// ------------------------------------------------------------------------------------------------

	/** How often belts advance, in Hz. 0 means every frame. */
	UFUNCTION(BlueprintCallable, Category = "BeltLine")
	void SetUpdatesPerSecond(float NewRate);

	/** How often belts advance, in Hz. */
	UFUNCTION(BlueprintPure, Category = "BeltLine")
	float GetUpdatesPerSecond() const { return UpdatesPerSecond; }

	// ------------------------------------------------------------------------------------------------
	// Stats
	// ------------------------------------------------------------------------------------------------

	/** Measured counters, refreshed on every update. */
	UFUNCTION(BlueprintPure, Category = "BeltLine")
	const FBeltStats& GetStats() const { return Stats; }

	/** Items that left a belt in the last second, across the world. */
	UFUNCTION(BlueprintPure, Category = "BeltLine")
	float GetThroughput() const { return Stats.ThroughputPerSecond; }

	/** Write the current numbers into the log, one line per counter. Backs the Belt.Stats command. */
	void LogStats() const;

	// ------------------------------------------------------------------------------------------------
	// Console support
	// ------------------------------------------------------------------------------------------------

	/**
	 * Build a closed loop of four belts joined by four nodes around a point, and fill it.
	 *
	 * This is the proof in one line: a loop keeps its items rather than draining into a sink, so the
	 * item count on the statistics box holds still while the instance set count sits at four. Passing
	 * zero items tears the loop down again.
	 */
	int32 BuildTestLoop(int32 ItemCount, const FVector& Origin, float LoopRadius);

	/** Take the Belt.Test loop away. */
	void DestroyTestLoop();

	/** Belts the Belt.Test loop currently owns. */
	int32 GetTestBeltCount() const { return TestBelts.Num(); }

private:
	/** Re-read the project settings into the cached hot fields. */
	void ApplySettings();

	/** Run one update: advance, drain, gather, write. */
	void UpdateBelts(float DeltaSeconds);

	/** Pass 1 and 2: advance every belt, then drain every node. */
	void AdvanceWorld(float DeltaSeconds, FBeltUpdateContext& Context);

	/** Pass 3 and 4: gather transforms per type and push each set in one bulk write. */
	void WriteInstances(FBeltUpdateContext& Context);

	/** Add up what is on the belts and in the nodes. */
	void RecountItems();

	/** Take items off the back of belts until the world is inside its budget. */
	void EnforceBudget();

	/** Build the context the belts read: the flat radius and weight tables. */
	FBeltUpdateContext MakeUpdateContext();

	/** Make sure the transient actor the instance sets hang off exists. */
	void EnsureBatchHolder();

	/** Create the instanced static mesh component for one item type. */
	UInstancedStaticMeshComponent* CreateBatchComponent(const UBeltItemType& ItemType);

	/** Tell every belt and node to resolve its type filter again, after a new type appeared. */
	void RefreshTypeFilters();

	/** Roll the one-second throughput window on. */
	void UpdateThroughputWindow(float DeltaSeconds);

	// ------------------------------------------------------------------------------------------------
	// State
	// ------------------------------------------------------------------------------------------------

	/** Registered belts, in registration order. That order is also the update order, and it is stable. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<ABeltActor>> Belts;

	/** Registered nodes. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<ABeltNode>> Nodes;

	/** Item types by index. An item's one-byte type field indexes straight into this. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UBeltItemType>> ItemTypes;

	/** The four code types, built on first use. */
	UPROPERTY(Transient)
	TMap<EBeltBuiltInItem, TObjectPtr<UBeltItemType>> BuiltInTypes;

	/** Transient actor the instance sets are attached to. */
	UPROPERTY(Transient)
	TObjectPtr<AActor> BatchHolder;

	/** Belts the Belt.Test loop owns. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<ABeltActor>> TestBelts;

	/** Nodes the Belt.Test loop owns. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<ABeltNode>> TestNodes;

	/** One batch per item type, parallel to ItemTypes. Not a UPROPERTY: the component is kept alive by
	 *  its owning actor, and the type by ItemTypes. */
	TArray<FBeltInstanceBatch> Batches;

	/** Item radii, flat and indexed by type index, so the movement pass never dereferences an object. */
	TArray<float> TypeRadii;

	/** Item weights, flat and indexed by type index. */
	TArray<float> TypeWeights;

	// ------------------------------------------------------------------------------------------------
	// Cached settings
	// ------------------------------------------------------------------------------------------------

	int32 ItemBudget = 20000;
	int32 MaxItemsPerBelt = 4000;
	int32 MaxItemTypes = 64;
	float MinItemPitch = 40.0f;
	float UpdatesPerSecond = 0.0f;
	float MaxUpdateStep = 0.1f;
	float PathSampleSpacing = 50.0f;
	bool bTickInEditorWorlds = true;

	// ------------------------------------------------------------------------------------------------
	// Running state
	// ------------------------------------------------------------------------------------------------

	/** Seconds since the last update, against the update rate. */
	float UpdateAccumulator = 0.0f;

	/** Items on belts, refreshed every update. */
	int32 TotalItems = 0;

	/** Items in node buffers, refreshed every update. */
	int32 TotalBufferedItems = 0;

	/** Items delivered inside the current throughput window. */
	int32 DeliveredThisWindow = 0;

	/** Seconds into the current throughput window. */
	float ThroughputWindowTime = 0.0f;

	/** Items delivered since the world started. */
	int32 TotalDelivered = 0;

	/** Spawn requests refused since the world started. */
	int32 TotalSpawnsRejected = 0;

	/** Buffer growths since the world started. Settles once the high-water mark is reached. */
	int32 TotalBufferGrowth = 0;

	/** Published through GetStats(). */
	FBeltStats Stats;
};
