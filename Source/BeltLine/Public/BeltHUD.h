// Copyright 2026 Simulated Flow. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "BeltHUD.generated.h"

class UBeltSubsystem;
class UCanvas;
class UFont;

/**
 * The counters box for BeltLine, drawn on UCanvas from AHUD.
 *
 * Canvas rather than UMG on purpose, and there are deliberately no buttons on it. Two reasons, pulling
 * the same way:
 *
 *   - The box has to survive a cooked Shipping build. DrawDebug is compiled out there and a debug
 *     widget is usually stripped; a Canvas overlay is not. It is a real overlay, not a development one.
 *
 *   - Anything that has to be *clicked* belongs in UMG instead. An AHUD hit box is tested against
 *     UGameViewportClient::GetMousePosition(), which reports nothing at all on a machine with no mouse
 *     attached - a capture rig, a build agent, a headless test. Widgets are not affected. So the
 *     numbers live here, where they cost nothing and always draw, and the controls live in a widget,
 *     where they always receive the click.
 *
 * Everything drawn is read from the subsystem on the frame it is drawn. Nothing is cached, so the box
 * cannot claim one thing while the belts do another.
 */
UCLASS(Blueprintable, meta = (DisplayName = "Belt HUD"))
class BELTLINE_API ABeltHUD : public AHUD
{
	GENERATED_BODY()

public:
	ABeltHUD();

	// AActor interface
	virtual void BeginPlay() override;

	// AHUD interface
	virtual void DrawHUD() override;

	/** Draw the counters box. Toggled from Blueprint, or from a widget button. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BeltLine|HUD")
	bool bShowStats = true;

	/** Top-left corner of the counters box, in pixels. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BeltLine|HUD")
	FVector2D StatsBoxOrigin = FVector2D(28.0f, 90.0f);

	/** Width of the counters box, in pixels. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BeltLine|HUD", meta = (ClampMin = "120.0"))
	float StatsBoxWidth = 400.0f;

	/** Flip the counters box on and off. This is what a STATS button in a widget calls. */
	UFUNCTION(BlueprintCallable, Category = "BeltLine|HUD")
	void ToggleStats();

protected:
	/** Draw the counters box. Returns the height it used. */
	float DrawStatsBox(UCanvas* InCanvas, UFont* Font, const UBeltSubsystem* Subsystem);
};
