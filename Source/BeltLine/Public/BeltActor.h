// Copyright 2026 Simulated Flow. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BeltPath.h"
#include "BeltTypes.h"
#include "BeltActor.generated.h"

class ABeltNode;
class UBeltItemType;
class USplineComponent;
class USplineMeshComponent;
class UStaticMesh;
class UMaterialInterface;

/**
 * One conveyor: a spline, a speed, and a ring buffer of things riding on it.
 *
 * There is no item actor and no item component. An item is two numbers and a flag, held in three
 * parallel arrays - distance, type index, jam flag - sized once when the belt is built and never
 * resized while it runs. The buffer is a ring: things join at the back and leave at the front, so
 * neither end costs a shuffle, and the entries stay sorted by distance because a belt cannot overtake
 * itself.
 *
 * The movement pass is one walk from the front of the queue to the back. Each item takes its step and
 * is then clamped against the item ahead of it, which is what makes backpressure fall out of the maths
 * rather than being bolted on: block the end and the front item stops on the end, the next stops a
 * radius and a gap behind it, and the queue grows backwards at exactly the spacing the items need.
 * Nothing overlaps, nothing is deleted to make room, and when the block is released the whole queue
 * pulls away again in the same single pass.
 *
 * In the editor the spline is draggable and reshapeable and the belt runs without pressing Play, so
 * what a layout does is visible while it is being laid out.
 */
UCLASS(Blueprintable, meta = (DisplayName = "Belt"))
class BELTLINE_API ABeltActor : public AActor
{
	GENERATED_BODY()

public:
	ABeltActor();

	// AActor interface
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void PostRegisterAllComponents() override;
	virtual void PostUnregisterAllComponents() override;
	virtual void PostInitializeComponents() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	// ------------------------------------------------------------------------------------------------
	// Components
	// ------------------------------------------------------------------------------------------------

	/** The path items travel. Drag its points in the viewport; the belt rebuilds itself. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "BeltLine")
	TObjectPtr<USplineComponent> Spline;

	// ------------------------------------------------------------------------------------------------
	// Flow
	// ------------------------------------------------------------------------------------------------

	/** How fast the belt runs, in cm per second. Negative values are clamped to zero, not reversed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BeltLine|Flow", meta = (ClampMin = "0.0", UIMax = "2000.0"))
	float Speed = 240.0f;

	/**
	 * Clear space kept between the edges of two neighbours, in cm. The true centre-to-centre distance
	 * is this plus both items' radii, so a belt of plates queues looser than a belt of ore.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BeltLine|Flow", meta = (ClampMin = "0.0", UIMax = "200.0"))
	float MinGap = 12.0f;

	/** Distance along the belt where arriving items are placed, in cm. Usually the very start. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BeltLine|Flow", meta = (ClampMin = "0.0"))
	float EntryDistance = 0.0f;

	/**
	 * Hold everything on the belt where it stands.
	 *
	 * This is the backpressure switch, and it is the honest test of a conveyor: the front item stops on
	 * the end, the queue packs backwards behind it, the throughput counter goes to zero, and the belt
	 * feeding this one starts refusing hand-offs and jams in turn. Release it and the whole line pulls
	 * away. Nothing is destroyed and nothing is spawned at either point.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BeltLine|Flow")
	bool bOutputBlocked = false;

	/**
	 * Types this belt will carry. Empty means anything.
	 *
	 * Checked when an item is pushed on, whether by a spawn, by an upstream belt or by a node, so a
	 * filter placed here backs traffic up rather than making items disappear.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BeltLine|Flow")
	TArray<TObjectPtr<UBeltItemType>> AllowedTypes;

	/** Heaviest single item this belt accepts, in kg. 0 means no limit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BeltLine|Flow", meta = (ClampMin = "0.0"))
	float MaxItemWeight = 0.0f;

	// ------------------------------------------------------------------------------------------------
	// Output
	// ------------------------------------------------------------------------------------------------

	/**
	 * Node this belt empties into. Takes precedence over OutputBelt.
	 *
	 * With neither set, items reaching the end are consumed and counted as delivered - a belt that ends
	 * in nothing is a sink, which is what you want at the mouth of a furnace or the edge of a level.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BeltLine|Output")
	TObjectPtr<ABeltNode> OutputNode;

	/** Belt this belt empties straight into, when no node is needed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BeltLine|Output")
	TObjectPtr<ABeltActor> OutputBelt;

	// ------------------------------------------------------------------------------------------------
	// Appearance
	// ------------------------------------------------------------------------------------------------

	/**
	 * Optional mesh tiled along the spline so the belt itself is visible.
	 *
	 * One spline mesh component per segment, built when the belt is constructed and not touched again -
	 * this is scenery, not the item path, and it costs what any spline-placed geometry costs. Leave it
	 * empty and the belt is just the spline, which is all the items need.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BeltLine|Appearance")
	TObjectPtr<UStaticMesh> BeltMesh;

	/** Material for the belt mesh. Empty keeps whatever the mesh carries. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BeltLine|Appearance")
	TObjectPtr<UMaterialInterface> BeltMaterial;

	/** Length of one BeltMesh along its own X axis, in cm. Sets how often the mesh repeats. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BeltLine|Appearance", meta = (ClampMin = "1.0"))
	float BeltMeshLength = 100.0f;

	/** Cross-section scale of the belt mesh: Y is width, Z is thickness. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BeltLine|Appearance")
	FVector2D BeltMeshScale = FVector2D(1.0f, 1.0f);

	/** Height above the spline that items ride at, in cm. Add the surface thickness of the belt mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BeltLine|Appearance")
	float ItemHeightOffset = 0.0f;

	/** Cap on spline mesh segments, so a very long belt cannot quietly create hundreds of components. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BeltLine|Appearance", meta = (ClampMin = "0", UIMax = "512"))
	int32 MaxBeltMeshSegments = 128;

	// ------------------------------------------------------------------------------------------------
	// Blueprint surface
	// ------------------------------------------------------------------------------------------------

	/** Change the belt's speed, in cm per second. Takes effect on the next update. */
	UFUNCTION(BlueprintCallable, Category = "BeltLine|Belt")
	void SetSpeed(float NewSpeed);

