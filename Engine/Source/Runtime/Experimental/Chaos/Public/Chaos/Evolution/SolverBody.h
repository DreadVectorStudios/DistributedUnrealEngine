// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Core.h"
#include "Chaos/VectorUtility.h"
#include "Math/Quat.h"

// Set to 1 to enable extra NaN tests in the constraint solver
// NOTE: These will cause slowness!
#ifndef CHAOS_CONSTRAINTSOLVER_NAN_DIAGNOSTIC
#define CHAOS_CONSTRAINTSOLVER_NAN_DIAGNOSTIC ((DO_CHECK && UE_BUILD_DEBUG) || ENABLE_NAN_DIAGNOSTIC)
#endif

// Set to 1 to force single precision in the constraint solver, even if the default numeric type is double
#define CHAOS_CONSTRAINTSOLVER_LOWPRECISION 1


// Add some NaN checking when CHAOS_CONSTRAINTSOLVER_NAN_DIAGNOSTIC is defined
#if CHAOS_CONSTRAINTSOLVER_NAN_DIAGNOSTIC
inline void ChaosSolverCheckNaN(const Chaos::FReal& V) { ensure(!FMath::IsNaN(V)); }
inline void ChaosSolverCheckNaN(const Chaos::FVec3& V) { ensure(!V.ContainsNaN()); }
inline void ChaosSolverCheckNaN(const Chaos::FRotation3& V) { ensure(!V.ContainsNaN()); }

#else
#define ChaosSolverCheckNaN(...)
#endif


namespace Chaos
{
	// Set the math types used by the constraint solvers when single precision is acceptable.
	// NOTE: public APIs should still use FReal, FVec3 etc. whereas FSolverReal and associated
	// types are for use internally where double precision is unnecessary and causes performance
	// issues and/or memory bloat.
#if CHAOS_CONSTRAINTSOLVER_LOWPRECISION
	using FSolverReal = FRealSingle;
	using SolverVectorRegister = VectorRegister4Float;
#else
	using FSolverReal = FReal;
	using SolverVectorRegister = VectorRegister4;
#endif
	using FSolverVec3 = TVec3<FSolverReal>;
	using FSolverMatrix33 = TMatrix33<FSolverReal>;

	class FSolverBody;
	class FSolverBodyContainer;


	// A pair of pointers to solver bodies
	// @note Pointers are only valid for the Constraint Solving phase of the tick
	using FSolverBodyPtrPair = TVector<FSolverBody*, 2>;

	/**
	 * @brief An approximate quaternion normalize for use in the solver
	 * 
	 * @note we need to correctly normalize the final quaternion before pushing it back to the
	 * particle otherwise some tolerance checks elsewhere will fail (Integrate)
	 * 
	 * This avoids the sqrt which is a massively dominating cost especially with doubles
	 * when we do not have a fast reciproical sqrt (AVX2)
	 *
	 * This uses the first order Pade approximation instead of a Taylor expansion
	 * to get more accurate results for small quaternion deltas (i.e., where
	 * Q.SizeSquared() is already near 1). 
	 *
	 * Q.Normalized ~= Q * (2 / (1 + Q.SizeSquared)))
	 * 
	 * In practice we can use this for almost any delta generated in collision detection
	 * but we have an accurate fallback just in case. The fallback adds a branch but this 
	 * does not seem to cost much.
	 * 
	*/
	CHAOS_API void SolverQuaternionNormalizeApprox(FRotation3& InOutQ);

	CHAOS_API FRotation3 SolverQuaternionApplyAngularDeltaApprox(const FRotation3& InQ0, const FVec3& InDR);


