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

#include "TrajectoryRenderer.hpp"

#include <osg/BlendColor>
#include <osg/BlendFunc>
#include <osg/Geode>
#include <osg/Geometry>
#include <osg/LineWidth>
#include <osg/MatrixTransform>
#include <osg/PositionAttitudeTransform>
#include <osgDB/ReadFile>

#include "pugixml.hpp"
#include "CommonMini.hpp"
#include "OsgUtil.hpp"

namespace
{
// Fixed default vehicle used to render every trajectory-editor-only ghost marker (see Trajectory_Editing.md
// section 7.2): these vehicles have no xosc CatalogReference of their own to look a real model up from.
const char* kDefaultCatalogRelPath = "xosc/Catalogs/Vehicles/VehicleCatalog.xosc";
const char* kDefaultVehicleEntry   = "car_white";

const osg::Vec4 kPathLineColor      = osg::Vec4(1.0f, 0.55f, 0.0f, 1.0f);  // orange, distinct from the cyan XML-driven trajectory lines
const osg::Vec4 kPreviewLineColor   = osg::Vec4(1.0f, 0.55f, 0.0f, 0.6f);  // same hue, only used for the not-yet-committed last segment
const osg::Vec4 kReachableLineColor = osg::Vec4(0.2f, 1.0f, 0.2f, 0.9f);   // green overlay for the drag-reachable range
const double    kLineZOffset        = 0.2;
const double    kVertexMarkerRadius = 0.6;
const double    kGizmoRadius        = 1.2;  // selected-point "gizmo" marker, deliberately larger/differently shaped than a plain vertex
const double    kKeyframeMarkerSize = 1.6;  // keyframe "diamond" (a box yawed 45 deg reads as a diamond in the top-down view)

osg::ref_ptr<osg::Node> BuildLineStripNode(const std::vector<osg::Vec3>& points, const osg::Vec4& color, float line_width)
{
    osg::ref_ptr<osg::Geode>     geode    = new osg::Geode();
    osg::ref_ptr<osg::Geometry>  geometry = new osg::Geometry();
    osg::ref_ptr<osg::Vec3Array> vertices = new osg::Vec3Array();

    for (const auto& p : points)
        vertices->push_back(p);

    geometry->setVertexArray(vertices.get());

    osg::ref_ptr<osg::Vec4Array> colors = new osg::Vec4Array();
    colors->push_back(color);
    geometry->setColorArray(colors.get());
    geometry->setColorBinding(osg::Geometry::BIND_OVERALL);

    geometry->addPrimitiveSet(new osg::DrawArrays(osg::PrimitiveSet::LINE_STRIP, 0, static_cast<int>(vertices->size())));

    osg::ref_ptr<osg::LineWidth> lw = new osg::LineWidth(line_width);
    geometry->getOrCreateStateSet()->setAttributeAndModes(lw, osg::StateAttribute::ON);
    geometry->getOrCreateStateSet()->setMode(GL_LIGHTING, osg::StateAttribute::OFF);

    geode->addDrawable(geometry.get());
    return geode;
}
}  // namespace

void EntityTrajectoryRenderer::Init(osg::Group* scene_root)
{
    if (root_.valid() || !scene_root)
        return;

    root_ = new osg::Group();
    root_->setName("EntityTrajectoryRenderer");
    scene_root->addChild(root_.get());

    picking_preview_group_ = new osg::Group();
    picking_preview_group_->setName("TrajectoryPickingPreview");
    root_->addChild(picking_preview_group_.get());
}

void EntityTrajectoryRenderer::MarkDirty(const std::string& entity_name)
{
    dirty_flags_[entity_name] = true;
}

void EntityTrajectoryRenderer::RemoveEntity(const std::string& entity_name)
{
    auto it = per_entity_groups_.find(entity_name);
    if (it != per_entity_groups_.end())
    {
        if (root_.valid())
            root_->removeChild(it->second.get());
        per_entity_groups_.erase(it);
    }
    dirty_flags_.erase(entity_name);
}

void EntityTrajectoryRenderer::SetSelectedPoint(const std::string& entity_name, int point_index)
{
    selected_entity_name_  = entity_name;
    selected_point_index_  = point_index;
}

osg::ref_ptr<osg::Node> EntityTrajectoryRenderer::GetOrLoadGhostModel(const std::string& entry_name)
{
    const std::string& key = entry_name.empty() ? std::string(kDefaultVehicleEntry) : entry_name;

    auto found = ghost_models_.find(key);
    if (found != ghost_models_.end())
        return found->second;

    osg::ref_ptr<osg::Node> model = LoadTrajectoryVehicleModel(key);
    if (!model)
    {
        LOG("EntityTrajectoryRenderer: failed to load ghost marker vehicle model (entry [%s]); falling back to a "
            "generic sphere marker",
            key.c_str());
        model = CreateGreenSphereGeometry(1.0, 12, 12);
    }
    ghost_models_[key] = model;
    return model;
}

