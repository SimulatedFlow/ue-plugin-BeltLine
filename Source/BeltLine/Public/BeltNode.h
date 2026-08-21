// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BeltTypes.h"
#include "BeltNode.generated.h"

class ABeltActor;
class UBeltItemType;
class UStaticMeshComponent;
class UStaticMesh;

/**
 * The place where belts meet: a small buffer with a capacity, a type filter and a list of outputs.
 *
 * A node is the whole of BeltLine's routing, and it is deliberately the whole of it. Point several
 * belts at one node and it is a merge; give one node several output belts and it is a split; give it a
 * filter and it is a sorter. That is enough to build a branching, merging network - and it stops
 * exactly there, because deciding *what* should go where is a game's business, not a conveyor's.
 *
 * The buffer is what makes a merge fair and a split honest. Without it a merge would either drop items
 * or let whichever belt updated first win every time; with it, an arriving item is accepted only if
 * there is a slot, so a belt feeding a full node backs up like it should. The buffer is a ring of type
 * indices sized once, and nodes drain in their own pass after every belt has moved, so an item never
 * crosses two belts in one update however the actors happen to be ordered.
 */
UCLASS(Blueprintable, meta = (DisplayName = "Belt Node"))
class BELTLINE_API ABeltNode : public AActor
{
	GENERATED_BODY()

public:
	ABeltNode();

	// AActor interface
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void PostRegisterAllComponents() override;
	virtual void PostUnregisterAllComponents() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	// ------------------------------------------------------------------------------------------------
	// Components
	// ------------------------------------------------------------------------------------------------

	/** Root. A node has no geometry of its own unless one is given to NodeMesh. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "BeltLine")
	TObjectPtr<USceneComponent> SceneRoot;

	/** Optional mesh so the node is something you can see and click in the viewport. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "BeltLine")
	TObjectPtr<UStaticMeshComponent> NodeMeshComponent;

	// ------------------------------------------------------------------------------------------------
	// Buffer
	// ------------------------------------------------------------------------------------------------

	/**
	 * Items the node can hold while it waits for an output to have room.
	 *
	 * Keep it small. A node is a junction, not a warehouse - eight items is plenty to keep a merge from
	 * stuttering, and a large buffer only moves the point at which the queue becomes visible, which is
	 * the opposite of what a conveyor should do.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BeltLine|Node", meta = (ClampMin = "1", UIMax = "256"))
	int32 Capacity = 8;

	/** Types the node accepts. Empty means anything. A belt feeding it a refused type backs up. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BeltLine|Node")
	TArray<TObjectPtr<UBeltItemType>> AllowedTypes;

	/** Belts the node feeds, in order. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BeltLine|Node")
	TArray<TObjectPtr<ABeltActor>> OutputBelts;

	/** How the node chooses between its outputs. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BeltLine|Node")
	EBeltNodeDistribution Distribution = EBeltNodeDistribution::RoundRobin;

	/**
	 * Use every output, or only the first.
	 *
	 * Turning it off is a split turned back into a straight line: the node keeps its full list of
	 * outputs and simply stops feeding all but the first, so the branch drains and the main line takes
	 * the whole flow. Turning it back on refills the branch. Nothing is spawned or destroyed either way.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BeltLine|Node")
	bool bSplitEnabled = true;

	/** Hold everything in the node. The belts feeding it back up, exactly as a blocked belt end does. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BeltLine|Node")
	bool bBlocked = false;

	/** Items handed on per drain pass. 0 lifts the limit and the node empties as fast as it can. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BeltLine|Node", meta = (ClampMin = "0", UIMax = "64"))
	int32 MaxHandOffsPerUpdate = 0;

	// ------------------------------------------------------------------------------------------------
	// Appearance
	// ------------------------------------------------------------------------------------------------

	/** Mesh drawn at the node. Purely so a junction is visible; nothing reads it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BeltLine|Appearance")
	TObjectPtr<UStaticMesh> NodeMesh;

	/** Scale for that mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BeltLine|Appearance")
	FVector NodeMeshScale = FVector(1.0f, 1.0f, 1.0f);

	// ------------------------------------------------------------------------------------------------
	// Blueprint surface
	// ------------------------------------------------------------------------------------------------

	/** Items waiting in the node. */
	UFUNCTION(BlueprintPure, Category = "BeltLine|Node")
	int32 GetItemCount() const { return BufferCount; }

