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

// Headless unit tests for the trajectory speed-profile & keyframe solver
// (Trajectory_Editing_Enhancement.md section 12). The solver module is UI/OSG-free by design, so every
// behavior - profile math, reachability oracle, Q0-Q3 cascade, pin-node lifecycle, boundary values and
// operation combinations - is exercised here directly against hand-built trajectory data.

#include "TrajectorySolver.hpp"

#include "gtest/gtest.h"

#include <cmath>
#include <vector>

namespace
{
EntitySpeedProfile MakeFlatProfile(double length, double speed)
{
    EntitySpeedProfile profile;
    profile.points_.push_back({0.0, speed});
    profile.points_.push_back({length, speed});
    return profile;
}

TrajectoryKeyframe MakeKeyframe(double t, double s)
{
    TrajectoryKeyframe kf;
    kf.t = t;
    kf.s = s;
    return kf;
}

// Mirrors EntityTrajectory::SyncSpeedProfileEndpoints(): first anchor at s=0, last anchor at path length.
void SyncEndpoints(EntitySpeedProfile& profile, double length)
{
    if (profile.points_.empty())
        return;
    profile.points_.front().s = 0.0;
    if (profile.points_.size() >= 2)
        profile.points_.back().s = length;
}

void Solve(EntitySpeedProfile& profile, std::vector<TrajectoryKeyframe>& keyframes, double length, const TrajectorySolverLimits& limits = TrajectorySolverLimits())
{
    SyncEndpoints(profile, length);
    SolveKeyframeChain(profile, keyframes, length, limits);
}

int FindNode(const EntitySpeedProfile& profile, double s)
{
    for (size_t i = 0; i < profile.points_.size(); i++)
        if (std::fabs(profile.points_[i].s - s) < 1e-6)
            return static_cast<int>(i);
    return -1;
}

int CountInteriorNodes(const EntitySpeedProfile& profile, double s_lo, double s_hi)
{
    int n = 0;
    for (const auto& p : profile.points_)
        if (p.s > s_lo + 1e-6 && p.s < s_hi - 1e-6)
            n++;
    return n;
}

bool ProfileSorted(const EntitySpeedProfile& profile)
{
    for (size_t i = 0; i + 1 < profile.points_.size(); i++)
        if (profile.points_[i].s > profile.points_[i + 1].s + 1e-9)
            return false;
    return true;
}

// Same peak-acceleration test the solver applies to the pieces it touched (a(s) = v * dv/ds peaks at the
// fast end of a linear-in-s piece).
bool AccelOkInRange(const EntitySpeedProfile& profile, double s_lo, double s_hi, const TrajectorySolverLimits& lim = TrajectorySolverLimits())
{
    const auto& pts = profile.points_;
    for (size_t i = 0; i + 1 < pts.size(); i++)
    {
        double s1 = pts[i].s, s2 = pts[i + 1].s;
        if (s2 <= s_lo + 1e-6 || s1 >= s_hi - 1e-6)
            continue;

        double ds = s2 - s1;
        double v1 = pts[i].speed, v2 = pts[i + 1].speed;
        if (ds < 1e-9)
        {
            if (std::fabs(v2 - v1) > 1e-6)
                return false;
            continue;
        }
        double slope   = (v2 - v1) / ds;
        double a_peak  = std::fabs(slope) * std::max(std::max(v1, v2), lim.v_min);
        double a_limit = (v2 > v1) ? lim.a_acc : lim.a_dec;
        if (a_peak > a_limit * 1.01 + 1e-9)
            return false;
    }
    return true;
}
}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// EntitySpeedProfile basics
// ---------------------------------------------------------------------------------------------------------------

TEST(SpeedProfile, EmptyAndSinglePoint)
{
    EntitySpeedProfile profile;
    EXPECT_DOUBLE_EQ(profile.EvaluateSpeed(10.0), 0.0);
    EXPECT_DOUBLE_EQ(profile.EvaluateTimeAtS(10.0), 0.0);
    EXPECT_DOUBLE_EQ(profile.EvaluateSAtTime(10.0), 0.0);

    profile.points_.push_back({0.0, 7.0});
    EXPECT_DOUBLE_EQ(profile.EvaluateSpeed(-5.0), 7.0);
    EXPECT_DOUBLE_EQ(profile.EvaluateSpeed(0.0), 7.0);
    EXPECT_DOUBLE_EQ(profile.EvaluateSpeed(100.0), 7.0);
    EXPECT_DOUBLE_EQ(profile.EvaluateTimeAtS(50.0), 0.0);  // profile spans zero length
}

