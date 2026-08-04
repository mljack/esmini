/*
 * esmini - Environment Simulator Minimalistic
 * https://github.com/esmini/esmini
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) partners of Simulation Scenarios
 * https://sites.google.com/view/simulationscenarios
 */

#pragma once

// Trajectory speed-profile & keyframe solver (Trajectory_Editing.md section 4/8, Trajectory_Editing_
// Enhancement.md section 12). Deliberately self-contained - no OSG / pugixml / roadmanager dependencies -
// so the whole solver can be unit-tested headlessly (StudioTrajectorySolver_test.cpp) without any UI.

#include <cmath>
#include <limits>
#include <vector>

// A single distance-based speed profile point.
struct SpeedProfilePoint
{
    double s     = 0.0;  // position along the owning EntityPath's arc length (distance-based)
    double speed = 0.0;  // target speed at that position, in m/s
};

// A distance-based speed profile: speed as a function of arc length s along the owning EntityPath.
class EntitySpeedProfile
{
public:
    enum class InterpMode
    {
        LINEAR,
        MONOTONIC_CUBIC
    };

    std::vector<SpeedProfilePoint> points_;
    InterpMode                     interp_mode_ = InterpMode::LINEAR;

    // Evaluate the speed at arc length s (clamped to the profile's [first, last] range).
    double EvaluateSpeed(double s) const;

    // Insert a new point so that points_ stays ordered by s. Returns the index it was inserted at.
    int InsertPoint(double s, double speed);

    // Remove the point at index (no-op if index is out of range).
    void RemovePoint(int index);

    // Numerically integrate ds / v(s) from 0 to s (used to drive the ghost preview animation, see
    // Trajectory_Editing.md section 9.3). Very low speeds are clamped to avoid divide-by-zero.
    double EvaluateTimeAtS(double s) const;

    // Inverse of EvaluateTimeAtS: given an elapsed time, find the corresponding arc length s.
    double EvaluateSAtTime(double t) const;
};

// A time keyframe constraint on a trajectory: "at time t, the vehicle should be at arc length s along the
// path" (Trajectory_Editing_Enhancement.md). Only (t, s) is persisted; the world position is derived from
// the path at render time. achieved_t/feasible are solver outputs refreshed by SolveKeyframeChain().
struct TrajectoryKeyframe
{
    double t = 0.0;  // desired arrival time, seconds
    double s = 0.0;  // position along the owning EntityPath's arc length

    // Solve results (not persisted): the arrival time actually achievable after the last resolve. When the
    // constraint cannot be met exactly (speed/acceleration limits), feasible is false and achieved_t tells
    // the user what the clamped outcome is (Q3 in Trajectory_Editing_Enhancement.md section 12.4).
    double achieved_t = 0.0;
    bool   feasible   = true;

    // The profile node this keyframe currently owns (the "pin node" invariant, Trajectory_Editing_
    // Enhancement.md 12.3). NaN until the first solve adopts/creates the node. Not persisted: after any
    // solve pin_s == s, so a freshly loaded keyframe re-adopts its node on the first resolve.
    double pin_s = std::numeric_limits<double>::quiet_NaN();
};

// Kinematic limits and solver tuning (finalized decisions, Trajectory_Editing_Enhancement.md section 11):
// code constants for now, bundled in a struct so unit tests can exercise other values.
struct TrajectorySolverLimits
{
    double v_max = 30.0;  // hard speed ceiling, m/s
    double v_min = 0.1;   // integration clamp, matches EntitySpeedProfile::EvaluateTimeAtS(), m/s
    double a_acc = 3.0;   // acceleration limit (speed increasing along s), m/s^2
    double a_dec = 5.0;   // deceleration limit magnitude (speed decreasing along s), m/s^2

    double min_auto_point_spacing_time = 5.0;   // 12.5: solver-inserted shaping points at least this many
                                                // seconds (travel time) away from their neighbours
    double solve_tolerance = 0.05;              // 12.4: |achieved - target| below this counts as exact
    double insertion_gain  = 0.2;               // 12.4: Q2 must beat Q1's residual by at least this much
};

// ---------------------------------------------------------------------------------------------------------------
// Reachability oracle (Trajectory_Editing_Enhancement.md 12.2): free-terminal-speed segment bounds under the
// piecewise-linear-v(s) model, shared verbatim by the solver's Q0 precheck and the drag-time pre-clamping.
// ---------------------------------------------------------------------------------------------------------------

// The reachable travel-time window across a segment of the given length entered at v_entry, when the profile
// inside the segment may be shaped arbitrarily (within limits) and the terminal speed is free.
void SegmentTimeWindow(double v_entry, double length, const TrajectorySolverLimits& limits, double* out_t_min, double* out_t_max);

// The farthest distance reachable within dt seconds when entering at v_entry (free terminal speed). Inverse
// of SegmentTimeWindow's t_min, solved by bisection; monotone in dt.
double MaxReachableDistance(double v_entry, double dt, const TrajectorySolverLimits& limits);

// ---------------------------------------------------------------------------------------------------------------
// Keyframe chain solver (Trajectory_Editing_Enhancement.md 12.3/12.4/12.5): pin-node invariant + Q0-Q3
// cascade with the minimal-insertion principle.
// ---------------------------------------------------------------------------------------------------------------

// Re-solve every keyframe constraint against the current profile:
//  - clamps each keyframe's s into [0, total_length] and sorts by t (auto re-solve after path edits);
//  - maintains the pin-node invariant: each keyframe owns exactly one profile node at its s (moved along
//    with the keyframe, never duplicated across repeated solves);
//  - per segment runs Q0 (physical window precheck/clamp) -> Q1 (adjust existing nodes only: multiplicative
//    and saturating-additive 1-DOF families, best residual wins) -> insertion-gain gate -> Q2 (free-terminal
//    ramp+plateau construction, at most 1 interior point, 5s-merge applied) -> Q3 (clamp + residual);
//  - refreshes achieved_t/feasible on every keyframe; the profile is mutated in place.
void SolveKeyframeChain(EntitySpeedProfile&              profile,
                        std::vector<TrajectoryKeyframe>& keyframes,
                        double                           total_length,
                        const TrajectorySolverLimits&    limits = TrajectorySolverLimits());

// Remove keyframes[index] together with its pin node (unless the node is the s=0/end anchor or shared with
// another keyframe), then re-solve the remaining chain. Returns false if index is out of range.
bool RemoveKeyframe(EntitySpeedProfile&              profile,
                    std::vector<TrajectoryKeyframe>& keyframes,
                    size_t                           index,
                    double                           total_length,
                    const TrajectorySolverLimits&    limits = TrajectorySolverLimits());
