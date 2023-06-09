// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/ChaosArchive.h"
#include "Chaos/Transform.h"
#include "UObject/FortniteReleaseBranchCustomObjectVersion.h"

namespace Chaos
{
	/**
	 * Controls how a kinematic body is integrated each Evolution Advance
	 */
	enum class EKinematicTargetMode
	{
		None,			/** Particle does not move and no data is changed */
		Reset,			/** Particle does not move, velocity and angular velocity are zeroed, then mode is set to "None". */
		Position,		/** Particle is moved to Kinematic Target transform, velocity and angular velocity updated to reflect the change, then mode is set to "Zero". */
		Velocity,		/** Particle is moved based on velocity and angular velocity, mode remains as "Velocity" until changed. */
	};

	/**
	 * Data used to integrate kinematic bodies
	 */
	template<class T, int d>
	class TKinematicTarget
	{
	public:
		TKinematicTarget()
			: Position(0)
			, Rotation(TRotation<T, d>::FromIdentity())
			, Mode(EKinematicTargetMode::None)
		{
		}

		/** Whether this kinematic target has been set (either velocity or position mode) */
		bool IsSet() const { return (Mode == EKinematicTargetMode::Position) || (Mode == EKinematicTargetMode::Velocity); }

		/** Get the kinematic target mode */
		EKinematicTargetMode GetMode() const { return Mode; }

		/** Get the target transform (asserts if not in Position mode) */
		TRigidTransform<T, d> GetTarget() const { check(Mode == EKinematicTargetMode::Position); return {Position, Rotation}; }

		/** Get the target position (asserts if not in Position mode) */
		TVector<T, d> GetTargetPosition() const { check(Mode == EKinematicTargetMode::Position); return Position; }

		/** Get the target rotation (asserts if not in Position mode) */
		TRotation<T, d> GetTargetRotation() const { check(Mode == EKinematicTargetMode::Position); return Rotation; }

		/** Clear the kinematic target */
		void Clear()
		{
			Position = TVector<T, d>();
			Rotation = TRotation<T, d>();
			Mode = EKinematicTargetMode::None;
		}

		/** Use transform target mode and set the transform target */
		void SetTargetMode(const TVector<T, d>& X, const TRotation<T, d>& R)
		{
			Position = X;
			Rotation = R;
			Mode = EKinematicTargetMode::Position;
		}

		/** Use transform target mode and set the transform target */
		void SetTargetMode(const TRigidTransform<T, d>& InTarget)
		{
			Position = InTarget.GetLocation();
			Rotation = InTarget.GetRotation();
			Mode = EKinematicTargetMode::Position;
		}

		/** Use velocity target mode */
		void SetVelocityMode() { Mode = EKinematicTargetMode::Velocity; }

		// For internal use only
		void SetMode(EKinematicTargetMode InMode) { Mode = InMode; }

		friend FChaosArchive& operator<<(FChaosArchive& Ar, TKinematicTarget<T, d>& KinematicTarget)
		{
			Ar.UsingCustomVersion(FPhysicsObjectVersion::GUID);

			if (Ar.CustomVer(FPhysicsObjectVersion::GUID) >= FPhysicsObjectVersion::ChaosKinematicTargetRemoveScale)
			{
				Ar << KinematicTarget.Position << KinematicTarget.Rotation << KinematicTarget.Mode;
			}
			else
			{
				FRigidTransform3 Transform;
				Ar << Transform << KinematicTarget.Mode;

				KinematicTarget.Position = TVec3<T>(Transform.GetLocation());
				KinematicTarget.Rotation = TRotation3<T>(Transform.GetRotation());
			}

			return Ar;
		}

		bool IsEqual(const TKinematicTarget& other) const
		{
			return (
				Mode == other.Mode &&
				Position == other.Position &&
				Rotation == other.Rotation
				);
		}

		template <typename TOther>
		bool IsEqual(const TOther& other) const
		{
			return IsEqual(other.KinematicTarget());
		}

		bool operator==(const TKinematicTarget& other) const
		{
			return IsEqual(other);
		}

		template <typename TOther>
		void CopyFrom(const TOther& Other)
		{
			Position = Other.KinematicTarget().Position;
			Rotation = Other.KinematicTarget().Rotation;
			Mode = Other.KinematicTarget().Mode;
		}

	private:
		TVector<T, d> Position;
		TRotation<T, d> Rotation;
		EKinematicTargetMode Mode;
	};
}
