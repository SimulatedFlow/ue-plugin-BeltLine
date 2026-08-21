// Copyright 2026 Simulated Flow. All Rights Reserved.

#include "BeltActor.h"

#include "BeltItemType.h"
#include "BeltLineLog.h"
#include "BeltNode.h"
#include "BeltSettings.h"
#include "BeltSubsystem.h"
#include "Components/SplineComponent.h"
#include "Components/SplineMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"

ABeltActor::ABeltActor()
{
	// Nothing on this actor ticks. The subsystem drives every belt in one pass, which is the difference
	// between one ordered update and a few hundred actor ticks in whatever order the engine felt like.
	PrimaryActorTick.bCanEverTick = false;
	PrimaryActorTick.bStartWithTickEnabled = false;

	Spline = CreateDefaultSubobject<USplineComponent>(TEXT("Spline"));
	RootComponent = Spline;

	// Movable, not Static: a belt may be dragged in the editor, spawned at runtime, or carried by a
	// moving platform, and the spline mesh segments attached under it cannot be more mobile than their
	// parent. Nothing about the item path depends on this - the items are instances in a component
	// somewhere else entirely - so the only thing Static would buy is baked lighting on the belt frame.
	Spline->SetMobility(EComponentMobility::Movable);

	// A default belt is a straight ten-metre run, so a freshly dropped actor already carries items.
	Spline->ClearSplinePoints(false);
	Spline->AddSplinePoint(FVector::ZeroVector, ESplineCoordinateSpace::Local, false);
	Spline->AddSplinePoint(FVector(1000.0f, 0.0f, 0.0f), ESplineCoordinateSpace::Local, false);
	Spline->SetSplinePointType(0, ESplinePointType::Linear, false);
	Spline->SetSplinePointType(1, ESplinePointType::Linear, false);
	Spline->UpdateSpline();
}

// --------------------------------------------------------------------------------------------------
// Lifetime
// --------------------------------------------------------------------------------------------------

void ABeltActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	RebuildPath();
	RebuildRingBuffer(nullptr);
	RebuildBeltMesh();
}

void ABeltActor::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	if (!Path.bValid)
	{
		RebuildPath();
		RebuildRingBuffer(nullptr);
	}
}

void ABeltActor::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();

	RegisterWithSubsystem();
}

void ABeltActor::PostUnregisterAllComponents()
{
	UnregisterFromSubsystem();

	Super::PostUnregisterAllComponents();
}

#if WITH_EDITOR
void ABeltActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Anything that changes the shape of the belt or what it may carry has to be reflected before the
	// next update, or the items would be following a path that is no longer there.
	RebuildPath();
	RebuildRingBuffer(nullptr);
	RebuildBeltMesh();

	if (UBeltSubsystem* Subsystem = UBeltSubsystem::Get(this))
	{
		RefreshAllowedTypeIndices(*Subsystem);
	}
}
#endif

void ABeltActor::RegisterWithSubsystem()
{
	if (bRegistered)
	{
		return;
	}

	if (UBeltSubsystem* Subsystem = UBeltSubsystem::Get(this))
	{
		Subsystem->RegisterBelt(this);
		bRegistered = true;
	}
}

void ABeltActor::UnregisterFromSubsystem()
{
	if (!bRegistered)
	{
		return;
	}

	if (UBeltSubsystem* Subsystem = UBeltSubsystem::Get(this))
	{
		Subsystem->UnregisterBelt(this);
	}

	bRegistered = false;
}

// --------------------------------------------------------------------------------------------------
// Path and buffers
// --------------------------------------------------------------------------------------------------

void ABeltActor::RebuildPath()
{
	if (!Spline)
	{
		Path.Reset();
		return;
	}

	Path.Build(*Spline, UBeltSettings::Get().PathSampleSpacing);
	PathBuiltAtTransform = GetActorTransform();
}

