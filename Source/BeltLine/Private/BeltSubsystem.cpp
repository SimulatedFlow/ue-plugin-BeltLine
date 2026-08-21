// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "BeltSubsystem.h"

#include "BeltActor.h"
#include "BeltItemType.h"
#include "BeltLineLog.h"
#include "BeltNode.h"
#include "BeltPath.h"
#include "BeltSettings.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SplineComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"

namespace BeltLinePrivate
{
	/** Belts a Belt.Test loop is made of. Four corners is the smallest shape that shows a real corner. */
	static constexpr int32 TestLoopBelts = 4;

	/** Ceiling on what Belt.Test will put on the loop, whatever the arguments say. */
	static constexpr int32 MaxTestItems = 200000;

	UBeltSubsystem* GetSubsystem(UWorld* World)
	{
		return World ? World->GetSubsystem<UBeltSubsystem>() : nullptr;
	}

	/** Somewhere in front of the viewpoint, so a console command builds where the camera is looking. */
	FVector ResolveCommandOrigin(UWorld* World)
	{
		if (const APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr)
		{
			FVector ViewLocation = FVector::ZeroVector;
			FRotator ViewRotation = FRotator::ZeroRotator;
			PlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);

			FVector Ahead = ViewRotation.Vector();
			Ahead.Z = 0.0f;
			Ahead = Ahead.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);

			return FVector(ViewLocation.X, ViewLocation.Y, 0.0f) + Ahead * 3000.0f;
		}

		return FVector::ZeroVector;
	}
}

// --------------------------------------------------------------------------------------------------
// Lifetime
// --------------------------------------------------------------------------------------------------

bool UBeltSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}

	const UWorld* World = Cast<UWorld>(Outer);
	if (!World)
	{
		return false;
	}

	return DoesSupportWorldType(World->WorldType);
}

bool UBeltSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	// Editor is in the list on purpose. A belt is a piece of level layout, and the useful question while
	// laying one out is "does this actually flow", which is not a question you can answer from a static
	// spline. The same subsystem, the same movement pass and the same instance sets run in the editor
	// viewport, so what is seen while building is what runs.
	return WorldType == EWorldType::Game
		|| WorldType == EWorldType::PIE
		|| WorldType == EWorldType::Editor;
}

void UBeltSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	ApplySettings();

	UE_LOG(LogBeltLine, Log, TEXT("BeltLine subsystem up. Item budget %d."), ItemBudget);
}

void UBeltSubsystem::Deinitialize()
{
	DestroyTestLoop();

	for (FBeltInstanceBatch& Batch : Batches)
	{
		if (IsValid(Batch.Component))
		{
			Batch.Component->DestroyComponent();
		}
	}
	Batches.Reset();

	if (IsValid(BatchHolder))
	{
		BatchHolder->Destroy();
	}
	BatchHolder = nullptr;

	Belts.Reset();
	Nodes.Reset();
	ItemTypes.Reset();
	BuiltInTypes.Reset();
	TypeRadii.Reset();
	TypeWeights.Reset();

	Super::Deinitialize();
}

void UBeltSubsystem::ApplySettings()
{
	const UBeltSettings& Settings = UBeltSettings::Get();

	ItemBudget = FMath::Max(0, Settings.MaxItems);
	MaxItemsPerBelt = FMath::Max(1, Settings.MaxItemsPerBelt);
	MaxItemTypes = FMath::Clamp(Settings.MaxItemTypes, 1, 255);
	MinItemPitch = FMath::Max(1.0f, Settings.MinItemPitch);
	UpdatesPerSecond = FMath::Max(0.0f, Settings.UpdatesPerSecond);
	MaxUpdateStep = FMath::Max(0.005f, Settings.MaxUpdateStep);
	PathSampleSpacing = FMath::Max(5.0f, Settings.PathSampleSpacing);
	bTickInEditorWorlds = Settings.bTickInEditorWorlds;
}

UBeltSubsystem* UBeltSubsystem::Get(const UObject* WorldContextObject)
{
	if (!WorldContextObject)
	{
		return nullptr;
	}

	UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;

	return World ? World->GetSubsystem<UBeltSubsystem>() : nullptr;
}

// --------------------------------------------------------------------------------------------------
// Ticking
// --------------------------------------------------------------------------------------------------

TStatId UBeltSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UBeltSubsystem, STATGROUP_Tickables);
}

bool UBeltSubsystem::IsTickableInEditor() const
{
	return bTickInEditorWorlds;
}

void UBeltSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (World->WorldType == EWorldType::Editor && !bTickInEditorWorlds)
	{
		return;
	}

	UpdateThroughputWindow(DeltaTime);

	// A fixed rate means the step handed to the movement pass is the time that actually went by, not
	// the frame's delta, or halving the rate would halve every belt's speed.
	if (UpdatesPerSecond > 0.0f)
	{
		UpdateAccumulator += DeltaTime;

		const float Interval = 1.0f / UpdatesPerSecond;
		if (UpdateAccumulator < Interval)
		{
			return;
		}

		const float Step = FMath::Min(UpdateAccumulator, MaxUpdateStep);
		UpdateAccumulator = 0.0f;
		UpdateBelts(Step);
		return;
	}

	UpdateBelts(FMath::Min(DeltaTime, MaxUpdateStep));
}

