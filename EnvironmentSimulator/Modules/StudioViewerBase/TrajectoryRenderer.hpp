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
    // points plus a rubber-band preview line to the current mouse position. Call every frame while picking is
    // active; call ClearPickingPreview() once the interaction ends (committed or cancelled).
    void UpdatePickingPreview(const std::vector<EntityPose>& confirmed_points, double mouse_x, double mouse_y, double mouse_z);
    void ClearPickingPreview();

private:
    osg::ref_ptr<osg::Group> root_;
    osg::ref_ptr<osg::Group> picking_preview_group_;

    std::map<std::string, osg::ref_ptr<osg::Group>> per_entity_groups_;
    std::map<std::string, bool>                      dirty_flags_;

    std::string selected_entity_name_;
    int         selected_point_index_ = -1;

    // Shared default vehicle model used for every ghost marker (Trajectory_Editing.md section 7.2): trajectory-
    // editor-only vehicles have no xosc CatalogReference, so a single fixed VehicleCatalog.xosc entry is used
    // for all of them instead of a per-entity 3D model lookup.
    osg::ref_ptr<osg::Node> ghost_model_;
    bool                    ghost_model_load_attempted_ = false;

    osg::ref_ptr<osg::Node> GetOrLoadGhostModel();
    osg::ref_ptr<osg::Node> LoadDefaultTrajectoryVehicleModel() const;
    void                    RebuildEntityGroup(const std::string& entity_name, const EntityTrajectory& traj, float virtual_time);
};