bool ABeltActor::RefreshPathIfMoved()
{
	// Comparing two transforms is a handful of floating point compares. Rebaking is a few hundred spline
	// evaluations. Doing the cheap test every update so the expensive one happens only when a belt has
	// actually been dragged is the right way round.
	if (PathBuiltAtTransform.Equals(GetActorTransform(), 0.01))
	{
		return false;
	}

	RebuildPath();
	return true;
}

void ABeltActor::RebuildRingBuffer(FBeltUpdateContext* Context)
{
	const UBeltSettings& Settings = UBeltSettings::Get();

	// The ring is sized from the belt's length and the tightest packing any item could ever ask for, so
	// it can hold a completely full belt and is never asked to grow while items are on it. It is one
	// allocation for the life of the belt.
	const float Pitch = FMath::Max(1.0f, Settings.MinItemPitch);
	const int32 NeededCapacity = FMath::Clamp(
		FMath::CeilToInt(Path.Length / Pitch) + 2,
		1,
		FMath::Max(1, Settings.MaxItemsPerBelt));

	if (NeededCapacity == RingCapacity)
	{
		return;
	}

	// Anything already on the belt is dropped rather than resampled: the buffer only ever changes size
	// when the belt itself was reshaped, and carrying items across a reshape would put them at
	// distances that no longer mean what they meant.
	const bool bHadItems = RingCount > 0;

	ItemDistance.SetNumUninitialized(NeededCapacity, EAllowShrinking::Yes);
	ItemType.SetNumUninitialized(NeededCapacity, EAllowShrinking::Yes);
	ItemJammed.SetNumUninitialized(NeededCapacity, EAllowShrinking::Yes);

	RingCapacity = NeededCapacity;
	RingHead = 0;
	RingCount = 0;
	JammedCount = 0;

	if (Context)
	{
		++Context->BufferGrowth;
	}

	if (bHadItems)
	{
		UE_LOG(LogBeltLine, Verbose, TEXT("Belt '%s' was reshaped; its items were taken off."), *GetName());
	}
}

void ABeltActor::RebuildBeltMesh()
{
	for (USplineMeshComponent* Segment : BeltMeshSegments)
	{
		if (IsValid(Segment))
		{
			Segment->DestroyComponent();
		}
	}
	BeltMeshSegments.Reset();

	if (!BeltMesh || !Spline || !Path.bValid || MaxBeltMeshSegments <= 0)
	{
		return;
	}

	const float Length = Path.Length;
	const int32 NumSegments = FMath::Clamp(
		FMath::CeilToInt(Length / FMath::Max(1.0f, BeltMeshLength)),
		1,
		MaxBeltMeshSegments);
	const float SegmentLength = Length / static_cast<float>(NumSegments);

	BeltMeshSegments.Reserve(NumSegments);

	for (int32 SegmentIndex = 0; SegmentIndex < NumSegments; ++SegmentIndex)
	{
		USplineMeshComponent* Segment = NewObject<USplineMeshComponent>(this, NAME_None, RF_Transient);
		if (!Segment)
		{
			break;
		}

		const float StartDistance = static_cast<float>(SegmentIndex) * SegmentLength;
		const float EndDistance = StartDistance + SegmentLength;

		const FVector StartPos = Spline->GetLocationAtDistanceAlongSpline(StartDistance, ESplineCoordinateSpace::Local);
		const FVector EndPos = Spline->GetLocationAtDistanceAlongSpline(EndDistance, ESplineCoordinateSpace::Local);

		// The tangent handed to a spline mesh is a direction *and* a length: it has to be the length of
		// the segment or the mesh bows out of the run it is supposed to follow.
		const FVector StartTangent = Spline->GetTangentAtDistanceAlongSpline(StartDistance, ESplineCoordinateSpace::Local).GetSafeNormal() * SegmentLength;
		const FVector EndTangent = Spline->GetTangentAtDistanceAlongSpline(EndDistance, ESplineCoordinateSpace::Local).GetSafeNormal() * SegmentLength;

		Segment->SetMobility(EComponentMobility::Movable);
		Segment->SetStaticMesh(BeltMesh);
		if (BeltMaterial)
		{
			Segment->SetMaterial(0, BeltMaterial);
		}
		Segment->SetForwardAxis(ESplineMeshAxis::X, false);
		Segment->SetStartAndEnd(StartPos, StartTangent, EndPos, EndTangent, false);
		Segment->SetStartScale(BeltMeshScale, false);
		Segment->SetEndScale(BeltMeshScale, false);
		Segment->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Segment->SetCanEverAffectNavigation(false);
		Segment->UpdateMesh();

		Segment->SetupAttachment(Spline);
		Segment->RegisterComponent();
		AddInstanceComponent(Segment);

		BeltMeshSegments.Add(Segment);
	}
}