	/**
	 * Used by the constraint solver loop to cache all state for a particle and accumulate solver results.
	 * Uses a gather/scatter mechanism to read/write data to the particle SOAs at the beginning/end of the constraint solve.
	 * Constraint solver algorithms, and collision Update functions are implemented to use FSolverBody, and do not 
	 * directly read/write to the particle handles. Constraint Solvers will modify P(), Q(), V() and W() via 
	 * ApplyTransformDelta() and other methods.
	 * 
	 * There is one SolverBody for each particle in an island. Most constraint solvers will actually wrap the
	 * FSolverBody in FConstraintSolverBody, which allows us to apply per-constraint modifiers to the Solver Body.
	 *
	 * @note the X(), P(), R(), Q() accessors on FSolverBody return the Center of Mass positions and rotations, in contrast
	 * to the Particle methods which gives Actor positions and rotations. This is because the Constraint Solvers all calculate
	 * impulses and position corrections relative to the center of mass.
	 * 
	 * @todo(chaos): layout for cache
	 * 
	 */
	class FSolverBody
	{
	public:

		/**
		 * @brief Create an empty solver body
		 * @note This is only used by unit tests
		*/
		FSolverBody();

		/**
		 * @brief Calculate and set the velocity and angular velocity from the net transform delta
		*/
		inline void SetImplicitVelocity(FReal Dt)
		{
			if (IsDynamic() && (Dt != FReal(0)))
			{
				const FSolverReal InvDt = FSolverReal(1) / FSolverReal(Dt);
				SetV(State.V + FVec3(State.DP * InvDt));
				SetW(State.W + FVec3(State.DQ * InvDt));
			}
		}

		/**
		 * @brief Get the inverse mass
		*/
		//inline FReal InvM() const { return State.InvM; }
		inline FSolverReal InvM() const { return State.InvM; }

		/**
		 * @brief Set the inverse mass
		*/
		inline void SetInvM(FReal InInvM) { State.InvM = FSolverReal(InInvM); }

		/**
		 * @brief Get the world-space inverse inertia
		*/
		inline const FSolverMatrix33& InvI() const { return State.InvI; }

		/**
		 * @brief Set the world-space inverse inertia
		*/
		inline void SetInvI(const FMatrix33& InInvI) { State.InvI = FSolverMatrix33(InInvI); }

		/**
		 * @brief Get the local-space inverse inertia (diagonal elements)
		*/
		inline const FSolverVec3& InvILocal() const { return State.InvILocal; }

		/**
		 * @brief Set the local-space inverse inertia (diagonal elements)
		*/
		inline void SetInvILocal(const FVec3& InInvILocal)
		{ 
			State.InvILocal = FSolverVec3(InInvILocal); 
			UpdateRotationDependentState();
		}

		/**
		 * @brief The current CoM transform
		*/
		inline FRigidTransform3 CoMTransform() const { return FRigidTransform3(P(), Q()); }

		/**
		 * @brief Pre-integration world-space center of mass position
		*/
		inline const FVec3& X() const { return State.X; }
		inline void SetX(const FVec3& InX) { State.X = InX; }

		/**
		 * @brief Pre-integration world-space center of mass rotation
		*/
		inline const FRotation3& R() const { return State.R; }
		inline void SetR(const FRotation3& InR)
		{
			ChaosSolverCheckNaN(InR);
			State.R = InR;
		}

		/**
		 * @brief Predicted (post-integrate) world-space center of mass position
		 * @note This does not get updated as we iterate
		 * @see DP(), CorrectedP()
		*/
		inline const FVec3& P() const { return State.P; }
		inline void SetP(const FVec3& InP) { State.P = InP; }

		/**
		 * @brief Predicted (post-integrate) world-space center of mass rotation
		 * @note This does not get updated as we iterate
		 * @see DQ(), CorrectedQ()
		*/
		inline const FRotation3& Q() const { return State.Q; }
		inline void SetQ(const FRotation3& InQ)
		{ 
			ChaosSolverCheckNaN(InQ);
			State.Q = InQ;
		}

		/**
		 * @brief World-space center of mass velocity
		*/
		inline const FVec3& V() const { return State.V; }
		inline void SetV(const FVec3& InV)
		{ 
			ChaosSolverCheckNaN(InV);
			State.V = InV;
		}

