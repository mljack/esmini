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

#include "pugixml.hpp"
#include <set>
#include <vector>
#include <deque>
#include <map>
#include <string>
#include <sstream>
#include <memory>
#include <functional>

#include "OSCBoundingBox.hpp"
#include "XmlUtil.hpp"
#include "Parameters.hpp"
#include "TrajectorySolver.hpp"

enum class StudioMode
{
    UNKNOWN,
    COMPOSER,
    VIEWER,
    INSPECTOR,
    ZOMBIE,
};

std::string GetConfigFilePath();
bool        LoadConfigValue(const std::string& key, int& value);
bool        LoadConfigValue(const std::string& key, float& value);
bool        LoadConfigValue(const std::string& key, std::string& value);

// ---------------------------------------------------------------------------------------------------------------
// Trajectory editing data model (see Trajectory_Editing.md).
//
// EntityTrajectory (path_ + speed_profile_) is stored independently from xml_doc_ / the OpenSCENARIO document,
// persisted to a sidecar ".traj.json" file, and has no effect whatsoever on scenario simulation. It is only used
// by the editor for its own visualization / editing / preview purposes.
// ---------------------------------------------------------------------------------------------------------------

// A single path sample point. Keeps both WorldPos and LanePos representations in sync via SyncFromWorld() /
// SyncFromLane(), which reuse the existing conversion helpers in PosUtil.hpp.
struct EntityPose
{
    // WorldPos
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double h = 0.0;

    // LanePos
    int    road_id     = 0;
    int    lane_id     = 0;
    double s           = 0.0;
    double lane_offset = 0.0;
    double relative_h  = 0.0;  // heading relative to the lane driving direction

    // Which representation was most recently edited by the user. Used to avoid unnecessary round-trip
    // conversions (and the small precision drift / lane ambiguity that comes with them) when only one side
    // of the pose is actually being edited.
    enum class SourceRepr
    {
        WORLD,
        LANE
    };
    SourceRepr source = SourceRepr::WORLD;

    // Recompute road_id/lane_id/s/lane_offset/relative_h from x/y/z/h.
    void SyncFromWorld(bool align_to_lane = true);

    // Recompute x/y/z/h from road_id/lane_id/s/lane_offset/relative_h.
    void SyncFromLane();
};

// A path made up of user-editable control points (points_). Dense, fixed arc-length samples used for rendering
// and for the speed profile's distance parameterization can be (re)generated on demand via RebuildDenseSamples().
class EntityPath
{
public:
    enum class InterpMode
    {
        LINEAR,
        CATMULL_ROM,  // "Spline" in the UI
        CLOTHOID
    };

    std::vector<EntityPose> points_;
    InterpMode               interp_mode_ = InterpMode::LINEAR;

    // Total arc length of the (interpolated) path.
    double GetTotalLength() const;

    // Evaluate the path at arc length s (clamped to [0, GetTotalLength()]).
    EntityPose Evaluate(double s) const;

    // Find the point in points_ (not the dense samples) closest to (x, y) in the XY plane.
    // Returns -1 if points_ is empty.
    int FindNearestPointIndex(double x, double y) const;

    // Project (x, y) onto the closest point of the control-point polyline (the same straight-line segments
    // between consecutive points_ that InsertPoint()'s own arc-length approximation uses, not the dense
    // interpolated curve), used to offer "Insert Point" at a sensible spot near an existing path. Returns
    // false if points_ has fewer than 2 points (nothing to project onto). out_dist_sqr is the squared XY
    // distance from (x, y) to the projected point, letting the caller apply its own proximity threshold.
    bool FindNearestPositionOnPath(double x, double y, double* out_s, double* out_x, double* out_y, double* out_z, double* out_dist_sqr) const;

    // Insert a new control point so that points_ stays ordered by (approximate, polyline-based) arc length.
    // Returns the index the point was inserted at.
    int InsertPoint(double s, const EntityPose& pose);

    // Remove the control point at index (no-op if index is out of range).
    void RemovePoint(int index);

    // (Re)generate dense_samples_ at a fixed arc length interval ds, following interp_mode_.
    void RebuildDenseSamples(double ds = 0.5);