int32 ABeltActor::GetAllocatedSize() const
{
	return ItemDistance.GetAllocatedSize()
		+ ItemType.GetAllocatedSize()
		+ ItemJammed.GetAllocatedSize()
		+ Path.GetAllocatedSize();
}

// --------------------------------------------------------------------------------------------------
// Type filter
// --------------------------------------------------------------------------------------------------

void ABeltActor::RefreshAllowedTypeIndices(UBeltSubsystem& Subsystem)
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

	// A filter listing only types the world refused to register would otherwise silently become "allow
	// everything", which is the opposite of what was asked for. It stays a filter that allows nothing.
	bHasTypeFilter = AllowedTypeIndices.Num() > 0;
}

bool ABeltActor::AllowsItemType(const UBeltItemType* InItemType) const
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

void ABeltActor::SetSpeed(float NewSpeed)
{
	Speed = FMath::Max(0.0f, NewSpeed);
}

void ABeltActor::SetOutputBlocked(bool bNewBlocked)
{
	bOutputBlocked = bNewBlocked;
}

bool ABeltActor::IsBlocked() const
{
	// "Blocked" means the queue is actually backing up, not that a switch is set: a held belt with
	// nothing on it is not blocking anything, and a belt feeding a full node is, without anyone having
	// set a flag on it.
	return RingCount > 0 && ItemJammed[RingHead] != 0;
}

void ABeltActor::ClearItems()
{
	RingHead = 0;
	RingCount = 0;
	JammedCount = 0;
}

// --------------------------------------------------------------------------------------------------
// Ring buffer
// --------------------------------------------------------------------------------------------------

bool ABeltActor::HasRoomAtEntry(int32 TypeIndex, const FBeltUpdateContext& Context) const
{
	if (RingCapacity <= 0 || !Path.bValid || RingCount >= RingCapacity)
	{
		return false;
	}

	if (!AllowsTypeIndex(TypeIndex))
	{
		return false;
	}

	if (MaxItemWeight > 0.0f && Context.GetWeight(TypeIndex) > MaxItemWeight)
	{
		return false;
	}

	if (RingCount == 0)
	{
		return true;
	}

	// The item that got on last is the one standing in the doorway. There is room only if its trailing
	// edge has cleared the entry by the gap plus the arriving item's own half-length.
	const int32 BackIndex = GetRingIndex(RingCount - 1);
	const float BackTrailingEdge = ItemDistance[BackIndex] - Context.GetRadius(ItemType[BackIndex]);
	const float NeededLeadingEdge = EntryDistance + Context.GetRadius(TypeIndex);

	return BackTrailingEdge - MinGap >= NeededLeadingEdge;
}

bool ABeltActor::PushItemAtEntry(int32 TypeIndex, const FBeltUpdateContext& Context)
{
	if (!HasRoomAtEntry(TypeIndex, Context))
	{
		return false;
	}

	const int32 NewIndex = GetRingIndex(RingCount);
	ItemDistance[NewIndex] = EntryDistance;
	ItemType[NewIndex] = static_cast<uint8>(TypeIndex);
	ItemJammed[NewIndex] = 0;
	++RingCount;

	return true;
}

