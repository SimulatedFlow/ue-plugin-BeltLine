// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class USplineComponent;

/**
 * A belt's spline, baked into a flat table of evenly spaced positions and rotations.
 *
 * This is the reason ten thousand items are affordable. USplineComponent::GetTransformAtDistanceAlongSpline
 * is not a cheap call: it walks a reparameterisation table to turn a distance into a spline key, then
 * evaluates three interpolated curves and builds a matrix. Doing that once per item per frame is what
 * makes hand-built conveyors fall over in the low thousands, and no amount of instancing fixes it,
 * because the cost is on the game thread before a single instance is written.
 *
 * So the spline is sampled once - when the belt is built, moved or reshaped - at a fixed spacing, and
 * an item's transform becomes two array lookups, a lerp and a quaternion lerp. A hundred metres of belt
 * at the default 50 cm spacing is 201 samples: about 11 kB, held for the lifetime of the belt, and it
 * does not grow with the number of items riding on it.
 *
 * The samples are in world space, which costs a rebuild if the belt actor is moved. Belts are furniture;
 * the subsystem watches for a moved belt and rebakes it, and a rebake is a few hundred spline
 * evaluations, not a per-frame cost.
 */
struct BELTLINE_API FBeltPath
{
	/** World-space position of every sample, from distance 0 to Length. */
	TArray<FVector> Positions;

	/** World-space orientation of every sample: X points the way the items travel. */
	TArray<FQuat> Rotations;

	/** Distance between two samples, in cm. */
	float SampleSpacing = 50.0f;

	/** Length of the baked spline, in cm. */
	float Length = 0.0f;

	/** True once there are at least two samples and a positive length. */
	bool bValid = false;

	/** Bake a spline. Safe to call on an empty or degenerate spline; the path is simply left invalid. */
	void Build(const USplineComponent& Spline, float InSampleSpacing);

	/** Drop the samples and mark the path invalid, keeping the allocation for the next bake. */
	void Reset();

	/** Bytes held by the two sample arrays. */
	int32 GetAllocatedSize() const
	{
		return Positions.GetAllocatedSize() + Rotations.GetAllocatedSize();
	}

	/**
	 * Position and orientation at a distance along the belt. Distances outside 0..Length clamp to the
	 * ends rather than wrapping, so an item that overshoots the end for one update sits on the last
	 * sample instead of teleporting to the start.
	 */
	FORCEINLINE void Evaluate(float Distance, FVector& OutPosition, FQuat& OutRotation) const
	{
		const int32 LastSample = Positions.Num() - 1;
		if (LastSample <= 0)
		{
			OutPosition = FVector::ZeroVector;
			OutRotation = FQuat::Identity;
			return;
		}

		const float SampleF = FMath::Clamp(Distance / SampleSpacing, 0.0f, static_cast<float>(LastSample));
		const int32 Index = FMath::Min(static_cast<int32>(SampleF), LastSample - 1);
		const float Alpha = SampleF - static_cast<float>(Index);

		OutPosition = FMath::Lerp(Positions[Index], Positions[Index + 1], Alpha);

		// FastLerp is a straight component lerp, so it needs the normalise afterwards. Over 50 cm of
		// belt the two samples are within a few degrees of each other and the shortest-arc term of a
		// real slerp would round to nothing - it is the cost of a square root per item for no picture.
		OutRotation = FQuat::FastLerp(Rotations[Index], Rotations[Index + 1], Alpha).GetNormalized();
	}
};