	/** Current speed in cm per second. */
	UFUNCTION(BlueprintPure, Category = "BeltLine|Belt")
	float GetSpeed() const { return Speed; }

	/** Hold the belt at its end, or release it. */
	UFUNCTION(BlueprintCallable, Category = "BeltLine|Belt")
	void SetOutputBlocked(bool bNewBlocked);

	/** True while the end is held shut. */
	UFUNCTION(BlueprintPure, Category = "BeltLine|Belt")
	bool IsOutputBlocked() const { return bOutputBlocked; }

	/**
	 * True when the belt is actually backing up: something is on it and the front item cannot move.
	 * That covers a held output, a full downstream belt and a full node, without asking which.
	 */
	UFUNCTION(BlueprintPure, Category = "BeltLine|Belt")
	bool IsBlocked() const;

	/** Items riding on this belt. */
	UFUNCTION(BlueprintPure, Category = "BeltLine|Belt")
	int32 GetItemCount() const { return RingCount; }

	/** Items this belt's ring buffer can hold, fixed when the belt was built. */
	UFUNCTION(BlueprintPure, Category = "BeltLine|Belt")
	int32 GetCapacity() const { return RingCapacity; }

	/** Items on this belt that could not take their full step on the last update. */
	UFUNCTION(BlueprintPure, Category = "BeltLine|Belt")
	int32 GetJammedCount() const { return JammedCount; }

	/** Length of the belt in cm, from the baked path. */
	UFUNCTION(BlueprintPure, Category = "BeltLine|Belt")
	float GetLength() const { return Path.Length; }

	/** Items that left this belt in the last second. */
	UFUNCTION(BlueprintPure, Category = "BeltLine|Belt")
	float GetThroughput() const { return ThroughputPerSecond; }

	/** Fraction of the belt's capacity that is in use, 0..1. */
	UFUNCTION(BlueprintPure, Category = "BeltLine|Belt")
	float GetLoadFactor() const { return RingCapacity > 0 ? static_cast<float>(RingCount) / static_cast<float>(RingCapacity) : 0.0f; }

	/** Take every item off the belt. Does not touch the buffers, so nothing is freed and nothing regrows. */
	UFUNCTION(BlueprintCallable, Category = "BeltLine|Belt")
	void ClearItems();

	/** Rebake the path from the spline. Called for you when the belt is built, moved or edited. */
	UFUNCTION(BlueprintCallable, Category = "BeltLine|Belt")
	void RebuildPath();

	/** True when this belt will take an item of this type at all, before asking whether there is room. */
	UFUNCTION(BlueprintPure, Category = "BeltLine|Belt")
	bool AllowsItemType(const UBeltItemType* ItemType) const;

	// ------------------------------------------------------------------------------------------------
	// Subsystem-facing core. Not reflected: this is the hot path.
	// ------------------------------------------------------------------------------------------------

	/**
	 * Resolve AllowedTypes into the flat indices the hot path compares against, registering any type
	 * the world has not seen yet. Called when the belt registers and whenever a new type appears.
	 */
	void RefreshAllowedTypeIndices(class UBeltSubsystem& Subsystem);

	/** True when this belt's filter lets a type index through. One compare when there is no filter. */
	FORCEINLINE bool AllowsTypeIndex(int32 TypeIndex) const
	{
		return !bHasTypeFilter || AllowedTypeIndices.Contains(TypeIndex);
	}

	/** True when a type index is allowed and there is room at the entry, given every item's radius. */
	bool HasRoomAtEntry(int32 TypeIndex, const FBeltUpdateContext& Context) const;

	/** Put an item at the entry. False when the type is refused or the entry is occupied. */
	bool PushItemAtEntry(int32 TypeIndex, const FBeltUpdateContext& Context);