int32 ABeltActor::FillWithItems(int32 TypeIndex, int32 Count, const FBeltUpdateContext& Context)
{
	if (Count <= 0 || RingCapacity <= 0 || !Path.bValid || !AllowsTypeIndex(TypeIndex))
	{
		return 0;
	}

	if (MaxItemWeight > 0.0f && Context.GetWeight(TypeIndex) > MaxItemWeight)
	{
		return 0;
	}

	const float Radius = Context.GetRadius(TypeIndex);

	// Start behind whatever is already on the belt - or, on an empty belt, at the very end, so a fill
	// lays the whole run out at once instead of stacking everything at the entry and waiting for the
	// belt to carry it away one item at a time.
	float NextDistance;
	if (RingCount > 0)
	{
		const int32 BackIndex = GetRingIndex(RingCount - 1);
		NextDistance = ItemDistance[BackIndex] - Context.GetRadius(ItemType[BackIndex]) - MinGap - Radius;
	}
	else
	{
		// Half a length short of the end, so the first item is fully on the belt rather than sitting
		// exactly on the hand-off line and leaving again on the very next update.
		NextDistance = Path.Length - Radius;
	}

	const float Step = 2.0f * Radius + MinGap;
	int32 Placed = 0;

	while (Placed < Count && RingCount < RingCapacity && NextDistance >= EntryDistance)
	{
		const int32 NewIndex = GetRingIndex(RingCount);
		ItemDistance[NewIndex] = NextDistance;
		ItemType[NewIndex] = static_cast<uint8>(TypeIndex);
		ItemJammed[NewIndex] = 0;
		++RingCount;
		++Placed;

		NextDistance -= Step;
	}

	return Placed;
}

bool ABeltActor::TryTakeFrontItem(int32& OutTypeIndex)
{
	if (RingCount <= 0)
	{
		return false;
	}

	OutTypeIndex = ItemType[RingHead];
	if (ItemJammed[RingHead] != 0)
	{
		JammedCount = FMath::Max(0, JammedCount - 1);
	}

	RingHead = (RingHead + 1 < RingCapacity) ? RingHead + 1 : 0;
	--RingCount;

	return true;
}

bool ABeltActor::TryTakeBackItem(int32& OutTypeIndex)
{
	if (RingCount <= 0)
	{
		return false;
	}

	const int32 BackIndex = GetRingIndex(RingCount - 1);
	OutTypeIndex = ItemType[BackIndex];
	if (ItemJammed[BackIndex] != 0)
	{
		JammedCount = FMath::Max(0, JammedCount - 1);
	}

	--RingCount;

	return true;
}

// --------------------------------------------------------------------------------------------------
// The movement pass
// --------------------------------------------------------------------------------------------------

bool ABeltActor::CanOutputAccept(int32 TypeIndex, const FBeltUpdateContext& Context) const
{
	if (bOutputBlocked)
	{
		return false;
	}

	if (OutputNode)
	{
		return OutputNode->CanAccept(TypeIndex);
	}

	if (OutputBelt)
	{
		return OutputBelt->HasRoomAtEntry(TypeIndex, Context);
	}

	// A belt that ends in nothing is a sink. That is a real answer, not a missing case: the mouth of a
	// furnace, the edge of a level, the ship that took the cargo away.
	return true;
}

bool ABeltActor::TryHandOffFrontItem(FBeltUpdateContext& Context)
{
	if (RingCount <= 0 || bOutputBlocked)
	{
		return false;
	}

	const int32 TypeIndex = ItemType[RingHead];

	if (OutputNode)
	{
		if (!OutputNode->TryAccept(TypeIndex))
		{
			return false;
		}
	}
	else if (OutputBelt)
	{
		if (!OutputBelt->PushItemAtEntry(TypeIndex, Context))
		{
			return false;
		}
	}

	int32 TakenType = INDEX_NONE;
	TryTakeFrontItem(TakenType);

	++DeliveredThisWindow;
	++Context.ItemsDelivered;

	return true;
}

