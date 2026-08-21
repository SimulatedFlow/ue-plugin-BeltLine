// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "BeltTypes.h"
#include "BeltStatics.generated.h"

class ABeltActor;
class ABeltNode;
class UBeltItemType;
class UBeltSubsystem;

/**
 * The whole of BeltLine from Blueprint.
 *
 * Everything a designer needs is here and everything here goes through the same world subsystem the
 * C++ side uses, so a demo button, a console command and a shipped game all drive one code path. There
 * is no second, simplified implementation hiding behind these calls.
 */
UCLASS(meta = (ScriptName = "BeltLineStatics"))
class BELTLINE_API UBeltStatics : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** The world's belt subsystem, or null outside a world that supports one. */
	UFUNCTION(BlueprintPure, Category = "BeltLine", meta = (WorldContext = "WorldContextObject"))
	static UBeltSubsystem* GetBeltSubsystem(const UObject* WorldContextObject);

	// ------------------------------------------------------------------------------------------------
	// Items
	// ------------------------------------------------------------------------------------------------

	/**
	 * Put one item on the start of a belt.
	 *
	 * False means it was refused, and the reason is always one of three: the world is at its item
	 * budget, the belt does not carry this type, or the entry is still occupied by the last thing that
	 * got on. All three are counted, none of them is silent, and none of them drops an item.
	 */
	UFUNCTION(BlueprintCallable, Category = "BeltLine|Items")
	static bool SpawnItem(ABeltActor* Belt, UBeltItemType* ItemType);

	/** Fill the free stretch of one belt and return how many items went on. */
	UFUNCTION(BlueprintCallable, Category = "BeltLine|Items")
	static int32 SpawnItems(ABeltActor* Belt, UBeltItemType* ItemType, int32 Count);

	/**
	 * Spread items over every belt in the world and return how many went on.
	 * This is what a "+500 items" button calls; it needs to know nothing about the layout.
	 */
	UFUNCTION(BlueprintCallable, Category = "BeltLine|Items", meta = (WorldContext = "WorldContextObject"))
	static int32 SpawnItemsAcrossBelts(const UObject* WorldContextObject, UBeltItemType* ItemType, int32 Count);

	/**
	 * Take the item nearest the end off a belt.
	 *
	 * This is the other half of a conveyor: a machine at the end of the line asks for what arrived
	 * instead of being handed it. False means the belt is empty.
	 */
	UFUNCTION(BlueprintCallable, Category = "BeltLine|Items")
	static bool TryTakeItem(ABeltActor* Belt, UBeltItemType*& OutItemType);

	/** Take every item off every belt and out of every node. */
	UFUNCTION(BlueprintCallable, Category = "BeltLine|Items", meta = (WorldContext = "WorldContextObject"))
	static void ClearAllItems(const UObject* WorldContextObject);

	/** One of the four code types, so a project can carry something before it has authored anything. */
	UFUNCTION(BlueprintCallable, Category = "BeltLine|Items", meta = (WorldContext = "WorldContextObject"))
	static UBeltItemType* GetBuiltInItemType(const UObject* WorldContextObject, EBeltBuiltInItem Shape);

	// ------------------------------------------------------------------------------------------------
	// Belts
	// ------------------------------------------------------------------------------------------------

	/** Set one belt's speed, in cm per second. */
	UFUNCTION(BlueprintCallable, Category = "BeltLine|Belts")
	static void SetBeltSpeed(ABeltActor* Belt, float NewSpeed);

	/** One belt's speed, in cm per second. */
	UFUNCTION(BlueprintPure, Category = "BeltLine|Belts")
	static float GetBeltSpeed(const ABeltActor* Belt);

	/** Set every belt in the world to one speed. */
	UFUNCTION(BlueprintCallable, Category = "BeltLine|Belts", meta = (WorldContext = "WorldContextObject"))
	static void SetAllBeltSpeeds(const UObject* WorldContextObject, float NewSpeed);

	/** Multiply every belt's speed. This is what a half-speed or double-speed button calls. */
	UFUNCTION(BlueprintCallable, Category = "BeltLine|Belts", meta = (WorldContext = "WorldContextObject"))
	static void ScaleAllBeltSpeeds(const UObject* WorldContextObject, float Scale);

	/** Average belt speed in the world, in cm per second. */
	UFUNCTION(BlueprintPure, Category = "BeltLine|Belts", meta = (WorldContext = "WorldContextObject"))
	static float GetAverageBeltSpeed(const UObject* WorldContextObject);

	/** Hold one belt at its end, or release it. */
	UFUNCTION(BlueprintCallable, Category = "BeltLine|Belts")
	static void SetOutputBlocked(ABeltActor* Belt, bool bBlocked);

	/**
	 * Hold, or release, every belt whose end feeds nothing, and return how many were affected.
	 * This is the backpressure button: press it and the queues grow backwards from the ends of the line.
	 */
	UFUNCTION(BlueprintCallable, Category = "BeltLine|Belts", meta = (WorldContext = "WorldContextObject"))
	static int32 SetAllOutputsBlocked(const UObject* WorldContextObject, bool bBlocked);

	/** True when a belt is actually backing up - not merely that a switch was set on it. */
	UFUNCTION(BlueprintPure, Category = "BeltLine|Belts")
	static bool IsBlocked(const ABeltActor* Belt);

	/** Items riding on one belt. */
	UFUNCTION(BlueprintPure, Category = "BeltLine|Belts")
	static int32 GetBeltItemCount(const ABeltActor* Belt);

	/** Items on one belt that could not take their full step on the last update. */
	UFUNCTION(BlueprintPure, Category = "BeltLine|Belts")
	static int32 GetBeltJammedCount(const ABeltActor* Belt);

	/** Every belt in the world, in registration order. */
	UFUNCTION(BlueprintPure, Category = "BeltLine|Belts", meta = (WorldContext = "WorldContextObject"))
	static TArray<ABeltActor*> GetAllBelts(const UObject* WorldContextObject);

	// ------------------------------------------------------------------------------------------------
	// Nodes
	// ------------------------------------------------------------------------------------------------

	/** Feed every output on one node, or only the first. */
	UFUNCTION(BlueprintCallable, Category = "BeltLine|Nodes")
	static void SetNodeSplitEnabled(ABeltNode* Node, bool bSplitEnabled);

	/** Feed every output on every node, or only the first. This is what a merge/split button calls. */
	UFUNCTION(BlueprintCallable, Category = "BeltLine|Nodes", meta = (WorldContext = "WorldContextObject"))
	static int32 SetAllNodesSplitEnabled(const UObject* WorldContextObject, bool bSplitEnabled);

	/** Hold one node, or release it. Its incoming belts back up while it is held. */
	UFUNCTION(BlueprintCallable, Category = "BeltLine|Nodes")
	static void SetNodeBlocked(ABeltNode* Node, bool bBlocked);

	// ------------------------------------------------------------------------------------------------
	// Throughput and budget
	// ------------------------------------------------------------------------------------------------

	/** Items that left a belt in the last second, across the world. */
	UFUNCTION(BlueprintPure, Category = "BeltLine|Stats", meta = (WorldContext = "WorldContextObject"))
	static float GetThroughput(const UObject* WorldContextObject);

	/** Items that left one belt in the last second. */
	UFUNCTION(BlueprintPure, Category = "BeltLine|Stats")
	static float GetBeltThroughput(const ABeltActor* Belt);

	/** Items riding on belts across the world. */
	UFUNCTION(BlueprintPure, Category = "BeltLine|Stats", meta = (WorldContext = "WorldContextObject"))
	static int32 GetItemCount(const UObject* WorldContextObject);

	/** Change the world's item ceiling. Lowering it trims from the back of the belts. */
	UFUNCTION(BlueprintCallable, Category = "BeltLine|Budget", meta = (WorldContext = "WorldContextObject"))
	static void SetItemBudget(const UObject* WorldContextObject, int32 NewBudget);

	/** The world's item ceiling. */
	UFUNCTION(BlueprintPure, Category = "BeltLine|Budget", meta = (WorldContext = "WorldContextObject"))
	static int32 GetItemBudget(const UObject* WorldContextObject);

	/** Every measured counter from the last update. */
	UFUNCTION(BlueprintPure, Category = "BeltLine|Stats", meta = (WorldContext = "WorldContextObject"))
	static FBeltStats GetBeltStats(const UObject* WorldContextObject);
};
