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

#include <osgViewer/ViewerEventHandlers>
#include "StudioViewer.hpp"
#include "StudioDataModel.hpp"
#include "XmlUtil.hpp"
#include "RoadManager.hpp"
#include "CommonMini.hpp"
#include "OSCBoundingBox.hpp"  // For OSCBoundingBox structure
#include "ScenarioReader.hpp"
#include "TrajectoryRenderer.hpp"
#include <deque>
#include <string>
#include <utility>
#include <vector>
#include <tuple>
#include <functional>
#include <unordered_set>
#include <map>

struct ImVec2;

class StudioGui : public osgGA::GUIEventHandler
{
public:
    StudioGui(viewer::StudioViewer* viewer);
    ~StudioGui();

    virtual bool     handle(const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter& aa) override;
    void             NewFrame(osg::RenderInfo& renderInfo);
    void             Render(osg::RenderInfo& renderInfo);
    StudioDataModel* GetModel()
    {
        return &data_model_;
    }

    void AddLogMessage(const char* message);

    std::string GetEntryNameFromScenarioObject(const std::string& scenario_object_name) const;

private:
    void Exit();
    void SetModified();

    void QueueNodeExpansion(pugi::xml_node node);
    void QueueSubtreeExpansion(pugi::xml_node node);

    void ResetCameraPos();
    void MoveMouse(double x, double y);

    void SwitchToViewer();
    void SwitchToComposer();
    void HandleSpaceKey();  // Unified space key handler for mode switching

    void UpdateMousePositionFromWorld();
    void ExtractPositionsFromXml();
    void DrawPositionMarkers();
    void ClearPositionMarkers();

    osg::ref_ptr<osg::Node> GetOSGBModelForScenarioObject(const std::string& scenario_object_name);
    osg::ref_ptr<osg::Node> LoadOSGBModelWithScale(const std::string& file_path, EntityScaleMode scale_mode, const OSCBoundingBox& bounding_box);

    struct OSGBModel;

    // OSGB model management
    void       PrepareReader();
    void       UpdateScenarioObjectMapping();
    OSGBModel* FindOSGBModelInCache(const std::string& model_name);
    void       AddOSGBModelToCache(const std::string& model_name, const std::string& file_path, osg::ref_ptr<osg::Node> node);
    int        GetModelIdFromCatalogEntry(const std::string& catalog_name, const std::string& entry_name);

    // Vehicle/object picking and XML highlighting functions
    PositionInfo* PickPosition();

    void ClearXmlHighlights();
    void HighlightXmlNodeForPosition(const PositionInfo& posInfo);

    // Move context menu and operation support
    void StartMoveOperation(PositionInfo* position_info);
    void UpdateMoveOperation();
    void EndMoveOperation();

    // Heading modify operation support ("Modify Heading" in the viewport context menu)
    void StartHeadingOperation(const PositionInfo& position_info);
    void UpdateHeadingOperation();
    void ConfirmHeadingOperation();
    void CancelHeadingOperation();

    // Locked target position update functions (for fixed target during move operations)
    void UpdatePositionFromLockedTarget(PositionInfo* position_info);

    void RenderRealTimeHUD();
    void RenderLogWindow();
    void RenderXmlTree();
    void RenderXmlSubTree(pugi::xml_node node,
                          int            level                 = 0,
                          bool           force_expand          = false,
                          bool           suppress_context_menu = false,
                          int            level_to_expand       = 0,
                          int            level_to_fold         = 0,
                          bool           parent_matches_filter = false);
    void HandleAttrDialog();
    void HandleNodeDialog();
    void HandleMovePositionMenu();
    void HandleEsminiSettingsDialog();
    void HandleViewportContextMenu();
    void HandleAddVehicleDialog();
    void OpenAddVehicleDialog();

    // Trajectory editing (Trajectory_Editing.md): "Add Trajectory" creates a brand-new vehicle that only
    // exists in entity_trajectories_ / .traj.json, entirely independent from Add Vehicle / xml_doc_.
    void OpenAddTrajectoryDialog();
    void HandleAddTrajectoryDialog();
    void RenderTrajectoriesTab();