void UBeltSubsystem::UpdateThroughputWindow(float DeltaSeconds)
{
	ThroughputWindowTime += DeltaSeconds;
	if (ThroughputWindowTime < 1.0f)
	{
		return;
	}

	Stats.ThroughputPerSecond = static_cast<float>(DeliveredThisWindow) / ThroughputWindowTime;

	for (ABeltActor* Belt : Belts)
	{
		if (IsValid(Belt))
		{
			Belt->TickThroughputWindow(ThroughputWindowTime);
		}
	}

	DeliveredThisWindow = 0;
	ThroughputWindowTime = 0.0f;
}

// --------------------------------------------------------------------------------------------------
// The update
// --------------------------------------------------------------------------------------------------

FBeltUpdateContext UBeltSubsystem::MakeUpdateContext()
{
	FBeltUpdateContext Context;
	Context.TypeRadii = TypeRadii.GetData();
	Context.TypeWeights = TypeWeights.GetData();
	Context.NumTypes = TypeRadii.Num();
	return Context;
}

void UBeltSubsystem::UpdateBelts(float DeltaSeconds)
{
	const double UpdateStart = FPlatformTime::Seconds();

	FBeltUpdateContext Context = MakeUpdateContext();

	const double AdvanceStart = FPlatformTime::Seconds();
	AdvanceWorld(DeltaSeconds, Context);
	const double AdvanceEnd = FPlatformTime::Seconds();

	WriteInstances(Context);
	const double UpdateEnd = FPlatformTime::Seconds();

	RecountItems();

	DeliveredThisWindow += Context.ItemsDelivered;
	TotalDelivered += Context.ItemsDelivered;
	TotalBufferGrowth += Context.BufferGrowth;

	// Stats -----------------------------------------------------------------------------------------

	Stats.Belts = Belts.Num();
	Stats.Nodes = Nodes.Num();
	Stats.ItemTypes = ItemTypes.Num();
	Stats.Items = TotalItems;
	Stats.BufferedItems = TotalBufferedItems;
	Stats.ItemBudget = ItemBudget;
	Stats.JammedItems = Context.JammedItems;
	Stats.ItemsDelivered = TotalDelivered;
	Stats.SpawnsRejected = TotalSpawnsRejected;
	Stats.UpdatesPerSecond = UpdatesPerSecond;
	Stats.BufferGrowthThisUpdate = Context.BufferGrowth;
	Stats.BufferGrowthTotal = TotalBufferGrowth;
	Stats.UpdateMilliseconds = static_cast<float>((UpdateEnd - UpdateStart) * 1000.0);
	Stats.AdvanceMilliseconds = static_cast<float>((AdvanceEnd - AdvanceStart) * 1000.0);
	Stats.InstanceMilliseconds = static_cast<float>((UpdateEnd - AdvanceEnd) * 1000.0);

	int32 InstanceSets = 0;
	int32 InstanceSlots = 0;
	int64 BufferBytes = 0;

	for (const FBeltInstanceBatch& Batch : Batches)
	{
		if (Batch.LastWrittenCount > 0)
		{
			++InstanceSets;
		}
		if (IsValid(Batch.Component))
		{
			InstanceSlots += Batch.Component->GetInstanceCount();
		}
		BufferBytes += Batch.ScratchTransforms.GetAllocatedSize() + Batch.ScratchCustomData.GetAllocatedSize();
	}

	for (const ABeltActor* Belt : Belts)
	{
		if (IsValid(Belt))
		{
			BufferBytes += Belt->GetAllocatedSize();
		}
	}

	for (const ABeltNode* Node : Nodes)
	{
		if (IsValid(Node))
		{
			BufferBytes += Node->GetAllocatedSize();
		}
	}

	Stats.InstanceSets = InstanceSets;
	Stats.InstanceSlots = InstanceSlots;
	Stats.BufferKilobytes = static_cast<float>(BufferBytes) / 1024.0f;
}

void UBeltSubsystem::AdvanceWorld(float DeltaSeconds, FBeltUpdateContext& Context)
{
	// Pass 1 - every belt moves. The order is registration order and it is stable, but nothing depends
	// on it: an item handed forward lands at the receiving belt's entry, where it waits for the next
	// update whether that belt has already moved or not.
	for (ABeltActor* Belt : Belts)
	{
		if (!IsValid(Belt))
		{
			continue;
		}

		if (Belt->RefreshPathIfMoved())
		{
			Belt->RebuildRingBuffer(&Context);
		}

		Belt->AdvanceItems(DeltaSeconds, Context);
	}

	// Pass 2 - nodes hand on what they are holding, after every belt has moved. Doing this here rather
	// than inside the belt loop is what stops an item from crossing a node and half of the next belt in
	// a single update just because the actors happened to be registered in that order.
	for (ABeltNode* Node : Nodes)
	{
		if (IsValid(Node))
		{
			Node->DrainInto(Context);
		}
	}
}

