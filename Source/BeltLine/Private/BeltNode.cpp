// Copyright 2026 Simulated Flow. All Rights Reserved.

#include "BeltNode.h"

#include "BeltActor.h"
#include "BeltItemType.h"
#include "BeltSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"

ABeltNode::ABeltNode()
{
	// Like the belts, a node does not tick. The subsystem drains every node in one pass, after every
	// belt has moved, which is what keeps a merge from depending on actor order.
	PrimaryActorTick.bCanEverTick = false;
	PrimaryActorTick.bStartWithTickEnabled = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	NodeMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("NodeMesh"));
	NodeMeshComponent->SetupAttachment(SceneRoot);
	NodeMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	NodeMeshComponent->SetCanEverAffectNavigation(false);
	NodeMeshComponent->SetGenerateOverlapEvents(false);
}

// --------------------------------------------------------------------------------------------------
// Lifetime
// --------------------------------------------------------------------------------------------------

void ABeltNode::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	RebuildBuffer(nullptr);

	if (NodeMeshComponent)
	{
		NodeMeshComponent->SetStaticMesh(NodeMesh);
		NodeMeshComponent->SetRelativeScale3D(NodeMeshScale);
	}
}

void ABeltNode::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();

	RegisterWithSubsystem();
}

void ABeltNode::PostUnregisterAllComponents()
{
	UnregisterFromSubsystem();

	Super::PostUnregisterAllComponents();
}

#if WITH_EDITOR
void ABeltNode::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	RebuildBuffer(nullptr);

	if (UBeltSubsystem* Subsystem = UBeltSubsystem::Get(this))
	{
		RefreshAllowedTypeIndices(*Subsystem);
	}
}
#endif

void ABeltNode::RegisterWithSubsystem()
{
	if (bRegistered)
	{
		return;
	}

	if (UBeltSubsystem* Subsystem = UBeltSubsystem::Get(this))
	{
		Subsystem->RegisterNode(this);
		bRegistered = true;
	}
}

void ABeltNode::UnregisterFromSubsystem()
{
	if (!bRegistered)
	{
		return;
	}

	if (UBeltSubsystem* Subsystem = UBeltSubsystem::Get(this))
	{
		Subsystem->UnregisterNode(this);
	}

	bRegistered = false;
}

void ABeltNode::RebuildBuffer(FBeltUpdateContext* Context)
{
	const int32 NeededCapacity = FMath::Max(1, Capacity);
	if (NeededCapacity == BufferCapacity)
	{
		return;
	}

	Buffer.SetNumUninitialized(NeededCapacity, EAllowShrinking::Yes);
	BufferCapacity = NeededCapacity;
	BufferHead = 0;
	BufferCount = 0;

	if (Context)
	{
		++Context->BufferGrowth;
	}
}

// --------------------------------------------------------------------------------------------------
// Type filter
// --------------------------------------------------------------------------------------------------

void ABeltNode::RefreshAllowedTypeIndices(UBeltSubsystem& Subsystem)
{
	AllowedTypeIndices.Reset();

	for (const TObjectPtr<UBeltItemType>& Type : AllowedTypes)
	{
		if (!Type)
		{
			continue;
		}

		const int32 TypeIndex = Subsystem.RegisterItemType(Type);
		if (TypeIndex != INDEX_NONE)
		{
			AllowedTypeIndices.AddUnique(TypeIndex);
		}
	}

	bHasTypeFilter = AllowedTypeIndices.Num() > 0;
}

bool ABeltNode::AllowsItemType(const UBeltItemType* InItemType) const
{
	if (!InItemType)
	{
		return false;
	}

	if (AllowedTypes.Num() == 0)
	{
		return true;
	}

	return AllowedTypes.Contains(InItemType);
}

// --------------------------------------------------------------------------------------------------
// Blueprint surface
// --------------------------------------------------------------------------------------------------

void ABeltNode::SetBlocked(bool bNewBlocked)
{
	bBlocked = bNewBlocked;
}

void ABeltNode::SetSplitEnabled(bool bNewSplitEnabled)
{
	bSplitEnabled = bNewSplitEnabled;
}

void ABeltNode::ClearItems()
{
	BufferHead = 0;
	BufferCount = 0;
}

// --------------------------------------------------------------------------------------------------
// Buffer
// --------------------------------------------------------------------------------------------------

bool ABeltNode::CanAccept(int32 TypeIndex) const
{
	if (bBlocked || BufferCapacity <= 0 || BufferCount >= BufferCapacity)
	{
		return false;
	}

	return AllowsTypeIndex(TypeIndex);
}

bool ABeltNode::TryAccept(int32 TypeIndex)
{
	if (!CanAccept(TypeIndex))
	{
		return false;
	}

	const int32 Raw = BufferHead + BufferCount;
	const int32 TailIndex = Raw < BufferCapacity ? Raw : Raw - BufferCapacity;

	Buffer[TailIndex] = static_cast<uint8>(TypeIndex);
	++BufferCount;

	return true;
}

bool ABeltNode::TryTakeItem(int32& OutTypeIndex)
{
	if (BufferCount <= 0)
	{
		return false;
	}

	OutTypeIndex = Buffer[BufferHead];
	BufferHead = (BufferHead + 1 < BufferCapacity) ? BufferHead + 1 : 0;
	--BufferCount;

	return true;
}

void ABeltNode::DrainInto(FBeltUpdateContext& Context)
{
	if (bBlocked || BufferCount <= 0 || OutputBelts.Num() == 0)
	{
		return;
	}

	const int32 NumOutputs = bSplitEnabled ? OutputBelts.Num() : 1;
	const int32 MaxHandOffs = MaxHandOffsPerUpdate > 0 ? MaxHandOffsPerUpdate : BufferCount;
	int32 HandedOff = 0;

	while (BufferCount > 0 && HandedOff < MaxHandOffs)
	{
		const int32 TypeIndex = Buffer[BufferHead];
		bool bPlaced = false;

		// Every output is tried once for this item before giving up, so a full branch never stops the
		// main line - which is the difference between a splitter and a bottleneck with two exits.
		for (int32 Attempt = 0; Attempt < NumOutputs && !bPlaced; ++Attempt)
		{
			const int32 OutputIndex = (Distribution == EBeltNodeDistribution::RoundRobin)
				? (OutputCursor + Attempt) % NumOutputs
				: Attempt;

			ABeltActor* Target = OutputBelts[OutputIndex];
			if (!IsValid(Target))
			{
				continue;
			}

			if (Target->PushItemAtEntry(TypeIndex, Context))
			{
				bPlaced = true;

				if (Distribution == EBeltNodeDistribution::RoundRobin)
				{
					// Move on from wherever the item actually landed, not from where we started
					// looking, or a stalled first output would be asked first for ever.
					OutputCursor = (OutputIndex + 1) % NumOutputs;
				}
			}
		}

		if (!bPlaced)
		{
			// Nothing will take it this update. Leaving it at the head is what makes the belts feeding
			// this node back up instead of overwriting each other.
			break;
		}

		BufferHead = (BufferHead + 1 < BufferCapacity) ? BufferHead + 1 : 0;
		--BufferCount;
		++HandedOff;
	}
}