    // Continuous point-picking interaction driven from HandleAddTrajectoryDialog()'s Confirm button; see
    // Trajectory_Editing.md section 6.1 for the Enter/Esc semantics.
    void StartTrajectoryPicking(const std::string& entity_name);
    void CommitTrajectoryPickingPoint();
    void FinishTrajectoryPicking();
    void CancelTrajectoryPickingPoint();

    // "Append Point" (right-click an existing path point in the map view): same continuous picking loop as
    // above, but on an *already-existing* trajectory; see trajectory_picking_is_append_ for the differences.
    void StartTrajectoryAppending(const std::string& entity_name);

    // Shared cleanup for deleting an entity trajectory (used by the Trajectories tab's Delete button and the
    // map view's right-click "Delete Trajectory" menu item): removes it from the data model and renderer,
    // clears any selection/drag state that referenced it, and pushes one undo step.
    void DeleteEntityTrajectory(const std::string& entity_name);

    // Dragging an existing path point in the map view (Trajectory_Editing.md section 7.4 / 6.3). A click only
    // selects/highlights the nearest point (shows a gizmo); a further click-drag starting on the already-
    // selected point's gizmo is what actually moves it, so a plain click never modifies the trajectory.
    bool HandleTrajectoryPointClick();
    void UpdateTrajectoryPointDrag();
    void EndTrajectoryPointDrag();

    // Right-click on/near a path point or path in the map view (Trajectory_Editing.md 7.4/8.2): sets up
    // trajectory_(point|insert)_context_* and returns true if the click landed close enough to a trajectory
    // path/point to be handled here, so the caller doesn't fall through to the xosc entity Move/Add Vehicle
    // context menus. Actual popup rendering happens in HandleViewportContextMenu().
    bool HandleTrajectoryPointRightClick();

    // Ghost keyframe editing (Trajectory_Editing_Enhancement.md section 7): pressing on a ghost vehicle in
    // COMPOSER starts a drag that slides it along its own path (mouse continuously projected onto the path,
    // never off it); releasing records/updates a (t, s) keyframe and runs the P0-P4 cascade re-solve.
    bool HandleGhostKeyframeClick();
    void UpdateGhostKeyframeDrag();
    void EndGhostKeyframeDrag(bool commit);

    // Re-run the keyframe cascade for one entity after any edit that invalidates arrival times (path geometry
    // changes, manual speed profile edits). No-op when the entity has no keyframes.
    void ResolveEntityKeyframes(const std::string& entity_name);

    // Unsaved-changes confirmation shown from the "Exit" menu item / window close (merged prompt covering
    // both modified_ and trajectories_modified_, see Trajectory_Editing.md section 5.4/13).
    void HandleExitConfirmDialog();

    std::vector<std::string> GetVehicleCatalogEntryNames() const;
    void RenderMenuBar();
    void RenderTimeline();
    void HandleElementContextMenu();
    void RenderValidationReport();

    void Undo();
    void Redo();

    // Trajectory-specific Undo/Redo helpers (Trajectory_Editing.md section 11): pack/unpack this StudioGui's
    // own path/speed-profile point selection state into the opaque TrajectorySelectionSnapshot that
    // StudioDataModel stores alongside each trajectory undo/redo entry.
    TrajectorySelectionSnapshot CaptureTrajectorySelectionSnapshot() const;
    void                        ApplyTrajectorySelectionSnapshot(const TrajectorySelectionSnapshot& sel);

    void TryLoadOpenDriveFromScenario();

    double time_;
    bool   left_mouse_pressed_   = false;
    bool   right_mouse_pressed_  = false;
    bool   middle_mouse_pressed_ = false;
    bool   shift_pressed_        = false;
    bool   ctrl_pressed_         = false;
    bool   alt_pressed_          = false;