void UBeltSubsystem::WriteInstances(FBeltUpdateContext& Context)
{
	if (Batches.Num() == 0)
	{
		return;
	}

	// Refresh the per-type presentation once, then never touch a UObject again inside the item loop.
	for (int32 TypeIndex = 0; TypeIndex < Batches.Num(); ++TypeIndex)
	{
		FBeltInstanceBatch& Batch = Batches[TypeIndex];
		Batch.ScratchTransforms.Reset();
		Batch.ScratchCustomData.Reset();

		if (const UBeltItemType* ItemType = ItemTypes.IsValidIndex(TypeIndex) ? ItemTypes[TypeIndex].Get() : nullptr)
		{
			Batch.CachedMeshRotation = ItemType->MeshRotation.Quaternion();
			Batch.CachedMeshOffset = ItemType->MeshOffset;
			Batch.CachedMeshScale = ItemType->MeshScale;
			Batch.CachedColor[0] = ItemType->Color.R;
			Batch.CachedColor[1] = ItemType->Color.G;
			Batch.CachedColor[2] = ItemType->Color.B;
		}
	}

	// Pass 3 - gather. One walk per belt over its ring, and per item two array lookups in the baked
	// path plus a lerp. No spline is evaluated here, and no per-item object is touched.
	for (const ABeltActor* Belt : Belts)
	{
		if (!IsValid(Belt))
		{
			continue;
		}

		const FBeltPath& Path = Belt->GetPath();
		if (!Path.bValid)
		{
			continue;
		}

		const int32 ItemCount = Belt->GetItemCount();
		if (ItemCount <= 0)
		{
			continue;
		}

		const float InvLength = Path.Length > KINDA_SMALL_NUMBER ? 1.0f / Path.Length : 0.0f;
		const FVector HeightOffset(0.0f, 0.0f, Belt->ItemHeightOffset);

		for (int32 QueuePosition = 0; QueuePosition < ItemCount; ++QueuePosition)
		{
			const int32 RingIndex = Belt->GetRingIndex(QueuePosition);
			const int32 TypeIndex = Belt->GetItemTypeAt(RingIndex);
			if (!Batches.IsValidIndex(TypeIndex))
			{
				continue;
			}

			FBeltInstanceBatch& Batch = Batches[TypeIndex];

			const float Distance = Belt->GetItemDistanceAt(RingIndex);

			FVector Position = FVector::ZeroVector;
			FQuat Rotation = FQuat::Identity;
			Path.Evaluate(Distance, Position, Rotation);

			const FVector Offset = Batch.CachedMeshOffset + HeightOffset;
			Position += Rotation.RotateVector(Offset);

			Batch.ScratchTransforms.Emplace(Rotation * Batch.CachedMeshRotation, Position, Batch.CachedMeshScale);
			Batch.LastLivePosition = Position;

			Batch.ScratchCustomData.Add(Distance * InvLength);
			Batch.ScratchCustomData.Add(Belt->IsItemJammedAt(RingIndex) ? 1.0f : 0.0f);
			Batch.ScratchCustomData.Add(Batch.CachedColor[0]);
			Batch.ScratchCustomData.Add(Batch.CachedColor[1]);
			Batch.ScratchCustomData.Add(Batch.CachedColor[2]);
		}
	}

	// Pass 4 - write. Two bulk calls per item type, and the render state is deliberately left alone:
	// the instance data manager streams the deltas to the GPU scene by itself, whereas marking the
	// state dirty would recreate the whole primitive proxy every frame.
	for (FBeltInstanceBatch& Batch : Batches)
	{
		UInstancedStaticMeshComponent* Component = Batch.Component;
		if (!IsValid(Component))
		{
			continue;
		}

		const int32 LiveCount = Batch.ScratchTransforms.Num();
		const int32 WriteCount = FMath::Max(LiveCount, Batch.LastWrittenCount);

		if (WriteCount <= 0)
		{
			Batch.LastWrittenCount = 0;
			continue;
		}

		// Slots that had an item last update and do not now are collapsed rather than removed.
		// RemoveInstance swaps the last instance into the hole, so every index above it would move -
		// and the count would go up and down with the item count, which is the reallocation this
		// design exists to avoid.
		for (int32 SlotIndex = LiveCount; SlotIndex < WriteCount; ++SlotIndex)
		{
			Batch.ScratchTransforms.Emplace(FQuat::Identity, Batch.LastLivePosition, FVector(BeltLine::HiddenInstanceScale));

			for (int32 FloatIndex = 0; FloatIndex < BeltLine::NumCustomDataFloats; ++FloatIndex)
			{
				Batch.ScratchCustomData.Add(0.0f);
			}
		}

		const int32 ExistingInstances = Component->GetInstanceCount();
		if (ExistingInstances < WriteCount)
		{
			// The only allocation in the whole update, and only while the world is still filling up.
			// Once the high-water mark is reached this never runs again, which is what the buffer
			// growth counter on the statistics box is there to show.
			TArray<FTransform> NewInstances;
			NewInstances.Reserve(WriteCount - ExistingInstances);
			for (int32 SlotIndex = ExistingInstances; SlotIndex < WriteCount; ++SlotIndex)
			{
				NewInstances.Add(Batch.ScratchTransforms[SlotIndex]);
			}

			Component->AddInstances(NewInstances, /*bShouldReturnIndices*/ false, /*bWorldSpace*/ true);
			++Context.BufferGrowth;
		}

		Component->BatchUpdateInstancesTransforms(
			0, Batch.ScratchTransforms, /*bWorldSpace*/ true, /*bMarkRenderStateDirty*/ false, /*bTeleport*/ true);

		if (Component->NumCustomDataFloats == BeltLine::NumCustomDataFloats
			&& Component->PerInstanceSMCustomData.Num() >= Batch.ScratchCustomData.Num())
		{
			Component->SetCustomData(0, WriteCount - 1, Batch.ScratchCustomData, /*bMarkRenderStateDirty*/ false);
		}

		Batch.LastWrittenCount = LiveCount;
	}
}