		/**
		 * @brief World-space center of mass angular velocity
		*/
		inline const FVec3& W() const { return State.W; }
		inline void SetW(const FVec3& InW)
		{
			ChaosSolverCheckNaN(InW);
			State.W = InW;
		}

		inline const FVec3& CoM() const { return State.CoM; }
		inline void SetCoM(const FVec3& InCoM) { State.CoM = InCoM; }

		inline const FRotation3& RoM() const { return State.RoM; }
		inline void SetRoM(const FRotation3& InRoM) { State.RoM = InRoM; }

		/**
		 * @brief Net world-space position correction applied by the constraints
		*/
		inline const FSolverVec3& DP() const { return State.DP; }

		/**
		 * @brief Net world-space rotation correction applied by the constraints (axis-angle vector equivalent to angular velocity but for position)
		*/
		inline const FSolverVec3& DQ() const { return State.DQ; }

		/**
		 * @brief World-space position after applying the net correction DP()
		 * @note Calculated on demand from P() and DP() (only requires vector addition)
		*/
		inline FVec3 CorrectedP() const { return State.P + FVec3(State.DP); }

		/**
		 * @brief World-space rotation after applying the net correction DQ()
		 * @note Calculated on demand from Q() and DQ() (requires quaternion multiply and normalization)
		*/
		inline FRotation3 CorrectedQ() const { return (IsDynamic() && !State.DQ.IsZero()) ? FRotation3::IntegrateRotationWithAngularVelocity(State.Q, FVec3(State.DQ), FReal(1)) : State.Q; }

		/**
		 * @brief Apply the accumulated position and rotation corrections to the predicted P and Q
		 * This is only used by unit tests that reuse solver bodies between ticks
		 * @todo(chaos): fix the unit tests (FJointSolverTest::Tick) and remove this
		*/
		void ApplyCorrections()
		{
			State.P = CorrectedP();
			State.Q = CorrectedQ();
			State.DP = FSolverVec3(0);
			State.DQ = FSolverVec3(0);
		}

		/**
		 * @brief Get the current world-space Actor position 
		 * @note This is recalculated from the current CoM transform including the accumulated position and rotation corrections.
		*/
		inline FVec3 ActorP() const { return CorrectedP() - ActorQ().RotateVector(CoM()); }

		/**
		 * @brief Get the current world-space Actor rotation
		 * @note This is recalculated from the current CoM transform including the accumulated position and rotation corrections.
		*/
		inline FRotation3 ActorQ() const { return CorrectedQ() * RoM().Inverse(); }

		/**
		 * @brief Contact graph level. This is used in shock propagation to determine which of two bodies should have its inverse mass scaled
		*/
		inline int32 Level() const { return State.Level; }
		inline void SetLevel(int32 InLevel) { State.Level = InLevel; }

		/**
		 * @brief Whether the body has a finite mass
		 * @note This is based on the current inverse mass, so a "dynamic" particle with 0 inverse mass will return true here.
		*/
		inline bool IsDynamic() const { return (State.InvM > SMALL_NUMBER); }

		/**
		 * @brief Apply a world-space position and rotation delta to the body center of mass, and update inverse mass
		*/
		inline void ApplyTransformDelta(const FSolverVec3& DP, const FSolverVec3& DR)
		{
			ApplyPositionDelta(DP);
			ApplyRotationDelta(DR);
		}

		/**
		 * @brief Apply a world-space position delta to the solver body center of mass
		*/
		inline void ApplyPositionDelta(const FSolverVec3& DP)
		{
			ChaosSolverCheckNaN(DP);
			State.DP += DP;
		}

		/**
		 * @brief Apply a world-space rotation delta to the solver body and update the inverse mass
		*/
		inline void ApplyRotationDelta(const FSolverVec3& DR)
		{
			ChaosSolverCheckNaN(DR);
			State.DQ += DR;
		}