    // Move context menu support
    bool          move_context_menu_to_open_  = false;
    bool          move_operation_active_      = false;
    PositionInfo* selected_position_for_move_ = nullptr;  // Position info for move operations

    // Right-click detection: distinguish a right-click (opens a context menu) from a right-drag (camera pan)
    bool  right_click_candidate_ = false;
    float right_press_x_         = 0.0f;
    float right_press_y_         = 0.0f;

    // Speed profile chart right-click context menu (Trajectory_Editing.md 8.2): right-clicking a point offers
    // "Delete Point", right-clicking empty space offers "Insert Point" at that location. Double-click gestures
    // were dropped for this (ImGui::IsMouseDoubleClicked() proved unreliable: it samples io.MouseDown[] once
    // per rendered frame, so a fast double-click landing entirely between two frames is silently missed,
    // regardless of io.MouseDoubleClickMaxDist). A plain right-click doesn't have that problem.
    int    speed_context_point_index_  = -1;   // index right-clicked, or -1 if the click landed on empty space
    double speed_context_insert_s_     = 0.0;  // plot-space position of the right-click, for "Insert Point"
    double speed_context_insert_speed_ = 0.0;

    // Viewport "Add Vehicle" context menu and modal dialog state
    bool                     add_vehicle_context_menu_to_open_ = false;
    bool                     add_vehicle_dialog_to_open_       = false;
    bool                     add_vehicle_dialog_active_        = false;
    std::string              add_vehicle_name_;
    std::string              add_vehicle_entry_name_;
    std::string              add_vehicle_catalog_name_;
    std::vector<std::string> add_vehicle_entry_options_;
    float                    add_vehicle_init_speed_ = 0.0f;

    // "Add Trajectory" modal dialog state (Trajectory_Editing.md section 6.1). Uses the same "Viewport Context
    // Menu" popup as Add Vehicle, but its own independent dialog implementation.
    bool        add_trajectory_dialog_to_open_ = false;
    bool        add_trajectory_dialog_active_  = false;
    std::string add_trajectory_name_;
    float       add_trajectory_init_speed_     = 0.0f;
    int         add_trajectory_interp_mode_    = 1;  // 0=Linear, 1=Spline (default), 2=Clothoid
    std::string              add_trajectory_entry_name_;     // Vehicle Type (VehicleCatalog.xosc entryName)
    std::vector<std::string> add_trajectory_entry_options_;  // independent from add_vehicle_entry_options_

    // Continuous point-picking session state (section 6.1): trajectory_picking_active_ is true from the moment
    // the Add Trajectory dialog is confirmed until Enter commits or Esc cancels the whole thing.
    bool        trajectory_picking_active_ = false;
    std::string trajectory_picking_entity_name_;

    // Set when the current picking session is "Append Point" on an *existing* trajectory (right-click a path
    // point in the map view) rather than "Add Trajectory" building a brand-new one. Changes Esc/Enter-at-zero
    // semantics: Esc must never pop/destroy points that existed before this session started, and Enter with
    // no newly-added points is ignored rather than doing nothing to an (nonexistent) empty new entity.
    bool   trajectory_picking_is_append_         = false;
    size_t trajectory_picking_start_point_count_ = 0;
    double trajectory_picking_start_path_length_ = 0.0;  // path length before this append session, used to
                                                          // decide where to freeze the old tail speed (below)

    // Selection state (section 7.4): a click selects/highlights a point without modifying anything; a further
    // click-drag on the already-selected point is what actually moves it.
    bool        trajectory_point_selected_        = false;
    std::string trajectory_selected_entity_name_;
    int         trajectory_selected_point_index_  = -1;

    // Path point dragging state (section 6.3 / 7.4). The point is moved by the same delta the mouse moved
    // since the drag started (not snapped straight to the cursor), so drag_start_* records both the mouse
    // position and the point's original position at the moment the drag began.
    bool        trajectory_point_drag_active_ = false;
    std::string trajectory_drag_entity_name_;
    int         trajectory_drag_point_index_  = -1;
    double      trajectory_drag_start_mouse_x_ = 0.0;
    double      trajectory_drag_start_mouse_y_ = 0.0;
    double      trajectory_drag_start_point_x_ = 0.0;
    double      trajectory_drag_start_point_y_ = 0.0;