void UBeltSubsystem::RecountItems()
{
	int32 OnBelts = 0;
	for (const ABeltActor* Belt : Belts)
	{
		if (IsValid(Belt))
		{
			OnBelts += Belt->GetItemCount();
		}
	}

	int32 InNodes = 0;
	for (const ABeltNode* Node : Nodes)
	{
		if (IsValid(Node))
		{
			InNodes += Node->GetItemCount();
		}
	}

	TotalItems = OnBelts;
	TotalBufferedItems = InNodes;
}

// --------------------------------------------------------------------------------------------------
// Registration
// --------------------------------------------------------------------------------------------------

void UBeltSubsystem::RegisterBelt(ABeltActor* Belt)
{
	if (!IsValid(Belt) || Belts.Contains(Belt))
	{
		return;
	}

	Belts.Add(Belt);
	Belt->RefreshAllowedTypeIndices(*this);
}

void UBeltSubsystem::UnregisterBelt(ABeltActor* Belt)
{
	Belts.Remove(Belt);
	TestBelts.Remove(Belt);
}

void UBeltSubsystem::RegisterNode(ABeltNode* Node)
{
	if (!IsValid(Node) || Nodes.Contains(Node))
	{
		return;
	}

	Nodes.Add(Node);
	Node->RefreshAllowedTypeIndices(*this);
}

void UBeltSubsystem::UnregisterNode(ABeltNode* Node)
{
	Nodes.Remove(Node);
	TestNodes.Remove(Node);
}

TArray<ABeltActor*> UBeltSubsystem::GetBelts() const
{
	TArray<ABeltActor*> Result;
	Result.Reserve(Belts.Num());

	for (const TObjectPtr<ABeltActor>& Belt : Belts)
	{
		if (IsValid(Belt))
		{
			Result.Add(Belt);
		}
	}

	return Result;
}

// --------------------------------------------------------------------------------------------------
// Item types
// --------------------------------------------------------------------------------------------------

int32 UBeltSubsystem::FindItemTypeIndex(const UBeltItemType* ItemType) const
{
	if (!ItemType)
	{
		return INDEX_NONE;
	}

	for (int32 Index = 0; Index < ItemTypes.Num(); ++Index)
	{
		if (ItemTypes[Index] == ItemType)
		{
			return Index;
		}
	}

	return INDEX_NONE;
}

UBeltItemType* UBeltSubsystem::GetItemTypeByIndex(int32 TypeIndex) const
{
	return ItemTypes.IsValidIndex(TypeIndex) ? ItemTypes[TypeIndex] : nullptr;
}

int32 UBeltSubsystem::RegisterItemType(UBeltItemType* ItemType)
{
	if (!IsValid(ItemType))
	{
		return INDEX_NONE;
	}

	const int32 Existing = FindItemTypeIndex(ItemType);
	if (Existing != INDEX_NONE)
	{
		return Existing;
	}

	if (ItemTypes.Num() >= MaxItemTypes)
	{
		UE_LOG(LogBeltLine, Warning,
			TEXT("BeltLine: '%s' was refused - this world already holds %d item types, which is the configured ceiling."),
			*ItemType->GetName(), MaxItemTypes);
		return INDEX_NONE;
	}

	const int32 NewIndex = ItemTypes.Add(ItemType);
	TypeRadii.Add(FMath::Max(0.5f, ItemType->Radius));
	TypeWeights.Add(FMath::Max(0.0f, ItemType->Weight));

	FBeltInstanceBatch& Batch = Batches.AddDefaulted_GetRef();
	Batch.ItemType = ItemType;
	Batch.Component = CreateBatchComponent(*ItemType);

	// A belt or node whose filter names this type has been holding an unresolved entry until now.
	RefreshTypeFilters();

	UE_LOG(LogBeltLine, Verbose, TEXT("BeltLine: item type '%s' registered as index %d."), *ItemType->GetName(), NewIndex);

	return NewIndex;
}

UBeltItemType* UBeltSubsystem::GetBuiltInItemType(EBeltBuiltInItem Shape)
{
	if (const TObjectPtr<UBeltItemType>* Existing = BuiltInTypes.Find(Shape))
	{
		if (IsValid(*Existing))
		{
			return *Existing;
		}
	}

	UBeltItemType* Created = UBeltItemType::CreateBuiltIn(this, Shape);
	if (!Created)
	{
		return nullptr;
	}

	BuiltInTypes.Add(Shape, Created);
	RegisterItemType(Created);

	return Created;
}

void UBeltSubsystem::RefreshTypeFilters()
{
	for (ABeltActor* Belt : Belts)
	{
		if (IsValid(Belt))
		{
			Belt->RefreshAllowedTypeIndices(*this);
		}
	}

	for (ABeltNode* Node : Nodes)
	{
		if (IsValid(Node))
		{
			Node->RefreshAllowedTypeIndices(*this);
		}
	}
}

// --------------------------------------------------------------------------------------------------
// Instance sets
// --------------------------------------------------------------------------------------------------

void UBeltSubsystem::EnsureBatchHolder()
{
	if (IsValid(BatchHolder))
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
#if WITH_EDITOR
	SpawnParams.bHideFromSceneOutliner = true;
#endif

	BatchHolder = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, SpawnParams);
	if (!BatchHolder)
	{
		return;
	}

	BatchHolder->SetFlags(RF_Transient);

	USceneComponent* Root = NewObject<USceneComponent>(BatchHolder, TEXT("Root"), RF_Transient);
	Root->SetMobility(EComponentMobility::Movable);
	BatchHolder->SetRootComponent(Root);
	Root->RegisterComponent();

	BatchHolder->SetActorEnableCollision(false);
	BatchHolder->SetCanBeDamaged(false);
}