TEST(SpeedProfile, LinearInterpolationAndClamp)
{
    EntitySpeedProfile profile = MakeFlatProfile(100.0, 10.0);
    profile.points_[1].speed   = 20.0;

    EXPECT_NEAR(profile.EvaluateSpeed(0.0), 10.0, 1e-12);
    EXPECT_NEAR(profile.EvaluateSpeed(50.0), 15.0, 1e-12);
    EXPECT_NEAR(profile.EvaluateSpeed(100.0), 20.0, 1e-12);
    EXPECT_NEAR(profile.EvaluateSpeed(-10.0), 10.0, 1e-12);  // clamped below
    EXPECT_NEAR(profile.EvaluateSpeed(200.0), 20.0, 1e-12);  // clamped above
}

TEST(SpeedProfile, InsertAndRemoveKeepOrdering)
{
    EntitySpeedProfile profile = MakeFlatProfile(100.0, 10.0);

    int idx = profile.InsertPoint(30.0, 12.0);
    EXPECT_EQ(idx, 1);
    idx = profile.InsertPoint(60.0, 14.0);
    EXPECT_EQ(idx, 2);
    idx = profile.InsertPoint(10.0, 11.0);
    EXPECT_EQ(idx, 1);

    ASSERT_EQ(profile.points_.size(), 5u);
    EXPECT_TRUE(ProfileSorted(profile));

    profile.RemovePoint(-1);                                   // out of range: no-op
    profile.RemovePoint(static_cast<int>(profile.points_.size()));  // out of range: no-op
    EXPECT_EQ(profile.points_.size(), 5u);

    profile.RemovePoint(1);
    EXPECT_EQ(profile.points_.size(), 4u);
    EXPECT_EQ(FindNode(profile, 10.0), -1);
}

TEST(SpeedProfile, TimeIntegrationConstantSpeed)
{
    EntitySpeedProfile profile = MakeFlatProfile(100.0, 10.0);

    EXPECT_NEAR(profile.EvaluateTimeAtS(100.0), 10.0, 1e-6);
    EXPECT_NEAR(profile.EvaluateTimeAtS(50.0), 5.0, 1e-6);
    EXPECT_NEAR(profile.EvaluateTimeAtS(500.0), 10.0, 1e-6);  // clamped to profile end
    EXPECT_DOUBLE_EQ(profile.EvaluateTimeAtS(0.0), 0.0);
}

TEST(SpeedProfile, TimeAndDistanceRoundTrip)
{
    EntitySpeedProfile profile = MakeFlatProfile(200.0, 10.0);
    profile.InsertPoint(100.0, 20.0);

    double t73 = profile.EvaluateTimeAtS(73.0);
    EXPECT_NEAR(profile.EvaluateSAtTime(t73), 73.0, 1e-3);

    EXPECT_DOUBLE_EQ(profile.EvaluateSAtTime(0.0), 0.0);
    EXPECT_DOUBLE_EQ(profile.EvaluateSAtTime(-1.0), 0.0);
    EXPECT_NEAR(profile.EvaluateSAtTime(1e9), 200.0, 1e-9);  // beyond total time: end of profile
}

TEST(SpeedProfile, MonotonicCubicHitsKnotsWithoutOvershoot)
{
    EntitySpeedProfile profile;
    profile.interp_mode_ = EntitySpeedProfile::InterpMode::MONOTONIC_CUBIC;
    profile.points_      = {{0.0, 10.0}, {100.0, 10.0}, {200.0, 20.0}, {300.0, 20.0}};

    EXPECT_NEAR(profile.EvaluateSpeed(0.0), 10.0, 1e-9);
    EXPECT_NEAR(profile.EvaluateSpeed(100.0), 10.0, 1e-9);
    EXPECT_NEAR(profile.EvaluateSpeed(200.0), 20.0, 1e-9);
    EXPECT_NEAR(profile.EvaluateSpeed(300.0), 20.0, 1e-9);

    for (int i = 0; i <= 50; i++)
    {
        double s = 100.0 + 2.0 * i;
        double v = profile.EvaluateSpeed(s);
        EXPECT_GE(v, 10.0 - 1e-9) << "overshoot below at s=" << s;
        EXPECT_LE(v, 20.0 + 1e-9) << "overshoot above at s=" << s;
    }
    for (int i = 0; i <= 20; i++)  // flat region stays flat
        EXPECT_NEAR(profile.EvaluateSpeed(5.0 * i), 10.0, 1e-9);
}

