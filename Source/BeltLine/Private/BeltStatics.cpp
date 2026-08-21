// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "BeltStatics.h"

#include "BeltActor.h"
#include "BeltNode.h"
#include "BeltSubsystem.h"

UBeltSubsystem* UBeltStatics::GetBeltSubsystem(const UObject* WorldContextObject)
{
	return UBeltSubsystem::Get(WorldContextObject);
}

// --------------------------------------------------------------------------------------------------
// Items
// --------------------------------------------------------------------------------------------------

bool UBeltStatics::SpawnItem(ABeltActor* Belt, UBeltItemType* ItemType)
{
	UBeltSubsystem* Subsystem = UBeltSubsystem::Get(Belt);
	return Subsystem ? Subsystem->SpawnItem(Belt, ItemType) : false;
}

int32 UBeltStatics::SpawnItems(ABeltActor* Belt, UBeltItemType* ItemType, int32 Count)
{
	UBeltSubsystem* Subsystem = UBeltSubsystem::Get(Belt);
	return Subsystem ? Subsystem->SpawnItems(Belt, ItemType, Count) : 0;
}

int32 UBeltStatics::SpawnItemsAcrossBelts(const UObject* WorldContextObject, UBeltItemType* ItemType, int32 Count)
{
	UBeltSubsystem* Subsystem = UBeltSubsystem::Get(WorldContextObject);
	return Subsystem ? Subsystem->SpawnItemsAcrossBelts(ItemType, Count) : 0;
}

bool UBeltStatics::TryTakeItem(ABeltActor* Belt, UBeltItemType*& OutItemType)
{
	OutItemType = nullptr;

	UBeltSubsystem* Subsystem = UBeltSubsystem::Get(Belt);
	return Subsystem ? Subsystem->TryTakeItem(Belt, OutItemType) : false;
}

void UBeltStatics::ClearAllItems(const UObject* WorldContextObject)
{
	if (UBeltSubsystem* Subsystem = UBeltSubsystem::Get(WorldContextObject))
	{
		Subsystem->ClearAllItems();
	}
}

UBeltItemType* UBeltStatics::GetBuiltInItemType(const UObject* WorldContextObject, EBeltBuiltInItem Shape)
{
	UBeltSubsystem* Subsystem = UBeltSubsystem::Get(WorldContextObject);
	return Subsystem ? Subsystem->GetBuiltInItemType(Shape) : nullptr;
}

// --------------------------------------------------------------------------------------------------
// Belts
// --------------------------------------------------------------------------------------------------

void UBeltStatics::SetBeltSpeed(ABeltActor* Belt, float NewSpeed)
{
	if (IsValid(Belt))
	{
		Belt->SetSpeed(NewSpeed);
	}
}

float UBeltStatics::GetBeltSpeed(const ABeltActor* Belt)
{
	return IsValid(Belt) ? Belt->GetSpeed() : 0.0f;
}

void UBeltStatics::SetAllBeltSpeeds(const UObject* WorldContextObject, float NewSpeed)
{
	if (UBeltSubsystem* Subsystem = UBeltSubsystem::Get(WorldContextObject))
	{
		Subsystem->SetAllBeltSpeeds(NewSpeed);
	}
}

void UBeltStatics::ScaleAllBeltSpeeds(const UObject* WorldContextObject, float Scale)
{
	if (UBeltSubsystem* Subsystem = UBeltSubsystem::Get(WorldContextObject))
	{
		Subsystem->ScaleAllBeltSpeeds(Scale);
	}
}

float UBeltStatics::GetAverageBeltSpeed(const UObject* WorldContextObject)
{
	const UBeltSubsystem* Subsystem = UBeltSubsystem::Get(WorldContextObject);
	return Subsystem ? Subsystem->GetAverageBeltSpeed() : 0.0f;
}

void UBeltStatics::SetOutputBlocked(ABeltActor* Belt, bool bBlocked)
{
	if (IsValid(Belt))
	{
		Belt->SetOutputBlocked(bBlocked);
	}
}

int32 UBeltStatics::SetAllOutputsBlocked(const UObject* WorldContextObject, bool bBlocked)
{
	UBeltSubsystem* Subsystem = UBeltSubsystem::Get(WorldContextObject);
	return Subsystem ? Subsystem->SetAllOutputsBlocked(bBlocked) : 0;
}

bool UBeltStatics::IsBlocked(const ABeltActor* Belt)
{
	return IsValid(Belt) ? Belt->IsBlocked() : false;
}

int32 UBeltStatics::GetBeltItemCount(const ABeltActor* Belt)
{
	return IsValid(Belt) ? Belt->GetItemCount() : 0;
}

int32 UBeltStatics::GetBeltJammedCount(const ABeltActor* Belt)
{
	return IsValid(Belt) ? Belt->GetJammedCount() : 0;
}

TArray<ABeltActor*> UBeltStatics::GetAllBelts(const UObject* WorldContextObject)
{
	const UBeltSubsystem* Subsystem = UBeltSubsystem::Get(WorldContextObject);
	return Subsystem ? Subsystem->GetBelts() : TArray<ABeltActor*>();
}

// --------------------------------------------------------------------------------------------------
// Nodes
// --------------------------------------------------------------------------------------------------

void UBeltStatics::SetNodeSplitEnabled(ABeltNode* Node, bool bSplitEnabled)
{
	if (IsValid(Node))
	{
		Node->SetSplitEnabled(bSplitEnabled);
	}
}

int32 UBeltStatics::SetAllNodesSplitEnabled(const UObject* WorldContextObject, bool bSplitEnabled)
{
	UBeltSubsystem* Subsystem = UBeltSubsystem::Get(WorldContextObject);
	return Subsystem ? Subsystem->SetAllNodesSplitEnabled(bSplitEnabled) : 0;
}

void UBeltStatics::SetNodeBlocked(ABeltNode* Node, bool bBlocked)
{
	if (IsValid(Node))
	{
		Node->SetBlocked(bBlocked);
	}
}

// --------------------------------------------------------------------------------------------------
// Throughput and budget
// --------------------------------------------------------------------------------------------------

float UBeltStatics::GetThroughput(const UObject* WorldContextObject)
{
	const UBeltSubsystem* Subsystem = UBeltSubsystem::Get(WorldContextObject);
	return Subsystem ? Subsystem->GetThroughput() : 0.0f;
}

float UBeltStatics::GetBeltThroughput(const ABeltActor* Belt)
{
	return IsValid(Belt) ? Belt->GetThroughput() : 0.0f;
}

int32 UBeltStatics::GetItemCount(const UObject* WorldContextObject)
{
	const UBeltSubsystem* Subsystem = UBeltSubsystem::Get(WorldContextObject);
	return Subsystem ? Subsystem->GetItemCount() : 0;
}

void UBeltStatics::SetItemBudget(const UObject* WorldContextObject, int32 NewBudget)
{
	if (UBeltSubsystem* Subsystem = UBeltSubsystem::Get(WorldContextObject))
	{
		Subsystem->SetItemBudget(NewBudget);
	}
}

int32 UBeltStatics::GetItemBudget(const UObject* WorldContextObject)
{
	const UBeltSubsystem* Subsystem = UBeltSubsystem::Get(WorldContextObject);
	return Subsystem ? Subsystem->GetItemBudget() : 0;
}

FBeltStats UBeltStatics::GetBeltStats(const UObject* WorldContextObject)
{
	const UBeltSubsystem* Subsystem = UBeltSubsystem::Get(WorldContextObject);
	return Subsystem ? Subsystem->GetStats() : FBeltStats();
}