void ABeltActor::AdvanceItems(float DeltaSeconds, FBeltUpdateContext& Context)
{
	JammedCount = 0;

	if (RingCount <= 0 || !Path.bValid)
	{
		return;
	}

	const float Advance = Speed * DeltaSeconds;
	const float Length = Path.Length;

	// Asked once, before the pass, so every item in the queue is clamped against the same answer. If
	// the answer turns out to be wrong - the node filled up between the question and the hand-off - the
	// drain below re-clamps whatever it has to, and that is an early-out walk, not a second full pass.
	const bool bCanExit = CanOutputAccept(ItemType[RingHead], Context);

	// LimitBase is the furthest a *centre* may sit, before the item's own half-length is taken off it.
	// For the front item it is the end of the belt; for everyone after it, it is the trailing edge of
	// the item in front minus the gap. One value carried down the queue is the whole of backpressure.
	float LimitBase = bCanExit ? (Length + Advance) : Length;
	bool bFirst = true;
	int32 Jammed = 0;

	for (int32 QueuePosition = 0; QueuePosition < RingCount; ++QueuePosition)
	{
		const int32 Index = GetRingIndex(QueuePosition);
		const float Radius = Context.GetRadius(ItemType[Index]);
		const float Limit = bFirst ? LimitBase : (LimitBase - Radius);

		const float Wanted = ItemDistance[Index] + Advance;
		float Actual = Wanted;
		uint8 bJammed = 0;

		if (Wanted > Limit)
		{
			Actual = FMath::Max(EntryDistance, Limit);
			bJammed = 1;
			++Jammed;
		}

		ItemDistance[Index] = Actual;
		ItemJammed[Index] = bJammed;

		LimitBase = Actual - Radius - MinGap;
		bFirst = false;
	}

	JammedCount = Jammed;
	Context.JammedItems += Jammed;

	// Everything that reached the end goes on to whatever is there. The loop is bounded by the queue,
	// so a very fast belt can hand off several items in one update without any of them skipping the
	// spacing the pass above just enforced.
	while (RingCount > 0 && ItemDistance[RingHead] >= Length)
	{
		if (!TryHandOffFrontItem(Context))
		{
			// The prediction was optimistic. Park the front item on the end and push the queue back
			// behind it; the walk stops at the first item that already fits.
			ItemDistance[RingHead] = Length;
			if (ItemJammed[RingHead] == 0)
			{
				ItemJammed[RingHead] = 1;
				++JammedCount;
				++Context.JammedItems;
			}
			EnforceSpacingFromFront(Context);
			break;
		}
	}
}

void ABeltActor::EnforceSpacingFromFront(const FBeltUpdateContext& Context)
{
	if (RingCount <= 1)
	{
		return;
	}

	const int32 FrontIndex = RingHead;
	float LimitBase = ItemDistance[FrontIndex] - Context.GetRadius(ItemType[FrontIndex]) - MinGap;

	for (int32 QueuePosition = 1; QueuePosition < RingCount; ++QueuePosition)
	{
		const int32 Index = GetRingIndex(QueuePosition);
		const float Radius = Context.GetRadius(ItemType[Index]);
		const float Limit = LimitBase - Radius;

		if (ItemDistance[Index] <= Limit)
		{
			// This one already fits, and so does everyone behind it - the queue was consistent before
			// the front item was pushed back, so the correction cannot reach past the first item that
			// no longer needed it.
			return;
		}

		ItemDistance[Index] = FMath::Max(EntryDistance, Limit);
		if (ItemJammed[Index] == 0)
		{
			ItemJammed[Index] = 1;
			++JammedCount;
		}

		LimitBase = ItemDistance[Index] - Radius - MinGap;
	}
}

void ABeltActor::TickThroughputWindow(float WindowSeconds)
{
	ThroughputPerSecond = WindowSeconds > KINDA_SMALL_NUMBER
		? static_cast<float>(DeliveredThisWindow) / WindowSeconds
		: 0.0f;
	DeliveredThisWindow = 0;
}