UInstancedStaticMeshComponent* UBeltSubsystem::CreateBatchComponent(const UBeltItemType& ItemType)
{
	EnsureBatchHolder();
	if (!IsValid(BatchHolder))
	{
		return nullptr;
	}

	UStaticMesh* Mesh = ItemType.ResolveMesh();
	if (!Mesh)
	{
		UE_LOG(LogBeltLine, Warning,
			TEXT("BeltLine: item type '%s' has no mesh. Its items will still move and still count; they just will not be drawn."),
			*ItemType.GetName());
		return nullptr;
	}

	const UBeltSettings& Settings = UBeltSettings::Get();

	// Plain ISM, not HISM, and for the same reason Material-Driven Shadows uses plain ISM: a
	// hierarchical component rebuilds its cluster tree whenever an instance's translation changes, and
	// every instance here moves every frame. That rebuild is precisely the cost this plugin exists to
	// remove. GPU-scene per-instance culling does the job without a tree.
	UInstancedStaticMeshComponent* Component = NewObject<UInstancedStaticMeshComponent>(BatchHolder, NAME_None, RF_Transient);
	if (!Component)
	{
		return nullptr;
	}

	Component->SetMobility(EComponentMobility::Movable);
	Component->SetStaticMesh(Mesh);
	if (ItemType.OverrideMaterial)
	{
		Component->SetMaterial(0, ItemType.OverrideMaterial);
	}

	Component->SetCastShadow(ItemType.bCastShadow);
	Component->bCastDynamicShadow = ItemType.bCastShadow;
	Component->bCastStaticShadow = false;
	Component->bCastVolumetricTranslucentShadow = false;
	Component->bCastContactShadow = false;
	Component->bAffectDynamicIndirectLighting = false;
	Component->bAffectDistanceFieldLighting = false;
	Component->bUseAsOccluder = false;
	Component->SetReceivesDecals(ItemType.bReceivesDecals);

	// No collision, no navigation, no overlap bookkeeping. An item is a picture of a crate; if a
	// project needs a physical crate it takes the item off the belt and spawns one.
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	Component->SetGenerateOverlapEvents(false);
	Component->SetCanEverAffectNavigation(false);

	// One set covers every belt in the world, so its bounds are the whole layout and a little headroom
	// keeps an item handed to a distant belt from popping in a frame late.
	Component->BoundsScale = FMath::Max(1.0f, Settings.InstanceBoundsScale);
	Component->SetCullDistances(
		FMath::Max(0, Settings.InstanceStartCullDistance),
		FMath::Max(0, Settings.InstanceEndCullDistance));

	Component->SetNumCustomDataFloats(BeltLine::NumCustomDataFloats);

	Component->SetupAttachment(BatchHolder->GetRootComponent());
	Component->RegisterComponent();
	BatchHolder->AddInstanceComponent(Component);

	return Component;
}

// --------------------------------------------------------------------------------------------------
// Spawning and taking
// --------------------------------------------------------------------------------------------------

bool UBeltSubsystem::SpawnItem(ABeltActor* Belt, UBeltItemType* ItemType)
{
	if (!IsValid(Belt) || !IsValid(ItemType))
	{
		++TotalSpawnsRejected;
		return false;
	}

	const int32 TypeIndex = RegisterItemType(ItemType);
	if (TypeIndex == INDEX_NONE)
	{
		++TotalSpawnsRejected;
		return false;
	}

	if (TotalItems >= ItemBudget)
	{
		++TotalSpawnsRejected;
		return false;
	}

	const FBeltUpdateContext Context = MakeUpdateContext();
	if (!Belt->PushItemAtEntry(TypeIndex, Context))
	{
		++TotalSpawnsRejected;
		return false;
	}

	++TotalItems;
	return true;
}

int32 UBeltSubsystem::SpawnItems(ABeltActor* Belt, UBeltItemType* ItemType, int32 Count)
{
	if (!IsValid(Belt) || !IsValid(ItemType) || Count <= 0)
	{
		return 0;
	}

	const int32 TypeIndex = RegisterItemType(ItemType);
	if (TypeIndex == INDEX_NONE)
	{
		++TotalSpawnsRejected;
		return 0;
	}

	const int32 Allowed = FMath::Min(Count, GetRemainingBudget());
	if (Allowed <= 0)
	{
		++TotalSpawnsRejected;
		return 0;
	}

	const FBeltUpdateContext Context = MakeUpdateContext();
	const int32 Placed = Belt->FillWithItems(TypeIndex, Allowed, Context);

	TotalItems += Placed;
	if (Placed < Count)
	{
		++TotalSpawnsRejected;
	}

	return Placed;
}

int32 UBeltSubsystem::SpawnItemsAcrossBelts(UBeltItemType* ItemType, int32 Count)
{
	if (!IsValid(ItemType) || Count <= 0 || Belts.Num() == 0)
	{
		return 0;
	}

	const int32 TypeIndex = RegisterItemType(ItemType);
	if (TypeIndex == INDEX_NONE)
	{
		++TotalSpawnsRejected;
		return 0;
	}

	int32 Remaining = FMath::Min(Count, GetRemainingBudget());
	if (Remaining <= 0)
	{
		++TotalSpawnsRejected;
		return 0;
	}

	const FBeltUpdateContext Context = MakeUpdateContext();
	int32 Placed = 0;

	// Two rounds: an even share first so no belt is loaded to the brim while the next stays empty, then
	// a mop-up round that gives whatever is left to whoever still has room.
	for (int32 Round = 0; Round < 2 && Remaining > 0; ++Round)
	{
		int32 BeltsLeft = Belts.Num();

		for (ABeltActor* Belt : Belts)
		{
			if (Remaining <= 0)
			{
				break;
			}

			--BeltsLeft;

			if (!IsValid(Belt))
			{
				continue;
			}

			const int32 Share = (Round == 0 && BeltsLeft > 0)
				? FMath::DivideAndRoundUp(Remaining, BeltsLeft + 1)
				: Remaining;

			const int32 Added = Belt->FillWithItems(TypeIndex, Share, Context);
			Placed += Added;
			Remaining -= Added;
		}
	}

	TotalItems += Placed;
	if (Placed < Count)
	{
		++TotalSpawnsRejected;
	}

	return Placed;
}

