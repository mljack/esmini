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

#include "TrajectorySolver.hpp"

#include <algorithm>

namespace
{
const double kSEps = 1e-6;  // arc length comparison epsilon, meters

double EvaluateLinearSpeed(const std::vector<SpeedProfilePoint>& points, double s)
{
    if (points.empty())
        return 0.0;
    if (points.size() == 1 || s <= points.front().s)
        return points.front().speed;
    if (s >= points.back().s)
        return points.back().speed;

    for (size_t i = 0; i + 1 < points.size(); i++)
    {
        if (s >= points[i].s && s <= points[i + 1].s)
        {
            double seg_len = points[i + 1].s - points[i].s;
            double t       = (seg_len > 1e-9) ? (s - points[i].s) / seg_len : 0.0;
            return points[i].speed + (points[i + 1].speed - points[i].speed) * t;
        }
    }
    return points.back().speed;
}

// Fritsch-Carlson monotonic cubic Hermite interpolation: avoids the overshoot a plain cubic spline would
// introduce between speed points, which matters since an interpolated speed must never exceed the two knots
// it lies between.
double EvaluateMonotonicCubicSpeed(const std::vector<SpeedProfilePoint>& points, double s)
{
    size_t n = points.size();
    if (n == 0)
        return 0.0;
    if (n == 1 || s <= points.front().s)
        return points.front().speed;
    if (s >= points.back().s)
        return points.back().speed;

    std::vector<double> d(n - 1);
    for (size_t i = 0; i + 1 < n; i++)
    {
        double dx = points[i + 1].s - points[i].s;
        d[i]      = (dx > 1e-9) ? (points[i + 1].speed - points[i].speed) / dx : 0.0;
    }

    std::vector<double> m(n);
    m[0]     = d[0];
    m[n - 1] = d[n - 2];
    for (size_t i = 1; i + 1 < n; i++)
        m[i] = (d[i - 1] * d[i] <= 0.0) ? 0.0 : (d[i - 1] + d[i]) * 0.5;

    for (size_t i = 0; i + 1 < n; i++)
    {
        if (std::abs(d[i]) < 1e-9)
        {
            m[i]     = 0.0;
            m[i + 1] = 0.0;
            continue;
        }
        double alpha = m[i] / d[i];
        double beta  = m[i + 1] / d[i];
        double sum2  = alpha * alpha + beta * beta;
        if (sum2 > 9.0)
        {
            double tau = 3.0 / std::sqrt(sum2);
            m[i]       = tau * alpha * d[i];
            m[i + 1]   = tau * beta * d[i];
        }
    }

    for (size_t i = 0; i + 1 < n; i++)
    {
        if (s >= points[i].s && s <= points[i + 1].s)
        {
            double h  = points[i + 1].s - points[i].s;
            double t  = (h > 1e-9) ? (s - points[i].s) / h : 0.0;
            double t2 = t * t;
            double t3 = t2 * t;

            double h00 = 2.0 * t3 - 3.0 * t2 + 1.0;
            double h10 = t3 - 2.0 * t2 + t;
            double h01 = -2.0 * t3 + 3.0 * t2;
            double h11 = t3 - t2;

            return h00 * points[i].speed + h10 * h * m[i] + h01 * points[i + 1].speed + h11 * h * m[i + 1];
        }
    }
    return points.back().speed;
}
}  // namespace

double EntitySpeedProfile::EvaluateSpeed(double s) const
{
    if (interp_mode_ == InterpMode::MONOTONIC_CUBIC)
        return EvaluateMonotonicCubicSpeed(points_, s);
    return EvaluateLinearSpeed(points_, s);
}

int EntitySpeedProfile::InsertPoint(double s, double speed)
{
    size_t insert_at = points_.size();
    for (size_t i = 0; i < points_.size(); i++)
    {
        if (points_[i].s >= s)
        {
            insert_at = i;
            break;
        }
    }

    SpeedProfilePoint point;
    point.s     = s;
    point.speed = speed;
    points_.insert(points_.begin() + static_cast<long>(insert_at), point);
    return static_cast<int>(insert_at);
}

void EntitySpeedProfile::RemovePoint(int index)
{
    if (index < 0 || static_cast<size_t>(index) >= points_.size())
        return;
    points_.erase(points_.begin() + index);
}