    const std::vector<EntityPose>& DenseSamples() const
    {
        return dense_samples_;
    }

private:
    std::vector<EntityPose> dense_samples_;  // cache for rendering; not persisted to JSON

    // Build a fine-grained polyline approximation of the interpolated curve plus its cumulative arc length,
    // shared by GetTotalLength()/Evaluate()/RebuildDenseSamples().
    void BuildFineSamples(std::vector<EntityPose>* out_poses, std::vector<double>* out_cumulative_s) const;
};

// SpeedProfilePoint / EntitySpeedProfile / TrajectoryKeyframe and the keyframe solver live in
// TrajectorySolver.hpp - a self-contained, UI/OSG-free module so the solver is unit-testable headlessly
// (Trajectory_Editing_Enhancement.md section 12).

// Binds together the path and speed profile of a single trajectory-editor-only vehicle. Such vehicles are
// created via the "Add Trajectory" interaction (Trajectory_Editing.md section 6) and never correspond to any
// xosc ScenarioObject.
class EntityTrajectory
{
public:
    std::string        entity_name_;  // only exists in .traj.json, never in xml_doc_
    EntityPath          path_;
    EntitySpeedProfile  speed_profile_;

    // VehicleCatalog.xosc entry name used for this trajectory's ghost marker model (e.g. "car_white"); empty
    // means "use the renderer's fixed default" (Trajectory_Editing.md section 7.2's original single-model
    // behavior, kept as the fallback for old .traj.json files and for robustness if the entry disappears).
    std::string vehicle_catalog_entry_name_;

    // Time keyframes sorted by t (Trajectory_Editing_Enhancement.md): user-specified arrival-time constraints
    // that ResolveKeyframes() turns into speed profile modifications via the Q0-Q3 cascade.
    std::vector<TrajectoryKeyframe> keyframes_;

    bool HasPath() const
    {
        return !path_.points_.empty();
    }

    // The first and last speed_profile_ points act as fixed "anchors" whose s is not user-editable: the first
    // is always s=0, the last always tracks the path's current total length. Call after any edit that can
    // change the path's length (new/moved point) to keep this invariant true; EvaluateSpeed()/EvaluateTimeAtS()
    // assume the profile spans exactly [0, path_.GetTotalLength()].
    void SyncSpeedProfileEndpoints()
    {
        if (speed_profile_.points_.empty())
            return;
        speed_profile_.points_.front().s = 0.0;
        if (speed_profile_.points_.size() >= 2)
            speed_profile_.points_.back().s = path_.GetTotalLength();
    }

    // Re-solve all keyframe constraints against the current path/speed profile via the v2 cascade
    // (SolveKeyframeChain(), Trajectory_Editing_Enhancement.md section 12): pin-node invariant, Q0 physical
    // window precheck, Q1 adjust-existing-nodes-only, insertion-gain gate, Q2 free-terminal construction with
    // 5s spacing merge, Q3 clamp + residual.
    void ResolveKeyframes()
    {
        SyncSpeedProfileEndpoints();
        SolveKeyframeChain(speed_profile_, keyframes_, path_.GetTotalLength());
    }

    // Remove keyframes_[index] together with its pin node, then re-solve the remaining chain.
    void RemoveKeyframe(int index)
    {
        if (index < 0)
            return;
        SyncSpeedProfileEndpoints();
        ::RemoveKeyframe(speed_profile_, keyframes_, static_cast<size_t>(index), path_.GetTotalLength());
    }
};

// Derive the ".traj.json" sidecar path from a ".xosc" path, e.g. "scenario.xosc" -> "scenario.traj.json".
std::string DeriveTrajJsonPath(const std::string& xosc_path);

// An opaque (from StudioDataModel's point of view) snapshot of which trajectory path/speed-profile point is
// currently selected in the UI (Trajectory_Editing.md section 11.3). StudioDataModel only stores/returns these
// alongside each undo/redo entry - it never reads or interprets them; StudioGui is the one that knows what
// "selected" means and packs/unpacks its own trajectory_point_selected_ etc. members into/from this struct.
struct TrajectorySelectionSnapshot
{
    bool        path_point_selected  = false;
    std::string path_point_entity;
    int         path_point_index     = -1;
    bool        speed_point_selected = false;
    std::string speed_point_entity;
    int         speed_point_index    = -1;
};

