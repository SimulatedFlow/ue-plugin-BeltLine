// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "BeltTypes.h"
#include "BeltItemType.generated.h"

class UMaterialInterface;
class UStaticMesh;

/**
 * What one kind of thing on a belt looks like and how much room it takes.
 *
 * This is the whole definition of an item. There is no item class, no item actor and no item object -
 * an item on a belt is a distance and an index into the list of these assets, and every item sharing
 * one asset is drawn as one instance in one instanced static mesh component. That is why the draw call
 * count follows the number of these assets in play and not the number of things moving.
 *
 * Nothing here is gameplay. There is no value, no stack size, no recipe and no crafting result on
 * purpose: BeltLine transports things, and what a thing *is* belongs to the project's own item system.
 * Weight is the one exception, and only because a belt has to be able to refuse to carry something.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Belt Item Type"))
class BELTLINE_API UBeltItemType : public UDataAsset
{
	GENERATED_BODY()

public:
	UBeltItemType();

	// ------------------------------------------------------------------------------------------------
	// Appearance
	// ------------------------------------------------------------------------------------------------

	/**
	 * The mesh every item of this type is drawn with. One mesh, one instance set, one draw call.
	 *
	 * Leave it empty and the type falls back to the engine primitive named by FallbackShape, so a belt
	 * carries something visible before anyone has authored art.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance")
	TObjectPtr<UStaticMesh> Mesh;

	/** Optional material override on slot 0. Leave empty to use whatever the mesh already carries. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance")
	TObjectPtr<UMaterialInterface> OverrideMaterial;

	/** Primitive used when Mesh is empty. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance")
	EBeltBuiltInItem FallbackShape = EBeltBuiltInItem::Box;

	/** Scale applied to every instance of this type. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance")
	FVector MeshScale = FVector(1.0f, 1.0f, 1.0f);

	/** Extra rotation in belt space, applied before the belt's own orientation. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance")
	FRotator MeshRotation = FRotator::ZeroRotator;

	/**
	 * Offset in belt space: X along the direction of travel, Y across the belt, Z up from the surface.
	 * Raise Z by half the mesh height so the item sits on the belt rather than through it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance")
	FVector MeshOffset = FVector(0.0f, 0.0f, 0.0f);

	/**
	 * Tint for this type. Written to the instance set's per-instance custom data slots 2, 3 and 4, so
	 * one material can colour every type differently without one material instance per type.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance")
	FLinearColor Color = FLinearColor(0.55f, 0.55f, 0.58f, 1.0f);

	/** Let decals land on these instances. Off by default; thousands of decal receivers are not free. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance")
	bool bReceivesDecals = false;

	/** Let the instances cast a dynamic shadow. Off by default for the same reason. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance")
	bool bCastShadow = false;

	// ------------------------------------------------------------------------------------------------
	// Flow
	// ------------------------------------------------------------------------------------------------

	/**
	 * Half the room this item needs along the direction of travel, in cm.
	 *
	 * This is the only number the movement pass reads, and it is what makes a queue a queue: two
	 * neighbours are kept at least RadiusA + Gap + RadiusB apart, so wide items back up sooner than
	 * narrow ones on the same belt at the same speed. It is not a collision shape and nothing is traced
	 * against it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flow", meta = (ClampMin = "0.5", UIMax = "200.0"))
	float Radius = 30.0f;

	/**
	 * Weight in kg. Carried along so a belt or a node can refuse a load it is not rated for, and so a
	 * project can total up what is riding on a line. Nothing in the movement pass reads it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flow", meta = (ClampMin = "0.0"))
	float Weight = 1.0f;

	// ------------------------------------------------------------------------------------------------
	// Built-in types
	// ------------------------------------------------------------------------------------------------

	/** Path of the engine primitive a fallback shape maps to. */
	static const TCHAR* GetBuiltInMeshPath(EBeltBuiltInItem Shape);

	/**
	 * Build one of the four code types. Transient, owned by whoever passes the Outer.
	 *
	 * The subsystem calls this the first time something asks for a built-in type and keeps the result
	 * for the lifetime of the world, so the four assets exist at most once per world and only when
	 * they are used.
	 */
	static UBeltItemType* CreateBuiltIn(UObject* Outer, EBeltBuiltInItem Shape);

	/** Human-readable name, for the log and the statistics box. */
	FString GetDisplayName() const;

	/** Resolve Mesh, or load the primitive named by FallbackShape. May return null if neither exists. */
	UStaticMesh* ResolveMesh() const;
};