double EntitySpeedProfile::EvaluateTimeAtS(double s) const
{
    if (points_.empty())
        return 0.0;

    double total_s = std::max(0.0, std::min(s, points_.back().s));

    // Numerically integrate 1 / v(s') from 0 to total_s over a fine grid, clamping v to a small epsilon so
    // stationary sections of the profile do not make the integral diverge.
    const double MIN_SPEED = 0.1;  // m/s
    const int    STEPS     = 200;
    double       step      = total_s / STEPS;
    if (step <= 1e-9)
        return 0.0;

    double time       = 0.0;
    double prev_inv_v = 1.0 / std::max(EvaluateSpeed(0.0), MIN_SPEED);
    for (int i = 1; i <= STEPS; i++)
    {
        double s_i   = step * i;
        double inv_v = 1.0 / std::max(EvaluateSpeed(s_i), MIN_SPEED);
        time += 0.5 * (prev_inv_v + inv_v) * step;
        prev_inv_v = inv_v;
    }
    return time;
}

double EntitySpeedProfile::EvaluateSAtTime(double t) const
{
    if (points_.empty() || t <= 0.0)
        return 0.0;

    double total_s = points_.back().s;
    if (total_s <= 1e-9)
        return 0.0;

    double total_time = EvaluateTimeAtS(total_s);
    if (t >= total_time)
        return total_s;

    // Binary search on EvaluateTimeAtS(), which is monotonically increasing in s.
    double lo = 0.0, hi = total_s;
    for (int iter = 0; iter < 30; iter++)
    {
        double mid      = 0.5 * (lo + hi);
        double time_mid = EvaluateTimeAtS(mid);
        if (time_mid < t)
            lo = mid;
        else
            hi = mid;
    }
    return 0.5 * (lo + hi);
}

// ---------------------------------------------------------------------------------------------------------------
// Reachability oracle
// ---------------------------------------------------------------------------------------------------------------

namespace
{
// Travel time across distance d with v(s) linear from v1 to v2: t = d * ln(v2/v1) / (v2 - v1).
double LinearRampTime(double v1, double v2, double d, const TrajectorySolverLimits& lim)
{
    if (d <= 1e-9)
        return 0.0;
    v1 = std::max(v1, lim.v_min);
    v2 = std::max(v2, lim.v_min);
    if (std::fabs(v2 - v1) < 1e-9)
        return d / v1;
    return d * std::log(v2 / v1) / (v2 - v1);
}

// Minimal distance needed to ramp v1 -> v2 with v(s) linear in s under the direction-appropriate acceleration
// limit. For linear-in-s speed, a(s) = v * dv/ds peaks at the fast end: |a|max = |slope| * max(v1, v2).
double MinRampDistance(double v1, double v2, const TrajectorySolverLimits& lim)
{
    double dv = std::fabs(v2 - v1);
    if (dv < 1e-9)
        return 0.0;
    double a_limit = (v2 > v1) ? lim.a_acc : lim.a_dec;
    return dv * std::max(std::max(v1, v2), lim.v_min) / a_limit;
}

// Check the acceleration limits on every profile piece that intersects (s_lo, s_hi) - only the pieces a
// cascade step actually touched (finalized decision: no whole-table scan).
bool SegmentAccelOk(const std::vector<SpeedProfilePoint>& pts, double s_lo, double s_hi, const TrajectorySolverLimits& lim)
{
    for (size_t i = 0; i + 1 < pts.size(); i++)
    {
        double s1 = pts[i].s, s2 = pts[i + 1].s;
        if (s2 <= s_lo + kSEps || s1 >= s_hi - kSEps)
            continue;  // piece entirely outside the touched range

        double ds = s2 - s1;
        double v1 = pts[i].speed, v2 = pts[i + 1].speed;
        if (ds < 1e-9)
        {
            if (std::fabs(v2 - v1) > 1e-6)
                return false;  // vertical jump
            continue;
        }
        double slope   = (v2 - v1) / ds;
        double a_peak  = std::fabs(slope) * std::max(std::max(v1, v2), lim.v_min);
        double a_limit = (v2 > v1) ? lim.a_acc : lim.a_dec;
        if (a_peak > a_limit * 1.001 + 1e-9)
            return false;
    }
    return true;
}
}  // namespace