class StudioDataModel
{
public:
    // Default window configuration constants
    static constexpr int   DEFAULT_XML_PANEL_WIDTH  = 800;
    static constexpr int   DEFAULT_MENU_BAR_HEIGHT  = 18;
    static constexpr int   DEFAULT_TIME_BAR_HEIGHT  = 36;
    static constexpr float DEFAULT_LOG_HEIGHT_RATIO = 0.25f;
    static constexpr int   DEFAULT_VIEWPORT_WIDTH   = 1920;
    static constexpr int   DEFAULT_VIEWPORT_HEIGHT  = 1080;

    StudioDataModel();
    ~StudioDataModel()
    {
    }

    void SetModified();

    void Clear();
    bool LoadXoscXml(const std::string& path);
    bool SaveXoscXml(const std::string& path);

    // Trajectory editing (Trajectory_Editing.md): entity_trajectories_ is independent from xml_doc_, persisted to
    // its own ".traj.json" sidecar file. Not wired into LoadXoscXml/SaveXoscXml - it is saved/loaded through its
    // own dedicated "Save Trajectories" entry point.
    bool              LoadTrajJson(const std::string& path);
    bool              SaveTrajJson(const std::string& path);
    EntityTrajectory& CreateEntityTrajectory(const std::string& entity_name);
    void              RemoveEntityTrajectory(const std::string& entity_name);

    void LoadConfig();
    void SaveConfig();
    void ResetWindowSizes();

    void ExtractPositionsRecursive(pugi::xml_node node, const std::string& context, std::vector<PositionInfo>* infos);

    std::string EvaluateString(const std::string& str);

    pugi::xml_node CreateNode(pugi::xml_node parent, const std::string& node_name, pugi::xml_node node_to_plug);
    void           DeleteNode(pugi::xml_node node);

    void           AddAttribute(pugi::xml_node node, const std::string& attr_name, const std::string& attr_value);
    void           UpdateAttribute(pugi::xml_node node, const std::string& attr_name, const std::string& attr_value);
    void           DeleteAttribute(pugi::xml_node node, const std::string& attr_name);
    bool           MoveNodeUp(pugi::xml_node node);
    bool           MoveNodeDown(pugi::xml_node node);
    pugi::xml_node DuplicateNode(pugi::xml_node node);

    bool        DeleteEntity(const std::string& entity_name);
    std::string CloneEntity(const std::string& entity_name);
    bool        RenameEntity(const std::string& old_name, const std::string& new_name);
    std::string AddVehicle(const std::string& name, const std::string& catalog_name, const std::string& entry_name, double init_speed);

    void        AssignDefaultName(pugi::xml_node node);
    bool        NameExists(const std::string& name, const pugi::xml_node exclude_node) const;
    std::string GenerateUniqueName(const std::string& base, const pugi::xml_node exclude_node) const;
    std::string EnsureUniqueName(const std::string& desired, const pugi::xml_node exclude_node) const;

    std::string GetDefaultNodeName(const pugi::xml_node node) const;
    void        SetNodeDefaultName(pugi::xml_node node);

    void UpdateEntityRefs(const std::string& old_name, const std::string& new_name);

    bool                     IsScenarioObjectNode(const pugi::xml_node node) const;
    std::vector<std::string> GetScenarioObjectNames() const;

    bool IsNodeInMainDocument(const pugi::xml_node node) const;

    pugi::xml_node RootNode()
    {
        return xml_doc_.child("OpenSCENARIO");
    }

    pugi::xml_node RootNode() const
    {
        return xml_doc_.child("OpenSCENARIO");
    }

    size_t GetDocumentHash() const
    {
        std::ostringstream oss;
        xml_doc_.save(oss);
        return std::hash<std::string>()(oss.str());
    }

    void PopulateRequiredStructure(pugi::xml_node node, int depth, pugi::xml_node node_to_plug);