bool UBeltSubsystem::TryTakeItem(ABeltActor* Belt, UBeltItemType*& OutItemType)
{
	OutItemType = nullptr;

	if (!IsValid(Belt))
	{
		return false;
	}

	int32 TypeIndex = INDEX_NONE;
	if (!Belt->TryTakeFrontItem(TypeIndex))
	{
		return false;
	}

	OutItemType = GetItemTypeByIndex(TypeIndex);
	TotalItems = FMath::Max(0, TotalItems - 1);

	return true;
}

void UBeltSubsystem::ClearAllItems()
{
	for (ABeltActor* Belt : Belts)
	{
		if (IsValid(Belt))
		{
			Belt->ClearItems();
		}
	}

	for (ABeltNode* Node : Nodes)
	{
		if (IsValid(Node))
		{
			Node->ClearItems();
		}
	}

	TotalItems = 0;
	TotalBufferedItems = 0;
}

// --------------------------------------------------------------------------------------------------
// Budget
// --------------------------------------------------------------------------------------------------

void UBeltSubsystem::SetItemBudget(int32 NewBudget)
{
	ItemBudget = FMath::Max(0, NewBudget);

	RecountItems();
	EnforceBudget();

	Stats.ItemBudget = ItemBudget;
	Stats.Items = TotalItems;
}

void UBeltSubsystem::EnforceBudget()
{
	int32 Excess = TotalItems - ItemBudget;
	if (Excess <= 0)
	{
		return;
	}

	// Items come off the back of belts - the ones that got on most recently - so what is already
	// halfway to the end still arrives and the line does not develop holes in the middle.
	bool bRemovedAny = true;
	while (Excess > 0 && bRemovedAny)
	{
		bRemovedAny = false;

		for (ABeltActor* Belt : Belts)
		{
			if (Excess <= 0)
			{
				break;
			}

			int32 TypeIndex = INDEX_NONE;
			if (IsValid(Belt) && Belt->TryTakeBackItem(TypeIndex))
			{
				--Excess;
				--TotalItems;
				bRemovedAny = true;
			}
		}
	}

	// Anything still over the line is sitting in node buffers.
	bRemovedAny = true;
	while (Excess > 0 && bRemovedAny)
	{
		bRemovedAny = false;

		for (ABeltNode* Node : Nodes)
		{
			if (Excess <= 0)
			{
				break;
			}

			int32 TypeIndex = INDEX_NONE;
			if (IsValid(Node) && Node->TryTakeItem(TypeIndex))
			{
				--Excess;
				bRemovedAny = true;
			}
		}
	}
}

// --------------------------------------------------------------------------------------------------
// Belts
// --------------------------------------------------------------------------------------------------

void UBeltSubsystem::SetAllBeltSpeeds(float NewSpeed)
{
	for (ABeltActor* Belt : Belts)
	{
		if (IsValid(Belt))
		{
			Belt->SetSpeed(NewSpeed);
		}
	}
}

void UBeltSubsystem::ScaleAllBeltSpeeds(float Scale)
{
	if (Scale <= 0.0f)
	{
		return;
	}

	for (ABeltActor* Belt : Belts)
	{
		if (IsValid(Belt))
		{
			Belt->SetSpeed(Belt->GetSpeed() * Scale);
		}
	}
}

int32 UBeltSubsystem::SetAllOutputsBlocked(bool bNewBlocked)
{
	int32 Affected = 0;

	for (ABeltActor* Belt : Belts)
	{
		if (!IsValid(Belt))
		{
			continue;
		}

		// Only the belts that actually end the line. Holding a belt in the middle would stop the flow
		// too, but it would stop it where nobody is looking; the queue is meant to grow from the end.
		if (Belt->OutputNode == nullptr && Belt->OutputBelt == nullptr)
		{
			Belt->SetOutputBlocked(bNewBlocked);
			++Affected;
		}
	}

	return Affected;
}

int32 UBeltSubsystem::SetAllNodesSplitEnabled(bool bNewSplitEnabled)
{
	int32 Affected = 0;

	for (ABeltNode* Node : Nodes)
	{
		if (IsValid(Node) && Node->OutputBelts.Num() > 1)
		{
			Node->SetSplitEnabled(bNewSplitEnabled);
			++Affected;
		}
	}

	return Affected;
}

float UBeltSubsystem::GetAverageBeltSpeed() const
{
	float Total = 0.0f;
	int32 Counted = 0;

	for (const ABeltActor* Belt : Belts)
	{
		if (IsValid(Belt))
		{
			Total += Belt->GetSpeed();
			++Counted;
		}
	}

	return Counted > 0 ? Total / static_cast<float>(Counted) : 0.0f;
}

