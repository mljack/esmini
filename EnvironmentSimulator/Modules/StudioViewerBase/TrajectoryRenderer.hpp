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

#include <osg/Group>
#include <osg/Node>
#include <map>
#include <string>
#include <vector>

#include "StudioDataModel.hpp"

// Independent renderer for trajectory-editor-only vehicles (see Trajectory_Editing.md). Unlike the marker
// drawing in StudioGui (DrawPositionMarkers), this renderer is not tied to viewer::StudioViewer /
// ScenarioPlayer's scene graph ownership: it is mounted once onto the application's root node and is never
// touched by StudioViewer::Cleanup() or by ScenarioPlayer/replayer taking over the scene, so it renders the
// same way in COMPOSER/VIEWER/INSPECTOR modes.
class EntityTrajectoryRenderer
{
public:
    // Mount root_ under scene_root. Safe to call more than once (a no-op after the first successful call).
    void Init(osg::Group* scene_root);

    // Rebuild (only) the entity groups marked dirty, and update the ghost marker positions for the given
    // virtual_time. Call once per frame; mode is currently only used to decide whether editing affordances
    // would apply (path/speed rendering itself is intentionally identical across modes, see
    // Trajectory_Editing.md section 7.2).
    void Update(const StudioDataModel& model, StudioMode mode, float virtual_time);

    // Mark a single entity's visualization as needing to be rebuilt on the next Update() call.
    void MarkDirty(const std::string& entity_name);

    // Drop a per-entity visualization entirely (e.g. after RemoveEntityTrajectory()).
    void RemoveEntity(const std::string& entity_name);

    // Highlight one path point with a distinct "gizmo" marker so a selected-but-not-yet-dragged point is
    // clearly distinguishable from a plain vertex (Trajectory_Editing.md 7.4: click selects, a further
    // click-drag on the gizmo is what actually moves it). Pass an empty entity_name / index -1 to clear.
    void SetSelectedPoint(const std::string& entity_name, int point_index);

    // While a new trajectory is being picked (Trajectory_Editing.md section 6.1), draw the already-confirmed
    // points plus a rubber-band preview line to the current mouse position. Before the first point is
    // confirmed there is no path/line to show yet, so instead the vehicle model itself (vehicle_entry_name,
    // e.g. from EntityTrajectory::vehicle_catalog_entry_name_) is drawn at the mouse's lane-snapped position,
    // oriented along the road direction, so the user can see where/how it will be placed. Call every frame
    // while picking is active; call ClearPickingPreview() once the interaction ends (committed or cancelled).
    void UpdatePickingPreview(const std::vector<EntityPose>& confirmed_points,
                             double                         mouse_x,
                             double                         mouse_y,
                             double                         mouse_z,
                             const std::string&             vehicle_entry_name);
    void ClearPickingPreview();

    // Ghost keyframe editing support (Trajectory_Editing_Enhancement.md section 7): the ghost's current arc
    // length + pose as computed during the last Update(), for hit-testing a click against the ghost vehicle.
    bool GetGhostState(const std::string& entity_name, double* out_s, EntityPose* out_pose) const;

    // While a ghost is being dragged along its path, override its rendered position with the drag's current
    // target arc length instead of the virtual_time-derived one. Pass an empty name to clear.
    void SetGhostOverride(const std::string& entity_name, double s);
    void ClearGhostOverride();

    // While a ghost keyframe drag is active, highlight the drag's reachable [s_lo, s_hi] arc length range
    // with a green overlay on the path line (Trajectory_Editing_Enhancement.md 12.2: the same oracle that
    // pre-clamps the drag). Pass an empty name to clear.
    void SetReachableRange(const std::string& entity_name, double s_lo, double s_hi);
    void ClearReachableRange();

private:
    osg::ref_ptr<osg::Group> root_;
    osg::ref_ptr<osg::Group> picking_preview_group_;

    std::map<std::string, osg::ref_ptr<osg::Group>> per_entity_groups_;
    std::map<std::string, bool>                      dirty_flags_;

    std::string selected_entity_name_;
    int         selected_point_index_ = -1;

    // Ghost states captured during the last Update() (arc length + world pose per entity), used by StudioGui
    // for ghost click hit-testing; plus the optional drag-time position override.
    struct GhostState
    {
        double     s = 0.0;
        EntityPose pose;
    };
    std::map<std::string, GhostState> ghost_states_;
    std::string                       ghost_override_entity_;
    double                            ghost_override_s_ = 0.0;
    std::string                       reachable_entity_;
    double                            reachable_s_lo_ = 0.0;
    double                            reachable_s_hi_ = 0.0;

    // Vehicle models used for ghost markers (Trajectory_Editing.md section 7.2, extended to be per-entity by
    // the Vehicle Type field on Add Trajectory): cached per VehicleCatalog.xosc entry name, so the same model
    // is loaded only once even if several trajectories share an entry. Falls back to a green sphere for any
    // entry that fails to load (missing catalog / model file).
    std::map<std::string, osg::ref_ptr<osg::Node>> ghost_models_;

    osg::ref_ptr<osg::Node> GetOrLoadGhostModel(const std::string& entry_name);
    osg::ref_ptr<osg::Node> LoadTrajectoryVehicleModel(const std::string& entry_name) const;
    void                    RebuildEntityGroup(const std::string& entity_name, const EntityTrajectory& traj, float virtual_time);
};