osg::ref_ptr<osg::Node> EntityTrajectoryRenderer::LoadTrajectoryVehicleModel(const std::string& entry_name) const
{
    std::vector<std::string> catalog_candidates;
    catalog_candidates.push_back(std::string("resources/") + kDefaultCatalogRelPath);
    catalog_candidates.push_back(std::string("../resources/") + kDefaultCatalogRelPath);
    for (const auto& path : SE_Env::Inst().GetPaths())
    {
        catalog_candidates.push_back(path + "/" + kDefaultCatalogRelPath);
        catalog_candidates.push_back(path + "/resources/" + kDefaultCatalogRelPath);
    }

    pugi::xml_document catalog_doc;
    std::string         catalog_dir;
    bool                catalog_loaded = false;
    for (const auto& candidate : catalog_candidates)
    {
        if (catalog_doc.load_file(candidate.c_str()))
        {
            catalog_dir    = DirNameOf(candidate);
            catalog_loaded = true;
            break;
        }
    }

    if (!catalog_loaded)
    {
        LOG("EntityTrajectoryRenderer: could not locate [%s] among the registered search paths", kDefaultCatalogRelPath);
        return nullptr;
    }

    std::string model3d;
    for (pugi::xml_node vehicle : catalog_doc.child("OpenSCENARIO").child("Catalog").children("Vehicle"))
    {
        if (std::string(vehicle.attribute("name").as_string("")) == entry_name)
        {
            model3d = vehicle.attribute("model3d").as_string("");
            break;
        }
    }

    if (model3d.empty())
    {
        LOG("EntityTrajectoryRenderer: entry [%s] not found (or has no model3d) in [%s]", entry_name.c_str(), kDefaultCatalogRelPath);
        return nullptr;
    }

    std::string               model_filename = FileNameOf(model3d);
    std::vector<std::string>  model_candidates;
    model_candidates.push_back(catalog_dir + "/" + model3d);
    for (const auto& path : SE_Env::Inst().GetPaths())
    {
        model_candidates.push_back(path + "/models/" + model_filename);
        model_candidates.push_back(path + "/../models/" + model_filename);
        model_candidates.push_back(path + "/resources/models/" + model_filename);
    }

    for (const auto& candidate : model_candidates)
    {
        if (FileExists(candidate.c_str()))
        {
            osg::ref_ptr<osg::Node> node = osgDB::readNodeFile(candidate);
            if (node.valid())
                return node;
        }
    }

    LOG("EntityTrajectoryRenderer: found catalog entry [%s] but failed to load its model file [%s]", entry_name.c_str(), model3d.c_str());
    return nullptr;
}