    // 3D view right-click Delete Point / Insert Point menus (Trajectory_Editing.md 7.4/8.2, mirroring the
    // speed profile chart's right-click menu): right-clicking on/near an existing path point offers Delete
    // Point; right-clicking near the path but not on a point offers Insert Point at the projected location.
    // Both are deferred-open (set from handle() on right-click release, opened from HandleViewportContextMenu()
    // on the next Render()), matching the existing add_vehicle_context_menu_to_open_ pattern.
    bool        trajectory_point_context_menu_to_open_ = false;
    std::string trajectory_context_entity_name_;
    int         trajectory_context_point_index_ = -1;  // Delete Point target

    bool        trajectory_insert_context_menu_to_open_ = false;
    std::string trajectory_insert_context_entity_name_;
    double      trajectory_insert_context_x_ = 0.0;
    double      trajectory_insert_context_y_ = 0.0;
    double      trajectory_insert_context_z_ = 0.0;
    double      trajectory_insert_context_s_ = 0.0;

    // Ghost keyframe drag state (Trajectory_Editing_Enhancement.md section 7): the ghost slides along its own
    // path (mouse projected to arc length), hard-blocked at the neighbouring keyframes' s values in both
    // directions. ghost_drag_existing_kf_ >= 0 means the drag updates that keyframe (same t) instead of
    // creating a new one on release.
    bool        ghost_keyframe_drag_active_ = false;
    std::string ghost_drag_entity_name_;
    double      ghost_drag_t_           = 0.0;
    double      ghost_drag_start_s_     = 0.0;
    double      ghost_drag_target_s_    = 0.0;
    double      ghost_drag_s_min_       = 0.0;
    double      ghost_drag_s_max_       = 0.0;
    int         ghost_drag_existing_kf_ = -1;
    bool        ghost_drag_blocked_     = false;

    // Right-click context menu on a keyframe diamond marker (Delete Keyframe)
    bool        trajectory_keyframe_context_menu_to_open_ = false;
    std::string trajectory_keyframe_context_entity_;
    int         trajectory_keyframe_context_index_ = -1;

    // Speed profile chart selection/drag state (section 8.2): a click selects a point (highlighted red in the
    // chart and its row in the table below); only the selected point can be dragged, and only in the same
    // press that selected it or a later one that lands on it again. Dragging is delta-based like the 3D path
    // point drag above: drag_start_mouse_* + drag_start_point_* record the state at the moment the press
    // landed on the point, and every subsequent frame computes new_value = start_point + (mouse_now -
    // mouse_start). By default only speed (the y axis) changes; holding Ctrl also allows s (x axis) to change,
    // except on the first/last (anchor) points whose s always stays fixed regardless of Ctrl.
    bool        speed_point_selected_       = false;
    std::string speed_selected_entity_name_;
    int         speed_selected_point_index_ = -1;

    bool        speed_point_drag_active_       = false;
    std::string speed_drag_entity_name_;
    int         speed_drag_point_index_        = -1;
    double      speed_drag_start_mouse_s_      = 0.0;
    double      speed_drag_start_mouse_speed_  = 0.0;
    double      speed_drag_start_point_s_      = 0.0;
    double      speed_drag_start_point_speed_  = 0.0;

    // Right panel ("XML Tree"/"Trajectories" tabs) defaults to the Trajectories tab once, on the first frame,
    // without forcing it to stay selected afterwards.
    bool right_panel_default_tab_applied_ = false;

    // Unsaved-changes-on-exit confirmation (section 5.4/13)
    bool exit_confirm_dialog_active_ = false;

    EntityTrajectoryRenderer trajectory_renderer_;