	/**
	 * Fill the free stretch behind the last item with as many items of one type as it holds, and return
	 * how many went on. This is how a belt is loaded in one call instead of one item per frame: the
	 * items are placed at the spacing the movement pass would have settled them at anyway.
	 */
	int32 FillWithItems(int32 TypeIndex, int32 Count, const FBeltUpdateContext& Context);

	/** Take the item nearest the end off the belt. False when the belt is empty. */
	bool TryTakeFrontItem(int32& OutTypeIndex);

	/** Take the item that got on last off the belt. Used when the world budget is lowered. */
	bool TryTakeBackItem(int32& OutTypeIndex);

	/** Advance every item one step, clamp the queue, and hand off whatever reached the end. */
	void AdvanceItems(float DeltaSeconds, FBeltUpdateContext& Context);

	/** Roll the throughput window on. Called once per second by the subsystem. */
	void TickThroughputWindow(float WindowSeconds);

	/** Baked path. Read by the subsystem when it gathers instance transforms. */
	const FBeltPath& GetPath() const { return Path; }

	/** Ring index of the Nth item counted from the front of the queue. */
	FORCEINLINE int32 GetRingIndex(int32 QueuePosition) const
	{
		const int32 Raw = RingHead + QueuePosition;
		return Raw < RingCapacity ? Raw : Raw - RingCapacity;
	}

	/** Distance along the belt of the item at a ring index. */
	FORCEINLINE float GetItemDistanceAt(int32 RingIndex) const { return ItemDistance[RingIndex]; }

	/** Type index of the item at a ring index. */
	FORCEINLINE int32 GetItemTypeAt(int32 RingIndex) const { return ItemType[RingIndex]; }

	/** Whether the item at a ring index was held up on the last update. */
	FORCEINLINE bool IsItemJammedAt(int32 RingIndex) const { return ItemJammed[RingIndex] != 0; }

	/** Bytes held by the ring buffers and the baked path. */
	int32 GetAllocatedSize() const;

	/** Rebake if the actor has been moved since the last check. Cheap when it has not. */
	bool RefreshPathIfMoved();

	/** Size the ring buffer from the belt's length. Allocates; called only when the belt is built. */
	void RebuildRingBuffer(FBeltUpdateContext* Context);

private:
	/** Hand the front item to the node, the belt or the sink. False when nothing would take it. */
	bool TryHandOffFrontItem(FBeltUpdateContext& Context);

	/** True when whatever is at the end of this belt would take an item of this type right now. */
	bool CanOutputAccept(int32 TypeIndex, const FBeltUpdateContext& Context) const;

	/** Re-clamp the queue after a hand-off was refused mid-drain. Stops at the first item that fits. */
	void EnforceSpacingFromFront(const FBeltUpdateContext& Context);

	/** Build or tear down the spline mesh segments that make the belt visible. */
	void RebuildBeltMesh();

	/** Ask the world's subsystem to take this belt on. Safe to call more than once. */
	void RegisterWithSubsystem();

	/** Tell the world's subsystem to forget this belt. Safe to call more than once. */
	void UnregisterFromSubsystem();

	// ------------------------------------------------------------------------------------------------
	// Ring buffer. Three parallel arrays, not an array of items: the movement pass touches only the
	// distances, and keeping them contiguous is the difference between one cache line per eight items
	// and one cache line per item.
	// ------------------------------------------------------------------------------------------------

	/** Distance of each item along the belt, in cm. Sorted descending from RingHead. */
	TArray<float> ItemDistance;

	/** Index into the subsystem's item type table, one byte per item. */
	TArray<uint8> ItemType;

	/** 1 when the item could not take its full step on the last update. Drives the jam tint. */
	TArray<uint8> ItemJammed;

	/** Ring index of the item nearest the end of the belt. */
	int32 RingHead = 0;

	/** Items currently on the belt. */
	int32 RingCount = 0;

	/** Entries the ring holds. Fixed when the belt is built. */
	int32 RingCapacity = 0;

	/** Items held up on the last update. */
	int32 JammedCount = 0;

	/** Baked spline. */
	FBeltPath Path;

	/** Where the actor was when the path was baked, so a moved belt can be spotted for the cost of a compare. */
	FTransform PathBuiltAtTransform;

	/** Items that have left the belt inside the current throughput window. */
	int32 DeliveredThisWindow = 0;

	/** Items that left the belt per second, from the last completed window. */
	float ThroughputPerSecond = 0.0f;

	/** AllowedTypes as flat indices, so the filter costs an integer compare instead of a pointer chase. */
	TArray<int32> AllowedTypeIndices;

	/** True when AllowedTypes holds at least one usable entry, so the filter is actually on. */
	bool bHasTypeFilter = false;

	/** True once the belt has told the subsystem it exists. */
	bool bRegistered = false;

	/** Spline mesh segments making the belt visible. Rebuilt on construction, untouched at runtime. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<USplineMeshComponent>> BeltMeshSegments;
};