    void Undo();
    void Redo();
    bool CanUndo() const
    {
        return !undo_stack_.empty();
    }
    bool CanRedo() const
    {
        return !redo_stack_.empty();
    }
    void ClearUndoRedoStacks();

    // Independent undo/redo for entity_trajectories_ (Trajectory_Editing.md section 11): full-JSON-snapshot
    // based rather than the xml_doc_ diff mechanism above, since the trajectory data volume is small. Routed
    // to/from the same Ctrl+Z/Ctrl+Shift+Z shortcuts as the xml Undo()/Redo() via the Last*Seq() accessors
    // below (see StudioGui::Undo()/Redo()), so pressing Ctrl+Z always undoes whichever kind of edit happened
    // most recently, whether it touched xml_doc_ or entity_trajectories_.
    void                        PushTrajectoryUndoState(const TrajectorySelectionSnapshot& selection_after_edit);
    TrajectorySelectionSnapshot UndoTrajectories();
    TrajectorySelectionSnapshot RedoTrajectories();
    bool                        CanUndoTrajectories() const
    {
        return !trajectory_undo_stack_.empty();
    }
    bool CanRedoTrajectories() const
    {
        return !trajectory_redo_stack_.empty();
    }
    void ClearTrajectoryUndoRedoStacks();

    // Sequence numbers of the most recent push on each stack, used purely to decide which of the two
    // independent undo systems is "more recent" when routing Ctrl+Z/Ctrl+Shift+Z (0 if the stack is empty).
    size_t LastXmlUndoSeq() const
    {
        return undo_seq_stack_.empty() ? 0 : undo_seq_stack_.back();
    }
    size_t LastXmlRedoSeq() const
    {
        return redo_seq_stack_.empty() ? 0 : redo_seq_stack_.back();
    }
    size_t LastTrajectoryUndoSeq() const
    {
        return trajectory_undo_seq_stack_.empty() ? 0 : trajectory_undo_seq_stack_.back();
    }
    size_t LastTrajectoryRedoSeq() const
    {
        return trajectory_redo_seq_stack_.empty() ? 0 : trajectory_redo_seq_stack_.back();
    }

    struct ValidationError
    {
        std::string         message;
        pugi::xml_node      node;
        pugi::xml_attribute attr;
        std::string         attr_name;
        bool                can_be_fixed = false;
    };

    std::set<pugi::xml_node>      invalid_nodes_;
    std::set<pugi::xml_attribute> invalid_attrs_;
    std::vector<ValidationError>  validation_errors_;
    void                          ValidateScenario();

    StudioMode mode_;
    StudioMode prev_mode_ = StudioMode::UNKNOWN;
    float      virtual_time_;
    float      virtual_time_max_value_;
    bool       virtual_time_manipulated_ = false;
    bool       modified_                 = false;

    std::string xosc_path_;
    std::string tmp_xosc_path_ = "temp.xosc";

    std::string        tmp_rec_path_ = "temp.rec";
    pugi::xml_document xml_doc_;
    pugi::xml_document xml_schema_;
    bool               markers_to_update_ = false;  // Track if markers have been updated

    // Trajectory editing data (see EntityTrajectory above); independent from xml_doc_ and saved separately.
    std::map<std::string, EntityTrajectory> entity_trajectories_;
    std::string                             traj_json_path_;               // derived from xosc_path_, see DeriveTrajJsonPath()
    bool                                    trajectories_modified_ = false;  // dirty flag independent from modified_

    // Window configuration (adjustable and persisted)
    int   xml_panel_width_  = DEFAULT_XML_PANEL_WIDTH;
    int   menu_bar_height_  = DEFAULT_MENU_BAR_HEIGHT;
    int   time_bar_height_  = DEFAULT_TIME_BAR_HEIGHT;
    float log_height_ratio_ = DEFAULT_LOG_HEIGHT_RATIO;
    int   viewport_width_   = DEFAULT_VIEWPORT_WIDTH;
    int   viewport_height_  = DEFAULT_VIEWPORT_HEIGHT;