void UBeltSubsystem::SetUpdatesPerSecond(float NewRate)
{
	UpdatesPerSecond = FMath::Max(0.0f, NewRate);
	UpdateAccumulator = 0.0f;
	Stats.UpdatesPerSecond = UpdatesPerSecond;
}

// --------------------------------------------------------------------------------------------------
// The test loop
// --------------------------------------------------------------------------------------------------

void UBeltSubsystem::DestroyTestLoop()
{
	for (ABeltActor* Belt : TestBelts)
	{
		if (IsValid(Belt))
		{
			Belt->Destroy();
		}
	}
	TestBelts.Reset();

	for (ABeltNode* Node : TestNodes)
	{
		if (IsValid(Node))
		{
			Node->Destroy();
		}
	}
	TestNodes.Reset();

	RecountItems();
}

int32 UBeltSubsystem::BuildTestLoop(int32 ItemCount, const FVector& Origin, float LoopRadius)
{
	DestroyTestLoop();

	UWorld* World = GetWorld();
	if (!World || ItemCount <= 0)
	{
		return 0;
	}

	ItemCount = FMath::Clamp(ItemCount, 0, BeltLinePrivate::MaxTestItems);
	LoopRadius = FMath::Max(200.0f, LoopRadius);

	// A closed loop, not a line into a sink: the items stay in the world, so the item count on the
	// statistics box holds still while the instance set count sits at four. A line would drain and the
	// numbers would tell you nothing.
	const FVector Corners[BeltLinePrivate::TestLoopBelts] =
	{
		Origin + FVector(-LoopRadius, -LoopRadius, 0.0f),
		Origin + FVector( LoopRadius, -LoopRadius, 0.0f),
		Origin + FVector( LoopRadius,  LoopRadius, 0.0f),
		Origin + FVector(-LoopRadius,  LoopRadius, 0.0f)
	};

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
#if WITH_EDITOR
	SpawnParams.bHideFromSceneOutliner = true;
#endif

	for (int32 CornerIndex = 0; CornerIndex < BeltLinePrivate::TestLoopBelts; ++CornerIndex)
	{
		ABeltNode* Node = World->SpawnActor<ABeltNode>(ABeltNode::StaticClass(), FTransform(Corners[CornerIndex]), SpawnParams);
		if (!Node)
		{
			DestroyTestLoop();
			return 0;
		}

		Node->Capacity = 8;
		Node->RebuildBuffer(nullptr);
		TestNodes.Add(Node);
	}

	for (int32 BeltIndex = 0; BeltIndex < BeltLinePrivate::TestLoopBelts; ++BeltIndex)
	{
		ABeltActor* Belt = World->SpawnActor<ABeltActor>(ABeltActor::StaticClass(), FTransform::Identity, SpawnParams);
		if (!Belt || !Belt->Spline)
		{
			DestroyTestLoop();
			return 0;
		}

		const FVector Start = Corners[BeltIndex];
		const FVector End = Corners[(BeltIndex + 1) % BeltLinePrivate::TestLoopBelts];

		Belt->Spline->ClearSplinePoints(false);
		Belt->Spline->AddSplinePoint(Start, ESplineCoordinateSpace::World, false);
		Belt->Spline->AddSplinePoint(End, ESplineCoordinateSpace::World, false);
		Belt->Spline->SetSplinePointType(0, ESplinePointType::Linear, false);
		Belt->Spline->SetSplinePointType(1, ESplinePointType::Linear, false);
		Belt->Spline->UpdateSpline();

		Belt->RebuildPath();
		Belt->RebuildRingBuffer(nullptr);
		Belt->SetSpeed(UBeltSettings::Get().DefaultSpeed);

		TestBelts.Add(Belt);
	}

	// Belt i runs from corner i to corner i+1 and empties into the node standing there; that node feeds
	// belt i+1. Four belts, four nodes, one closed circuit.
	for (int32 BeltIndex = 0; BeltIndex < TestBelts.Num(); ++BeltIndex)
	{
		const int32 NextIndex = (BeltIndex + 1) % TestBelts.Num();
		TestBelts[BeltIndex]->OutputNode = TestNodes[NextIndex];
		TestNodes[NextIndex]->OutputBelts.Reset();
		TestNodes[NextIndex]->OutputBelts.Add(TestBelts[NextIndex]);
	}

	// One item type per belt, so the statistics box reads exactly four instance sets - one draw call
	// each - however many items are on the loop.
	const EBeltBuiltInItem Shapes[BeltLinePrivate::TestLoopBelts] =
	{
		EBeltBuiltInItem::Box,
		EBeltBuiltInItem::Barrel,
		EBeltBuiltInItem::Ore,
		EBeltBuiltInItem::Plate
	};

	int32 Placed = 0;
	const int32 PerBelt = FMath::DivideAndRoundUp(ItemCount, TestBelts.Num());

	for (int32 BeltIndex = 0; BeltIndex < TestBelts.Num(); ++BeltIndex)
	{
		UBeltItemType* Type = GetBuiltInItemType(Shapes[BeltIndex]);
		Placed += SpawnItems(TestBelts[BeltIndex], Type, FMath::Min(PerBelt, ItemCount - Placed));
	}

	RecountItems();

	return Placed;
}

// --------------------------------------------------------------------------------------------------
// Stats
// --------------------------------------------------------------------------------------------------

