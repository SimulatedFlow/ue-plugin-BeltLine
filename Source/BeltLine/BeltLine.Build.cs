// Copyright 2026 Simulated Flow. All Rights Reserved.

using UnrealBuildTool;

public class BeltLine : ModuleRules
{
	public BeltLine(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		// One runtime module and nothing else.
		//
		// Deliberately NOT here:
		//   UMG      - an item is an instance in an instanced static mesh component; nothing in the
		//              transport or the drawing path needs a widget. The demo map's buttons are UMG
		//              assets in Content calling the Blueprint library, exactly as a project would.
		//   Niagara  - a crate on a belt has a position, a rotation and a type. It is not a particle,
		//              it has to be takeable off the belt again, and it has to jam.
		//   Chaos    - the items carry no physics. That is a decision, not an omission: the whole point
		//              is that ten thousand of them cost one pass over a float array.
		//   UnrealEd - everything here ships. There is no editor module and no editor-only code path;
		//              the belts preview in the editor through the same runtime subsystem.
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"DeveloperSettings",
		});

		// RenderCore gives us GWhiteTexture for the statistics box background.
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"RenderCore",
		});
	}
}