    // Esmini Settings
    bool        esmini_settings_dialog_to_open_ = false;
    int         esmini_seed_                    = 0;
    float       esmini_timestep_                = 0.05f;
    std::string esmini_resource_paths_;

private:
    void        ExtractObjectType(pugi::xml_node node, std::string* object_type, std::string* entity_name, std::string* condition_name);
    std::string FindEntityNameFromNode(pugi::xml_node node);

    bool RecursiveNameExists(const std::string& name, const pugi::xml_node current, const pugi::xml_node exclude_node) const;
    void AddRequiredAttributes(pugi::xml_node node, const std::string& node_name);
    void EnsureUniqueNamesForSubtree(pugi::xml_node node);

    void PushUndoState();

    void UpdateEsminiSettings();

    void        RebuildParameters();
    std::string EvaluateAttribute(pugi::xml_node node, const std::string& attr_name);
    double      EvaluateAttributeDouble(pugi::xml_node node, const std::string& attr_name, double default_val = 0.0);
    int         EvaluateAttributeInt(pugi::xml_node node, const std::string& attr_name, int default_val = 0);

    // Parameter management and expression evaluation
    scenarioengine::Parameters parameters_;

    // Store diffs instead of full snapshots to save memory
    std::deque<std::vector<DiffChunk>> undo_stack_;
    std::deque<std::vector<DiffChunk>> redo_stack_;

    static const size_t MAX_UNDO_STACK_SIZE = 50;

    // Current snapshot text to compute diffs against
    std::string current_snapshot_;

    // Parallel to undo_stack_/redo_stack_ (index-aligned), recording the shared sequence counter value at the
    // moment each entry was pushed - used only to compare recency against the trajectory undo/redo stacks
    // below when routing Ctrl+Z/Ctrl+Shift+Z (see Last*Seq() accessors and StudioGui::Undo()/Redo()).
    std::deque<size_t> undo_seq_stack_;
    std::deque<size_t> redo_seq_stack_;

    // One entry = one full JSON snapshot of entity_trajectories_ plus the selection state that should be
    // restored alongside it (Trajectory_Editing.md section 11.3). Kept as a plain JSON string (not a live
    // nlohmann::json object) so this header does not need to include json.hpp.
    struct TrajectoryUndoEntry
    {
        std::string                 data_json;  // TrajectoriesToJsonString()
        TrajectorySelectionSnapshot selection;
    };
    std::deque<TrajectoryUndoEntry> trajectory_undo_stack_;
    std::deque<TrajectoryUndoEntry> trajectory_redo_stack_;
    TrajectoryUndoEntry              current_trajectory_entry_;

    std::deque<size_t> trajectory_undo_seq_stack_;
    std::deque<size_t> trajectory_redo_seq_stack_;

    static const size_t MAX_TRAJECTORY_UNDO_STACK_SIZE = 50;

    // Shared between the xml and trajectory undo systems so their stacks can be compared for recency:
    // edit_sequence_counter_ is bumped on every successful Push*UndoState() (and every Redo, since redoing
    // makes that state the most recent edit again); undo_sequence_counter_ is bumped on every Undo, so a
    // subsequent Redo can tell which of the two systems' redo stacks was undone more recently.
    size_t edit_sequence_counter_ = 0;
    size_t undo_sequence_counter_ = 0;

    // Internal (de)serialization helpers shared by SaveTrajJson/LoadTrajJson and the trajectory undo/redo
    // functions above (Trajectory_Editing.md section 11.2) - kept as std::string (a JSON dump), not a raw
    // nlohmann::json object, so that type does not need to appear in this header either.
    std::string TrajectoriesToJsonString() const;
    bool        TrajectoriesFromJsonString(const std::string& json_text);

    struct NodeStruct
    {
        std::string name;
        int         idx = -1;
    };

    struct CommonlyUsedNodeRule
    {
        std::string             name;
        std::vector<NodeStruct> parents;
        int                     idx = -1;
    };

    struct CommonlyUsedAttrRule
    {
        std::string             name;
        std::string             default_value;
        std::vector<NodeStruct> parents;
    };

    std::vector<CommonlyUsedNodeRule> node_rules_;
    std::vector<CommonlyUsedAttrRule> attr_rules_;
};