void UBeltSubsystem::LogStats() const
{
	UE_LOG(LogBeltLine, Display, TEXT("--- BeltLine ---"));
	UE_LOG(LogBeltLine, Display, TEXT("  Belts              %d  (%d nodes)"), Stats.Belts, Stats.Nodes);
	UE_LOG(LogBeltLine, Display, TEXT("  Items              %d / %d  (%d buffered in nodes)"), Stats.Items, Stats.ItemBudget, Stats.BufferedItems);
	UE_LOG(LogBeltLine, Display, TEXT("  Jammed             %d"), Stats.JammedItems);
	UE_LOG(LogBeltLine, Display, TEXT("  Instance sets      %d  (one draw call each, %d slots)"), Stats.InstanceSets, Stats.InstanceSlots);
	UE_LOG(LogBeltLine, Display, TEXT("  Item types         %d"), Stats.ItemTypes);
	UE_LOG(LogBeltLine, Display, TEXT("  Throughput         %.1f /s  (%d delivered, %d spawns refused)"),
		Stats.ThroughputPerSecond, Stats.ItemsDelivered, Stats.SpawnsRejected);
	UE_LOG(LogBeltLine, Display, TEXT("  Update             %.3f ms  (advance %.3f, instances %.3f)"),
		Stats.UpdateMilliseconds, Stats.AdvanceMilliseconds, Stats.InstanceMilliseconds);
	UE_LOG(LogBeltLine, Display, TEXT("  Buffer growth      %d this update, %d total"), Stats.BufferGrowthThisUpdate, Stats.BufferGrowthTotal);
	UE_LOG(LogBeltLine, Display, TEXT("  Buffers            %.1f KB"), Stats.BufferKilobytes);
	UE_LOG(LogBeltLine, Display, TEXT("  Update rate        %s"),
		Stats.UpdatesPerSecond > 0.0f ? *FString::Printf(TEXT("%.0f Hz"), Stats.UpdatesPerSecond) : TEXT("every frame"));
}

// --------------------------------------------------------------------------------------------------
// Console commands
// --------------------------------------------------------------------------------------------------

namespace BeltLinePrivate
{
	static FAutoConsoleCommandWithWorldAndArgs CmdTest(
		TEXT("Belt.Test"),
		TEXT("Belt.Test [Items] [LoopRadius] - build a closed loop of four belts in front of the camera and fill it. 0 removes it."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			UBeltSubsystem* Subsystem = GetSubsystem(World);
			if (!Subsystem)
			{
				UE_LOG(LogBeltLine, Warning, TEXT("Belt.Test: no BeltLine subsystem in this world."));
				return;
			}

			const int32 ItemCount = Args.Num() > 0
				? FMath::Clamp(FCString::Atoi(*Args[0]), 0, MaxTestItems)
				: UBeltSettings::Get().TestItemCount;

			if (ItemCount <= 0)
			{
				Subsystem->DestroyTestLoop();
				UE_LOG(LogBeltLine, Display, TEXT("Belt.Test: loop removed."));
				return;
			}

			const float LoopRadius = Args.Num() > 1 ? FMath::Max(200.0f, FCString::Atof(*Args[1])) : 4000.0f;
			const int32 Placed = Subsystem->BuildTestLoop(ItemCount, ResolveCommandOrigin(World), LoopRadius);

			UE_LOG(LogBeltLine, Display,
				TEXT("Belt.Test: %d items on a four-belt loop. Watch the instance set count on the stats box - it stays at four."),
				Placed);
		}));

	static FAutoConsoleCommandWithWorldAndArgs CmdBudget(
		TEXT("Belt.Budget"),
		TEXT("Belt.Budget [Items] - read or set the world's item ceiling. Lowering it trims from the back of the belts."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			UBeltSubsystem* Subsystem = GetSubsystem(World);
			if (!Subsystem)
			{
				UE_LOG(LogBeltLine, Warning, TEXT("Belt.Budget: no BeltLine subsystem in this world."));
				return;
			}

			if (Args.Num() == 0)
			{
				UE_LOG(LogBeltLine, Display, TEXT("Belt.Budget: %d items allowed, %d on the belts."),
					Subsystem->GetItemBudget(), Subsystem->GetItemCount());
				return;
			}

			const int32 NewBudget = FMath::Max(0, FCString::Atoi(*Args[0]));
			Subsystem->SetItemBudget(NewBudget);

			UE_LOG(LogBeltLine, Display,
				TEXT("Belt.Budget: ceiling is now %d, %d items on the belts. The instance slot count does not move."),
				Subsystem->GetItemBudget(), Subsystem->GetItemCount());
		}));

	static FAutoConsoleCommandWithWorldAndArgs CmdClear(
		TEXT("Belt.Clear"),
		TEXT("Belt.Clear - take every item off every belt and out of every node. Frees nothing and nothing regrows."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			UBeltSubsystem* Subsystem = GetSubsystem(World);
			if (!Subsystem)
			{
				UE_LOG(LogBeltLine, Warning, TEXT("Belt.Clear: no BeltLine subsystem in this world."));
				return;
			}

			Subsystem->ClearAllItems();
			UE_LOG(LogBeltLine, Display, TEXT("Belt.Clear: the belts are empty."));
		}));

	static FAutoConsoleCommandWithWorldAndArgs CmdStats(
		TEXT("Belt.Stats"),
		TEXT("Belt.Stats - print the measured counters to the log."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			const UBeltSubsystem* Subsystem = GetSubsystem(World);
			if (!Subsystem)
			{
				UE_LOG(LogBeltLine, Warning, TEXT("Belt.Stats: no BeltLine subsystem in this world."));
				return;
			}

			Subsystem->LogStats();
		}));
}