    // Heading modify operation state
    bool         heading_operation_active_         = false;
    PositionInfo heading_position_info_;                     // locked target position (snapshot taken at operation start)
    bool         heading_is_relative_              = false;  // h is stored lane-relative in the file
    double       heading_base_h_                   = 0.0;    // lane direction at the position (relative case only)
    double       heading_preview_h_                = 0.0;    // h value currently written to the file (shown in the label)
    bool         heading_orig_attr_present_        = false;  // original h attribute existed
    std::string  heading_orig_h_value_;                      // original raw h attribute value
    bool         heading_orig_orientation_present_ = false;  // LanePosition only: Orientation child existed

    // Real-time HUD data
    double hud_mouse_world_x_     = 0.0;
    double hud_mouse_world_y_     = 0.0;
    double hud_mouse_world_z_     = 0.0;
    int    hud_mouse_road_id_     = -1;
    int    hud_mouse_lane_id_     = -1;
    double hud_mouse_s_           = 0.0;
    double hud_mouse_t_           = 0.0;
    double hud_mouse_lane_offset_ = 0.0;
    double hud_mouse_lane_h_      = 0.0;
    bool   hud_show_position_     = true;

    std::vector<PositionInfo> extracted_positions_;

    // Position marker tracking
    bool   positions_extracted_ = false;  // Track if positions have been extracted
    size_t last_scenario_hash_;           // Track scenario changes

    // Click classification data for context menu distinction
    int          pending_move_count_ = 0;
    PositionInfo last_picked_position_info_;

    float mouse_wheel_;
    bool  initialized_;

    int                   viewport_x_ = -1;
    int                   viewport_y_ = -1;
    viewer::StudioViewer* viewer_;
    StudioDataModel       data_model_;

    // Log window data
    std::deque<std::string> log_messages_;
    std::string             log_text_buffer_;  // Combined text buffer for selectable display
    bool                    log_auto_scroll_  = true;
    int                     max_log_messages_ = 1000;

    // XML Tree highlighting support
    std::vector<pugi::xml_node> highlighted_nodes_;

    // XML Tree Search support
    struct SearchResult
    {
        pugi::xml_node      node;
        pugi::xml_attribute attr;
        std::string         attribute_name;  // Empty if match is on node name
    };

    char                      search_text_[128] = "";
    std::vector<SearchResult> search_results_;
    int                       current_search_index_ = -1;
    pugi::xml_node            search_highlight_node_;
    pugi::xml_attribute       search_highlight_attr_;
    pugi::xml_node            node_to_scroll_to_;
    pugi::xml_attribute       attr_to_scroll_to_;
    pugi::xml_node            node_and_attrs_to_scroll_to_;

    // XML Tree Filter support
    char filter_text_[128] = "";
    bool filter_active_    = false;
    bool filter_isolate_   = false;

    bool to_reset_camera_pos_ = true;

    std::vector<pugi::xml_node> nodes_to_expand_;
    std::vector<pugi::xml_node> nodes_to_collapse_;

    bool show_validation_window_ = false;

    // OSGB model loading and caching system
    struct OSGBModel
    {
        std::string             name;       // Model identifier (e.g., entryName)
        std::string             file_path;  // Path to the OSGB file
        osg::ref_ptr<osg::Node> node;       // Loaded model node
        bool                    is_loaded;  // Whether the model is successfully loaded
    };

    std::map<std::string, std::string> scenario_object_name_to_entryname_;  // Cache for name mapping
    std::vector<OSGBModel>             osgb_model_cache_;                   // Cache for loaded OSGB models
    bool                               scenario_object_map_dirty_;          // Whether the mapping needs to be updated

    // ScenarioReader for parsing entity nodes to get model_id and model3d
    std::unique_ptr<scenarioengine::Entities>       temp_entities_;    // Temporary entities for parsing
    std::unique_ptr<scenarioengine::Catalogs>       temp_catalogs_;    // Temporary catalogs for parsing
    std::unique_ptr<scenarioengine::ScenarioReader> scenario_reader_;  // For parsing entity nodes
};