void EntityTrajectoryRenderer::RebuildEntityGroup(const std::string& entity_name, const EntityTrajectory& traj, float virtual_time)
{
    osg::ref_ptr<osg::Group>& group = per_entity_groups_[entity_name];
    if (!group.valid())
    {
        group = new osg::Group();
        group->setName("Trajectory_" + entity_name);
        if (root_.valid())
            root_->addChild(group.get());
    }
    group->removeChildren(0, group->getNumChildren());

    if (traj.path_.points_.empty())
        return;

    // Path line (dense, curve-following samples rather than the raw sparse control points).
    EntityPath path_copy = traj.path_;
    path_copy.RebuildDenseSamples(0.5);
    const std::vector<EntityPose>& dense = path_copy.DenseSamples();

    if (dense.size() >= 2)
    {
        std::vector<osg::Vec3> line_points;
        line_points.reserve(dense.size());
        for (const auto& p : dense)
            line_points.emplace_back(static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z + kLineZOffset));
        group->addChild(BuildLineStripNode(line_points, kPathLineColor, 3.0f));

        // Reachable-range overlay while a ghost keyframe drag is active (Trajectory_Editing_Enhancement.md
        // 12.2): a thicker green sub-polyline over [s_lo, s_hi]. Dense samples are 0.5 m apart, so the arc
        // length maps directly onto sample indices.
        if (entity_name == reachable_entity_ && reachable_s_hi_ > reachable_s_lo_)
        {
            int last_idx = static_cast<int>(dense.size()) - 1;
            int idx_lo   = std::max(0, std::min(last_idx, static_cast<int>(std::floor(reachable_s_lo_ / 0.5))));
            int idx_hi   = std::max(0, std::min(last_idx, static_cast<int>(std::ceil(reachable_s_hi_ / 0.5))));
            if (idx_hi > idx_lo)
            {
                std::vector<osg::Vec3> range_points;
                range_points.reserve(static_cast<size_t>(idx_hi - idx_lo) + 1);
                for (int i = idx_lo; i <= idx_hi; i++)
                {
                    const EntityPose& p = dense[static_cast<size_t>(i)];
                    range_points.emplace_back(static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z + kLineZOffset + 0.05));
                }
                group->addChild(BuildLineStripNode(range_points, kReachableLineColor, 6.0f));
            }
        }
    }

    // Vertex markers for the sparse, user-editable control points. The selected point (if any, Trajectory_
    // Editing.md 7.4) is drawn as a bigger, differently-shaped "gizmo" so it is unambiguous which point a
    // subsequent click-drag would move, versus a plain click that only (re)selects a point.
    for (size_t i = 0; i < traj.path_.points_.size(); i++)
    {
        const EntityPose& p            = traj.path_.points_[i];
        bool               is_selected = (entity_name == selected_entity_name_) && (static_cast<int>(i) == selected_point_index_);

        osg::ref_ptr<osg::PositionAttitudeTransform> tx = new osg::PositionAttitudeTransform();
        tx->setPosition(osg::Vec3(static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z + kLineZOffset)));
        if (is_selected)
            tx->addChild(CreateRedCylinderGeometry(kGizmoRadius, kGizmoRadius * 2.0, 12));
        else
            tx->addChild(CreateGreenSphereGeometry(kVertexMarkerRadius, 8, 8));
        group->addChild(tx.get());
    }

    // Keyframe markers (Trajectory_Editing_Enhancement.md 7.4): a 45-degree-yawed blue box ("diamond" from
    // the top-down view) at each keyframe's position on the path. Always a blue diamond - never a red
    // cylinder - so keyframes can not be confused with the selected path point's red gizmo; the clamped
    // (infeasible) state is signalled by the Keyframes table status column and the red reference line in the
    // speed profile chart instead.
    for (const auto& kf : traj.keyframes_)
    {
        EntityPose kf_pose = traj.path_.Evaluate(kf.s);

        osg::ref_ptr<osg::PositionAttitudeTransform> kf_tx = new osg::PositionAttitudeTransform();
        kf_tx->setPosition(osg::Vec3(static_cast<float>(kf_pose.x), static_cast<float>(kf_pose.y), static_cast<float>(kf_pose.z + kLineZOffset)));
        kf_tx->setAttitude(osg::Quat(M_PI / 4.0, osg::Vec3(0.0, 0.0, 1.0)));
        kf_tx->addChild(CreateBlueBoxGeometry(kKeyframeMarkerSize, kKeyframeMarkerSize, kKeyframeMarkerSize));
        group->addChild(kf_tx.get());
    }

    // Ghost marker: the vehicle's position along the path at the current virtual_time (Trajectory_Editing.md 9.3),
    // or the drag override while the user is sliding the ghost along the path (Trajectory_Editing_Enhancement.md 7.2).
    double total_len = traj.path_.GetTotalLength();
    double s;
    if (entity_name == ghost_override_entity_)
        s = ghost_override_s_;
    else
        s = traj.speed_profile_.EvaluateSAtTime(static_cast<double>(virtual_time));
    s                     = std::max(0.0, std::min(s, total_len));
    EntityPose ghost_pose = traj.path_.Evaluate(s);

    ghost_states_[entity_name] = GhostState{s, ghost_pose};

    osg::ref_ptr<osg::Node> ghost_model = GetOrLoadGhostModel(traj.vehicle_catalog_entry_name_);
    if (ghost_model.valid())
    {
        osg::ref_ptr<osg::PositionAttitudeTransform> ghost_tx = new osg::PositionAttitudeTransform();
        ghost_tx->setPosition(osg::Vec3(static_cast<float>(ghost_pose.x), static_cast<float>(ghost_pose.y), static_cast<float>(ghost_pose.z)));
        ghost_tx->setAttitude(osg::Quat(ghost_pose.h, osg::Vec3(0.0, 0.0, 1.0)));
        ghost_tx->addChild(ghost_model.get());

        // While this vehicle is being Ctrl-dragged along its path (keyframe editing), render it semi-
        // transparent (0.7 alpha) as the visual cue for the "ghost editing" state. Constant-alpha blending
        // is applied on the per-entity transform so the shared model node itself is not modified (same
        // technique as the entity models in StudioGui::GetOSGBModelForScenarioObject()).
        if (entity_name == ghost_override_entity_)
        {
            osg::ref_ptr<osg::StateSet>   state_set   = ghost_tx->getOrCreateStateSet();
            osg::ref_ptr<osg::BlendColor> blend_color = new osg::BlendColor(osg::Vec4(1.0f, 1.0f, 1.0f, 0.7f));
            osg::ref_ptr<osg::BlendFunc>  blend_func  = new osg::BlendFunc(osg::BlendFunc::CONSTANT_ALPHA, osg::BlendFunc::ONE_MINUS_CONSTANT_ALPHA);
            state_set->setAttributeAndModes(blend_color.get(), osg::StateAttribute::ON | osg::StateAttribute::OVERRIDE);
            state_set->setAttributeAndModes(blend_func.get(), osg::StateAttribute::ON | osg::StateAttribute::OVERRIDE);
            state_set->setMode(GL_BLEND, osg::StateAttribute::ON);
            state_set->setRenderingHint(osg::StateSet::TRANSPARENT_BIN);
        }

        group->addChild(ghost_tx.get());
    }
}