	/** Items the node can hold. */
	UFUNCTION(BlueprintPure, Category = "BeltLine|Node")
	int32 GetCapacity() const { return BufferCapacity; }

	/** True when the node is holding everything it has. */
	UFUNCTION(BlueprintPure, Category = "BeltLine|Node")
	bool IsFull() const { return BufferCount >= BufferCapacity; }

	/** Hold the node, or release it. */
	UFUNCTION(BlueprintCallable, Category = "BeltLine|Node")
	void SetBlocked(bool bNewBlocked);

	/** True while the node is held. */
	UFUNCTION(BlueprintPure, Category = "BeltLine|Node")
	bool IsBlocked() const { return bBlocked; }

	/** Feed every output belt, or only the first. */
	UFUNCTION(BlueprintCallable, Category = "BeltLine|Node")
	void SetSplitEnabled(bool bNewSplitEnabled);

	/** True while every output is being fed. */
	UFUNCTION(BlueprintPure, Category = "BeltLine|Node")
	bool IsSplitEnabled() const { return bSplitEnabled; }

	/** Empty the node. The items are gone, not delivered; the throughput counter does not move. */
	UFUNCTION(BlueprintCallable, Category = "BeltLine|Node")
	void ClearItems();

	/** True when this node will take an item of this type at all, before asking whether there is room. */
	UFUNCTION(BlueprintPure, Category = "BeltLine|Node")
	bool AllowsItemType(const UBeltItemType* ItemType) const;

	// ------------------------------------------------------------------------------------------------
	// Subsystem-facing core. Not reflected: this is the hot path.
	// ------------------------------------------------------------------------------------------------

	/**
	 * Resolve AllowedTypes into the flat indices the hot path compares against, registering any type
	 * the world has not seen yet. Called when the node registers and whenever a new type appears.
	 */
	void RefreshAllowedTypeIndices(class UBeltSubsystem& Subsystem);

	/** True when this node's filter lets a type index through. One compare when there is no filter. */
	FORCEINLINE bool AllowsTypeIndex(int32 TypeIndex) const
	{
		return !bHasTypeFilter || AllowedTypeIndices.Contains(TypeIndex);
	}

	/** True when the node would take an item of this type right now. */
	bool CanAccept(int32 TypeIndex) const;

	/** Take an item of this type into the buffer. False when it is refused or the buffer is full. */
	bool TryAccept(int32 TypeIndex);

	/** Take the oldest item out of the buffer, ignoring the outputs. Used when the budget is lowered. */
	bool TryTakeItem(int32& OutTypeIndex);

	/** Hand as much of the buffer to the output belts as they will take. */
	void DrainInto(FBeltUpdateContext& Context);

	/** Size the buffer from Capacity. Allocates; called only when the node is built. */
	void RebuildBuffer(FBeltUpdateContext* Context);

	/** Bytes held by the buffer. */
	int32 GetAllocatedSize() const { return Buffer.GetAllocatedSize(); }

private:
	/** Ask the world's subsystem to take this node on. Safe to call more than once. */
	void RegisterWithSubsystem();

	/** Tell the world's subsystem to forget this node. Safe to call more than once. */
	void UnregisterFromSubsystem();

	/** Ring of type indices. One byte per waiting item; a node holds no more than a handful. */
	TArray<uint8> Buffer;

	/** Ring index of the item that has been waiting longest. */
	int32 BufferHead = 0;

	/** Items waiting. */
	int32 BufferCount = 0;

	/** Entries the buffer holds. */
	int32 BufferCapacity = 0;

	/** Output the round-robin cursor will try first. */
	int32 OutputCursor = 0;

	/** AllowedTypes as flat indices, so the filter costs an integer compare instead of a pointer chase. */
	TArray<int32> AllowedTypeIndices;

	/** True when AllowedTypes holds at least one usable entry, so the filter is actually on. */
	bool bHasTypeFilter = false;

	/** True once the node has told the subsystem it exists. */
	bool bRegistered = false;
};