// ---------------------------------------------------------------------------------------------------------------
// Reachability oracle
// ---------------------------------------------------------------------------------------------------------------

TEST(Oracle, WindowSanity)
{
    TrajectorySolverLimits lim;
    double                 t_min = 0.0, t_max = 0.0;

    // v=10 m/s entry, 100 m: fastest = full-length accel ramp to ~23 m/s (~6.4 s), slowest = crawl.
    SegmentTimeWindow(10.0, 100.0, lim, &t_min, &t_max);
    EXPECT_GT(t_min, 0.0);
    EXPECT_NEAR(t_min, 6.40, 0.1);
    EXPECT_LT(t_min, 10.0);   // faster than staying at entry speed
    EXPECT_GT(t_max, 100.0);  // crawling at v_min takes much longer
}

TEST(Oracle, WindowZeroLengthAndStandstill)
{
    TrajectorySolverLimits lim;
    double                 t_min = -1.0, t_max = -1.0;

    SegmentTimeWindow(10.0, 0.0, lim, &t_min, &t_max);
    EXPECT_DOUBLE_EQ(t_min, 0.0);
    EXPECT_DOUBLE_EQ(t_max, 0.0);

    // From standstill the fastest 100 m is still finite (accel-limited ramp).
    SegmentTimeWindow(0.0, 100.0, lim, &t_min, &t_max);
    EXPECT_GT(t_min, 5.0);
    EXPECT_LT(t_min, 40.0);
    EXPECT_GE(t_max, t_min);
}

TEST(Oracle, MaxReachableDistanceMonotoneAndConsistent)
{
    TrajectorySolverLimits lim;

    EXPECT_DOUBLE_EQ(MaxReachableDistance(10.0, 0.0, lim), 0.0);

    double d2 = MaxReachableDistance(10.0, 2.0, lim);
    double d4 = MaxReachableDistance(10.0, 4.0, lim);
    double d8 = MaxReachableDistance(10.0, 8.0, lim);
    EXPECT_GT(d2, 10.0);  // faster than holding entry speed (can accelerate)
    EXPECT_GT(d4, d2);
    EXPECT_GT(d8, d4);
    EXPECT_LE(d8, lim.v_max * 8.0 + 1e-6);  // can never beat the ceiling speed

    // Inverse consistency: travelling the returned distance takes (at best) almost exactly dt.
    double t_min = 0.0;
    SegmentTimeWindow(10.0, d8, lim, &t_min, nullptr);
    EXPECT_NEAR(t_min, 8.0, 0.05);
}

// ---------------------------------------------------------------------------------------------------------------
// Chain solve: Q1 (adjust existing nodes only, no insertion)
// ---------------------------------------------------------------------------------------------------------------

TEST(ChainSolve, ExactSolutionViaExistingNodesOnly)
{
    EntitySpeedProfile              profile   = MakeFlatProfile(200.0, 10.0);
    std::vector<TrajectoryKeyframe> keyframes = {MakeKeyframe(8.0, 100.0)};

    Solve(profile, keyframes, 200.0);

    // Needs avg 12.5 m/s over [0,100]: solved by scaling v(0)/pin only - no shaping points.
    EXPECT_TRUE(keyframes[0].feasible);
    EXPECT_NEAR(keyframes[0].achieved_t, 8.0, 1e-3);
    EXPECT_NEAR(profile.EvaluateTimeAtS(100.0), 8.0, 1e-3);
    ASSERT_EQ(profile.points_.size(), 3u);  // two anchors + the pin node, nothing else
    EXPECT_GE(FindNode(profile, 100.0), 0);
    EXPECT_NEAR(profile.EvaluateSpeed(0.0), 12.5, 0.01);
    EXPECT_NEAR(profile.EvaluateSpeed(100.0), 12.5, 0.01);
    EXPECT_NEAR(profile.EvaluateSpeed(200.0), 10.0, 1e-9);  // untouched outside the segment
    EXPECT_NEAR(keyframes[0].pin_s, 100.0, 1e-9);
    EXPECT_TRUE(AccelOkInRange(profile, 0.0, 100.0));
}

