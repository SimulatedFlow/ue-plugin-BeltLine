// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "BeltItemType.h"

#include "BeltLineLog.h"
#include "Engine/StaticMesh.h"
#include "UObject/Package.h"

UBeltItemType::UBeltItemType()
{
}

const TCHAR* UBeltItemType::GetBuiltInMeshPath(EBeltBuiltInItem Shape)
{
	// The engine's basic shapes ship with every install, cook into a packaged build and need no plugin
	// content, which is the whole point of having code types at all: a fresh project can put something
	// on a belt in one line without importing anything.
	switch (Shape)
	{
	case EBeltBuiltInItem::Barrel:	return TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
	case EBeltBuiltInItem::Ore:		return TEXT("/Engine/BasicShapes/Sphere.Sphere");
	case EBeltBuiltInItem::Plate:	return TEXT("/Engine/BasicShapes/Cube.Cube");
	case EBeltBuiltInItem::Box:
	default:						return TEXT("/Engine/BasicShapes/Cube.Cube");
	}
}

UStaticMesh* UBeltItemType::ResolveMesh() const
{
	if (Mesh)
	{
		return Mesh;
	}

	const TCHAR* Path = GetBuiltInMeshPath(FallbackShape);
	return LoadObject<UStaticMesh>(nullptr, Path);
}

FString UBeltItemType::GetDisplayName() const
{
	return GetName();
}

UBeltItemType* UBeltItemType::CreateBuiltIn(UObject* Outer, EBeltBuiltInItem Shape)
{
	if (!Outer)
	{
		Outer = GetTransientPackage();
	}

	// The engine primitives are all 100 cm across, so every number below is a fraction of a metre and
	// the radii are the true half-extent along the belt after scaling - not a guess. Getting that wrong
	// is what makes a self-built conveyor look like the crates are inside one another.
	FName AssetName;
	FVector Scale = FVector::OneVector;
	FVector Offset = FVector::ZeroVector;
	FLinearColor Color = FLinearColor::White;
	float Radius = 30.0f;
	float Weight = 1.0f;

	switch (Shape)
	{
	case EBeltBuiltInItem::Barrel:
		// Cylinder, 60 cm across and 80 cm tall, standing on the belt.
		AssetName = TEXT("BeltItem_Barrel");
		Scale = FVector(0.6f, 0.6f, 0.8f);
		Offset = FVector(0.0f, 0.0f, 40.0f);
		Color = FLinearColor(0.72f, 0.36f, 0.14f, 1.0f);
		Radius = 32.0f;
		Weight = 60.0f;
		break;

	case EBeltBuiltInItem::Ore:
		// Sphere, 36 cm across. The smallest of the four, so a belt carries the most of them.
		AssetName = TEXT("BeltItem_Ore");
		Scale = FVector(0.36f, 0.36f, 0.36f);
		Offset = FVector(0.0f, 0.0f, 18.0f);
		Color = FLinearColor(0.35f, 0.40f, 0.48f, 1.0f);
		Radius = 20.0f;
		Weight = 12.0f;
		break;

	case EBeltBuiltInItem::Plate:
		// Flat sheet, 110 cm along travel and 12 cm thick. The widest of the four: it jams first, and
		// on a belt shared with the others that is immediately visible.
		AssetName = TEXT("BeltItem_Plate");
		Scale = FVector(1.1f, 0.75f, 0.12f);
		Offset = FVector(0.0f, 0.0f, 6.0f);
		Color = FLinearColor(0.62f, 0.66f, 0.72f, 1.0f);
		Radius = 58.0f;
		Weight = 40.0f;
		break;

	case EBeltBuiltInItem::Box:
	default:
		// Crate, 60 cm cube. The default thing on a conveyor.
		AssetName = TEXT("BeltItem_Box");
		Scale = FVector(0.6f, 0.6f, 0.6f);
		Offset = FVector(0.0f, 0.0f, 30.0f);
		Color = FLinearColor(0.58f, 0.44f, 0.24f, 1.0f);
		Radius = 32.0f;
		Weight = 25.0f;
		break;
	}

	UBeltItemType* Type = NewObject<UBeltItemType>(Outer, AssetName, RF_Transient);
	if (!Type)
	{
		return nullptr;
	}

	Type->FallbackShape = Shape;
	Type->MeshScale = Scale;
	Type->MeshOffset = Offset;
	Type->Color = Color;
	Type->Radius = Radius;
	Type->Weight = Weight;

	if (!Type->ResolveMesh())
	{
		UE_LOG(LogBeltLine, Warning,
			TEXT("Built-in item type '%s' found no mesh at '%s'. It will still move on the belt, it just will not be drawn."),
			*AssetName.ToString(), GetBuiltInMeshPath(Shape));
	}

	return Type;
}
