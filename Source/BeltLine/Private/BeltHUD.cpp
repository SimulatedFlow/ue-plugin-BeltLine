// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "BeltHUD.h"

#include "BeltSettings.h"
#include "BeltSubsystem.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "GlobalRenderResources.h"
#include "Misc/StringBuilder.h"
#include "SceneTypes.h"

namespace BeltHudPrivate
{
	static constexpr float LineHeight = 15.0f;
	static constexpr float BoxPadding = 8.0f;
	static constexpr int32 StatsLineCount = 13;

	static const FLinearColor PanelBackground(0.0f, 0.0f, 0.0f, 0.62f);
	static const FLinearColor HeadingColor(1.0f, 0.78f, 0.35f, 1.0f);
	static const FLinearColor BodyColor(0.9f, 0.9f, 0.9f, 1.0f);
	static const FLinearColor GoodColor(0.55f, 0.95f, 0.55f, 1.0f);
	static const FLinearColor WarnColor(0.98f, 0.65f, 0.35f, 1.0f);

	static void DrawFilledRect(UCanvas* Canvas, const FVector2D& Position, const FVector2D& Size, const FLinearColor& Color)
	{
		FCanvasTileItem Tile(Position, GWhiteTexture, Size, Color);
		Tile.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Tile);
	}
}

ABeltHUD::ABeltHUD()
{
	PrimaryActorTick.bCanEverTick = false;
}

void ABeltHUD::BeginPlay()
{
	Super::BeginPlay();

	bShowStats = UBeltSettings::Get().bShowStatsByDefault;
}

void ABeltHUD::ToggleStats()
{
	bShowStats = !bShowStats;
}

void ABeltHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!Canvas || !bShowStats)
	{
		return;
	}

	UFont* Font = GEngine ? GEngine->GetSmallFont() : nullptr;
	if (!Font)
	{
		return;
	}

	DrawStatsBox(Canvas, Font, UBeltSubsystem::Get(this));
}

float ABeltHUD::DrawStatsBox(UCanvas* InCanvas, UFont* Font, const UBeltSubsystem* Subsystem)
{
	using namespace BeltHudPrivate;

	const float BoxHeight = StatsLineCount * LineHeight + BoxPadding * 2.0f;
	DrawFilledRect(
		InCanvas,
		FVector2D(StatsBoxOrigin.X - BoxPadding, StatsBoxOrigin.Y - BoxPadding),
		FVector2D(StatsBoxWidth, BoxHeight),
		PanelBackground);

	float LineY = static_cast<float>(StatsBoxOrigin.Y);
	auto DrawLine = [&](FStringView Line, const FLinearColor& Color)
	{
		FCanvasTextStringViewItem Item(FVector2D(StatsBoxOrigin.X, LineY), Line, Font, Color);
		InCanvas->DrawItem(Item);
		LineY += LineHeight;
	};

	TStringBuilder<192> Line;

	Line.Reset();
	Line.Append(TEXT("BeltLine"));
	DrawLine(Line.ToView(), HeadingColor);

	if (!Subsystem)
	{
		Line.Reset();
		Line.Append(TEXT("no subsystem in this world"));
		DrawLine(Line.ToView(), WarnColor);
		return BoxHeight;
	}

	const FBeltStats& Stats = Subsystem->GetStats();

	Line.Reset();
	Line.Appendf(TEXT("Items              %d / %d"), Stats.Items, Stats.ItemBudget);
	DrawLine(Line.ToView(), BodyColor);

	// The headline number. It follows the number of item *types*, never the number of items, which is
	// the claim the whole plugin is sold on - so it is on the box, not in the small print.
	Line.Reset();
	Line.Appendf(TEXT("Instance sets      %d  (1 draw call each)"), Stats.InstanceSets);
	DrawLine(Line.ToView(), GoodColor);

	Line.Reset();
	Line.Appendf(TEXT("Instance slots     %d"), Stats.InstanceSlots);
	DrawLine(Line.ToView(), BodyColor);

	Line.Reset();
	Line.Appendf(TEXT("Belts              %d      Nodes %d  (%d buffered)"), Stats.Belts, Stats.Nodes, Stats.BufferedItems);
	DrawLine(Line.ToView(), BodyColor);

	Line.Reset();
	Line.Appendf(TEXT("Throughput         %.1f /s   (%d delivered)"), Stats.ThroughputPerSecond, Stats.ItemsDelivered);
	DrawLine(Line.ToView(), Stats.ThroughputPerSecond > 0.0f ? BodyColor : WarnColor);

	// Backpressure, as a number, next to the queue you can see growing on screen.
	Line.Reset();
	Line.Appendf(TEXT("Jammed             %d"), Stats.JammedItems);
	DrawLine(Line.ToView(), Stats.JammedItems > 0 ? WarnColor : BodyColor);

	Line.Reset();
	Line.Appendf(TEXT("Update             %.3f ms"), Stats.UpdateMilliseconds);
	DrawLine(Line.ToView(), BodyColor);

	Line.Reset();
	Line.Appendf(TEXT("  advance          %.3f ms"), Stats.AdvanceMilliseconds);
	DrawLine(Line.ToView(), BodyColor);

	Line.Reset();
	Line.Appendf(TEXT("  instances        %.3f ms"), Stats.InstanceMilliseconds);
	DrawLine(Line.ToView(), BodyColor);

	// The "nothing is allocated once the buffers stand" claim, readable instead of trusted.
	Line.Reset();
	Line.Appendf(TEXT("Buffer growth      %d this update  (%d total)"), Stats.BufferGrowthThisUpdate, Stats.BufferGrowthTotal);
	DrawLine(Line.ToView(), Stats.BufferGrowthThisUpdate > 0 ? WarnColor : GoodColor);

	Line.Reset();
	Line.Appendf(TEXT("Buffers            %.1f KB      Types %d"), Stats.BufferKilobytes, Stats.ItemTypes);
	DrawLine(Line.ToView(), BodyColor);

	Line.Reset();
	if (Stats.UpdatesPerSecond > 0.0f)
	{
		Line.Appendf(TEXT("Update rate        %.0f Hz       Refused %d"), Stats.UpdatesPerSecond, Stats.SpawnsRejected);
	}
	else
	{
		Line.Appendf(TEXT("Update rate        every frame   Refused %d"), Stats.SpawnsRejected);
	}
	DrawLine(Line.ToView(), BodyColor);

	return BoxHeight;
}