void SegmentTimeWindow(double v_entry, double length, const TrajectorySolverLimits& lim, double* out_t_min, double* out_t_max)
{
    v_entry = std::min(lim.v_max, std::max(0.0, v_entry));

    if (length <= kSEps)
    {
        if (out_t_min)
            *out_t_min = 0.0;
        if (out_t_max)
            *out_t_max = 0.0;
        return;
    }

    // Fastest: accelerate towards the highest plateau whose entry ramp still fits, then hold (free terminal,
    // no closing ramp). The largest plateau v_p with (v_p - v_entry) * v_p / a_acc <= length solves the
    // quadratic v_p^2 - v_entry*v_p - a_acc*length = 0.
    {
        double v_fit = 0.5 * (v_entry + std::sqrt(v_entry * v_entry + 4.0 * lim.a_acc * length));
        double v_p   = std::min(lim.v_max, std::max(v_entry, v_fit));
        double d1    = std::min(length, MinRampDistance(v_entry, v_p, lim));
        double t     = LinearRampTime(v_entry, v_p, d1, lim) + (length - d1) / std::max(v_p, lim.v_min);
        if (out_t_min)
            *out_t_min = t;
    }

    // Slowest: decelerate towards v_min as fast as allowed, then crawl. If even the full-length ramp cannot
    // reach v_min, the slowest profile is the pure ramp to whatever speed the length allows.
    {
        double v_reachable = v_entry - lim.a_dec * length / std::max(v_entry, lim.v_min);
        double v_p         = std::max(lim.v_min, std::min(v_entry, v_reachable));
        double d1          = std::min(length, MinRampDistance(v_entry, v_p, lim));
        double t           = LinearRampTime(v_entry, v_p, d1, lim) + (length - d1) / std::max(v_p, lim.v_min);
        if (out_t_max)
            *out_t_max = t;
    }
}

double MaxReachableDistance(double v_entry, double dt, const TrajectorySolverLimits& lim)
{
    if (dt <= 0.0)
        return 0.0;

    double lo = 0.0;
    double hi = std::max(1.0, lim.v_max * dt);  // upper bound: full time at the ceiling speed

    for (int i = 0; i < 48; i++)
    {
        double mid   = 0.5 * (lo + hi);
        double t_min = 0.0;
        SegmentTimeWindow(v_entry, mid, lim, &t_min, nullptr);
        if (t_min <= dt)
            lo = mid;
        else
            hi = mid;
    }
    return lo;
}

// ---------------------------------------------------------------------------------------------------------------
// Keyframe chain solver
// ---------------------------------------------------------------------------------------------------------------