		/**
		 * @brief Apply a world-space velocity delta to the solver body
		*/
		inline void ApplyVelocityDelta(const FSolverVec3& DV, const FSolverVec3& DW)
		{
			ApplyLinearVelocityDelta(DV);
			ApplyAngularVelocityDelta(DW);
		}

		/**
		 * @brief Apply a world-space linear velocity delta to the solver body
		*/
		inline void ApplyLinearVelocityDelta(const FSolverVec3& DV)
		{
			ChaosSolverCheckNaN(DV);
			State.V += FVec3(DV);
		}

		/**
		 * @brief Apply an world-space angular velocity delta to the solver body
		*/
		inline void ApplyAngularVelocityDelta(const FSolverVec3& DW)
		{
			ChaosSolverCheckNaN(DW);
			SetW(State.W + DW);
		}

		/**
		 * @brief Update the rotation to be in the same hemisphere as the provided quaternion.
		 * This is used by joints with angular constraint/drives
		*/
		inline void EnforceShortestRotationTo(const FRotation3& InQ)
		{
			State.Q.EnforceShortestArcWith(InQ);
		}

		/**
		 * @brief Update cached state that depends on rotation (i.e., world space inertia)
		*/
		void UpdateRotationDependentState();

	private:

		// The struct exists only so that we can use the variable names
		// as accessor names without violation the variable naming convention
		struct FState
		{
			FState()
				: InvILocal(0)
				, InvM(0)
				, InvI(0)
				, DP(0)
				, DQ(0)
				, Level(0)
				, X(0)
				, R(FRotation3::Identity)
				, P(0)
				, Q(FRotation3::Identity)
				, V(0)
				, W(0)
				, CoM(0)
				, RoM(FRotation3::Identity)
			{}

			// Local-space inverse inertia (diagonal, so only 3 elements)
			FSolverVec3 InvILocal;

			// Inverse mass
			FSolverReal InvM;

			// World-space inverse inertia
			// @todo(chaos): do we need this, or should we force all systems to use the FConstraintSolverBody decorator?
			FSolverMatrix33 InvI;

			// Net position delta applied by all constraints (constantly changing as we iterate over constraints)
			FSolverVec3 DP;

			// Net rotation delta applied by all constraints (constantly changing as we iterate over constraints)
			FSolverVec3 DQ;

			// Distance to a kinmatic body (through the contact graph). Used by collision shock propagation
			int32 Level;

			// World-space center of mass state at start of sub step
			FVec3 X;

			// World-space rotation of mass at start of sub step
			FRotation3 R;

			// Predicted world-space center of mass position (post-integration, pre-constraint-solve)
			FVec3 P;

			// Predicted world-space center of mass rotation (post-integration, pre-constraint-solve)
			FRotation3 Q;

			// World-space center of mass velocity
			FVec3 V;

			// World-space center of mass angular velocity
			FVec3 W;

			// Actor-space center of mass location
			FVec3 CoM;

			// Actor-space center of mass rotation
			FRotation3 RoM;
		};

		FState State;
	};


	/**
	 * An FSolverBody decorator for adding mass modifiers to a SolverBody. This will scale the
	 * inverse mass and inverse inertia using the supplied scale. It also updates IsDynamic() to
	 * return false if the scaled inverse mass is zero.
	 * 
	 * See FSolverBody for comments on methods.
	 * 
	 * @note This functionality cannot be in FSolverBody because two constraints referencing
	 * the same body may be applying different mass modifiers (e.g., Joints support "bParentDominates"
	 * which is a per-constraint property, not a per-body property.
	 */
	class FConstraintSolverBody
	{
	public:
		FConstraintSolverBody()
			: Body(nullptr)
		{
		}

		FConstraintSolverBody(FSolverBody& InBody)
			: Body(&InBody)
		{
		}

		FConstraintSolverBody(FSolverBody& InBody, FReal InInvMassScale)
			: Body(&InBody)
		{
			SetInvMScale(InInvMassScale);
		}

