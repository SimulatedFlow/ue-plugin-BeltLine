// Copyright 2026 Simulated Flow. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/**
 * Runtime module for BeltLine. Loads at PreDefault so the world subsystem, the belt and node actors
 * and the Belt.* console commands exist before the first game world is created.
 */
class FBeltLineModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