namespace
{
int FindNodeIndexAt(const EntitySpeedProfile& profile, double s)
{
    for (size_t i = 0; i < profile.points_.size(); i++)
        if (std::fabs(profile.points_[i].s - s) < kSEps)
            return static_cast<int>(i);
    return -1;
}

// Pin-node invariant maintenance (Trajectory_Editing_Enhancement.md 12.3): each keyframe owns exactly one
// node at its s. When a keyframe moved since the last solve, its old node is relocated (not duplicated);
// the s=0 / s=end anchors and nodes shared with other keyframes are never removed.
void MaintainPinNodes(EntitySpeedProfile& profile, std::vector<TrajectoryKeyframe>& keyframes, double total_length)
{
    for (auto& kf : keyframes)
    {
        if (!std::isnan(kf.pin_s) && std::fabs(kf.pin_s - kf.s) > kSEps)
        {
            bool anchored = kf.pin_s < kSEps || kf.pin_s > total_length - kSEps;
            bool shared   = false;
            for (const auto& other : keyframes)
                if (&other != &kf && (std::fabs(other.s - kf.pin_s) < kSEps || (!std::isnan(other.pin_s) && std::fabs(other.pin_s - kf.pin_s) < kSEps)))
                    shared = true;

            int old_idx = FindNodeIndexAt(profile, kf.pin_s);
            if (old_idx >= 0 && !anchored && !shared)
                profile.RemovePoint(old_idx);
        }

        if (FindNodeIndexAt(profile, kf.s) < 0)
            profile.InsertPoint(kf.s, profile.EvaluateSpeed(kf.s));

        kf.pin_s = kf.s;
    }
}

// One inter-keyframe segment: Q0 precheck -> Q1 adjust-existing -> gate -> Q2 construct -> Q3 clamp
// (Trajectory_Editing_Enhancement.md 12.4). Returns the achieved travel time across [s_a, s_b].
double SolveSegment(EntitySpeedProfile& profile, double s_a, double s_b, double target_dt, const TrajectorySolverLimits& lim, bool* out_feasible)
{
    *out_feasible = true;

    auto segment_time = [&]() { return profile.EvaluateTimeAtS(s_b) - profile.EvaluateTimeAtS(s_a); };

    double length = s_b - s_a;
    if (length <= kSEps || target_dt <= kSEps)
    {
        *out_feasible = (length <= kSEps && target_dt <= kSEps);
        return segment_time();
    }

    double v_entry = std::min(lim.v_max, std::max(0.0, profile.EvaluateSpeed(s_a)));

    // --- Q0: physical reachability window (free interior shape + free terminal speed).
    double t_min = 0.0, t_max = 0.0;
    SegmentTimeWindow(v_entry, length, lim, &t_min, &t_max);
    double solve_dt = std::min(t_max, std::max(t_min, target_dt));
    double r_phys   = std::fabs(target_dt - solve_dt);
    if (r_phys > lim.solve_tolerance)
        *out_feasible = false;

    std::vector<SpeedProfilePoint> snapshot = profile.points_;

    // --- Q1: adjust existing nodes only. Free set = nodes strictly inside (s_a, s_b] plus, for the first
    // segment, the start anchor at s=0 (v(0) is adjustable by finalized decision). Two saturating 1-DOF
    // families are tried and the best valid result wins; "leave everything unchanged" is always a valid
    // fallback candidate, so r_Q1 is the true best achievable without inserting points.
    std::vector<size_t> free_set;
    for (size_t i = 0; i < snapshot.size(); i++)
    {
        double s        = snapshot[i].s;
        bool   in_range = (s_a <= kSEps) ? (s <= s_b + kSEps) : (s > s_a + kSEps && s <= s_b + kSEps);
        if (in_range)
            free_set.push_back(i);
    }

    std::vector<SpeedProfilePoint> best_q1 = snapshot;
    double                         r_q1    = std::fabs(segment_time() - target_dt);

    if (!free_set.empty())
    {
        auto try_family = [&](bool multiplicative) {
            double max_v = 0.0;
            for (size_t idx : free_set)
                max_v = std::max(max_v, snapshot[idx].speed);
            if (multiplicative && max_v < 1e-6)
                return;  // nothing to scale

            auto apply = [&](double lambda) {
                profile.points_ = snapshot;
                for (size_t idx : free_set)
                {
                    double v = snapshot[idx].speed;
                    v        = multiplicative ? v * lambda : v + lambda;
                    profile.points_[idx].speed = std::min(lim.v_max, std::max(0.0, v));
                }
                return segment_time();
            };

            double lo = multiplicative ? 0.02 : -lim.v_max;  // slow end
            double hi = multiplicative ? (lim.v_max / std::max(max_v, 1e-6)) : lim.v_max;  // fast end

            double t_slow = apply(lo);
            double t_fast = apply(hi);

            double lambda;
            if (solve_dt >= t_slow)
                lambda = lo;  // saturated: as slow as this family can go
            else if (solve_dt <= t_fast)
                lambda = hi;  // saturated: as fast as this family can go
            else
            {
                double a = lo, b = hi;  // invariant: time(a) >= solve_dt >= time(b)
                for (int i = 0; i < 48; i++)
                {
                    double mid = 0.5 * (a + b);
                    if (apply(mid) > solve_dt)
                        a = mid;
                    else
                        b = mid;
                }
                lambda = 0.5 * (a + b);
            }

            double t_final = apply(lambda);
            if (SegmentAccelOk(profile.points_, s_a, s_b + kSEps, lim))
            {
                double r = std::fabs(t_final - target_dt);
                if (r < r_q1)
                {
                    r_q1    = r;
                    best_q1 = profile.points_;
                }
            }
        };

        try_family(true);   // Q1a multiplicative (shape preserving)
        try_family(false);  // Q1b saturating additive (works from zero speeds)
    }

    profile.points_ = best_q1;

    // --- Insertion-gain gate (12.4): stop here when Q1 is exact enough, or already as close as physics
    // allows (inserting points cannot beat the physical window).
    if (r_q1 <= lim.solve_tolerance || (r_q1 - r_phys) <= lim.insertion_gain)
        return segment_time();

    // --- Q2: free-terminal ramp+plateau construction; at most 1 interior shaping point. The terminal (pin)
    // node takes the plateau speed - the "carry momentum forward" greedy heuristic.
    int pin_idx = FindNodeIndexAt(profile, s_b);
    if (pin_idx < 0)
        return segment_time();  // defensive: pin invariant should have ensured it

    auto apply_q2 = [&](double v_p) {
        profile.points_ = snapshot;

        // Drop interior nodes strictly inside (s_a, s_b); Q1 already proved they cannot satisfy the target.
        profile.points_.erase(std::remove_if(profile.points_.begin(),
                                             profile.points_.end(),
                                             [&](const SpeedProfilePoint& p) { return p.s > s_a + kSEps && p.s < s_b - kSEps; }),
                              profile.points_.end());

        int pi = FindNodeIndexAt(profile, s_b);
        if (pi >= 0)
            profile.points_[static_cast<size_t>(pi)].speed = v_p;

        double d1 = std::min(length, MinRampDistance(v_entry, v_p, lim));
        if (d1 > kSEps && d1 < length - kSEps)
            profile.InsertPoint(s_a + d1, v_p);

        return segment_time();
    };

    // Feasible plateau range (same closed forms as the oracle).
    double v_hi = std::min(lim.v_max, std::max(v_entry, 0.5 * (v_entry + std::sqrt(v_entry * v_entry + 4.0 * lim.a_acc * length))));
    double v_lo = std::max(lim.v_min, std::min(v_entry, v_entry - lim.a_dec * length / std::max(v_entry, lim.v_min)));

    double a = v_lo, b = v_hi;  // time decreasing in v_p; invariant time(a) >= solve_dt >= time(b)
    for (int i = 0; i < 48; i++)
    {
        double mid = 0.5 * (a + b);
        if (apply_q2(mid) > solve_dt)
            a = mid;
        else
            b = mid;
    }
    double v_p = 0.5 * (a + b);
    apply_q2(v_p);

    // --- 5s spacing merge (12.5): if the single shaping point sits closer than the minimum travel-time
    // spacing to either segment boundary, try dropping it; keep the merge only if the acceleration limits
    // still hold (the accel limit is hard, the spacing rule is soft).
    {
        int ramp_idx = -1;
        for (size_t i = 0; i < profile.points_.size(); i++)
            if (profile.points_[i].s > s_a + kSEps && profile.points_[i].s < s_b - kSEps)
                ramp_idx = static_cast<int>(i);

        if (ramp_idx >= 0)
        {
            double ramp_s  = profile.points_[static_cast<size_t>(ramp_idx)].s;
            double t_left  = profile.EvaluateTimeAtS(ramp_s) - profile.EvaluateTimeAtS(s_a);
            double t_right = profile.EvaluateTimeAtS(s_b) - profile.EvaluateTimeAtS(ramp_s);
            if (t_left < lim.min_auto_point_spacing_time || t_right < lim.min_auto_point_spacing_time)
            {
                std::vector<SpeedProfilePoint> with_ramp = profile.points_;
                profile.RemovePoint(ramp_idx);
                if (!SegmentAccelOk(profile.points_, s_a, s_b + kSEps, lim))
                    profile.points_ = with_ramp;
            }
        }
    }

    double r_q2 = std::fabs(segment_time() - target_dt);

    // --- Adoption test (12.4): the insertion must effectively reduce the residual, otherwise revert to Q1.
    if (r_q2 < r_q1 - lim.insertion_gain)
        return segment_time();

    profile.points_ = best_q1;
    return segment_time();
}
}  // namespace

