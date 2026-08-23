// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BeltTypes.generated.h"

class UBeltItemType;

/**
 * The four item types that exist in code, built from engine primitives.
 *
 * They are here so a fresh project has something to put on a belt before anyone has authored a mesh.
 * Ask the subsystem for one, drop it on a belt, press Play. A real project replaces them with its own
 * UBeltItemType data assets; nothing in the plugin treats these four as special afterwards.
 */
UENUM(BlueprintType)
enum class EBeltBuiltInItem : uint8
{
	/** A crate. Cube, 60 cm, the default thing on a conveyor. */
	Box		UMETA(DisplayName = "Box"),

	/** Upright cylinder, taller than it is wide. Reads clearly from the side in a video. */
	Barrel	UMETA(DisplayName = "Barrel"),

	/** A small lump. Sphere, deliberately the smallest of the four, so a belt carries more of them. */
	Ore		UMETA(DisplayName = "Ore"),

	/** Flat and long. The widest of the four along the direction of travel, so it jams first. */
	Plate	UMETA(DisplayName = "Plate")
};

/** How a node picks which of its output belts an item goes to. */
UENUM(BlueprintType)
enum class EBeltNodeDistribution : uint8
{
	/**
	 * Take turns. The cursor moves on after every accepted item, whether or not the belt it landed on
	 * was the one asked first, so a stalled output cannot starve the others and a running output cannot
	 * hog the node. This is what a splitter is expected to do.
	 */
	RoundRobin		UMETA(DisplayName = "Round Robin"),

	/**
	 * Always feed the first output that has room. Later outputs only ever see items the earlier ones
	 * could not take, which is how you build an overflow lane.
	 */
	FirstAvailable	UMETA(DisplayName = "First Available")
};

/**
 * What the last update actually did. Read it from Blueprint, from the Canvas box, or from Belt.Stats.
 *
 * These are measured, not estimated. Every field is written by the code that does the work, on the
 * update it did it, which is why the numbers can be put on screen next to the thing they describe.
 */
USTRUCT(BlueprintType)
struct BELTLINE_API FBeltStats
{
	GENERATED_BODY()

	/** Belts registered in this world. */
	UPROPERTY(BlueprintReadOnly, Category = "BeltLine|Stats")
	int32 Belts = 0;

	/** Nodes registered in this world. */
	UPROPERTY(BlueprintReadOnly, Category = "BeltLine|Stats")
	int32 Nodes = 0;

	/** Item types that have been registered, and therefore have an instance set. */
	UPROPERTY(BlueprintReadOnly, Category = "BeltLine|Stats")
	int32 ItemTypes = 0;

	/** Items riding on belts right now. */
	UPROPERTY(BlueprintReadOnly, Category = "BeltLine|Stats")
	int32 Items = 0;

	/** Items sitting inside node buffers, waiting for an output belt to take them. */
	UPROPERTY(BlueprintReadOnly, Category = "BeltLine|Stats")
	int32 BufferedItems = 0;

	/** The world's cap on items. Spawning past it fails; lowering it below the current count trims. */
	UPROPERTY(BlueprintReadOnly, Category = "BeltLine|Stats")
	int32 ItemBudget = 0;

	/** Items that could not move their full step this update because something was in front of them. */
	UPROPERTY(BlueprintReadOnly, Category = "BeltLine|Stats")
	int32 JammedItems = 0;

	/**
	 * Instance sets holding at least one item - one instanced static mesh component per item type,
	 * and therefore one draw call each. This is the number the plugin is sold on: it follows the number
	 * of item *types*, never the number of items.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "BeltLine|Stats")
	int32 InstanceSets = 0;

	/**
	 * Instance slots allocated across all sets. Slots are never given back, so this settles at the
	 * high-water mark and lowering the budget does not move it - which is exactly why lowering the
	 * budget costs nothing and frees nothing.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "BeltLine|Stats")
	int32 InstanceSlots = 0;

	/** Items that left a belt in the last second, measured over a sliding window. */
	UPROPERTY(BlueprintReadOnly, Category = "BeltLine|Stats")
	float ThroughputPerSecond = 0.0f;

	/** Items that have left a belt since the world started. Only ever grows. */
	UPROPERTY(BlueprintReadOnly, Category = "BeltLine|Stats")
	int32 ItemsDelivered = 0;

	/** Spawn requests refused since the world started - budget full, belt full, or type not allowed. */
	UPROPERTY(BlueprintReadOnly, Category = "BeltLine|Stats")
	int32 SpawnsRejected = 0;

	/** Wall time of the whole update: advance, hand-offs, node drain and instance writes. */
	UPROPERTY(BlueprintReadOnly, Category = "BeltLine|Stats")
	float UpdateMilliseconds = 0.0f;

	/** Wall time of the movement pass alone - the one addition and one comparison per item. */
	UPROPERTY(BlueprintReadOnly, Category = "BeltLine|Stats")
	float AdvanceMilliseconds = 0.0f;

	/** Wall time of gathering transforms and pushing them into the instance sets. */
	UPROPERTY(BlueprintReadOnly, Category = "BeltLine|Stats")
	float InstanceMilliseconds = 0.0f;

	/**
	 * Times a buffer had to grow during this update. Steady state is zero, and it is on the box so the
	 * "nothing is allocated once the buffers stand" claim can be read off the screen instead of trusted.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "BeltLine|Stats")
	int32 BufferGrowthThisUpdate = 0;

	/** Times a buffer has grown since the world started. Settles at the high-water mark. */
	UPROPERTY(BlueprintReadOnly, Category = "BeltLine|Stats")
	int32 BufferGrowthTotal = 0;

	/** Updates per second the subsystem is running at. 0 means it runs every frame. */
	UPROPERTY(BlueprintReadOnly, Category = "BeltLine|Stats")
	float UpdatesPerSecond = 0.0f;

	/** Memory held by the ring buffers, the baked paths and the transform scratch, in kilobytes. */
	UPROPERTY(BlueprintReadOnly, Category = "BeltLine|Stats")
	float BufferKilobytes = 0.0f;
};

/**
 * Everything one belt update needs that is not on the belt, handed down by reference.
 *
 * Passing this instead of letting a belt reach back into the subsystem keeps the movement pass free of
 * virtual calls and pointer chasing: the radii are a flat float array indexed by the same uint8 that is
 * stored next to each item's distance.
 */
struct FBeltUpdateContext
{
	/** Radius of every registered item type along the direction of travel, indexed by type index. */
	const float* TypeRadii = nullptr;

	/** Weight of every registered item type in kg, indexed by type index. */
	const float* TypeWeights = nullptr;

	/** How many entries TypeRadii and TypeWeights have. */
	int32 NumTypes = 0;

	/** Items that reached the end of a belt and were consumed, handed to a node, or handed to a belt. */
	int32 ItemsDelivered = 0;

	/** Items that could not take their full step this update. */
	int32 JammedItems = 0;

	/** Buffers that had to grow. Steady state is zero. */
	int32 BufferGrowth = 0;

	FORCEINLINE float GetRadius(int32 TypeIndex) const
	{
		return (TypeRadii && TypeIndex >= 0 && TypeIndex < NumTypes) ? TypeRadii[TypeIndex] : 20.0f;
	}

	FORCEINLINE float GetWeight(int32 TypeIndex) const
	{
		return (TypeWeights && TypeIndex >= 0 && TypeIndex < NumTypes) ? TypeWeights[TypeIndex] : 0.0f;
	}
};
