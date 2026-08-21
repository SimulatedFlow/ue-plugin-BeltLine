// Copyright 2026 Simulated Flow. All Rights Reserved.

#include "BeltLine.h"
#include "BeltLineLog.h"

DEFINE_LOG_CATEGORY(LogBeltLine);

#define LOCTEXT_NAMESPACE "FBeltLineModule"

void FBeltLineModule::StartupModule()
{
	UE_LOG(LogBeltLine, Log, TEXT("BeltLine started."));
}

void FBeltLineModule::ShutdownModule()
{
	UE_LOG(LogBeltLine, Log, TEXT("BeltLine shut down."));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FBeltLineModule, BeltLine)