void SolveKeyframeChain(EntitySpeedProfile& profile, std::vector<TrajectoryKeyframe>& keyframes, double total_length, const TrajectorySolverLimits& limits)
{
    if (keyframes.empty() || profile.points_.empty())
        return;

    for (auto& kf : keyframes)
        kf.s = std::min(total_length, std::max(0.0, kf.s));

    std::sort(keyframes.begin(), keyframes.end(), [](const TrajectoryKeyframe& a, const TrajectoryKeyframe& b) { return a.t < b.t; });

    MaintainPinNodes(profile, keyframes, total_length);

    double prev_t = 0.0;
    double prev_s = 0.0;
    for (auto& kf : keyframes)
    {
        bool   feasible    = true;
        double achieved_dt = SolveSegment(profile, prev_s, kf.s, kf.t - prev_t, limits, &feasible);
        kf.achieved_t      = prev_t + achieved_dt;
        kf.feasible        = feasible && std::fabs(kf.achieved_t - kf.t) < limits.solve_tolerance;
        prev_t             = kf.achieved_t;
        prev_s             = kf.s;
    }
}

bool RemoveKeyframe(EntitySpeedProfile&              profile,
                    std::vector<TrajectoryKeyframe>& keyframes,
                    size_t                           index,
                    double                           total_length,
                    const TrajectorySolverLimits&    limits)
{
    if (index >= keyframes.size())
        return false;

    const TrajectoryKeyframe& kf       = keyframes[index];
    double                    pin      = std::isnan(kf.pin_s) ? kf.s : kf.pin_s;
    bool                      anchored = pin < kSEps || pin > total_length - kSEps;
    bool                      shared   = false;
    for (size_t i = 0; i < keyframes.size(); i++)
        if (i != index && std::fabs(keyframes[i].s - pin) < kSEps)
            shared = true;

    if (!anchored && !shared)
    {
        int idx = FindNodeIndexAt(profile, pin);
        if (idx >= 0)
            profile.RemovePoint(idx);
    }

    keyframes.erase(keyframes.begin() + static_cast<long>(index));
    SolveKeyframeChain(profile, keyframes, total_length, limits);
    return true;
}
