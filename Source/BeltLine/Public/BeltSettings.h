// Copyright 2026 Simulated Flow. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "BeltSettings.generated.h"

/**
 * Project-wide defaults, ceilings and the update rate for BeltLine.
 * Edit under Project Settings > Plugins > BeltLine; stored in DefaultGame.ini.
 *
 * Everything here is read once, when a world's subsystem comes up. The budget, the update rate and the
 * belt speeds can all be moved afterwards at runtime - through the subsystem, the Blueprint library or
 * the Belt.* console commands - without touching the config, so a demo button and a shipped game drive
 * exactly the same code.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "BeltLine"))
class BELTLINE_API UBeltSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UBeltSettings();

	// UDeveloperSettings interface
	virtual FName GetCategoryName() const override;
	virtual FName GetSectionName() const override;

	/** Convenience accessor. Never returns null. */
	static const UBeltSettings& Get();

	// ------------------------------------------------------------------------------------------------
	// Budget
	// ------------------------------------------------------------------------------------------------

	/**
	 * Items allowed on belts in one world at once.
	 *
	 * This is a hard ceiling, not a target: a spawn past it fails and says so, rather than the frame
	 * time quietly going with it. Lowering it at runtime trims the world back down, and it does that by
	 * taking items off the *back* of belts - the ones that just got on - so what is already halfway to
	 * the end still arrives. Instance slots are not given back when this falls, which is exactly why
	 * the draw call count does not move and nothing is reallocated when it goes up again.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Budget", meta = (ClampMin = "0", UIMax = "100000"))
	int32 MaxItems = 20000;

	/**
	 * Items allowed on one belt, whatever its length says it could hold.
	 *
	 * A belt's ring buffer is sized from its length and MinItemPitch when the belt is built, and this
	 * caps that. It is the only thing standing between a five-kilometre spline and a ring buffer nobody
	 * asked for.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Budget", meta = (ClampMin = "1", UIMax = "20000"))
	int32 MaxItemsPerBelt = 4000;

	/**
	 * Closest two item centres may ever be, in cm. Used to size a belt's ring buffer, nothing else.
	 *
	 * The real spacing at runtime comes from the two items' own radii and the belt's gap; this is the
	 * floor used to work out how many entries a belt could possibly need, so the buffer is allocated
	 * once and never grows. Set it lower than any item radius you intend to carry and the ring is
	 * simply bigger than it needs to be; set it higher and a belt full of small items will refuse
	 * loads it had room for.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Budget", meta = (ClampMin = "1.0", UIMax = "500.0"))
	float MinItemPitch = 40.0f;

	/** Item types one world may register. The type index stored next to each item is one byte. */
	UPROPERTY(Config, EditAnywhere, Category = "Budget", meta = (ClampMin = "1", ClampMax = "255"))
	int32 MaxItemTypes = 64;

	// ------------------------------------------------------------------------------------------------
	// Update
	// ------------------------------------------------------------------------------------------------

	/**
	 * How often belts are advanced, in Hz. 0 - the default - means every frame.
	 *
	 * Unlike fog or shadows, a conveyor is watched directly and the eye reads a stepped update as
	 * stutter, so this stays at every frame unless a project has hundreds of belts and wants the whole
	 * pass on a fixed budget. The advance itself is a pass over a float array; it is rarely the thing
	 * worth throttling.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Update", meta = (ClampMin = "0.0", UIMax = "120.0"))
	float UpdatesPerSecond = 0.0f;

	/**
	 * Largest step one update may take, in seconds. A frame hitch or a breakpoint would otherwise hand
	 * the movement pass a step big enough to jump items past their neighbours.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Update", meta = (ClampMin = "0.005", UIMax = "1.0"))
	float MaxUpdateStep = 0.1f;

	/** Run the belts in a plain editor world too, so a belt shows what it does without pressing Play. */
	UPROPERTY(Config, EditAnywhere, Category = "Update")
	bool bTickInEditorWorlds = true;

	// ------------------------------------------------------------------------------------------------
	// Belt defaults
	// ------------------------------------------------------------------------------------------------

	/** Speed a new belt starts at, in cm per second. */
	UPROPERTY(Config, EditAnywhere, Category = "Belt Defaults", meta = (ClampMin = "0.0", UIMax = "2000.0"))
	float DefaultSpeed = 240.0f;

	/** Clear space a new belt keeps between two items' edges, in cm. */
	UPROPERTY(Config, EditAnywhere, Category = "Belt Defaults", meta = (ClampMin = "0.0", UIMax = "200.0"))
	float DefaultMinGap = 12.0f;

	/**
	 * Distance between two samples of a belt's baked path, in cm.
	 *
	 * This trades memory for how faithfully a curve is followed. At 50 cm a belt costs about 110 bytes
	 * per metre and a corner of a few metres' radius is followed to well under a centimetre. Halve it
	 * for tight helixes; double it for long straight runs.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Belt Defaults", meta = (ClampMin = "5.0", UIMax = "500.0"))
	float PathSampleSpacing = 50.0f;

	// ------------------------------------------------------------------------------------------------
	// Rendering
	// ------------------------------------------------------------------------------------------------

	/**
	 * Bounds scale on every instance set.
	 *
	 * One set holds every item of one type across every belt in the world, so its bounds are the whole
	 * layout. A little headroom keeps an item that has just been handed to a distant belt from popping
	 * in a frame late.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Rendering", meta = (ClampMin = "1.0", UIMax = "8.0"))
	float InstanceBoundsScale = 1.2f;

	/** Distance at which instances start being culled, in cm. 0 disables the near cut. */
	UPROPERTY(Config, EditAnywhere, Category = "Rendering", meta = (ClampMin = "0", UIMax = "100000"))
	int32 InstanceStartCullDistance = 0;

	/** Distance at which instances stop drawing, in cm. 0 means never. */
	UPROPERTY(Config, EditAnywhere, Category = "Rendering", meta = (ClampMin = "0", UIMax = "500000"))
	int32 InstanceEndCullDistance = 0;

	// ------------------------------------------------------------------------------------------------
	// Debug
	// ------------------------------------------------------------------------------------------------

	/** Show the statistics box as soon as a world with a BeltLine HUD comes up. */
	UPROPERTY(Config, EditAnywhere, Category = "Debug")
	bool bShowStatsByDefault = true;

	/** Items Belt.Test puts on its loop when no count is given. */
	UPROPERTY(Config, EditAnywhere, Category = "Debug", meta = (ClampMin = "0", UIMax = "100000"))
	int32 TestItemCount = 10000;
};