TEST(ChainSolve, FirstSegmentFromStandstillUsesAdditiveFamily)
{
    // All-zero profile: the multiplicative family is dead (0 * lambda == 0); the saturating additive family
    // must raise v(0) itself (v(0) adjustable by finalized decision).
    EntitySpeedProfile              profile   = MakeFlatProfile(200.0, 0.0);
    std::vector<TrajectoryKeyframe> keyframes = {MakeKeyframe(10.0, 100.0)};

    Solve(profile, keyframes, 200.0);

    EXPECT_TRUE(keyframes[0].feasible);
    EXPECT_NEAR(keyframes[0].achieved_t, 10.0, 0.05);
    EXPECT_NEAR(profile.EvaluateSpeed(0.0), 10.0, 0.1);
    EXPECT_NEAR(profile.EvaluateSpeed(50.0), 10.0, 0.1);
    EXPECT_EQ(CountInteriorNodes(profile, 0.0, 100.0), 0);  // no shaping points needed
    ASSERT_EQ(profile.points_.size(), 3u);
}

TEST(ChainSolve, ManualShapePreservedByMultiplicativeFamily)
{
    // A user-authored hump: 10 -> 20 -> 10. Speeding the whole thing up must preserve the 2:1 shape and add
    // no points (Q1a is shape preserving).
    EntitySpeedProfile profile;
    profile.points_ = {{0.0, 10.0}, {150.0, 20.0}, {300.0, 10.0}};

    std::vector<TrajectoryKeyframe> keyframes = {MakeKeyframe(16.6, 300.0)};
    Solve(profile, keyframes, 300.0);

    EXPECT_TRUE(keyframes[0].feasible);
    EXPECT_NEAR(keyframes[0].achieved_t, 16.6, 0.05);
    ASSERT_EQ(profile.points_.size(), 3u);  // pin == end anchor, nothing inserted
    double ratio = profile.EvaluateSpeed(150.0) / profile.EvaluateSpeed(0.0);
    EXPECT_NEAR(ratio, 2.0, 0.02);
    EXPECT_TRUE(AccelOkInRange(profile, 0.0, 300.0));
}

// ---------------------------------------------------------------------------------------------------------------
// Chain solve: Q0/Q3 (physical window, clamping, no pointless insertion)
// ---------------------------------------------------------------------------------------------------------------

TEST(ChainSolve, ImpossibleTargetClampedWithoutInsertion)
{
    // 100 m in 1 s needs avg 100 m/s >> v_max: expect clamp to the physical optimum (flat v_max) with
    // feasible=false and zero shaping points (insertion-gain gate: inserting cannot beat physics).
    EntitySpeedProfile              profile   = MakeFlatProfile(100.0, 10.0);
    std::vector<TrajectoryKeyframe> keyframes = {MakeKeyframe(1.0, 100.0)};

    Solve(profile, keyframes, 100.0);

    TrajectorySolverLimits lim;
    EXPECT_FALSE(keyframes[0].feasible);
    EXPECT_NEAR(keyframes[0].achieved_t, 100.0 / lim.v_max, 0.02);
    ASSERT_EQ(profile.points_.size(), 2u);  // pin == end anchor; no interior points at all
    EXPECT_EQ(CountInteriorNodes(profile, 0.0, 100.0), 0);
    EXPECT_NEAR(profile.EvaluateSpeed(0.0), lim.v_max, 0.01);
    EXPECT_NEAR(profile.EvaluateSpeed(100.0), lim.v_max, 0.01);
}

