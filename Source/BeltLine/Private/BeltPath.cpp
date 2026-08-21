// Copyright 2026 Simulated Flow. All Rights Reserved.

#include "BeltPath.h"

#include "Components/SplineComponent.h"

void FBeltPath::Reset()
{
	Positions.Reset();
	Rotations.Reset();
	Length = 0.0f;
	bValid = false;
}

void FBeltPath::Build(const USplineComponent& Spline, float InSampleSpacing)
{
	SampleSpacing = FMath::Max(1.0f, InSampleSpacing);
	Length = Spline.GetSplineLength();

	if (Length <= KINDA_SMALL_NUMBER || Spline.GetNumberOfSplinePoints() < 2)
	{
		Reset();
		return;
	}

	// One sample every SampleSpacing plus a final one exactly on the end, so the last segment is at
	// most one spacing long and an item at Length lands on the end of the belt rather than short of it.
	const int32 NumSegments = FMath::Max(1, FMath::CeilToInt(Length / SampleSpacing));
	const int32 NumSamples = NumSegments + 1;

	// The spacing is widened so the samples divide the length exactly. Without this the final segment
	// would be a stub of arbitrary length and Evaluate's index arithmetic - which assumes uniform
	// spacing, because that is what makes it two lookups instead of a search - would drift over it.
	SampleSpacing = Length / static_cast<float>(NumSegments);

	Positions.SetNumUninitialized(NumSamples, EAllowShrinking::No);
	Rotations.SetNumUninitialized(NumSamples, EAllowShrinking::No);

	for (int32 SampleIndex = 0; SampleIndex < NumSamples; ++SampleIndex)
	{
		const float Distance = FMath::Min(static_cast<float>(SampleIndex) * SampleSpacing, Length);
		Positions[SampleIndex] = Spline.GetLocationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);
		Rotations[SampleIndex] = Spline.GetQuaternionAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);
	}

	// Neighbouring quaternions can come back on opposite hemispheres even though they describe almost
	// the same orientation. FastLerp between those two takes the long way round and an item would flip
	// end over end for one sample. Flipping the sign here is free and makes the whole table consistent.
	for (int32 SampleIndex = 1; SampleIndex < NumSamples; ++SampleIndex)
	{
		if ((Rotations[SampleIndex] | Rotations[SampleIndex - 1]) < 0.0f)
		{
			Rotations[SampleIndex] *= -1.0f;
		}
	}

	bValid = true;
}