		/**
		 * @brief True if we have been set up to decorate a SolverBody
		*/
		inline bool IsValid() const { return Body != nullptr; }

		/**
		 * @brief Invalidate the solver body reference
		*/
		inline void Reset() { Body = nullptr; }

		/**
		 * @brief The decorated SolverBody
		*/
		inline FSolverBody& SolverBody() { check(IsValid()); return *Body; }
		inline const FSolverBody& SolverBody() const { check(IsValid()); return *Body; }

		/**
		 * @brief A scale applied to both inverse mass and inverse inertia
		*/
		inline FSolverReal InvMScale() const { return State.InvMassScale; }
		inline void SetInvMScale(FReal InInvMassScale) { State.InvMassScale = FSolverReal(InInvMassScale); }

		/**
		 * @brief The scaled inverse mass
		*/
		FSolverReal InvM() const { return State.InvMassScale * Body->InvM(); }

		/**
		 * @brief The scaled inverse inertia
		*/
		FSolverMatrix33 InvI() const { return State.InvMassScale * Body->InvI(); }

		/**
		 * @brief The scaled local space inverse inertia
		*/
		FSolverVec3 InvILocal() const { return State.InvMassScale * Body->InvILocal(); }

		/**
		 * @brief Whether the body is dynamic (i.e., has a finite mass) after InvMassScale is applied
		*/
		inline bool IsDynamic() const { return (Body->InvM() != 0) && (InvMScale() != 0); }

		//
		// From here all methods just forward to the FSolverBody
		//

		inline void SetImplicitVelocity(FReal Dt) { Body->SetImplicitVelocity(Dt); }
		inline FRigidTransform3 CoMTransform() const { return Body->CoMTransform(); }
		inline const FVec3& X() const { return Body->X(); }
		inline const FRotation3& R() const { return Body->R(); }
		inline const FVec3& P() const { return Body->P(); }
		inline const FRotation3& Q() const { return Body->Q(); }
		inline const FVec3 ActorP() const { return Body->ActorP(); }
		inline const FRotation3 ActorQ() const { return Body->ActorQ(); }
		inline const FVec3& V() const { return Body->V(); }
		inline const FVec3& W() const { return Body->W(); }
		inline int32 Level() const { return Body->Level(); }
		inline const FSolverVec3& DP() const { return Body->DP(); }
		inline const FSolverVec3& DQ() const { return Body->DQ(); }
		inline FVec3 CorrectedP() const { return Body->CorrectedP(); }
		inline FRotation3 CorrectedQ() const { return Body->CorrectedQ(); }

		inline void ApplyTransformDelta(const FSolverVec3& DP, const FSolverVec3& DR) { Body->ApplyTransformDelta(DP, DR); }
		inline void ApplyPositionDelta(const FSolverVec3& DP) { Body->ApplyPositionDelta(DP); }
		inline void ApplyRotationDelta(const FSolverVec3& DR) { Body->ApplyRotationDelta(DR); }
		inline void ApplyVelocityDelta(const FSolverVec3& DV, const FSolverVec3& DW) { Body->ApplyVelocityDelta(DV, DW); }
		inline void ApplyLinearVelocityDelta(const FSolverVec3& DV) { Body->ApplyLinearVelocityDelta(DV); }
		inline void ApplyAngularVelocityDelta(const FSolverVec3& DW) { Body->ApplyAngularVelocityDelta(DW); }
		inline void EnforceShortestRotationTo(const FRotation3& InQ) { Body->EnforceShortestRotationTo(InQ); }
		inline void UpdateRotationDependentState() { Body->UpdateRotationDependentState(); }

	private:
		// Struct is only so that we can use the same var names as function names
		struct FState
		{
			FState() 
				: InvMassScale(FSolverReal(1))
			{}
			FSolverReal InvMassScale;
		};

		// The body we decorate
		FSolverBody* Body;

		// The body modifiers
		FState State;
	};
}