void EntityTrajectoryRenderer::Update(const StudioDataModel& model, StudioMode /*mode*/, float virtual_time)
{
    if (!root_.valid())
        return;

    // Drop groups for entities that no longer exist (e.g. deleted via the Trajectories tab).
    for (auto it = per_entity_groups_.begin(); it != per_entity_groups_.end();)
    {
        if (model.entity_trajectories_.find(it->first) == model.entity_trajectories_.end())
        {
            root_->removeChild(it->second.get());
            dirty_flags_.erase(it->first);
            ghost_states_.erase(it->first);
            it = per_entity_groups_.erase(it);
        }
        else
        {
            ++it;
        }
    }

    // The ghost marker position depends on virtual_time, so every entity is rebuilt every frame. Path/speed
    // profile editing is expected to involve a modest number of entities and points, so this is cheap enough
    // in practice; MarkDirty()/dirty_flags_ are kept for a future incremental-update optimization.
    for (const auto& entry : model.entity_trajectories_)
    {
        RebuildEntityGroup(entry.first, entry.second, virtual_time);
    }
    dirty_flags_.clear();
}

bool EntityTrajectoryRenderer::GetGhostState(const std::string& entity_name, double* out_s, EntityPose* out_pose) const
{
    auto it = ghost_states_.find(entity_name);
    if (it == ghost_states_.end())
        return false;
    if (out_s)
        *out_s = it->second.s;
    if (out_pose)
        *out_pose = it->second.pose;
    return true;
}

void EntityTrajectoryRenderer::SetGhostOverride(const std::string& entity_name, double s)
{
    ghost_override_entity_ = entity_name;
    ghost_override_s_      = s;
}

void EntityTrajectoryRenderer::ClearGhostOverride()
{
    ghost_override_entity_.clear();
    ghost_override_s_ = 0.0;
}

void EntityTrajectoryRenderer::SetReachableRange(const std::string& entity_name, double s_lo, double s_hi)
{
    reachable_entity_ = entity_name;
    reachable_s_lo_   = s_lo;
    reachable_s_hi_   = s_hi;
}

void EntityTrajectoryRenderer::ClearReachableRange()
{
    reachable_entity_.clear();
    reachable_s_lo_ = 0.0;
    reachable_s_hi_ = 0.0;
}

void EntityTrajectoryRenderer::UpdatePickingPreview(const std::vector<EntityPose>& confirmed_points, double mouse_x, double mouse_y, double mouse_z)
{
    if (!picking_preview_group_.valid())
        return;

    picking_preview_group_->removeChildren(0, picking_preview_group_->getNumChildren());

    if (confirmed_points.empty())
        return;

    // The confirmed points themselves are already drawn by Update()/RebuildEntityGroup() as part of the
    // entity's own interpolated curve (it is committed directly into entity_trajectories_ as the user clicks,
    // see StudioGui::CommitTrajectoryPickingPoint()). Re-drawing a straight polyline over all of them here
    // would duplicate that line in the same color; only the not-yet-committed "last segment" to the mouse
    // belongs to the preview.
    for (const auto& p : confirmed_points)
    {
        osg::ref_ptr<osg::PositionAttitudeTransform> tx = new osg::PositionAttitudeTransform();
        tx->setPosition(osg::Vec3(static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z + kLineZOffset)));
        tx->addChild(CreateGreenSphereGeometry(kVertexMarkerRadius, 8, 8));
        picking_preview_group_->addChild(tx.get());
    }

    const EntityPose&       last = confirmed_points.back();
    std::vector<osg::Vec3> line_points = {
        osg::Vec3(static_cast<float>(last.x),    static_cast<float>(last.y),    static_cast<float>(last.z + kLineZOffset)   ),
        osg::Vec3(static_cast<float>(mouse_x), static_cast<float>(mouse_y), static_cast<float>(mouse_z + kLineZOffset))
    };
    picking_preview_group_->addChild(BuildLineStripNode(line_points, kPreviewLineColor, 2.0f));
}

void EntityTrajectoryRenderer::ClearPickingPreview()
{
    if (picking_preview_group_.valid())
        picking_preview_group_->removeChildren(0, picking_preview_group_->getNumChildren());
}