TEST(ChainSolve, AbsurdlySlowTargetClampedToCrawl)
{
    // 100 m in 10000 s is below the v_min crawl: clamp to t_max, feasible=false, profile stays sane.
    EntitySpeedProfile              profile   = MakeFlatProfile(100.0, 10.0);
    std::vector<TrajectoryKeyframe> keyframes = {MakeKeyframe(10000.0, 100.0)};

    Solve(profile, keyframes, 100.0);

    EXPECT_FALSE(keyframes[0].feasible);
    EXPECT_LT(keyframes[0].achieved_t, 10000.0);
    EXPECT_GT(keyframes[0].achieved_t, 100.0 / 30.0);
    EXPECT_TRUE(ProfileSorted(profile));
    for (const auto& p : profile.points_)
    {
        EXPECT_GE(p.speed, 0.0);
        EXPECT_LE(p.speed, 30.0 + 1e-9);
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Chain solve: Q2 (single shaping point, free terminal speed, 5s merge)
// ---------------------------------------------------------------------------------------------------------------

TEST(ChainSolve, Q2InsertsExactlyOneShapingPoint)
{
    // Segment [100,400] entered at 10 m/s, target 15 s (avg 20 m/s). The 1-DOF Q1 ramp saturates at
    // 300*ln(3)/20 = 16.48 s, so the gate opens and Q2 must build ramp+plateau with exactly one interior
    // point, hitting the target exactly.
    EntitySpeedProfile              profile   = MakeFlatProfile(500.0, 10.0);
    std::vector<TrajectoryKeyframe> keyframes = {MakeKeyframe(10.0, 100.0), MakeKeyframe(25.0, 400.0)};

    Solve(profile, keyframes, 500.0);

    EXPECT_TRUE(keyframes[0].feasible);
    EXPECT_NEAR(keyframes[0].achieved_t, 10.0, 0.05);
    EXPECT_TRUE(keyframes[1].feasible);
    EXPECT_NEAR(keyframes[1].achieved_t, 25.0, 0.05);

    ASSERT_EQ(CountInteriorNodes(profile, 100.0, 400.0), 1);
    int ramp_idx = -1;
    for (size_t i = 0; i < profile.points_.size(); i++)
        if (profile.points_[i].s > 100.0 + 1e-6 && profile.points_[i].s < 400.0 - 1e-6)
            ramp_idx = static_cast<int>(i);
    ASSERT_GE(ramp_idx, 0);

    // Ramp node and pin node share the plateau speed (free-terminal construction carries momentum forward).
    double v_plateau = profile.points_[static_cast<size_t>(ramp_idx)].speed;
    EXPECT_GT(v_plateau, 20.0);
    EXPECT_LT(v_plateau, 26.0);
    EXPECT_NEAR(profile.EvaluateSpeed(400.0), v_plateau, 1e-6);

    EXPECT_TRUE(AccelOkInRange(profile, 0.0, 400.0));
}

TEST(ChainSolve, NearBoundaryShapingPointKeptWhenPrecisionRequiresIt)
{
    // Target barely above the physical optimum: the plateau lasts < 5 s, so the 5s merge is attempted, but
    // dropping the point would regress the residual badly - the merge must be reverted (spacing rule is
    // soft, precision wins) and the point kept.
    EntitySpeedProfile              profile   = MakeFlatProfile(500.0, 10.0);
    std::vector<TrajectoryKeyframe> keyframes = {MakeKeyframe(10.0, 100.0), MakeKeyframe(24.33, 400.0)};

    Solve(profile, keyframes, 500.0);

    EXPECT_TRUE(keyframes[1].feasible);
    EXPECT_NEAR(keyframes[1].achieved_t, 24.33, 0.05);
    EXPECT_EQ(CountInteriorNodes(profile, 100.0, 400.0), 1);
    EXPECT_TRUE(AccelOkInRange(profile, 0.0, 400.0));
}

TEST(ChainSolve, LooseToleranceStopsAtQ1WithoutInsertion)
{
    // Same scenario, but with a loose solve tolerance the Q1 saturated ramp (16.48 s vs target 14.4 s,
    // residual ~2.1 s) already counts as "solved": no shaping point may be inserted.
    TrajectorySolverLimits lim;
    lim.solve_tolerance = 3.0;

    EntitySpeedProfile              profile   = MakeFlatProfile(500.0, 10.0);
    std::vector<TrajectoryKeyframe> keyframes = {MakeKeyframe(10.0, 100.0), MakeKeyframe(24.4, 400.0)};

    Solve(profile, keyframes, 500.0, lim);

    EXPECT_TRUE(keyframes[1].feasible);  // within the loose tolerance
    EXPECT_EQ(CountInteriorNodes(profile, 100.0, 400.0), 0);
    EXPECT_NEAR(profile.EvaluateSpeed(400.0), 30.0, 0.1);  // Q1 multiplicative family saturated at v_max
}

// ---------------------------------------------------------------------------------------------------------------
// Pin-node lifecycle: stability, relocation, removal
// ---------------------------------------------------------------------------------------------------------------

TEST(PinNodes, RepeatedResolvesDoNotAccumulateNodes)
{
    EntitySpeedProfile              profile   = MakeFlatProfile(600.0, 10.0);
    std::vector<TrajectoryKeyframe> keyframes = {MakeKeyframe(12.0, 100.0), MakeKeyframe(32.0, 300.0), MakeKeyframe(52.0, 500.0)};

    Solve(profile, keyframes, 600.0);
    size_t count_after_first = profile.points_.size();
    EXPECT_EQ(count_after_first, 5u);  // 2 anchors + 3 pins

    Solve(profile, keyframes, 600.0);
    Solve(profile, keyframes, 600.0);
    EXPECT_EQ(profile.points_.size(), count_after_first);  // idempotent: no duplicates

    for (const auto& kf : keyframes)
    {
        EXPECT_TRUE(kf.feasible);
        EXPECT_NEAR(kf.achieved_t, kf.t, 0.05);
        EXPECT_NEAR(kf.pin_s, kf.s, 1e-9);
        EXPECT_GE(FindNode(profile, kf.s), 0);
    }
}

TEST(PinNodes, MovingKeyframeRelocatesItsNode)
{
    EntitySpeedProfile              profile   = MakeFlatProfile(600.0, 10.0);
    std::vector<TrajectoryKeyframe> keyframes = {MakeKeyframe(12.0, 100.0), MakeKeyframe(32.0, 300.0), MakeKeyframe(52.0, 500.0)};
    Solve(profile, keyframes, 600.0);

    keyframes[1].s = 350.0;  // user drags the middle keyframe along the path
    Solve(profile, keyframes, 600.0);
    Solve(profile, keyframes, 600.0);

    EXPECT_EQ(FindNode(profile, 300.0), -1);  // old pin relocated, not duplicated
    EXPECT_GE(FindNode(profile, 350.0), 0);
    EXPECT_EQ(profile.points_.size(), 5u);
    for (const auto& kf : keyframes)
        EXPECT_TRUE(kf.feasible);
}

TEST(PinNodes, RemoveKeyframeCleansUpItsNode)
{
    EntitySpeedProfile              profile   = MakeFlatProfile(600.0, 10.0);
    std::vector<TrajectoryKeyframe> keyframes = {MakeKeyframe(12.0, 100.0), MakeKeyframe(32.0, 300.0), MakeKeyframe(52.0, 500.0)};
    Solve(profile, keyframes, 600.0);

    EXPECT_TRUE(RemoveKeyframe(profile, keyframes, 1, 600.0));
    EXPECT_EQ(keyframes.size(), 2u);
    EXPECT_EQ(FindNode(profile, 300.0), -1);  // pin node removed together with the keyframe
    EXPECT_EQ(profile.points_.size(), 4u);
    for (const auto& kf : keyframes)
        EXPECT_TRUE(kf.feasible);  // remaining chain re-solved

    EXPECT_FALSE(RemoveKeyframe(profile, keyframes, 99, 600.0));  // out of range
    EXPECT_EQ(keyframes.size(), 2u);
}

TEST(PinNodes, AnchorsSurviveKeyframeRemoval)
{
    // A keyframe pinned on the end anchor: removing it must not delete the anchor node.
    EntitySpeedProfile              profile   = MakeFlatProfile(200.0, 10.0);
    std::vector<TrajectoryKeyframe> keyframes = {MakeKeyframe(18.0, 200.0)};
    Solve(profile, keyframes, 200.0);
    EXPECT_EQ(profile.points_.size(), 2u);  // pin == end anchor, nothing added

    EXPECT_TRUE(RemoveKeyframe(profile, keyframes, 0, 200.0));
    EXPECT_TRUE(keyframes.empty());
    ASSERT_EQ(profile.points_.size(), 2u);
    EXPECT_GE(FindNode(profile, 0.0), 0);
    EXPECT_GE(FindNode(profile, 200.0), 0);
}

TEST(PinNodes, SharedPinSurvivesUntilLastOwnerRemoved)
{
    // Two keyframes at the same s (the second one is unsatisfiable, but must not crash): the shared node
    // survives the first removal and disappears with the last owner.
    EntitySpeedProfile              profile   = MakeFlatProfile(200.0, 10.0);
    std::vector<TrajectoryKeyframe> keyframes = {MakeKeyframe(10.0, 100.0), MakeKeyframe(20.0, 100.0)};

    Solve(profile, keyframes, 200.0);
    EXPECT_EQ(profile.points_.size(), 3u);  // one shared node at s=100
    EXPECT_FALSE(keyframes[1].feasible);    // zero-length segment with dt > 0

    EXPECT_TRUE(RemoveKeyframe(profile, keyframes, 0, 200.0));
    EXPECT_GE(FindNode(profile, 100.0), 0);  // still owned by the remaining keyframe
    EXPECT_EQ(profile.points_.size(), 3u);

    EXPECT_TRUE(RemoveKeyframe(profile, keyframes, 0, 200.0));
    EXPECT_EQ(FindNode(profile, 100.0), -1);
    EXPECT_EQ(profile.points_.size(), 2u);
}

// ---------------------------------------------------------------------------------------------------------------
// Boundary values and degenerate inputs
// ---------------------------------------------------------------------------------------------------------------

TEST(Boundaries, KeyframeAtStartWithPositiveTime)
{
    EntitySpeedProfile              profile   = MakeFlatProfile(200.0, 10.0);
    std::vector<TrajectoryKeyframe> keyframes = {MakeKeyframe(5.0, 0.0)};

    Solve(profile, keyframes, 200.0);  // "be at s=0 after 5 s" is unsolvable but must not crash

    EXPECT_FALSE(keyframes[0].feasible);
    EXPECT_NEAR(keyframes[0].achieved_t, 0.0, 1e-9);
    EXPECT_EQ(profile.points_.size(), 2u);  // pin == start anchor
}

TEST(Boundaries, KeyframeAtZeroTime)
{
    EntitySpeedProfile              profile   = MakeFlatProfile(200.0, 10.0);
    std::vector<TrajectoryKeyframe> keyframes = {MakeKeyframe(0.0, 100.0)};

    Solve(profile, keyframes, 200.0);  // "reach s=100 in 0 s" is unsolvable but must not crash

    EXPECT_FALSE(keyframes[0].feasible);
    EXPECT_TRUE(ProfileSorted(profile));
}

TEST(Boundaries, KeyframeSClampedIntoPathRange)
{
    EntitySpeedProfile              profile   = MakeFlatProfile(200.0, 10.0);
    std::vector<TrajectoryKeyframe> keyframes = {MakeKeyframe(5.0, -50.0), MakeKeyframe(25.0, 350.0)};

    Solve(profile, keyframes, 200.0);

    EXPECT_DOUBLE_EQ(keyframes[0].s, 0.0);
    EXPECT_DOUBLE_EQ(keyframes[1].s, 200.0);
    EXPECT_TRUE(ProfileSorted(profile));
}

TEST(Boundaries, DuplicateTimesDoNotCrash)
{
    EntitySpeedProfile              profile   = MakeFlatProfile(300.0, 10.0);
    std::vector<TrajectoryKeyframe> keyframes = {MakeKeyframe(10.0, 100.0), MakeKeyframe(10.0, 150.0)};

    Solve(profile, keyframes, 300.0);

    // One of the two gets dt == 0 for a non-empty distance: infeasible, but well-defined.
    EXPECT_TRUE(ProfileSorted(profile));
    EXPECT_FALSE(keyframes[0].feasible && keyframes[1].feasible);
}

TEST(Boundaries, EmptyInputsAreNoOps)
{
    EntitySpeedProfile              empty_profile;
    std::vector<TrajectoryKeyframe> keyframes = {MakeKeyframe(5.0, 50.0)};
    SolveKeyframeChain(empty_profile, keyframes, 100.0, TrajectorySolverLimits());
    EXPECT_TRUE(empty_profile.points_.empty());

    EntitySpeedProfile              profile      = MakeFlatProfile(100.0, 10.0);
    std::vector<TrajectoryKeyframe> no_keyframes;
    SolveKeyframeChain(profile, no_keyframes, 100.0, TrajectorySolverLimits());
    EXPECT_EQ(profile.points_.size(), 2u);
    EXPECT_FALSE(RemoveKeyframe(profile, no_keyframes, 0, 100.0));
}

// ---------------------------------------------------------------------------------------------------------------
// Operation combinations
// ---------------------------------------------------------------------------------------------------------------

TEST(Combinations, PathShrinkClampsKeyframeAndPrunesStaleNodes)
{
    EntitySpeedProfile              profile   = MakeFlatProfile(200.0, 10.0);
    std::vector<TrajectoryKeyframe> keyframes = {MakeKeyframe(15.0, 150.0)};
    Solve(profile, keyframes, 200.0);
    EXPECT_EQ(profile.points_.size(), 3u);

    // Path edited: total length shrinks to 120 m. The caller syncs the end anchor; the solver must clamp
    // the keyframe, prune the now-out-of-range pin node and re-solve without leaving stale points behind.
    Solve(profile, keyframes, 120.0);

    EXPECT_DOUBLE_EQ(keyframes[0].s, 120.0);
    EXPECT_NEAR(keyframes[0].pin_s, 120.0, 1e-9);
    EXPECT_TRUE(ProfileSorted(profile));
    for (const auto& p : profile.points_)
        EXPECT_LE(p.s, 120.0 + 1e-9);
    EXPECT_EQ(profile.points_.size(), 2u);  // stale node at s=150 pruned; pin == new end anchor
    EXPECT_TRUE(keyframes[0].feasible);
    EXPECT_NEAR(keyframes[0].achieved_t, 15.0, 0.05);
    EXPECT_NEAR(profile.EvaluateSpeed(0.0), 8.0, 0.05);  // 120 m in 15 s = 8 m/s
}

TEST(Combinations, UserNodesInsideSegmentSurviveQ1Solves)
{
    // A user-authored slowdown dip inside the solved segment: a Q1-solvable target must keep the node
    // (only its speed may change, shape-preserving).
    EntitySpeedProfile profile = MakeFlatProfile(200.0, 10.0);
    profile.InsertPoint(50.0, 5.0);  // manual dip

    std::vector<TrajectoryKeyframe> keyframes = {MakeKeyframe(20.0, 100.0)};
    Solve(profile, keyframes, 200.0);

    EXPECT_GE(FindNode(profile, 50.0), 0);  // manual node still there
    EXPECT_TRUE(keyframes[0].feasible);
    EXPECT_NEAR(keyframes[0].achieved_t, 20.0, 0.05);
    EXPECT_EQ(profile.points_.size(), 4u);  // 2 anchors + manual dip + pin
}

TEST(Combinations, ChainAchievedTimesAreCumulative)
{
    // Even when one keyframe is clamped, downstream keyframes solve from the *achieved* upstream time (no
    // impossible debt accumulation).
    EntitySpeedProfile              profile   = MakeFlatProfile(400.0, 10.0);
    std::vector<TrajectoryKeyframe> keyframes = {MakeKeyframe(1.0, 200.0), MakeKeyframe(1.0 + 20.0, 400.0)};

    Solve(profile, keyframes, 400.0);

    EXPECT_FALSE(keyframes[0].feasible);              // 200 m in 1 s is impossible
    double clamped_t = keyframes[0].achieved_t;
    EXPECT_NEAR(clamped_t, 200.0 / 30.0, 0.05);       // clamped to the physical optimum

    // Second segment: 200 m in (21 - clamped_t) s from a fast entry - easily feasible.
    EXPECT_TRUE(keyframes[1].feasible);
    EXPECT_NEAR(keyframes[1].achieved_t, 21.0, 0.05);
    EXPECT_GE(keyframes[1].achieved_t, clamped_t);
}

TEST(Combinations, SolverOutputAlwaysWithinSpeedLimits)
{
    // Property check across a spread of targets: solved profiles never leave [0, v_max] and stay sorted.
    TrajectorySolverLimits lim;
    const double           targets[] = {2.0, 5.0, 8.0, 12.0, 20.0, 40.0, 120.0};

    for (double target : targets)
    {
        EntitySpeedProfile              profile   = MakeFlatProfile(300.0, 10.0);
        std::vector<TrajectoryKeyframe> keyframes = {MakeKeyframe(target, 250.0)};
        Solve(profile, keyframes, 300.0);

        EXPECT_TRUE(ProfileSorted(profile)) << "target=" << target;
        for (const auto& p : profile.points_)
        {
            EXPECT_GE(p.speed, -1e-9) << "target=" << target;
            EXPECT_LE(p.speed, lim.v_max + 1e-9) << "target=" << target;
        }
        // Achieved time is honest: re-integrating the profile reproduces it.
        EXPECT_NEAR(profile.EvaluateTimeAtS(250.0), keyframes[0].achieved_t, 1e-6) << "target=" << target;
    }
}

int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
