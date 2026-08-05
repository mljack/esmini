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

#include "StudioGui.hpp"
#include "PosUtil.hpp"
#include "OsgUtil.hpp"
#include <string.h>
#include <iostream>
#include <fstream>
#include <osgViewer/ViewerEventHandlers>
#include <osgViewer/Viewer>
#include <osg/MatrixTransform>
#include <osgDB/ReadFile>
#include <osg/BlendFunc>
#include <osg/BlendColor>
#include <osg/Vec3>
#include <osg/PositionAttitudeTransform>
#include <cstdlib>
#include <cstdio>
#include <cstdarg>
#include <climits>
#define _USE_MATH_DEFINES
#include <cmath>
#include <time.h>
#include <algorithm>
#include <functional>
#include <unordered_set>
#include <cctype>
#include <deque>
#include "CommonMini.hpp"      // For EntityScaleMode enum
#include "OSCBoundingBox.hpp"  // For OSCBoundingBox structure

#include "portable-file-dialogs.h"
#ifdef __linux__
#include <X11/Xlib.h>
#else
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "CommonMini.hpp"
#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"

extern std::vector<std::string> g_paths;

void FetchKeyEvent(viewer::KeyEvent* keyEvent, void*);  // from main.cpp of scenariostudio

namespace
{
    const int    TREE_VIEW_WIDTH = 400;
    const double EGO_ZOOM_DIST   = 8000;

    bool                        xml_panel_status                 = true;
    bool                        xml_node_modify_menu_to_open     = false;
    bool                        position_conversion_test_to_open = false;
    std::string                 modify_menu_context;
    pugi::xml_node              modify_menu_context_node;
    std::vector<std::string>    attrs_to_create;
    std::vector<std::string>    attrs_to_delete;
    std::vector<SubelementInfo> elements_to_create;  // Now using SubelementInfo to store content model type

    std::vector<std::string> scenario_filter  = {"OpenSCENARIO Files", "*.xosc"};
    std::vector<std::string> map_filter       = {"OpenDRIVE Files", "*.xodr"};
    std::vector<std::string> traj_json_filter = {"Trajectory Files", "*.traj.json"};
    std::vector<std::string> traj_csv_filter  = {"CSV Files", "*.csv"};

    // Check for attribute value dialog to open (in the main loop, not inside RenderNodeModifyMenu)
    bool        attr_value_dialog_to_open = false;
    std::string attr_to_create_name;
    std::string new_attr_value;
    bool        attr_creation_active = false;

    // New node creation dialog variables
    bool               node_creation_dialog_to_open = false;
    std::string        node_to_create_name;
    bool               node_creation_active = false;
    pugi::xml_document temp_doc;

    // Rename entity dialog variables
    bool        rename_dialog_to_open = false;
    bool        rename_dialog_active  = false;
    std::string rename_old_name;
    std::string rename_new_name;

    bool to_save_file               = false;
    bool attr_context_menu_launched = false;

    pugi::xml_attribute right_clicked_attr;

    // group to hold all position markers
    osg::ref_ptr<osg::Group> position_markers_group;

    void TextUnformattedRightAligned(const char* buf, int pos)
    {
        ImVec2 text_size = ImGui::CalcTextSize(buf);
        ImGui::SetCursorPosX(pos - text_size.x);
        ImGui::TextUnformatted(buf);
        ImGui::SameLine(pos + 6);
    }
}  // namespace

int         find_enum_index(const std::vector<std::string>& enums, const char* value);
const char* get_enum(void* user_data, int idx);

// Helper function to normalize path by replacing backslashes with forward slashes
static std::string normalize_path(const std::string& path)
{
    std::string normalized = path;
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    return normalized;
}

namespace
{
    struct ImGuiNewFrameCallback : public osg::Camera::DrawCallback
    {
        ImGuiNewFrameCallback(StudioGui& handler) : handler_(handler)
        {
        }

        void operator()(osg::RenderInfo& renderInfo) const override
        {
            handler_.NewFrame(renderInfo);
        }

    private:
        StudioGui& handler_;
    };

    struct ImGuiRenderCallback : public osg::Camera::DrawCallback
    {
        ImGuiRenderCallback(StudioGui& handler) : handler_(handler)
        {
        }

        void operator()(osg::RenderInfo& renderInfo) const override
        {
            handler_.Render(renderInfo);
        }

    private:
        StudioGui& handler_;
    };

}  // anonymous namespace

#ifdef _WIN32

static const char* GetClipboardText(void* user_data)
{
    static std::string g_clipboard_buffer;
    if (!OpenClipboard(NULL))
        return NULL;

    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (hData == NULL)
    {
        CloseClipboard();
        return NULL;
    }

    wchar_t* pszText = static_cast<wchar_t*>(GlobalLock(hData));
    if (pszText == NULL)
    {
        CloseClipboard();
        return NULL;
    }

    int size_needed = WideCharToMultiByte(CP_UTF8, 0, pszText, -1, NULL, 0, NULL, NULL);
    g_clipboard_buffer.resize(size_needed);
    WideCharToMultiByte(CP_UTF8, 0, pszText, -1, &g_clipboard_buffer[0], size_needed, NULL, NULL);
    if (!g_clipboard_buffer.empty() && g_clipboard_buffer.back() == '\0')
        g_clipboard_buffer.pop_back();

    GlobalUnlock(hData);
    CloseClipboard();
    return g_clipboard_buffer.c_str();
}

static void SetClipboardText(void* user_data, const char* text)
{
    if (!OpenClipboard(NULL))
        return;

    EmptyClipboard();
    int     size_needed = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    HGLOBAL hMem        = GlobalAlloc(GMEM_MOVEABLE, size_needed * sizeof(wchar_t));
    if (hMem)
    {
        wchar_t* pMem = static_cast<wchar_t*>(GlobalLock(hMem));
        MultiByteToWideChar(CP_UTF8, 0, text, -1, pMem, size_needed);
        GlobalUnlock(hMem);
        SetClipboardData(CF_UNICODETEXT, hMem);
    }
    CloseClipboard();
}
#endif

#ifdef __linux__
// Linux clipboard implementation using X11
// Note: This requires linking against X11 library
// The implementation uses a hidden window to handle selection requests

static const char* GetClipboardText(void* user_data)
{
    static std::string g_clipboard_buffer;
    g_clipboard_buffer.clear();

    Display* display = XOpenDisplay(NULL);
    if (!display)
        return NULL;

    Window window      = XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0, 1, 1, 0, 0, 0);
    Atom   clipboard   = XInternAtom(display, "CLIPBOARD", False);
    Atom   utf8_string = XInternAtom(display, "UTF8_STRING", False);
    Atom   property    = XInternAtom(display, "XSEL_DATA", False);

    XConvertSelection(display, clipboard, utf8_string, property, window, CurrentTime);
    XSync(display, False);

    XEvent event;
    while (true)
    {
        XNextEvent(display, &event);
        if (event.type == SelectionNotify)
        {
            if (event.xselection.property == None)
                break;

            Atom           actual_type;
            int            actual_format;
            unsigned long  nitems, bytes_after;
            unsigned char* data = NULL;

            XGetWindowProperty(display,
                               window,
                               property,
                               0,
                               LONG_MAX,
                               False,
                               utf8_string,
                               &actual_type,
                               &actual_format,
                               &nitems,
                               &bytes_after,
                               &data);

            if (data)
            {
                g_clipboard_buffer = reinterpret_cast<char*>(data);
                XFree(data);
            }
            break;
        }
    }

    XDestroyWindow(display, window);
    XCloseDisplay(display);

    return g_clipboard_buffer.empty() ? NULL : g_clipboard_buffer.c_str();
}

static void SetClipboardText(void* user_data, const char* text)
{
    // Setting clipboard on Linux/X11 is complex as it requires an event loop to respond to SelectionRequest.
    // For a simple implementation without a persistent event loop for the clipboard owner,
    // we can use an external tool like xclip if available, or a simplified approach.
    // However, since this application already has an event loop (OSG), a proper implementation
    // would integrate with it. Given the constraints, we'll use a pipe to 'xclip' as a robust fallback
    // which is common in Linux dev environments.

    FILE* pipe = popen("xclip -selection clipboard -i", "w");
    if (pipe)
    {
        fwrite(text, 1, strlen(text), pipe);
        pclose(pipe);
    }
}
#endif

StudioGui::StudioGui(viewer::StudioViewer* viewer)
    : time_(0.0f),
      mouse_wheel_(0.0f),
      initialized_(false),
      viewer_(viewer),
      scenario_object_map_dirty_(true)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io    = ImGui::GetIO();
    io.IniFilename = NULL;
    io.LogFilename = NULL;

    io.SetClipboardTextFn = SetClipboardText;
    io.GetClipboardTextFn = GetClipboardText;
    io.ClipboardUserData  = NULL;

    viewer_->gw_->setCursor(osgViewer::GraphicsWindow::LeftArrowCursor);
    // ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow);

    // Initialize ScenarioReader for parsing entity nodes
    PrepareReader();

    // Load window configuration from file
    data_model_.LoadConfig();

    // Mount the trajectory-editing overlay once, independent of StudioViewer/ScenarioPlayer's own scene graph
    // ownership (Trajectory_Editing.md section 7.1).
    trajectory_renderer_.Init(viewer_->rootnode_);
}

void StudioGui::PrepareReader()
{
    temp_entities_ = std::make_unique<scenarioengine::Entities>();
    temp_catalogs_ = std::make_unique<scenarioengine::Catalogs>();
    scenario_reader_ =
        std::make_unique<scenarioengine::ScenarioReader>(temp_entities_.get(), temp_catalogs_.get(), true);  // disable_controllers=true
}

StudioGui::~StudioGui()
{
    // Save window configuration on exit
    data_model_.SaveConfig();
}

void StudioGui::Exit()
{
    if (data_model_.modified_ || data_model_.trajectories_modified_)
    {
        exit_confirm_dialog_active_ = true;
        return;
    }
    data_model_.mode_ = StudioMode::ZOMBIE;
    viewer_->osgViewer_->setDone(true);
}

/**
 * Important Note: Dear ImGui expects the control Keys indices not to be
 * greater thant 511. It actually uses an array of 512 elements. However,
 * OSG has indices greater than that. So here I do a conversion for special
 * keys between ImGui and OSG.
 */
static int ConvertOSGKeyToImGuiKey(int key)
{
    using KEY                 = osgGA::GUIEventAdapter::KeySymbol;
    std::map<KEY, ImGuiKey> m = {
        {KEY::KEY_Tab, ImGuiKey_Tab},
        {KEY::KEY_Left, ImGuiKey_LeftArrow},
        {KEY::KEY_Right, ImGuiKey_RightArrow},
        {KEY::KEY_Up, ImGuiKey_UpArrow},
        {KEY::KEY_Down, ImGuiKey_DownArrow},
        {KEY::KEY_Page_Up, ImGuiKey_PageUp},
        {KEY::KEY_Page_Down, ImGuiKey_PageDown},
        {KEY::KEY_Home, ImGuiKey_Home},
        {KEY::KEY_End, ImGuiKey_End},
        {KEY::KEY_Delete, ImGuiKey_Delete},
        {KEY::KEY_BackSpace, ImGuiKey_Backspace},
        {KEY::KEY_Space, ImGuiKey_Space},
        {KEY::KEY_Return, ImGuiKey_Enter},
        {KEY::KEY_Escape, ImGuiKey_Escape},
        {KEY::KEY_KP_0, ImGuiKey_Keypad0},
        {KEY::KEY_KP_1, ImGuiKey_Keypad1},
        {KEY::KEY_KP_2, ImGuiKey_Keypad2},
        {KEY::KEY_KP_3, ImGuiKey_Keypad3},
        {KEY::KEY_KP_4, ImGuiKey_Keypad4},
        {KEY::KEY_KP_5, ImGuiKey_Keypad5},
        {KEY::KEY_KP_6, ImGuiKey_Keypad6},
        {KEY::KEY_KP_7, ImGuiKey_Keypad7},
        {KEY::KEY_KP_8, ImGuiKey_Keypad8},
        {KEY::KEY_KP_9, ImGuiKey_Keypad9},
        {KEY::KEY_KP_Decimal, ImGuiKey_KeypadDecimal},
        {KEY::KEY_KP_Divide, ImGuiKey_KeypadDivide},
        {KEY::KEY_KP_Multiply, ImGuiKey_KeypadMultiply},
        {KEY::KEY_KP_Subtract, ImGuiKey_KeypadSubtract},
        {KEY::KEY_KP_Add, ImGuiKey_KeypadAdd},
        {KEY::KEY_KP_Enter, ImGuiKey_Enter},
        {KEY::KEY_KP_Equal, ImGuiKey_KeypadEqual},
    };
    if (m.count((KEY)key) != 0)
        return m.at((KEY)key);
    return -1;
}

void StudioGui::NewFrame(osg::RenderInfo& renderInfo)
{
    ImGui_ImplOpenGL3_NewFrame();

    ImGuiIO& io = ImGui::GetIO();

    osg::Viewport* viewport = renderInfo.getCurrentCamera()->getViewport();
    io.DisplaySize          = ImVec2(viewport->width(), viewport->height());
    viewport_x_             = viewport->x();
    viewport_y_             = viewport->y();

    // Detect viewport size changes and save
    int new_width  = viewport->width();
    int new_height = viewport->height();
    if (new_width != data_model_.viewport_width_ || new_height != data_model_.viewport_height_)
    {
        LOG("Viewport size changed to %ux%u", new_width, new_height);
        // Save to configured app window size
        data_model_.viewport_width_  = new_width;
        data_model_.viewport_height_ = new_height;
        data_model_.SaveConfig();
    }

    data_model_.viewport_width_  = new_width;
    data_model_.viewport_height_ = new_height;

    viewer_->SetViewportSize(data_model_.viewport_width_, data_model_.viewport_height_);
    float  log_height = std::floor(data_model_.viewport_height_ * data_model_.log_height_ratio_);
    double x          = xml_panel_status ? -data_model_.xml_panel_width_ / 2 : 0.0;
    double y          = data_model_.menu_bar_height_;
    y += (data_model_.viewport_height_ - data_model_.menu_bar_height_ - data_model_.time_bar_height_ - log_height) / 2;
    y -= data_model_.viewport_height_ / 2;

    viewer_->SetViewportCenterPixelOffset(x, -y);

    double currentTime = renderInfo.getView()->getFrameStamp()->getSimulationTime();
    io.DeltaTime       = currentTime - time_ + 0.00001;
    time_              = currentTime;

    io.MouseDown[0] = left_mouse_pressed_;
    io.MouseDown[1] = right_mouse_pressed_;
    io.MouseDown[2] = middle_mouse_pressed_;
    io.KeyShift     = shift_pressed_;
    io.KeyCtrl      = ctrl_pressed_;
    io.KeyAlt       = alt_pressed_;

    io.MouseWheel = mouse_wheel_;
    mouse_wheel_  = 0.0f;

    ImGui::NewFrame();
}

void StudioGui::ResetCameraPos()
{
    viewer_->MoveCameraToMapCenter();
    for (auto& pos : extracted_positions_)
    {
        if (pos.name != "ego" && pos.name != "Ego" && pos.name != "EGO")
            continue;
        viewer_->SetCameraDistance(EGO_ZOOM_DIST);
        viewer_->MoveCameraWithoutOriginOffset(pos.x, pos.y);
    }
}

void StudioGui::Render(osg::RenderInfo&)
{
    RenderMenuBar();
    HandleEsminiSettingsDialog();
    RenderLogWindow();
    RenderTimeline();
    RenderXmlTree();

    // Update mouse position data and render real-time HUD
    UpdateMousePositionFromWorld();
    RenderRealTimeHUD();
    RenderValidationReport();
    HandleExitConfirmDialog();

    // Trajectory-editor-only rendering (Trajectory_Editing.md section 7): independent of
    // viewer::StudioViewer/ScenarioPlayer's scene graph ownership, updated every frame in every mode.
    trajectory_renderer_.SetSelectedPoint(trajectory_point_selected_ ? trajectory_selected_entity_name_ : std::string(),
                                          trajectory_point_selected_ ? trajectory_selected_point_index_ : -1);
    trajectory_renderer_.Update(data_model_, data_model_.mode_, data_model_.virtual_time_);
    if (trajectory_picking_active_)
    {
        auto it = data_model_.entity_trajectories_.find(trajectory_picking_entity_name_);
        if (it != data_model_.entity_trajectories_.end())
            trajectory_renderer_.UpdatePickingPreview(it->second.path_.points_,
                                                      hud_mouse_world_x_,
                                                      hud_mouse_world_y_,
                                                      hud_mouse_world_z_,
                                                      it->second.vehicle_catalog_entry_name_);
    }
    else
    {
        trajectory_renderer_.ClearPickingPreview();
    }

    // Live feedback while a ghost keyframe is being dragged along its path (Trajectory_Editing_Enhancement.md
    // 7.3): time, s from->to, the required average speed of the affected segment, and whether the drag is
    // currently blocked at a neighbouring keyframe or would exceed the speed ceiling (=> will be clamped).
    if (ghost_keyframe_drag_active_)
    {
        auto it = data_model_.entity_trajectories_.find(ghost_drag_entity_name_);
        if (it != data_model_.entity_trajectories_.end())
        {
            // The affected segment runs from the latest keyframe before the drag time (or the path start).
            double seg_t0 = 0.0, seg_s0 = 0.0;
            for (const auto& kf : it->second.keyframes_)
            {
                if (kf.t < ghost_drag_t_ - 0.05 && kf.t > seg_t0)
                {
                    seg_t0 = kf.t;
                    seg_s0 = kf.s;
                }
            }

            double dt    = ghost_drag_t_ - seg_t0;
            double v_avg = (dt > 1e-6) ? (ghost_drag_target_s_ - seg_s0) / dt : 0.0;

            ImGui::BeginTooltip();
            ImGui::Text("t=%.2f s   s: %.1f -> %.1f m", ghost_drag_t_, ghost_drag_start_s_, ghost_drag_target_s_);
            if (dt > 1e-6)
                ImGui::Text("Segment avg speed: %.1f m/s", v_avg);
            else
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Time equals the previous keyframe");
            if (ghost_drag_blocked_)
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Clamped to the reachable range (green)");
            else if (v_avg > 30.0 || v_avg < 0.0)
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "Exceeds limits - will be clamped on release");
            ImGui::EndTooltip();
        }
    }

    if (pending_move_count_ > 0)
    {
        pending_move_count_--;
        if (pending_move_count_ == 0)
            StartMoveOperation(&last_picked_position_info_);
    }

    // Always try to extract positions when document changes or not yet extracted
    if (data_model_.mode_ == StudioMode::COMPOSER && (!positions_extracted_ || data_model_.GetDocumentHash() != last_scenario_hash_))
    {
        // Extract positions from current XML
        ExtractPositionsFromXml();
        positions_extracted_           = true;
        data_model_.markers_to_update_ = true;
    }

    if (to_reset_camera_pos_)
    {
        ResetCameraPos();
        to_reset_camera_pos_ = false;
    }

    if (data_model_.mode_ == StudioMode::COMPOSER && data_model_.markers_to_update_)
    {
        // Redraw markers when positions change or document changes
        DrawPositionMarkers();
        data_model_.markers_to_update_ = false;
        last_scenario_hash_            = data_model_.GetDocumentHash();

        // LOG("Position markers updated due to document changes");
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void StudioGui::RenderXmlTree()
{
    static int viewport_width_old  = -1;
    static int viewport_height_old = -1;
    static int xml_panel_width_old = -1;

    ImGui::SetNextWindowPos(ImVec2(data_model_.viewport_width_ - data_model_.xml_panel_width_, data_model_.menu_bar_height_));
    ImGui::SetNextWindowSize(
        (ImVec2(data_model_.xml_panel_width_, data_model_.viewport_height_ - data_model_.menu_bar_height_ - data_model_.time_bar_height_)));
    ImGui::SetNextWindowBgAlpha(1.0f);
    xml_panel_status = ImGui::Begin("XML Tree",
                                    nullptr,
                                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                                        ImGuiWindowFlags_NoTitleBar);
    if (xml_panel_status)
    {
        if (ImGui::BeginTabBar("RightPanelTabs"))
        {
        if (ImGui::BeginTabItem("XML Tree"))
        {
        ImGui::BeginGroup();
        ImGui::PushItemWidth(150);
        static bool reclaim_focus = false;
        if (reclaim_focus)
        {
            ImGui::SetKeyboardFocusHere();
            reclaim_focus = false;
        }
        std::string old_search_text  = search_text_;
        bool        search_triggered = ImGui::InputText("##search", search_text_, IM_ARRAYSIZE(search_text_), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::PopItemWidth();
        ImGui::SameLine();
        static std::string last_search_text;
        if (ImGui::Button("Find", ImVec2(60, 0)) || search_triggered)
        {
            if (search_triggered)
                reclaim_focus = true;

            // Clear highlight if search text is empty
            if (strlen(search_text_) == 0)
            {
                search_results_.clear();
                current_search_index_  = -1;
                search_highlight_node_ = pugi::xml_node();
                search_highlight_attr_ = pugi::xml_attribute();
            }
            else
            {
                std::string current_text = search_text_;

                // Case insensitive comparison for text change detection
                if (current_text != last_search_text)
                {
                    search_results_.clear();
                    current_search_index_ = -1;
                    last_search_text      = current_text;

                    // Convert to lower for case-insensitive search
                    std::string search_lower = current_text;
                    std::transform(search_lower.begin(), search_lower.end(), search_lower.begin(), ::tolower);

                    std::function<void(pugi::xml_node)> find_nodes = [&](pugi::xml_node node)
                    {
                        std::string name       = node.name();
                        std::string name_lower = name;
                        std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), ::tolower);

                        // Check node name
                        if (name_lower.find(search_lower) != std::string::npos)
                        {
                            search_results_.push_back({node, pugi::xml_attribute(), ""});
                        }

                        // Check attributes
                        for (pugi::xml_attribute attr = node.first_attribute(); attr; attr = attr.next_attribute())
                        {
                            std::string attr_val       = attr.value();
                            std::string attr_val_lower = attr_val;
                            std::transform(attr_val_lower.begin(), attr_val_lower.end(), attr_val_lower.begin(), ::tolower);

                            std::string attr_name       = attr.name();
                            std::string attr_name_lower = attr_name;
                            std::transform(attr_name_lower.begin(), attr_name_lower.end(), attr_name_lower.begin(), ::tolower);

                            if (attr_val_lower.find(search_lower) != std::string::npos || attr_name_lower.find(search_lower) != std::string::npos)
                            {
                                search_results_.push_back({node, attr, attr.name()});
                            }
                        }

                        for (pugi::xml_node child = node.first_child(); child; child = child.next_sibling())
                            find_nodes(child);
                    };

                    find_nodes(data_model_.RootNode());
                }

                if (!search_results_.empty())
                {
                    current_search_index_  = (current_search_index_ + 1) % search_results_.size();
                    const auto& result     = search_results_[current_search_index_];
                    search_highlight_node_ = result.node;
                    search_highlight_attr_ = result.attr;
                    node_to_scroll_to_     = result.node;
                    attr_to_scroll_to_     = result.attr;
                    QueueNodeExpansion(search_highlight_node_);
                }
                else
                {
                    search_highlight_node_ = pugi::xml_node();
                    search_highlight_attr_ = pugi::xml_attribute();
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear", ImVec2(100, 0)) || (!old_search_text.empty() && strlen(search_text_) == 0))
        {
            search_text_[0] = '\0';
            search_results_.clear();
            current_search_index_  = -1;
            search_highlight_node_ = pugi::xml_node();
            last_search_text.clear();
            if (strlen(search_text_) != 0)
                reclaim_focus = true;
        }

        static bool reclaim_focus2 = false;
        if (reclaim_focus2)
        {
            ImGui::SetKeyboardFocusHere();
            reclaim_focus2 = false;
        }
        ImGui::PushItemWidth(150);
        bool filter_triggered = ImGui::InputText("##Filter", filter_text_, IM_ARRAYSIZE(filter_text_), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::PopItemWidth();
        ImGui::SameLine();
        if (ImGui::Button("Filter", ImVec2(60, 0)) || filter_triggered)
        {
            if (filter_triggered)
                reclaim_focus2 = true;
            // Don't filter with empty string
            if (strlen(filter_text_) == 0)
            {
                // Do nothing
            }
            else
            {
                filter_active_ = true;
                if (!filter_isolate_)
                {
                    nodes_to_expand_.clear();
                    std::string filter_lower = filter_text_;
                    std::transform(filter_lower.begin(), filter_lower.end(), filter_lower.begin(), ::tolower);

                    std::function<void(pugi::xml_node)> find_and_expand = [&](pugi::xml_node node)
                    {
                        std::string name       = node.name();
                        std::string name_lower = name;
                        std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), ::tolower);

                        if (name_lower.find(filter_lower) != std::string::npos)
                        {
                            QueueNodeExpansion(node);
                            QueueSubtreeExpansion(node);
                            return;
                        }

                        for (pugi::xml_node child = node.first_child(); child; child = child.next_sibling())
                            find_and_expand(child);
                    };
                    find_and_expand(data_model_.RootNode());
                }
            }
        }
        ImGui::SameLine();
        ImGui::SameLine();
        if (ImGui::Button("Clear Filter", ImVec2(100, 0)))
        {
            filter_text_[0] = '\0';
            filter_active_  = false;
            filter_isolate_ = false;
        }
        ImGui::SameLine();
        ImGui::Checkbox("Isolate", &filter_isolate_);
        ImGui::EndGroup();

        ImGui::SameLine();
        ImGui::BeginGroup();
        if (ImGui::Button("Expand All", ImVec2(100, 0)))
            GetNodesBelow(data_model_.RootNode(), &nodes_to_expand_);
        if (ImGui::Button("Collapse All", ImVec2(100, 0)))
            GetNodesBelow(data_model_.RootNode(), &nodes_to_collapse_);
        ImGui::EndGroup();
        ImGui::SameLine(ImGui::GetWindowWidth() - 50);
        ImGui::BeginGroup();
        if (ImGui::Button("<"))
        {
            data_model_.xml_panel_width_ = std::min(1000, data_model_.xml_panel_width_ + 100);
            data_model_.SaveConfig();
        }
        ImGui::SameLine();
        if (ImGui::Button(">"))
        {
            data_model_.xml_panel_width_ = std::max(600, data_model_.xml_panel_width_ - 100);
            data_model_.SaveConfig();
        }
        ImGui::EndGroup();

        ImGui::BeginChild("node", ImVec2(-FLT_MIN, -FLT_MIN));
        ImGui::PushItemWidth(TREE_VIEW_WIDTH);
        RenderXmlSubTree(data_model_.RootNode(), 0);
        ImGui::PopItemWidth();
        ImGui::EndChild();
        ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Trajectories",
                                nullptr,
                                right_panel_default_tab_applied_ ? ImGuiTabItemFlags_None : ImGuiTabItemFlags_SetSelected))
        {
            right_panel_default_tab_applied_ = true;
            RenderTrajectoriesTab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
        }

        HandleElementContextMenu();
        HandleAttrDialog();
        HandleNodeDialog();
        HandleMovePositionMenu();
        HandleViewportContextMenu();
        HandleAddVehicleDialog();
        HandleAddTrajectoryDialog();
    }
    ImGui::End();
}

void StudioGui::HandleAttrDialog()
{
    if (attr_value_dialog_to_open)
    {
        attr_value_dialog_to_open = false;
        attr_creation_active      = true;  // Set this flag to true when opening the popup
        // Open the popup immediately
        ImGui::OpenPopup("Enter Attribute Value");
    }

    // Render the attribute value input dialog
    if (ImGui::BeginPopupModal("Enter Attribute Value", &attr_creation_active))
    {
        std::vector<std::string> enums;
        std::string type = QueryValueTypeFromSchema(data_model_.xml_schema_, modify_menu_context_node.name(), attr_to_create_name, &enums);
        ImGui::Text("Enter value for attribute '%s':", attr_to_create_name.c_str());

        bool value_handled = false;

        if (strstr(new_attr_value.c_str(), "$") != nullptr)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 64, 255));
            new_attr_value.resize(1024);
            ImGui::InputText((attr_to_create_name + " (EXPRESSION)").c_str(), new_attr_value.data(), new_attr_value.size());
            ImGui::PopStyleColor();

            // Evaluate and display result
            std::string eval_result = data_model_.EvaluateString(new_attr_value.c_str());
            if (!eval_result.empty())
            {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), " = %s", eval_result.c_str());
            }

            value_handled = true;
        }

        if (!value_handled && attr_to_create_name == "entityRef")
        {
            auto scenario_names = data_model_.GetScenarioObjectNames();
            if (!scenario_names.empty())
            {
                int current_idx = 0;
                int found_idx   = -1;
                if (!new_attr_value.empty())
                {
                    found_idx = find_enum_index(scenario_names, new_attr_value.c_str());
                    if (found_idx >= 0)
                        current_idx = found_idx;
                }
                if (found_idx < 0)
                {
                    new_attr_value = scenario_names[current_idx];
                }

                std::vector<const char*> values;
                for (auto& s : scenario_names)
                    values.push_back(s.c_str());
                if (ImGui::Combo(attr_to_create_name.c_str(), &current_idx, values.data(), enums.size()))
                {
                    new_attr_value = scenario_names[current_idx];
                }
                value_handled = true;
            }
        }

        if (!value_handled && !enums.empty())
        {
            int enum_idx = 0;
            if (!new_attr_value.empty())
            {
                enum_idx = find_enum_index(enums, new_attr_value.c_str());
                if (enum_idx < 0)
                    enum_idx = 0;
            }
            std::vector<const char*> values;
            for (auto& e : enums)
                values.push_back(e.c_str());
            if (ImGui::Combo(attr_to_create_name.c_str(), &enum_idx, values.data(), enums.size()))
            {
                new_attr_value = enums[enum_idx];
            }
            value_handled = true;
        }

        if (!value_handled && (type == "UnsignedInt" || type == "UnsignedShort"))
        {
            // step/step_fast set to 0 hides the +/- buttons and gives a plain keyboard-editable field
            int v = new_attr_value.empty() ? 0 : atoi(new_attr_value.c_str());
            if (ImGui::InputInt(attr_to_create_name.c_str(), &v, 0, 0))
            {
                if (v < 0)
                    v = 0;
                new_attr_value = std::to_string(v);
            }
            value_handled = true;
        }
        if (!value_handled && type == "Int")
        {
            int v = new_attr_value.empty() ? 0 : atoi(new_attr_value.c_str());
            if (ImGui::InputInt(attr_to_create_name.c_str(), &v, 0, 0))
            {
                new_attr_value = std::to_string(v);
            }
            value_handled = true;
        }
        if (!value_handled && (type == "Double" || type == "Float"))
        {
            double v = new_attr_value.empty() ? 0.0 : atof(new_attr_value.c_str());
            if (ImGui::InputDouble(attr_to_create_name.c_str(), &v, 0.0, 0.0, "%g", ImGuiInputTextFlags_CharsScientific))
            {
                new_attr_value = std::to_string(v);
            }
            value_handled = true;
        }
        if (!value_handled && type == "Boolean")
        {
            bool v = new_attr_value.empty() ? false : atof(new_attr_value.c_str());
            if (ImGui::Checkbox(attr_to_create_name.c_str(), &v))
            {
                new_attr_value = std::to_string(v);
                SetModified();
            }
            value_handled = true;
        }

        if (!value_handled)
        {
            new_attr_value.resize(1024);
            ImGui::InputText(attr_to_create_name.c_str(), new_attr_value.data(), 1024);
        }

        if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
            ImGui::OpenPopup("Attr Context Menu2");
        if (ImGui::BeginPopup("Attr Context Menu2"))
        {
            if (strstr(new_attr_value.c_str(), "$"))
            {
                if (ImGui::MenuItem("Reset to Value"))
                {
                    new_attr_value.clear();
                    SetModified();
                    ImGui::CloseCurrentPopup();
                }
            }
            else if (ImGui::MenuItem("Convert to Expression"))
            {
                new_attr_value = std::string("${") + new_attr_value + std::string("}");
                SetModified();
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::IsKeyPressedMap(ImGuiKey_Escape))
            {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        ImGui::Dummy(ImVec2(0.0f, ImGui::GetFrameHeightWithSpacing()));

        ImGuiStyle& style        = ImGui::GetStyle();
        float       ok_width     = ImGui::CalcTextSize("OK").x + style.FramePadding.x * 2.0f;
        float       cancel_width = ImGui::CalcTextSize("Cancel").x + style.FramePadding.x * 2.0f;
        float       total_width  = ok_width + style.ItemSpacing.x + cancel_width;
        float       available    = ImGui::GetContentRegionAvail().x;
        float       cursor_x     = ImGui::GetCursorPosX();
        if (total_width < available)
        {
            float new_pos = cursor_x + (available - total_width) * 0.5f;
            if (new_pos < cursor_x)
                new_pos = cursor_x;
            ImGui::SetCursorPosX(new_pos);
        }

        bool ok_confirmed = ImGui::Button("OK", ImVec2(ok_width, 0.0f));
        if (ImGui::IsKeyPressedMap(ImGuiKey_Enter))
            ok_confirmed = true;
        if (ok_confirmed)
        {
            std::string final_value = new_attr_value.c_str();
            data_model_.AddAttribute(modify_menu_context_node, attr_to_create_name, final_value);
            new_attr_value = final_value;
            // LOG("Attribute [%s] with value [%s] created for node [%s]",
            //     attr_to_create_name.c_str(),
            //     final_value.c_str(),
            //     modify_menu_context_node.name());

            attr_creation_active = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        bool cancel_confirmed = ImGui::Button("Cancel", ImVec2(cancel_width, 0.0f));
        if (ImGui::IsKeyPressedMap(ImGuiKey_Escape))
            cancel_confirmed = true;
        if (cancel_confirmed)
        {
            attr_creation_active = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void StudioGui::HandleNodeDialog()
{
    // Handle node creation dialog opening
    if (node_creation_dialog_to_open)
    {
        node_creation_dialog_to_open = false;
        node_creation_active         = true;

        // Create temporary node for editing
        temp_doc.reset();
        pugi::xml_node temp_root = temp_doc.append_child("temp");

        // Create temporary node and populate structure, using CreateNode to ensure required attributes
        // and child elements are added
        pugi::xml_node new_temp_node = data_model_.CreateNode(temp_root, node_to_create_name, modify_menu_context_node);

        // Open the popup immediately
        ImGui::OpenPopup("Create Node");
    }

    // Render the node creation dialog with RenderXmlTree
    ImGui::SetNextWindowSize(ImVec2(800.0f, 600.0f), ImGuiCond_FirstUseEver);
    if (ImGui::BeginPopupModal("Create Node", &node_creation_active, ImGuiWindowFlags_NoScrollbar))
    {
        ImGui::Text("Configure new '%s' node:", node_to_create_name.c_str());

        // Use RenderXmlTree to display and edit the temporary node
        if (temp_doc.child("temp").first_child())
        {
            ImGui::PushItemWidth(TREE_VIEW_WIDTH);
            ImGui::SetNextItemOpen(true);
            RenderXmlSubTree(temp_doc.child("temp").first_child(), 0, true, true, 99);
            ImGui::PopItemWidth();
        }

        auto elements_to_remove = QuerySubelementsWithModelInfo(data_model_.xml_schema_, modify_menu_context);
        FindElementsToRemove(data_model_.xml_schema_, modify_menu_context_node, &elements_to_remove, node_to_create_name);
        if (!elements_to_remove.empty())
        {
            ImGui::NewLine();
            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
            ImGui::TextUnformatted("Conflicted nodes that will be REMOVED:");
            ImGui::Indent();
            for (const auto& info : elements_to_remove)
                ImGui::TextUnformatted(info.xsd_path.back().attribute("name").value());
            ImGui::Unindent();
            ImGui::PopStyleColor();
        }

        float footer_height = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().WindowPadding.y;
        float target_y      = ImGui::GetWindowContentRegionMax().y - footer_height;
        if (target_y > ImGui::GetCursorPosY())
            ImGui::SetCursorPosY(target_y);

        ImGuiStyle& style        = ImGui::GetStyle();
        float       ok_width     = ImGui::CalcTextSize("OK").x + style.FramePadding.x * 2.0f;
        float       cancel_width = ImGui::CalcTextSize("Cancel").x + style.FramePadding.x * 2.0f;
        float       total_width  = ok_width + style.ItemSpacing.x + cancel_width;
        float       available    = ImGui::GetContentRegionAvail().x;
        float       cursor_x     = ImGui::GetCursorPosX();
        float       new_pos      = cursor_x + (available - total_width) * 0.5f;
        if (new_pos < cursor_x)
            new_pos = cursor_x;
        ImGui::SetCursorPosX(new_pos);

        bool ok_confirmed = elements_to_remove.empty() || ImGui::Button("OK", ImVec2(ok_width, 0.0f));
        if (ImGui::IsKeyPressedMap(ImGuiKey_Enter))
            ok_confirmed = true;
        pugi::xml_node source_node = temp_doc.child("temp").first_child();
        if (ok_confirmed)
        {
            pugi::xml_node new_node = modify_menu_context_node.append_copy(source_node);

            QueueNodeExpansion(new_node);
            QueueSubtreeExpansion(new_node);
            node_to_scroll_to_ = new_node;

            // LOG("Node [%s] created with attributes", new_node.name());
            RemoveElements(data_model_.xml_schema_, modify_menu_context_node, elements_to_remove);
            SetModified();

            node_creation_active = false;
            ImGui::CloseCurrentPopup();
            if (strcmp(source_node.name(), "LanePosition") == 0 || strcmp(source_node.name(), "WorldPosition") == 0)
            {
                data_model_.ExtractPositionsRecursive(data_model_.xml_doc_, "", &extracted_positions_);
                auto iter = std::find_if(extracted_positions_.begin(),
                                         extracted_positions_.end(),
                                         [&](const PositionInfo& info) { return info.node == new_node; });
                if (iter != extracted_positions_.end())
                {
                    last_picked_position_info_ = *iter;
                    StartMoveOperation(&last_picked_position_info_);
                }
            }
        }
        ImGui::SameLine();
        bool cancelled = ImGui::Button("Cancel", ImVec2(cancel_width, 0.0f));
        if (ImGui::IsKeyPressedMap(ImGuiKey_Escape))
            cancelled = true;
        if (cancelled)
        {
            node_creation_active = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void StudioGui::HandleMovePositionMenu()
{
    // Check for move context menu to open
    if (move_context_menu_to_open_)
    {
        move_context_menu_to_open_ = false;
        ImGui::OpenPopup("Move Context Menu");
    }

    // Render the move context menu
    if (ImGui::BeginPopup("Move Context Menu"))
    {
        if (ImGui::MenuItem("Move"))
        {
            StartMoveOperation(&last_picked_position_info_);
        }

        // Heading editing is only supported for LanePosition and WorldPosition
        bool can_modify_heading =
            (last_picked_position_info_.type == PositionType::LANE_POSITION || last_picked_position_info_.type == PositionType::WORLD_POSITION);
        if (ImGui::MenuItem("Modify Heading", nullptr, false, can_modify_heading))
        {
            StartHeadingOperation(last_picked_position_info_);
        }

        const std::string entity_name = last_picked_position_info_.name;
        bool              is_ego      = (entity_name == "ego" || entity_name == "Ego" || entity_name == "EGO");

        if (ImGui::MenuItem("Clone", nullptr, false, !entity_name.empty()))
        {
            std::string new_name = data_model_.CloneEntity(entity_name);
            if (!new_name.empty())
            {
                scenario_object_map_dirty_ = true;
                ClearXmlHighlights();
                ExtractPositionsFromXml();

                // Pick the clone's init position and let the mouse move it
                auto iter = std::find_if(extracted_positions_.begin(),
                                         extracted_positions_.end(),
                                         [&](const PositionInfo& info) { return info.name == new_name; });
                if (iter != extracted_positions_.end())
                {
                    for (auto& info : extracted_positions_)
                        info.selected = false;
                    iter->selected             = true;
                    last_picked_position_info_ = *iter;
                    HighlightXmlNodeForPosition(*iter);
                    StartMoveOperation(&last_picked_position_info_);
                }
                data_model_.markers_to_update_ = true;
            }
        }

        // Ego must not be renamed
        if (ImGui::MenuItem("Rename", nullptr, false, !entity_name.empty() && !is_ego))
        {
            rename_old_name       = entity_name;
            rename_new_name       = entity_name;
            rename_dialog_to_open = true;
        }

        // Ego must not be deleted
        if (ImGui::MenuItem("Delete", nullptr, false, !entity_name.empty() && !is_ego))
        {
            if (data_model_.DeleteEntity(entity_name))
            {
                scenario_object_map_dirty_ = true;
                ClearXmlHighlights();
                last_picked_position_info_ = PositionInfo();
                ExtractPositionsFromXml();
                data_model_.markers_to_update_ = true;
            }
        }
        ImGui::EndPopup();
    }

    // Handle the entity rename dialog (deferred open, mirroring HandleAttrDialog)
    if (rename_dialog_to_open)
    {
        rename_dialog_to_open = false;
        rename_dialog_active  = true;
        ImGui::OpenPopup("Rename Entity");
    }

    ImGuiIO& io     = ImGui::GetIO();
    ImVec2   center = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("Rename Entity", &rename_dialog_active, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("Rename to");
        ImGui::SameLine();
        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere();
        rename_new_name.resize(256);
        ImGui::InputText("##rename_to", rename_new_name.data(), rename_new_name.size(), ImGuiInputTextFlags_AutoSelectAll);

        std::string new_name     = rename_new_name.c_str();
        bool        is_duplicate = false;
        if (new_name != rename_old_name)
        {
            auto names   = data_model_.GetScenarioObjectNames();
            is_duplicate = (std::find(names.begin(), names.end(), new_name) != names.end());
        }

        if (is_duplicate)
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "An entity named '%s' already exists.", new_name.c_str());

        bool can_confirm = !new_name.empty() && new_name != rename_old_name && !is_duplicate;

        if (!can_confirm)
            ImGui::BeginDisabled(true);
        bool confirmed = ImGui::Button("Confirm", ImVec2(120, 0));
        if (!can_confirm)
            ImGui::EndDisabled();
        if (can_confirm && ImGui::IsKeyPressedMap(ImGuiKey_Enter))
            confirmed = true;

        if (confirmed)
        {
            if (data_model_.RenameEntity(rename_old_name, new_name))
            {
                scenario_object_map_dirty_ = true;
                ClearXmlHighlights();
                last_picked_position_info_ = PositionInfo();
                ExtractPositionsFromXml();
                data_model_.markers_to_update_ = true;
            }
            rename_dialog_active = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0)) || ImGui::IsKeyPressedMap(ImGuiKey_Escape))
        {
            rename_dialog_active = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

std::vector<std::string> StudioGui::GetVehicleCatalogEntryNames() const
{
    std::vector<std::string> names;

    pugi::xml_node catalog_locations = data_model_.RootNode().child("CatalogLocations");
    if (!catalog_locations)
        return names;

    // Resolve the VehicleCatalog directory path
    std::string    catalog_dir;
    pugi::xml_node vehicle_catalog = catalog_locations.child("VehicleCatalog");
    if (vehicle_catalog)
    {
        pugi::xml_node dir_node = vehicle_catalog.child("Directory");
        if (dir_node)
            catalog_dir = dir_node.attribute("path").as_string("");
    }

    if (catalog_dir.empty())
        return names;

    // Build candidate paths for the VehicleCatalog.xosc file, mirroring GetModelIdFromCatalogEntry
    std::vector<std::string> candidate_paths;
    std::string              scenario_dir = DirNameOf(data_model_.xosc_path_);
    if (!scenario_dir.empty())
        candidate_paths.push_back(scenario_dir + "/" + catalog_dir + "/VehicleCatalog.xosc");
    candidate_paths.push_back(catalog_dir + "/VehicleCatalog.xosc");
    for (const auto& env_path : SE_Env::Inst().GetPaths())
        candidate_paths.push_back(env_path + "/" + catalog_dir + "/VehicleCatalog.xosc");

    // Load the first VehicleCatalog.xosc that exists and parses successfully
    pugi::xml_document catalog_doc;
    bool               loaded = false;
    for (const auto& path : candidate_paths)
    {
        if (FileExists(path.c_str()) && catalog_doc.load_file(path.c_str()))
        {
            loaded = true;
            break;
        }
    }

    if (!loaded)
        return names;

    // Collect the name attribute of every Vehicle under OpenSCENARIO > Catalog
    pugi::xml_node catalog_root = catalog_doc.child("OpenSCENARIO");
    if (!catalog_root)
        return names;

    for (pugi::xml_node catalog : catalog_root.children("Catalog"))
    {
        for (pugi::xml_node vehicle : catalog.children("Vehicle"))
        {
            std::string vehicle_name = vehicle.attribute("name").as_string("");
            if (!vehicle_name.empty())
                names.push_back(vehicle_name);
        }
    }

    return names;
}

void StudioGui::OpenAddVehicleDialog()
{
    // Determine the ego entity's catalog reference, used as the fallback entry name
    std::string    ego_catalog_name = "VehicleCatalog";
    std::string    ego_entry_name;
    pugi::xml_node entities = data_model_.RootNode().child("Entities");
    for (pugi::xml_node obj : entities.children("ScenarioObject"))
    {
        std::string obj_name = obj.attribute("name").as_string("");
        if (obj_name == "ego" || obj_name == "Ego" || obj_name == "EGO")
        {
            pugi::xml_node catalog_ref = obj.child("CatalogReference");
            if (catalog_ref)
            {
                ego_catalog_name = catalog_ref.attribute("catalogName").as_string(ego_catalog_name.c_str());
                ego_entry_name   = catalog_ref.attribute("entryName").as_string("");
            }
            break;
        }
    }

    add_vehicle_catalog_name_ = ego_catalog_name;

    // Populate the entry-name dropdown from VehicleCatalog.xosc, falling back to the ego entry name
    add_vehicle_entry_options_ = GetVehicleCatalogEntryNames();
    if (add_vehicle_entry_options_.empty() && !ego_entry_name.empty())
        add_vehicle_entry_options_.push_back(ego_entry_name);

    // Default the selection to the ego entry when available, otherwise the first option
    if (!add_vehicle_entry_options_.empty())
    {
        auto it = std::find(add_vehicle_entry_options_.begin(), add_vehicle_entry_options_.end(), ego_entry_name);
        add_vehicle_entry_name_ = (it != add_vehicle_entry_options_.end()) ? *it : add_vehicle_entry_options_.front();
    }
    else
    {
        add_vehicle_entry_name_ = ego_entry_name;
    }

    // Suggest a unique default name and reset the init speed
    add_vehicle_name_       = data_model_.EnsureUniqueName("v_1", pugi::xml_node());
    add_vehicle_init_speed_ = 0.0f;

    add_vehicle_dialog_to_open_ = true;
}

void StudioGui::HandleViewportContextMenu()
{
    if (add_vehicle_context_menu_to_open_)
    {
        add_vehicle_context_menu_to_open_ = false;
        ImGui::OpenPopup("Viewport Context Menu");
    }

    if (ImGui::BeginPopup("Viewport Context Menu"))
    {
        if (ImGui::MenuItem("Add Vehicle"))
        {
            OpenAddVehicleDialog();
        }
        if (ImGui::MenuItem("Add Trajectory"))
        {
            OpenAddTrajectoryDialog();
        }
        ImGui::EndPopup();
    }

    // Right-click Delete Point, for a click on/near an existing path point (Trajectory_Editing.md 7.4).
    if (trajectory_point_context_menu_to_open_)
    {
        trajectory_point_context_menu_to_open_ = false;
        ImGui::OpenPopup("Trajectory Point Context Menu");
    }
    if (ImGui::BeginPopup("Trajectory Point Context Menu"))
    {
        auto it = data_model_.entity_trajectories_.find(trajectory_context_entity_name_);
        if (it != data_model_.entity_trajectories_.end() && trajectory_context_point_index_ >= 0 &&
            static_cast<size_t>(trajectory_context_point_index_) < it->second.path_.points_.size())
        {
            // Continue picking, appending new points onto the end of the path until Esc/Enter (mirrors Add
            // Trajectory's picking loop); which point was actually right-clicked doesn't matter, since new
            // points are always appended at the end.
            if (ImGui::MenuItem("Append Point"))
            {
                StartTrajectoryAppending(trajectory_context_entity_name_);
            }

            bool can_delete = it->second.path_.points_.size() > 1;  // never leave a path with 0 points
            if (!can_delete)
                ImGui::BeginDisabled(true);
            if (ImGui::MenuItem("Delete Point") && can_delete)
            {
                it->second.path_.RemovePoint(trajectory_context_point_index_);
                it->second.SyncSpeedProfileEndpoints();  // removing a point changes the path's total length
                data_model_.trajectories_modified_ = true;
                trajectory_renderer_.MarkDirty(trajectory_context_entity_name_);
                if (trajectory_point_selected_ && trajectory_selected_entity_name_ == trajectory_context_entity_name_ &&
                    trajectory_selected_point_index_ == trajectory_context_point_index_)
                {
                    trajectory_point_selected_       = false;
                    trajectory_selected_point_index_ = -1;
                    trajectory_selected_entity_name_.clear();
                }
                ResolveEntityKeyframes(trajectory_context_entity_name_);  // arc lengths changed
                data_model_.PushTrajectoryUndoState(CaptureTrajectorySelectionSnapshot());
            }
            if (!can_delete)
                ImGui::EndDisabled();
        }
        else
        {
            ImGui::TextDisabled("Point no longer exists");
        }
        ImGui::EndPopup();
    }

    // Right-click Insert Point, for a click near an existing path but not on a point (Trajectory_Editing.md
    // 6.3-style path editing, extended to existing paths rather than only the initial pick session).
    if (trajectory_insert_context_menu_to_open_)
    {
        trajectory_insert_context_menu_to_open_ = false;
        ImGui::OpenPopup("Trajectory Insert Context Menu");
    }
    if (ImGui::BeginPopup("Trajectory Insert Context Menu"))
    {
        if (!trajectory_insert_context_allow_insert_)
            ImGui::BeginDisabled(true);
        if (ImGui::MenuItem("Insert Point") && trajectory_insert_context_allow_insert_)
        {
            auto it = data_model_.entity_trajectories_.find(trajectory_insert_context_entity_name_);
            if (it != data_model_.entity_trajectories_.end())
            {
                EntityPose pose;
                pose.x = trajectory_insert_context_x_;
                pose.y = trajectory_insert_context_y_;
                pose.z = trajectory_insert_context_z_;
                pose.h = 0.0;
                pose.SyncFromWorld(/*align_to_lane=*/true);
                pose.SyncFromLane();  // snap x/y/z onto the matched lane, same as CommitTrajectoryPickingPoint()

                it->second.path_.InsertPoint(trajectory_insert_context_s_, pose);
                it->second.SyncSpeedProfileEndpoints();  // inserting a point changes the path's total length
                data_model_.trajectories_modified_ = true;
                trajectory_renderer_.MarkDirty(trajectory_insert_context_entity_name_);
                ResolveEntityKeyframes(trajectory_insert_context_entity_name_);  // arc lengths changed
                data_model_.PushTrajectoryUndoState(CaptureTrajectorySelectionSnapshot());
            }
        }
        if (!trajectory_insert_context_allow_insert_)
            ImGui::EndDisabled();
        ImGui::Separator();
        if (ImGui::MenuItem("Delete Trajectory"))
        {
            DeleteEntityTrajectory(trajectory_insert_context_entity_name_);
        }
        ImGui::EndPopup();
    }

    // Right-click Delete Keyframe, for a click on a keyframe diamond marker (Trajectory_Editing_Enhancement.md 7.2).
    if (trajectory_keyframe_context_menu_to_open_)
    {
        trajectory_keyframe_context_menu_to_open_ = false;
        ImGui::OpenPopup("Trajectory Keyframe Context Menu");
    }
    if (ImGui::BeginPopup("Trajectory Keyframe Context Menu"))
    {
        auto it = data_model_.entity_trajectories_.find(trajectory_keyframe_context_entity_);
        if (it != data_model_.entity_trajectories_.end() && trajectory_keyframe_context_index_ >= 0 &&
            static_cast<size_t>(trajectory_keyframe_context_index_) < it->second.keyframes_.size())
        {
            char label[64];
            snprintf(label,
                     sizeof(label),
                     "Delete Keyframe (t=%.2fs)",
                     it->second.keyframes_[static_cast<size_t>(trajectory_keyframe_context_index_)].t);
            if (ImGui::MenuItem(label))
            {
                // RemoveKeyframe also deletes the keyframe's pin node from the speed profile and re-solves
                // the remaining chain (Trajectory_Editing_Enhancement.md 12.3).
                it->second.RemoveKeyframe(trajectory_keyframe_context_index_);
                data_model_.trajectories_modified_ = true;
                trajectory_renderer_.MarkDirty(trajectory_keyframe_context_entity_);
                data_model_.PushTrajectoryUndoState(CaptureTrajectorySelectionSnapshot());
            }
        }
        else
        {
            ImGui::TextDisabled("Keyframe no longer exists");
        }
        ImGui::EndPopup();
    }
}

void StudioGui::HandleAddVehicleDialog()
{
    if (add_vehicle_dialog_to_open_)
    {
        add_vehicle_dialog_to_open_ = false;
        add_vehicle_dialog_active_  = true;
        ImGui::OpenPopup("Add Vehicle");
    }

    ImGuiIO& io     = ImGui::GetIO();
    ImVec2   center = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("Add Vehicle", &add_vehicle_dialog_active_, ImGuiWindowFlags_AlwaysAutoResize))
    {
        // Vehicle type (entryName) dropdown
        ImGui::TextUnformatted("Vehicle Type (entryName)");
        if (ImGui::BeginCombo("##add_vehicle_entry", add_vehicle_entry_name_.c_str()))
        {
            for (const auto& entry : add_vehicle_entry_options_)
            {
                bool is_selected = (entry == add_vehicle_entry_name_);
                if (ImGui::Selectable(entry.c_str(), is_selected))
                    add_vehicle_entry_name_ = entry;
                if (is_selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        // Entity name input
        ImGui::TextUnformatted("Name");
        add_vehicle_name_.resize(256);
        ImGui::InputText("##add_vehicle_name", add_vehicle_name_.data(), add_vehicle_name_.size());
        std::string entered_name = add_vehicle_name_.c_str();

        // Init speed input
        ImGui::TextUnformatted("Init Speed (m/s)");
        ImGui::InputFloat("##add_vehicle_speed", &add_vehicle_init_speed_, 0.0f, 0.0f, "%.2f");

        // Validate the entity name (must be non-empty and not clash with an existing ScenarioObject)
        bool                     name_empty     = entered_name.empty();
        std::vector<std::string> existing_names = data_model_.GetScenarioObjectNames();
        bool is_duplicate = std::find(existing_names.begin(), existing_names.end(), entered_name) != existing_names.end();
        bool can_confirm  = !name_empty && !is_duplicate && !add_vehicle_entry_name_.empty();

        if (name_empty)
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Name cannot be empty.");
        else if (is_duplicate)
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "An entity named '%s' already exists.", entered_name.c_str());

        if (!can_confirm)
            ImGui::BeginDisabled(true);
        bool confirmed = ImGui::Button("Confirm", ImVec2(120, 0));
        if (!can_confirm)
            ImGui::EndDisabled();

        ImGui::SameLine();
        bool cancelled = ImGui::Button("Cancel", ImVec2(120, 0)) || ImGui::IsKeyPressedMap(ImGuiKey_Escape);
        // Pressing Enter confirms the dialog, but only while the Confirm button is enabled
        if (can_confirm && ImGui::IsKeyPressedMap(ImGuiKey_Enter))
            confirmed = true;

        if (confirmed && can_confirm)
        {
            std::string new_name =
                data_model_.AddVehicle(entered_name, add_vehicle_catalog_name_, add_vehicle_entry_name_, static_cast<double>(add_vehicle_init_speed_));
            if (!new_name.empty())
            {
                scenario_object_map_dirty_ = true;
                ClearXmlHighlights();
                ExtractPositionsFromXml();

                // Select the new entity's init position and enter the viewport drag state to place it
                auto iter = std::find_if(extracted_positions_.begin(),
                                         extracted_positions_.end(),
                                         [&](const PositionInfo& info) { return info.name == new_name; });
                if (iter != extracted_positions_.end())
                {
                    for (auto& info : extracted_positions_)
                        info.selected = false;
                    iter->selected             = true;
                    last_picked_position_info_ = *iter;
                    HighlightXmlNodeForPosition(*iter);
                    StartMoveOperation(&last_picked_position_info_);
                }
                data_model_.markers_to_update_ = true;
            }
            add_vehicle_dialog_active_ = false;
            ImGui::CloseCurrentPopup();
        }
        else if (cancelled)
        {
            add_vehicle_dialog_active_ = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Trajectory editing (Trajectory_Editing.md): "Add Trajectory" creates a brand-new vehicle that only ever
// exists in entity_trajectories_ / .traj.json, entirely independent from Add Vehicle / xml_doc_ (section 6).
// ---------------------------------------------------------------------------------------------------------------

void StudioGui::OpenAddTrajectoryDialog()
{
    // Suggest a name unique among entity_trajectories_ only - trajectory names don't need to avoid clashing
    // with xosc ScenarioObject names (Trajectory_Editing.md 6.1/12: the two namespaces are independent).
    int         suffix = 1;
    std::string candidate;
    do
    {
        candidate = "traj_" + std::to_string(suffix++);
    } while (data_model_.entity_trajectories_.count(candidate) > 0);

    add_trajectory_name_        = candidate;
    add_trajectory_init_speed_  = 0.0f;
    add_trajectory_interp_mode_ = 1;  // Spline by default
    // Defaults to wherever the timeline is currently scrubbed to, so a trajectory created while previewing
    // later in the scenario appears there rather than always at t=0 (Trajectory_Editing.md, "delayed
    // appearance" design discussion); still freely editable afterwards via the Trajectories tab.
    add_trajectory_start_time_ = data_model_.virtual_time_;

    // Vehicle Type dropdown (mirrors OpenAddVehicleDialog(), independent state): defaults to "car_white" (the
    // renderer's previous fixed default, Trajectory_Editing.md 7.2) when present, else the first entry.
    add_trajectory_entry_options_ = GetVehicleCatalogEntryNames();
    const std::string kDefaultEntry = "car_white";
    auto              it = std::find(add_trajectory_entry_options_.begin(), add_trajectory_entry_options_.end(), kDefaultEntry);
    if (it != add_trajectory_entry_options_.end())
        add_trajectory_entry_name_ = *it;
    else if (!add_trajectory_entry_options_.empty())
        add_trajectory_entry_name_ = add_trajectory_entry_options_.front();
    else
        add_trajectory_entry_name_.clear();

    add_trajectory_dialog_to_open_ = true;
}

void StudioGui::HandleAddTrajectoryDialog()
{
    if (add_trajectory_dialog_to_open_)
    {
        add_trajectory_dialog_to_open_ = false;
        add_trajectory_dialog_active_  = true;
        ImGui::OpenPopup("Add Trajectory");
    }

    ImGuiIO& io     = ImGui::GetIO();
    ImVec2   center = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("Add Trajectory", &add_trajectory_dialog_active_, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("This creates a brand-new vehicle stored only in .traj.json, not in the OpenSCENARIO file.");

        // Vehicle type (entryName) dropdown, same pattern as Add Vehicle (independent state/options).
        ImGui::TextUnformatted("Vehicle Type (entryName)");
        if (ImGui::BeginCombo("##add_trajectory_entry", add_trajectory_entry_name_.c_str()))
        {
            for (const auto& entry : add_trajectory_entry_options_)
            {
                bool is_selected = (entry == add_trajectory_entry_name_);
                if (ImGui::Selectable(entry.c_str(), is_selected))
                    add_trajectory_entry_name_ = entry;
                if (is_selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        // Entity name input
        ImGui::TextUnformatted("Name");
        add_trajectory_name_.resize(256);
        ImGui::InputText("##add_trajectory_name", add_trajectory_name_.data(), add_trajectory_name_.size());
        std::string entered_name = add_trajectory_name_.c_str();

        // Init speed input (becomes the sole initial speed_profile_ point, unrelated to any xosc SpeedAction)
        ImGui::TextUnformatted("Init Speed (m/s)");
        ImGui::InputFloat("##add_trajectory_speed", &add_trajectory_init_speed_, 0.0f, 0.0f, "%.2f");

        // Path interpolation mode, default Spline (Trajectory_Editing.md 6.1/9.1)
        ImGui::TextUnformatted("Path Interpolation");
        const char* interp_labels[] = {"Linear", "Spline", "Clothoid"};
        ImGui::Combo("##add_trajectory_interp", &add_trajectory_interp_mode_, interp_labels, IM_ARRAYSIZE(interp_labels));

        // Start Time: the global virtual_time_ at which this vehicle first appears (defaults to wherever the
        // timeline was scrubbed to when this dialog was opened, but freely editable, and adjustable
        // afterwards via the Trajectories tab too).
        ImGui::TextUnformatted("Start Time (s)");
        ImGui::InputFloat("##add_trajectory_start_time", &add_trajectory_start_time_, 0.0f, 0.0f, "%.2f");
        if (add_trajectory_start_time_ < 0.0f)
            add_trajectory_start_time_ = 0.0f;

        // Validate uniqueness among entity_trajectories_ only - trajectory names are independent of xosc
        // ScenarioObject names (section 6.1/12).
        bool name_empty   = entered_name.empty();
        bool is_duplicate = !name_empty && data_model_.entity_trajectories_.count(entered_name) > 0;
        bool can_confirm  = !name_empty && !is_duplicate;

        if (name_empty)
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Name cannot be empty.");
        else if (is_duplicate)
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "A trajectory named '%s' already exists.", entered_name.c_str());

        if (!can_confirm)
            ImGui::BeginDisabled(true);
        bool confirmed = ImGui::Button("Confirm", ImVec2(120, 0));
        if (!can_confirm)
            ImGui::EndDisabled();

        ImGui::SameLine();
        bool cancelled = ImGui::Button("Cancel", ImVec2(120, 0)) || ImGui::IsKeyPressedMap(ImGuiKey_Escape);
        if (can_confirm && ImGui::IsKeyPressedMap(ImGuiKey_Enter))
            confirmed = true;

        if (confirmed && can_confirm)
        {
            EntityTrajectory& traj = data_model_.CreateEntityTrajectory(entered_name);
            traj.path_.interp_mode_ =
                (add_trajectory_interp_mode_ == 0)   ? EntityPath::InterpMode::LINEAR
                : (add_trajectory_interp_mode_ == 2) ? EntityPath::InterpMode::CLOTHOID
                                                      : EntityPath::InterpMode::CATMULL_ROM;
            traj.vehicle_catalog_entry_name_ = add_trajectory_entry_name_;
            traj.start_time_                 = static_cast<double>(add_trajectory_start_time_);

            // Start and end speed points are always created together: the start's s is fixed at 0, the end's
            // s is kept in sync with the path's total length as points are picked/dragged (both are 0 for now
            // since the path is still empty). Only the intermediate points a user adds later have a free s.
            SpeedProfilePoint start_point;
            start_point.s     = 0.0;
            start_point.speed = static_cast<double>(add_trajectory_init_speed_);
            traj.speed_profile_.points_.push_back(start_point);

            SpeedProfilePoint end_point;
            end_point.s     = 0.0;
            end_point.speed = static_cast<double>(add_trajectory_init_speed_);
            traj.speed_profile_.points_.push_back(end_point);

            StartTrajectoryPicking(entered_name);

            add_trajectory_dialog_active_ = false;
            ImGui::CloseCurrentPopup();
        }
        else if (cancelled)
        {
            add_trajectory_dialog_active_ = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void StudioGui::StartTrajectoryPicking(const std::string& entity_name)
{
    // Mutually exclusive with the other viewport interaction modes (Trajectory_Editing.md 6.2): cancel
    // whichever one happens to be active rather than letting two interactions run at once.
    if (heading_operation_active_)
        CancelHeadingOperation();
    if (move_operation_active_)
        EndMoveOperation();
    if (trajectory_point_drag_active_)
        EndTrajectoryPointDrag();

    trajectory_picking_active_       = true;
    trajectory_picking_entity_name_  = entity_name;
    trajectory_picking_is_append_    = false;
}

void StudioGui::StartTrajectoryAppending(const std::string& entity_name)
{
    auto it = data_model_.entity_trajectories_.find(entity_name);
    if (it == data_model_.entity_trajectories_.end())
        return;

    StartTrajectoryPicking(entity_name);  // shared mutual-exclusivity + base state setup
    trajectory_picking_is_append_         = true;
    trajectory_picking_start_point_count_ = it->second.path_.points_.size();
    trajectory_picking_start_path_length_ = it->second.path_.GetTotalLength();
}

void StudioGui::CommitTrajectoryPickingPoint()
{
    if (!trajectory_picking_active_)
        return;

    auto it = data_model_.entity_trajectories_.find(trajectory_picking_entity_name_);
    if (it == data_model_.entity_trajectories_.end())
    {
        LOG("Trajectory picking: entity [%s] disappeared while picking, aborting", trajectory_picking_entity_name_.c_str());
        trajectory_picking_active_ = false;
        return;
    }

    UpdateMousePositionFromWorld();

    EntityPose pose;
    pose.x = hud_mouse_world_x_;
    pose.y = hud_mouse_world_y_;
    pose.z = hud_mouse_world_z_;
    pose.h = 0.0;
    pose.SyncFromWorld(/*align_to_lane=*/true);
    pose.SyncFromLane();  // snap x/y/z onto the matched lane so the point sits on the road surface

    it->second.path_.points_.push_back(pose);
    it->second.SyncSpeedProfileEndpoints();  // the end speed point's s tracks the path's total length
    data_model_.trajectories_modified_ = true;
    trajectory_renderer_.MarkDirty(trajectory_picking_entity_name_);
}

void StudioGui::FinishTrajectoryPicking()
{
    if (!trajectory_picking_active_)
        return;

    auto it = data_model_.entity_trajectories_.find(trajectory_picking_entity_name_);
    if (it == data_model_.entity_trajectories_.end() || it->second.path_.points_.empty())
    {
        // Enter with zero points picked so far is ignored (Trajectory_Editing.md 6.1/15).
        return;
    }

    if (trajectory_picking_is_append_ && it->second.path_.points_.size() <= trajectory_picking_start_point_count_)
    {
        // Append session with no newly-added points yet: ignore Enter, same rule as above.
        return;
    }

    trajectory_picking_active_ = false;
    trajectory_renderer_.ClearPickingPreview();

    if (trajectory_picking_is_append_)
    {
        // Keep the newly appended path segment moving at the previous constant tail speed, adding at most one
        // new speed profile point: if the profile was still actively transitioning right at the old path end
        // (last two points differ by more than 0.3 m/s), freeze that transition there with a new point at the
        // OLD path length; otherwise the tail was already essentially flat and no extra point is needed.
        EntitySpeedProfile& sp = it->second.speed_profile_;
        if (sp.points_.size() >= 2)
        {
            size_t n    = sp.points_.size();
            double diff = std::abs(sp.points_[n - 1].speed - sp.points_[n - 2].speed);
            if (diff > 0.3)
                sp.InsertPoint(trajectory_picking_start_path_length_, sp.points_[n - 1].speed);
        }
        it->second.SyncSpeedProfileEndpoints();
        data_model_.trajectories_modified_ = true;
        trajectory_renderer_.MarkDirty(trajectory_picking_entity_name_);
        ResolveEntityKeyframes(trajectory_picking_entity_name_);
    }

    // Whole Add Trajectory / Append Point session = one undo step (Trajectory_Editing.md section 11.5); the
    // Esc-based per-point undo during picking (CancelTrajectoryPickingPoint()) is a separate, local-only
    // mechanism that never touches the global trajectory undo stack.
    data_model_.PushTrajectoryUndoState(CaptureTrajectorySelectionSnapshot());
}

void StudioGui::CancelTrajectoryPickingPoint()
{
    if (!trajectory_picking_active_)
        return;

    auto   it            = data_model_.entity_trajectories_.find(trajectory_picking_entity_name_);
    size_t baseline_count = trajectory_picking_is_append_ ? trajectory_picking_start_point_count_ : 0;
    bool   has_new_point  = it != data_model_.entity_trajectories_.end() && it->second.path_.points_.size() > baseline_count;

    if (has_new_point)
    {
        it->second.path_.points_.pop_back();
        it->second.SyncSpeedProfileEndpoints();  // the end speed point's s tracks the path's total length
        trajectory_renderer_.MarkDirty(trajectory_picking_entity_name_);
        return;
    }

    if (trajectory_picking_is_append_)
    {
        // Appending to an already-existing entity: nothing new was added this session, so just abort without
        // touching any of its pre-existing points (never destroy the entity, unlike the Add Trajectory case).
        trajectory_picking_active_ = false;
        trajectory_renderer_.ClearPickingPreview();
        return;
    }

    // No point left to undo: abandon the whole new trajectory (Trajectory_Editing.md 6.1).
    data_model_.RemoveEntityTrajectory(trajectory_picking_entity_name_);
    trajectory_renderer_.RemoveEntity(trajectory_picking_entity_name_);
    trajectory_picking_active_ = false;
    trajectory_renderer_.ClearPickingPreview();
}

void StudioGui::DeleteEntityTrajectory(const std::string& entity_name)
{
    data_model_.RemoveEntityTrajectory(entity_name);
    trajectory_renderer_.RemoveEntity(entity_name);

    if (trajectory_picking_active_ && trajectory_picking_entity_name_ == entity_name)
    {
        trajectory_picking_active_ = false;
        trajectory_renderer_.ClearPickingPreview();
    }
    if (trajectory_point_selected_ && trajectory_selected_entity_name_ == entity_name)
    {
        trajectory_point_selected_       = false;
        trajectory_selected_point_index_ = -1;
        trajectory_selected_entity_name_.clear();
    }
    if (trajectory_point_drag_active_ && trajectory_drag_entity_name_ == entity_name)
    {
        trajectory_point_drag_active_ = false;
        trajectory_drag_point_index_  = -1;
        trajectory_drag_entity_name_.clear();
    }
    if (speed_point_selected_ && speed_selected_entity_name_ == entity_name)
    {
        speed_point_selected_       = false;
        speed_selected_point_index_ = -1;
        speed_selected_entity_name_.clear();
    }
    if (speed_point_drag_active_ && speed_drag_entity_name_ == entity_name)
    {
        speed_point_drag_active_ = false;
        speed_drag_point_index_  = -1;
        speed_drag_entity_name_.clear();
    }
    if (ghost_keyframe_drag_active_ && ghost_drag_entity_name_ == entity_name)
    {
        ghost_keyframe_drag_active_ = false;
        trajectory_renderer_.ClearGhostOverride();
        trajectory_renderer_.ClearReachableRange();
    }

    data_model_.PushTrajectoryUndoState(CaptureTrajectorySelectionSnapshot());
}

bool StudioGui::RenameEntityTrajectory(const std::string& old_name, const std::string& new_name)
{
    if (!data_model_.RenameEntityTrajectory(old_name, new_name))
        return false;

    // The renderer's per-entity group/ghost-state/dirty-flag maps are also keyed by name: drop the old one
    // and mark the new key dirty so Update() (which iterates entity_trajectories_ every frame regardless of
    // the dirty flag) builds a fresh group for it next frame instead of leaving an orphaned old-named one.
    trajectory_renderer_.RemoveEntity(old_name);
    trajectory_renderer_.MarkDirty(new_name);

    if (trajectory_picking_active_ && trajectory_picking_entity_name_ == old_name)
        trajectory_picking_entity_name_ = new_name;
    if (trajectory_point_selected_ && trajectory_selected_entity_name_ == old_name)
        trajectory_selected_entity_name_ = new_name;
    if (trajectory_point_drag_active_ && trajectory_drag_entity_name_ == old_name)
        trajectory_drag_entity_name_ = new_name;
    if (speed_point_selected_ && speed_selected_entity_name_ == old_name)
        speed_selected_entity_name_ = new_name;
    if (speed_point_drag_active_ && speed_drag_entity_name_ == old_name)
        speed_drag_entity_name_ = new_name;
    if (ghost_keyframe_drag_active_ && ghost_drag_entity_name_ == old_name)
        ghost_drag_entity_name_ = new_name;

    data_model_.PushTrajectoryUndoState(CaptureTrajectorySelectionSnapshot());
    return true;
}

void StudioGui::ResetTrajectoryInteractionStateAndUndoStacks()
{
    trajectory_picking_active_       = false;
    trajectory_point_selected_        = false;
    trajectory_selected_point_index_  = -1;
    trajectory_selected_entity_name_.clear();
    trajectory_point_drag_active_    = false;
    trajectory_drag_point_index_     = -1;
    trajectory_drag_entity_name_.clear();
    speed_point_selected_             = false;
    speed_selected_point_index_       = -1;
    speed_selected_entity_name_.clear();
    speed_point_drag_active_          = false;
    speed_drag_point_index_           = -1;
    speed_drag_entity_name_.clear();
    ghost_keyframe_drag_active_       = false;
    trajectory_renderer_.ClearGhostOverride();
    trajectory_renderer_.ClearReachableRange();
    trajectory_renderer_.ClearPickingPreview();

    // Don't allow undoing back into whatever existed before this reset (Trajectory_Editing.md section 11.5),
    // and seed a fresh baseline so the first real edit afterwards can still be undone.
    data_model_.ClearTrajectoryUndoRedoStacks();
    data_model_.PushTrajectoryUndoState(TrajectorySelectionSnapshot());
}

bool StudioGui::HandleTrajectoryPointClick()
{
    const double kHitRadius = 2.0;  // meters, in the ground plane

    // hud_mouse_world_x_/y_ are otherwise only refreshed once per Render() frame; since this is called from
    // handle() at the moment of the mouse press, recompute them now so the hit-test uses the actual click
    // position instead of a potentially stale value from the previous frame (see UpdateHeadingOperation() for
    // the same pattern).
    UpdateMousePositionFromWorld();

    // Find the nearest point across all trajectory entities, whether or not one was already selected: every
    // press that lands on a point both (re)selects it and arms a potential drag in the same motion. This does
    // not violate "a plain click never modifies the trajectory" (Trajectory_Editing.md 7.4): if the button is
    // released without the mouse having moved, the drag delta computed in UpdateTrajectoryPointDrag() is
    // (0, 0), so nothing actually changes - only an intentional drag moves the point.
    std::string best_entity;
    int         best_index    = -1;
    double      best_dist_sqr = kHitRadius * kHitRadius;

    for (auto& entry : data_model_.entity_trajectories_)
    {
        if (!entry.second.show_path_points_)
            continue;  // path point selection/drag is disabled for this entity (Trajectories tab checkbox)
        if (!entry.second.IsVisibleAtTime(data_model_.virtual_time_, entry.second.path_.GetTotalLength()))
            continue;  // not shown in the 3D view at the current virtual_time_, so not clickable there either

        int index = entry.second.path_.FindNearestPointIndex(hud_mouse_world_x_, hud_mouse_world_y_);
        if (index < 0)
            continue;

        const EntityPose& p  = entry.second.path_.points_[static_cast<size_t>(index)];
        double            dx = p.x - hud_mouse_world_x_;
        double            dy = p.y - hud_mouse_world_y_;
        double            d2 = dx * dx + dy * dy;
        if (d2 < best_dist_sqr)
        {
            best_dist_sqr = d2;
            best_entity   = entry.first;
            best_index    = index;
        }
    }

    if (best_index >= 0)
    {
        const EntityPose& p = data_model_.entity_trajectories_[best_entity].path_.points_[static_cast<size_t>(best_index)];

        trajectory_point_selected_       = true;
        trajectory_selected_entity_name_ = best_entity;
        trajectory_selected_point_index_ = best_index;

        trajectory_point_drag_active_  = true;
        trajectory_drag_entity_name_   = best_entity;
        trajectory_drag_point_index_   = best_index;
        trajectory_drag_start_mouse_x_ = hud_mouse_world_x_;
        trajectory_drag_start_mouse_y_ = hud_mouse_world_y_;
        trajectory_drag_start_point_x_ = p.x;
        trajectory_drag_start_point_y_ = p.y;
        return true;  // consume the click: it selected a point (and armed a possible drag)
    }

    // Clicked empty space: clear the selection and let the click fall through to the existing logic below.
    trajectory_point_selected_       = false;
    trajectory_selected_point_index_ = -1;
    trajectory_selected_entity_name_.clear();
    return false;
}

void StudioGui::UpdateTrajectoryPointDrag()
{
    if (!trajectory_point_drag_active_)
        return;

    auto it = data_model_.entity_trajectories_.find(trajectory_drag_entity_name_);
    if (it == data_model_.entity_trajectories_.end() || trajectory_drag_point_index_ < 0 ||
        static_cast<size_t>(trajectory_drag_point_index_) >= it->second.path_.points_.size())
    {
        trajectory_point_drag_active_ = false;
        return;
    }

    UpdateMousePositionFromWorld();

    // Move the point by the same amount the mouse has moved since the drag started (a "pan"), rather than
    // snapping it straight to the cursor - this avoids a jump if the initial click wasn't exactly on the
    // point (the hit-test tolerance in HandleTrajectoryPointClick() allows a couple of meters of slack).
    EntityPose& pose = it->second.path_.points_[static_cast<size_t>(trajectory_drag_point_index_)];
    double      dx   = hud_mouse_world_x_ - trajectory_drag_start_mouse_x_;
    double      dy   = hud_mouse_world_y_ - trajectory_drag_start_mouse_y_;
    pose.x           = trajectory_drag_start_point_x_ + dx;
    pose.y           = trajectory_drag_start_point_y_ + dy;
    pose.SyncFromWorld(/*align_to_lane=*/true);
    pose.SyncFromLane();

    data_model_.trajectories_modified_ = true;
    it->second.SyncSpeedProfileEndpoints();  // dragging a point changes the path's total length
    trajectory_renderer_.MarkDirty(trajectory_drag_entity_name_);
}

void StudioGui::EndTrajectoryPointDrag()
{
    trajectory_point_drag_active_ = false;
    trajectory_drag_point_index_  = -1;
    std::string dragged_entity    = trajectory_drag_entity_name_;
    trajectory_drag_entity_name_.clear();

    // Dragging a path point changes arc lengths, so keyframe arrival times drift: auto re-solve (finalized
    // decision, Trajectory_Editing_Enhancement.md section 6) before committing the undo step so the snapshot
    // captures the fully consistent state.
    ResolveEntityKeyframes(dragged_entity);

    // Commit one undo step for the whole drag gesture (Trajectory_Editing.md section 11.5); PushTrajectoryUndoState()
    // itself is a no-op if the point never actually moved (e.g. a click-and-release with zero movement).
    data_model_.PushTrajectoryUndoState(CaptureTrajectorySelectionSnapshot());
}

void StudioGui::ResolveEntityKeyframes(const std::string& entity_name)
{
    auto it = data_model_.entity_trajectories_.find(entity_name);
    if (it == data_model_.entity_trajectories_.end() || it->second.keyframes_.empty())
        return;
    it->second.ResolveKeyframes();
    data_model_.trajectories_modified_ = true;
    trajectory_renderer_.MarkDirty(entity_name);
}

bool StudioGui::HandleGhostKeyframeClick()
{
    const double kGhostHitRadius = 2.5;  // meters; the ghost is a car-sized model, slightly larger than a point gizmo
    const double kSameTimeEps    = 0.05;  // seconds; scrubbing back to (almost) a keyframe's time edits that keyframe

    // Only Ctrl+drag enters ghost keyframe editing; a plain drag on the vehicle does nothing. This keeps
    // accidental clicks from silently creating arrival-time constraints.
    if (!ctrl_pressed_)
        return false;

    if (data_model_.entity_trajectories_.empty())
        return false;

    UpdateMousePositionFromWorld();

    std::string best_entity;
    double      best_dist_sqr = kGhostHitRadius * kGhostHitRadius;
    double      best_s        = 0.0;

    for (const auto& entry : data_model_.entity_trajectories_)
    {
        if (entry.second.path_.points_.size() < 2)
            continue;  // no meaningful path to slide along
        if (!entry.second.IsVisibleAtTime(data_model_.virtual_time_, entry.second.path_.GetTotalLength()))
            continue;  // not shown in the 3D view at the current virtual_time_, so its ghost isn't clickable

        double     ghost_s = 0.0;
        EntityPose ghost_pose;
        if (!trajectory_renderer_.GetGhostState(entry.first, &ghost_s, &ghost_pose))
            continue;

        double dx = ghost_pose.x - hud_mouse_world_x_;
        double dy = ghost_pose.y - hud_mouse_world_y_;
        double d2 = dx * dx + dy * dy;
        if (d2 < best_dist_sqr)
        {
            best_dist_sqr = d2;
            best_entity   = entry.first;
            best_s        = ghost_s;
        }
    }

    if (best_entity.empty())
        return false;

    EntityTrajectory& traj   = data_model_.entity_trajectories_[best_entity];
    // Keyframes (like the rest of the speed profile) are in the trajectory's own local time, always starting
    // at 0 regardless of when it appears on the shared timeline, so the global virtual_time_ must be offset
    // by start_time_ before being used as/compared against a keyframe's t.
    double            drag_t = static_cast<double>(data_model_.virtual_time_) - traj.start_time_;

    // Neighbour keyframe blocking bounds (finalized decision: a keyframe's s must never cross an adjacent
    // keyframe's s, in either direction) plus "am I updating an existing keyframe at this time?".
    int    existing_kf = -1;
    double s_min       = 0.0;
    double s_max       = traj.path_.GetTotalLength();
    double prev_t      = 0.0;
    bool   has_prev    = false;
    bool   has_next    = false;
    double next_t      = 0.0;
    double next_s      = 0.0;
    for (size_t i = 0; i < traj.keyframes_.size(); i++)
    {
        const TrajectoryKeyframe& kf = traj.keyframes_[i];
        if (std::fabs(kf.t - drag_t) <= kSameTimeEps)
        {
            existing_kf = static_cast<int>(i);
        }
        else if (kf.t < drag_t)
        {
            s_min    = std::max(s_min, kf.s);
            prev_t   = std::max(prev_t, kf.t);
            has_prev = true;
        }
        else
        {
            s_max = std::min(s_max, kf.s);
            if (!has_next || kf.t < next_t)
            {
                next_t = kf.t;
                next_s = kf.s;
            }
            has_next = true;
        }
    }

    // A brand-new keyframe needs a usable time segment before it: "be at s>start at t=0" (timeline never
    // scrubbed) or "arrive before/at the previous keyframe's time" is physically unsolvable - the solver
    // would clamp and the vehicle would visibly snap back on release. Refuse up front with a clear reason
    // instead (the click is still consumed so it doesn't fall through to xosc entity picking).
    if (existing_kf < 0 && drag_t <= prev_t + kSameTimeEps)
    {
        if (prev_t <= kSameTimeEps)
            LOG("Ghost keyframe: virtual time is %.2f s. Drag the timeline to the desired arrival time first, then Ctrl-drag the vehicle.",
                drag_t);
        else
            LOG("Ghost keyframe: virtual time %.2f s is not after the previous keyframe at %.2f s. Drag the timeline further right first.",
                drag_t,
                prev_t);
        return true;
    }

    // Oracle pre-clamping (Trajectory_Editing_Enhancement.md 12.2): shrink the draggable range to what the
    // solver could actually achieve, so releases never snap back. Forward: the farthest point reachable from
    // the previous pin within the available time (first segment: v(0) is adjustable, so the ceiling speed is
    // the honest optimum). Backward: dropping too close to the path end must leave the next keyframe's leg
    // coverable at the ceiling speed.
    TrajectorySolverLimits limits;
    double                 v_entry = has_prev ? std::min(limits.v_max, std::max(0.0, traj.speed_profile_.EvaluateSpeed(s_min))) : limits.v_max;
    s_max                          = std::min(s_max, s_min + MaxReachableDistance(v_entry, drag_t - prev_t, limits));
    if (has_next)
        s_min = std::max(s_min, next_s - limits.v_max * (next_t - drag_t));
    if (s_min > s_max)
        s_min = s_max;  // degenerate sandwich: the drag collapses to the single physically consistent point

    ghost_keyframe_drag_active_ = true;
    ghost_drag_entity_name_     = best_entity;
    ghost_drag_t_               = drag_t;
    ghost_drag_start_s_         = best_s;
    ghost_drag_target_s_        = best_s;
    ghost_drag_blocked_         = false;
    ghost_drag_existing_kf_     = existing_kf;
    ghost_drag_s_min_           = s_min;
    ghost_drag_s_max_           = s_max;

    // Show the 0.7-alpha "editing" ghost right away, before the first mouse move, plus the green highlight
    // of the reachable path range.
    trajectory_renderer_.SetGhostOverride(best_entity, best_s);
    trajectory_renderer_.SetReachableRange(best_entity, s_min, s_max);

    return true;
}

void StudioGui::UpdateGhostKeyframeDrag()
{
    if (!ghost_keyframe_drag_active_)
        return;

    auto it = data_model_.entity_trajectories_.find(ghost_drag_entity_name_);
    if (it == data_model_.entity_trajectories_.end())
    {
        EndGhostKeyframeDrag(false);
        return;
    }

    UpdateMousePositionFromWorld();

    // Slide along the path only: continuously project the mouse onto the path, discard the lateral component
    // entirely (Trajectory_Editing_Enhancement.md section 2, finalized).
    double s = 0.0, px = 0.0, py = 0.0, pz = 0.0, d2 = 0.0;
    if (!it->second.path_.FindNearestPositionOnPath(hud_mouse_world_x_, hud_mouse_world_y_, &s, &px, &py, &pz, &d2))
        return;

    double clamped        = std::min(ghost_drag_s_max_, std::max(ghost_drag_s_min_, s));
    ghost_drag_blocked_   = std::fabs(clamped - s) > 1e-6;
    ghost_drag_target_s_  = clamped;

    trajectory_renderer_.SetGhostOverride(ghost_drag_entity_name_, ghost_drag_target_s_);
}

void StudioGui::EndGhostKeyframeDrag(bool commit)
{
    if (!ghost_keyframe_drag_active_)
        return;

    ghost_keyframe_drag_active_ = false;
    trajectory_renderer_.ClearGhostOverride();
    trajectory_renderer_.ClearReachableRange();

    auto it = data_model_.entity_trajectories_.find(ghost_drag_entity_name_);
    if (!commit || it == data_model_.entity_trajectories_.end())
        return;

    EntityTrajectory& traj = it->second;

    double committed_t = ghost_drag_t_;
    if (ghost_drag_existing_kf_ >= 0 && static_cast<size_t>(ghost_drag_existing_kf_) < traj.keyframes_.size())
    {
        committed_t                                                     = traj.keyframes_[static_cast<size_t>(ghost_drag_existing_kf_)].t;
        traj.keyframes_[static_cast<size_t>(ghost_drag_existing_kf_)].s = ghost_drag_target_s_;
    }
    else
    {
        // Ignore a plain click that never actually moved the ghost - creating a keyframe that pins the
        // current natural position would be a surprising side effect of an accidental click.
        if (std::fabs(ghost_drag_target_s_ - ghost_drag_start_s_) < 0.05)
            return;

        TrajectoryKeyframe kf;
        kf.t = ghost_drag_t_;
        kf.s = ghost_drag_target_s_;
        traj.keyframes_.push_back(kf);
    }

    traj.ResolveKeyframes();

    // If the constraint had to be clamped (speed/acceleration limits), the vehicle will render at the
    // reachable position instead of the drop position - explain the apparent "snap back" in the log.
    for (const auto& kf : traj.keyframes_)
    {
        if (std::fabs(kf.t - committed_t) <= 0.05 && !kf.feasible)
        {
            LOG("Keyframe t=%.2f s cannot be met exactly (speed/acceleration limits). Reachable arrival is %.2f s. "
                "The vehicle shows the reachable position for the current time, hence the jump after release.",
                kf.t,
                kf.achieved_t);
            break;
        }
    }

    data_model_.trajectories_modified_ = true;
    trajectory_renderer_.MarkDirty(ghost_drag_entity_name_);
    data_model_.PushTrajectoryUndoState(CaptureTrajectorySelectionSnapshot());
}

bool StudioGui::HandleTrajectoryPointRightClick()
{
    if (data_model_.entity_trajectories_.empty())
        return false;

    UpdateMousePositionFromWorld();

    const double kPointHitRadius = 2.0;  // meters; matches HandleTrajectoryPointClick()'s left-click radius
    const double kPathHitRadius  = 1.5;  // half of the original 3.0m (user-requested tightening of the Insert
                                          // Point hit test, since it was too easy to trigger by mistake)

    // On/near an existing control point takes priority over "near the path in general": Delete Point.
    std::string best_entity;
    int         best_index    = -1;
    double      best_dist_sqr = kPointHitRadius * kPointHitRadius;

    for (auto& entry : data_model_.entity_trajectories_)
    {
        if (!entry.second.show_path_points_)
            continue;  // path point selection/context menu is disabled for this entity (Trajectories tab checkbox)
        if (!entry.second.IsVisibleAtTime(data_model_.virtual_time_, entry.second.path_.GetTotalLength()))
            continue;  // not shown in the 3D view at the current virtual_time_, so not clickable there either

        int index = entry.second.path_.FindNearestPointIndex(hud_mouse_world_x_, hud_mouse_world_y_);
        if (index < 0)
            continue;

        const EntityPose& p  = entry.second.path_.points_[static_cast<size_t>(index)];
        double            dx = p.x - hud_mouse_world_x_;
        double            dy = p.y - hud_mouse_world_y_;
        double            d2 = dx * dx + dy * dy;
        if (d2 < best_dist_sqr)
        {
            best_dist_sqr = d2;
            best_entity   = entry.first;
            best_index    = index;
        }
    }

    if (best_index >= 0)
    {
        trajectory_context_entity_name_        = best_entity;
        trajectory_context_point_index_        = best_index;
        trajectory_point_context_menu_to_open_ = true;
        return true;
    }

    // Next priority: a keyframe diamond marker (Trajectory_Editing_Enhancement.md 7.2) - checked before the
    // generic "near the path" test below, because keyframe markers always sit ON the path and would otherwise
    // be unreachable behind the Insert Point menu.
    {
        std::string kf_entity;
        int         kf_index    = -1;
        double      kf_dist_sqr = kPointHitRadius * kPointHitRadius;

        for (auto& entry : data_model_.entity_trajectories_)
        {
            if (!entry.second.IsVisibleAtTime(data_model_.virtual_time_, entry.second.path_.GetTotalLength()))
                continue;  // not shown in the 3D view at the current virtual_time_

            for (size_t i = 0; i < entry.second.keyframes_.size(); i++)
            {
                EntityPose kf_pose = entry.second.path_.Evaluate(entry.second.keyframes_[i].s);
                double     dx      = kf_pose.x - hud_mouse_world_x_;
                double     dy      = kf_pose.y - hud_mouse_world_y_;
                double     d2      = dx * dx + dy * dy;
                if (d2 < kf_dist_sqr)
                {
                    kf_dist_sqr = d2;
                    kf_entity   = entry.first;
                    kf_index    = static_cast<int>(i);
                }
            }
        }

        if (kf_index >= 0)
        {
            trajectory_keyframe_context_entity_       = kf_entity;
            trajectory_keyframe_context_index_        = kf_index;
            trajectory_keyframe_context_menu_to_open_ = true;
            return true;
        }
    }

    // Otherwise: close enough to an existing path (its control-point polyline) to offer inserting a new point
    // there? Checked across all entities, closest one wins.
    std::string insert_entity;
    double      insert_s = 0.0, insert_x = 0.0, insert_y = 0.0, insert_z = 0.0;
    double      insert_best_dist_sqr = kPathHitRadius * kPathHitRadius;
    bool        found_insert         = false;

    for (auto& entry : data_model_.entity_trajectories_)
    {
        if (!entry.second.IsVisibleAtTime(data_model_.virtual_time_, entry.second.path_.GetTotalLength()))
            continue;  // not shown in the 3D view at the current virtual_time_, so Insert Point can't target it either

        double s = 0.0, x = 0.0, y = 0.0, z = 0.0, dist_sqr = 0.0;
        if (!entry.second.path_.FindNearestPositionOnPath(hud_mouse_world_x_, hud_mouse_world_y_, &s, &x, &y, &z, &dist_sqr))
            continue;
        if (dist_sqr < insert_best_dist_sqr)
        {
            insert_best_dist_sqr = dist_sqr;
            insert_entity        = entry.first;
            insert_s             = s;
            insert_x             = x;
            insert_y             = y;
            insert_z             = z;
            found_insert         = true;
        }
    }

    if (found_insert)
    {
        trajectory_insert_context_entity_name_  = insert_entity;
        trajectory_insert_context_s_            = insert_s;
        trajectory_insert_context_x_            = insert_x;
        trajectory_insert_context_y_            = insert_y;
        trajectory_insert_context_z_            = insert_z;
        // Insert Point is a path point edit, so it stays disabled for entities with show_path_points_ off;
        // Delete Trajectory (in the same popup) remains available regardless.
        trajectory_insert_context_allow_insert_ = data_model_.entity_trajectories_.at(insert_entity).show_path_points_;
        trajectory_insert_context_menu_to_open_ = true;
        return true;
    }

    return false;
}

void StudioGui::RenderTrajectoriesTab()
{
    if (data_model_.entity_trajectories_.empty())
    {
        ImGui::TextDisabled("No trajectories yet. Right-click the map view and choose 'Add Trajectory'.");
        return;
    }

    std::string entity_to_delete;
    // Deferred to after the loop below (like entity_to_delete above): entity_trajectories_ is keyed by name,
    // so renaming mid-iteration (erase + re-insert under a new key) would invalidate the range-for's iterator.
    std::string rename_from, rename_to;

    // Fetched once per call (not per entity) since it re-parses VehicleCatalog.xosc from disk; shared by every
    // entity's "Vehicle Type" combo below.
    std::vector<std::string> vehicle_type_options = GetVehicleCatalogEntryNames();

    // Counts per id_, so each entity's ID field below can tell in O(1) whether its own id_ clashes with any
    // other trajectory's (Trajectories tab ID field, highlighted red on a clash).
    std::map<int, int> trajectory_id_counts;
    for (const auto& entry : data_model_.entity_trajectories_)
        trajectory_id_counts[entry.second.id_]++;

    for (auto& entry : data_model_.entity_trajectories_)
    {
        const std::string& name = entry.first;
        EntityTrajectory&   traj = entry.second;

        ImGui::PushID(name.c_str());
        bool header_open = ImGui::CollapsingHeader(name.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
        // Without this, the header (whose clickable row spans the full window width) captures the click for
        // any widget drawn on top of the same row, so the "x" button below would also toggle the header
        // open/closed instead of (only) deleting the trajectory.
        ImGui::SetItemAllowOverlap();

        // Small red "x" button on the same row as the header title, right-aligned, to delete the whole
        // trajectory - moved off its own separate "Delete" button (previously the first thing inside the
        // header body) so it's reachable without expanding the section. Drawn manually (instead of
        // SmallButton, which hard-codes FramePadding.y to 0) so the "x" glyph itself can be nudged up a
        // couple of pixels within the button box - ImGui always centers a button's text, so there is no
        // built-in way to offset just the glyph while keeping the button's own position/size unchanged.
        ImGui::SameLine(ImGui::GetWindowWidth() - 30.0f);
        ImVec2 delete_btn_size(16.0f, 16.0f);
        ImVec2 delete_btn_pos = ImGui::GetCursorScreenPos();
        delete_btn_pos.y += (ImGui::GetFrameHeight() - delete_btn_size.y) * 0.5f;  // center vertically in the row
        ImGui::InvisibleButton("##delete_trajectory", delete_btn_size);
        bool delete_btn_hovered = ImGui::IsItemHovered();
        bool delete_btn_active  = ImGui::IsItemActive();
        if (ImGui::IsItemClicked())
            entity_to_delete = name;

        ImU32 delete_btn_color = delete_btn_active   ? IM_COL32(140, 25, 25, 255)
                                 : delete_btn_hovered ? IM_COL32(230, 50, 50, 255)
                                                      : IM_COL32(178, 38, 38, 255);
        ImDrawList* draw_list  = ImGui::GetWindowDrawList();
        draw_list->AddRectFilled(delete_btn_pos, delete_btn_pos + delete_btn_size, delete_btn_color, 3.0f);
        ImVec2 x_text_size = ImGui::CalcTextSize("x");
        ImVec2 x_text_pos  = ImVec2(delete_btn_pos.x + (delete_btn_size.x - x_text_size.x) * 0.5f,
                                    delete_btn_pos.y + (delete_btn_size.y - x_text_size.y) * 0.5f - 2.0);  // nudge glyph up 2px
        draw_list->AddText(x_text_pos, IM_COL32(255, 255, 255, 255), "x");

        if (header_open)
        {
            // ID: user-editable unique identifier (also used as the CSV export's ID/raw_id columns);
            // highlighted red when it clashes with another trajectory's id_. Uniqueness is only enforced
            // visually - the user resolves a clash manually by editing one of the offending fields.
            ImGui::TextUnformatted("ID");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80.0f);
            bool duplicate_id = trajectory_id_counts[traj.id_] > 1;
            if (duplicate_id)
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.6f, 0.0f, 0.0f, 0.8f));
            int id_value = traj.id_;
            if (ImGui::InputInt("##traj_id", &id_value, 0, 0))
            {
                traj.id_                           = id_value;
                data_model_.trajectories_modified_ = true;
            }
            if (duplicate_id)
                ImGui::PopStyleColor();
            if (ImGui::IsItemDeactivatedAfterEdit())
                data_model_.PushTrajectoryUndoState(CaptureTrajectorySelectionSnapshot());

            // Name: renames the trajectory (entity_trajectories_'s map key). Deferred to after the loop (see
            // rename_from/rename_to above) since entity_trajectories_ is being iterated right now. Rejected
            // silently (reverts to the current name next frame) if left empty or if it clashes with another
            // trajectory's name - uniqueness is only checked among entity_trajectories_ itself, never against
            // xosc ScenarioObject names (Trajectory_Editing.md 6.1/12).
            ImGui::SameLine();
            ImGui::TextUnformatted("Name");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(150.0f);
            std::string name_buffer = name;
            name_buffer.resize(256);
            ImGui::InputText("##traj_name", name_buffer.data(), name_buffer.size());
            if (ImGui::IsItemDeactivatedAfterEdit())
            {
                std::string new_name = name_buffer.c_str();
                if (!new_name.empty() && new_name != name && data_model_.entity_trajectories_.count(new_name) == 0)
                {
                    rename_from = name;
                    rename_to   = new_name;
                }
            }

            // Start Time: the global virtual_time_ at which this vehicle first appears (its own speed profile
            // time still starts at 0 regardless) - real captured data often has vehicles entering partway
            // through a recording rather than at t=0.
            ImGui::SameLine();
            ImGui::TextUnformatted("Start Time");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80.0f);
            float start_time_value = static_cast<float>(traj.start_time_);
            if (ImGui::InputFloat("##start_time", &start_time_value, 0.0f, 0.0f, "%.2f"))
            {
                traj.start_time_                   = std::max(0.0f, start_time_value);
                data_model_.trajectories_modified_ = true;
                trajectory_renderer_.MarkDirty(name);
            }
            if (ImGui::IsItemDeactivatedAfterEdit())
                data_model_.PushTrajectoryUndoState(CaptureTrajectorySelectionSnapshot());
            ImGui::SameLine();

            // Vehicle Type (VehicleCatalog.xosc entryName) used for this trajectory's ghost marker model
            // (Trajectory_Editing.md 7.2, extended): changing it here re-loads the model on the next
            // MarkDirty()'d rebuild, same as picking a different one in the Add Trajectory dialog.
            ImGui::TextUnformatted("Vehicle Type");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(150.0f);
            std::string current_entry = traj.vehicle_catalog_entry_name_;
            if (ImGui::BeginCombo("##vehicle_type", current_entry.c_str()))
            {
                for (const auto& option : vehicle_type_options)
                {
                    bool is_selected = (option == current_entry);
                    if (ImGui::Selectable(option.c_str(), is_selected))
                    {
                        traj.vehicle_catalog_entry_name_   = option;
                        data_model_.trajectories_modified_ = true;
                        trajectory_renderer_.MarkDirty(name);
                        data_model_.PushTrajectoryUndoState(CaptureTrajectorySelectionSnapshot());
                    }
                    if (is_selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            // Hide: master visibility switch, independent of Show Path Point/Show Speed Point (which only
            // affect per-point editing) - while on, nothing is rendered for this trajectory at all.
            ImGui::SameLine();
            bool hidden = traj.hidden_;
            if (ImGui::Checkbox("Hide", &hidden))
            {
                traj.hidden_                       = hidden;
                data_model_.trajectories_modified_ = true;
                trajectory_renderer_.MarkDirty(name);
                data_model_.PushTrajectoryUndoState(CaptureTrajectorySelectionSnapshot());
            }

            // Show Path Point / Show Speed Point (Trajectory_Editing.md, extended): disable all path/speed
            // point editing UI for this entity while off, while still rendering the path/speed curve itself.
            // Off by default for CSV-imported trajectories, which can have hundreds of raw points where
            // per-point editing/markers would be impractical (see StudioDataModel::ImportTrajectoriesCsv()).
            bool show_path_points = traj.show_path_points_;
            if (ImGui::Checkbox("Show Path Point", &show_path_points))
            {
                traj.show_path_points_             = show_path_points;
                data_model_.trajectories_modified_ = true;
                trajectory_renderer_.MarkDirty(name);
                if (!show_path_points)
                {
                    // Drop any selection/drag referencing this entity's path point - it's no longer
                    // interactive, so leaving it "selected" would be misleading and could dangle.
                    if (trajectory_point_selected_ && trajectory_selected_entity_name_ == name)
                    {
                        trajectory_point_selected_       = false;
                        trajectory_selected_point_index_ = -1;
                        trajectory_selected_entity_name_.clear();
                    }
                    if (trajectory_point_drag_active_ && trajectory_drag_entity_name_ == name)
                    {
                        trajectory_point_drag_active_ = false;
                        trajectory_drag_point_index_  = -1;
                        trajectory_drag_entity_name_.clear();
                    }
                }
                data_model_.PushTrajectoryUndoState(CaptureTrajectorySelectionSnapshot());
            }

            ImGui::SameLine();
            bool show_speed_points = traj.show_speed_points_;
            if (ImGui::Checkbox("Show Speed Point", &show_speed_points))
            {
                traj.show_speed_points_            = show_speed_points;
                data_model_.trajectories_modified_ = true;
                trajectory_renderer_.MarkDirty(name);
                if (!show_speed_points)
                {
                    if (speed_point_selected_ && speed_selected_entity_name_ == name)
                    {
                        speed_point_selected_       = false;
                        speed_selected_point_index_ = -1;
                        speed_selected_entity_name_.clear();
                    }
                    if (speed_point_drag_active_ && speed_drag_entity_name_ == name)
                    {
                        speed_point_drag_active_ = false;
                        speed_drag_point_index_  = -1;
                        speed_drag_entity_name_.clear();
                    }
                }
                data_model_.PushTrajectoryUndoState(CaptureTrajectorySelectionSnapshot());
            }

            // Alpha: rendering opacity for the path line/ghost marker, quantized to 6 steps (0.0-1.0 in 0.2
            // increments) via an int slider over sixths rather than a free float, per the requested "6 档".
            ImGui::SameLine();
            ImGui::TextUnformatted("Alpha");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120.0f);
            int  alpha_step = std::min(5, std::max(0, static_cast<int>(std::lround(traj.alpha_ * 5.0))));
            char alpha_overlay[8];
            snprintf(alpha_overlay, sizeof(alpha_overlay), "%.1f", alpha_step / 5.0);
            if (ImGui::SliderInt("##alpha", &alpha_step, 0, 5, alpha_overlay))
            {
                traj.alpha_                        = alpha_step / 5.0;
                data_model_.trajectories_modified_ = true;
                trajectory_renderer_.MarkDirty(name);
            }
            if (ImGui::IsItemDeactivatedAfterEdit())
                data_model_.PushTrajectoryUndoState(CaptureTrajectorySelectionSnapshot());


            // Initial position/speed mirror the path's/speed profile's first point (Trajectory_Editing.md 8.2);
            // editable only by dragging in the map view / the speed chart below, not via text input here.
            if (!traj.path_.points_.empty())
            {
                const EntityPose& p0 = traj.path_.points_.front();
                ImGui::Text("Init pos: x=%.2f, y=%.2f, road %d, lane %d, s=%.2f", p0.x, p0.y, p0.road_id, p0.lane_id, p0.s);
            }
            else
            {
                ImGui::TextDisabled("Init pos: (no path points)");
            }

            double init_speed = traj.speed_profile_.points_.empty() ? 0.0 : traj.speed_profile_.points_.front().speed;
            ImGui::SameLine();
            ImGui::Text("; Init speed: %.2f m/s", init_speed);

            double length = traj.path_.GetTotalLength();

            // Keep the speed profile's start (s=0) / end (s=path length) anchors correct even if something
            // upstream forgot to call SyncSpeedProfileEndpoints() after changing the path (Trajectory_Editing.md
            // 8.2): idempotent, so calling it again here every frame is harmless.
            traj.SyncSpeedProfileEndpoints();

            // Speed Profile editing (Trajectory_Editing.md 8.2): a chart for quick/approximate graphical
            // editing (drag existing points, double-click to insert one), plus a precise numeric table below
            // it for exact values and for deleting points (the chart alone has no delete gesture). The s axis
            // always auto-fits to the current path length and the speed axis is a fixed 0-30 m/s range (the
            // keyframe solver's hard v_max, Trajectory_Editing_Enhancement.md section 11), so box-select-to-
            // zoom is disabled (it would fight the fixed/auto-fit ranges every frame anyway).
            const double kSpeedAxisMax = 30.0;
            double       s_axis_max    = std::max(1.0, length);

            // NoMenus: disable ImPlot's own right-click context menu (axis/fit options), which would otherwise
            // intercept right clicks instead of letting our own Insert/Delete Point menu see them.
            ImVec2 plot_frame_min = ImGui::GetCursorScreenPos();
            float  plot_width     = ImGui::GetContentRegionAvail().x;
            if (ImPlot::BeginPlot(("Speed Profile (?)##" + name).c_str(), ImVec2(-1, 200), ImPlotFlags_NoBoxSelect | ImPlotFlags_NoMenus))
            {
                ImPlot::SetupAxes("s (m)", "speed (m/s)");
                ImPlot::SetupAxisLimits(ImAxis_X1, 0.0, s_axis_max, ImGuiCond_Always);
                ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, kSpeedAxisMax, ImGuiCond_Always);

                // The "(?)" is baked directly into ImPlot's own title text (drawn by ImPlot itself, so it can't
                // be a separate hoverable ImGui widget); approximate its tooltip by hovering the whole title
                // strip above the plot's canvas (ImPlot::GetPlotPos() is the canvas's top-left, below the
                // title). GetPlotPos() locks the Setup phase, so it must come after all Setup* calls above.
                ImVec2 title_area_max = ImVec2(plot_frame_min.x + plot_width, ImPlot::GetPlotPos().y);
                if (ImGui::IsMouseHoveringRect(plot_frame_min, title_area_max))
                {
                    ImGui::BeginTooltip();
                    ImGui::TextUnformatted("Click a point to select it (turns red; its row below highlights too).\n"
                                           "Drag the selected point to change its speed; hold Ctrl to also change s.\n"
                                           "Right-click a point to delete it, or right-click empty space to insert one.");
                    ImGui::EndTooltip();
                }

                std::vector<double> xs, ys;
                xs.reserve(traj.speed_profile_.points_.size());
                ys.reserve(traj.speed_profile_.points_.size());
                for (const auto& sp : traj.speed_profile_.points_)
                {
                    xs.push_back(sp.s);
                    ys.push_back(sp.speed);
                }

                bool profile_changed          = false;
                bool this_is_drag_target      = speed_point_drag_active_ && speed_drag_entity_name_ == name;

                // Vertical reference lines at each keyframe's s (Trajectory_Editing_Enhancement.md 7.4), so
                // it is obvious which part of the speed shape is pinned by which arrival-time constraint.
                for (size_t k = 0; k < traj.keyframes_.size(); k++)
                {
                    const TrajectoryKeyframe& kf     = traj.keyframes_[k];
                    double                    ref_x[2] = {kf.s, kf.s};
                    double                    ref_y[2] = {0.0, kSpeedAxisMax};
                    ImPlot::SetNextLineStyle(kf.feasible ? ImVec4(0.3f, 0.6f, 1.0f, 0.7f) : ImVec4(1.0f, 0.2f, 0.2f, 0.8f), 1.5f);
                    ImPlot::PlotLine(("##kf" + std::to_string(k)).c_str(), ref_x, ref_y, 2);
                }

                if (!xs.empty())
                {
                    ImPlot::PlotLine("speed", xs.data(), ys.data(), static_cast<int>(xs.size()));

                    // Per-point markers/drag handles are skipped when show_speed_points_ is off (e.g.
                    // CSV-imported profiles with hundreds of raw points) - the curve above is still drawn.
                    if (traj.show_speed_points_)
                    {
                        for (size_t i = 0; i < xs.size(); i++)
                        {
                            bool is_selected = speed_point_selected_ && speed_selected_entity_name_ == name &&
                                               speed_selected_point_index_ == static_cast<int>(i);
                            ImVec4 col = is_selected ? ImVec4(1.0f, 0.15f, 0.15f, 1.0f) : ImVec4(1.0f, 0.55f, 0.0f, 1.0f);
                            double px  = xs[i];
                            double py  = ys[i];
                            // NoInputs: rendering only. Selection/dragging is handled manually below so the
                            // "select before drag", delta-based movement, and Ctrl-for-s rules can be enforced
                            // (Trajectory_Editing.md 8.2), which ImPlot::DragPoint's built-in snap-to-cursor drag
                            // cannot express on its own.
                            ImPlot::DragPoint(static_cast<int>(i), &px, &py, col, 6.0f, ImPlotDragToolFlags_NoInputs);
                        }
                    }
                }

                // Continue an already-armed drag on this entity's point regardless of hover, so a fast mouse
                // move doesn't drop it.
                if (speed_point_drag_active_ && this_is_drag_target)
                {
                    if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
                    {
                        ImPlotPoint mp          = ImPlot::GetPlotMousePos();
                        double      delta_s     = mp.x - speed_drag_start_mouse_s_;
                        double      delta_speed = mp.y - speed_drag_start_mouse_speed_;

                        size_t idx       = static_cast<size_t>(speed_drag_point_index_);
                        bool   is_anchor = (idx == 0) || (idx == traj.speed_profile_.points_.size() - 1);

                        traj.speed_profile_.points_[idx].speed =
                            std::min(kSpeedAxisMax, std::max(0.0, speed_drag_start_point_speed_ + delta_speed));
                        if (ctrl_pressed_ && !is_anchor)
                        {
                            traj.speed_profile_.points_[idx].s =
                                std::min(s_axis_max, std::max(0.0, speed_drag_start_point_s_ + delta_s));
                        }
                        profile_changed = true;
                    }
                    else
                    {
                        // Mouse released: end the drag. Also flag profile_changed on this exact frame (even
                        // though no value changes here) so the commit-check below - which is skipped on every
                        // in-progress drag frame - actually fires exactly once, right here, to finalize the
                        // whole gesture into its own undo step. Without this, the drag's changes (already
                        // applied to traj.speed_profile_.points_ on earlier frames) would never be pushed on
                        // their own and would silently get bundled into whichever *next* edit happens to
                        // trigger a commit (Trajectory_Editing.md section 11.5).
                        speed_point_drag_active_ = false;
                        profile_changed          = true;
                    }
                }

                // Manual pixel-space hit-testing for select/insert/delete: more robust than
                // ImPlot::IsPlotHovered(), which can report false while a DragPoint tool has hover captured.
                // The whole block is skipped when show_speed_points_ is off - no selection, drag, insert or
                // delete of individual points, only the curve itself remains visible.
                if (traj.show_speed_points_)
                {
                ImVec2 plot_min      = ImPlot::GetPlotPos();
                ImVec2 plot_max      = ImVec2(plot_min.x + ImPlot::GetPlotSize().x, plot_min.y + ImPlot::GetPlotSize().y);
                ImVec2 mouse_pos     = ImGui::GetMousePos();
                bool   mouse_in_plot = mouse_pos.x >= plot_min.x && mouse_pos.x <= plot_max.x &&
                                     mouse_pos.y >= plot_min.y && mouse_pos.y <= plot_max.y;

                auto find_nearest_pixel = [&](double pixel_radius) -> int
                {
                    int    best    = -1;
                    double best_d2 = pixel_radius * pixel_radius;
                    for (size_t i = 0; i < xs.size(); i++)
                    {
                        ImVec2 p  = ImPlot::PlotToPixels(ImPlotPoint(xs[i], ys[i]));
                        double dx = p.x - mouse_pos.x;
                        double dy = p.y - mouse_pos.y;
                        double d2 = dx * dx + dy * dy;
                        if (d2 < best_d2)
                        {
                            best_d2 = d2;
                            best    = static_cast<int>(i);
                        }
                    }
                    return best;
                };

                if (mouse_in_plot)
                {
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                    {
                        // Remember which point (if any) was right-clicked and where, then defer the actual
                        // insert/delete to the popup below: it can only run once ImGui confirms the menu
                        // choice, possibly several frames later.
                        speed_context_point_index_ = find_nearest_pixel(10.0);
                        ImPlotPoint mp              = ImPlot::GetPlotMousePos();
                        speed_context_insert_s_     = std::min(s_axis_max, std::max(0.0, mp.x));
                        speed_context_insert_speed_ = std::min(kSpeedAxisMax, std::max(0.0, mp.y));
                        ImGui::OpenPopup("SpeedProfileContextMenu");
                    }
                    else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                    {
                        int idx = find_nearest_pixel(10.0);
                        if (idx >= 0)
                        {
                            // Select and simultaneously arm a potential drag: if the press ends without
                            // moving, the delta computed above next frame is (0, 0) so nothing changes; a
                            // plain click never modifies the profile (Trajectory_Editing.md 7.4/8.2).
                            speed_point_selected_       = true;
                            speed_selected_entity_name_ = name;
                            speed_selected_point_index_ = idx;

                            speed_point_drag_active_ = true;
                            speed_drag_entity_name_  = name;
                            speed_drag_point_index_  = idx;
                            ImPlotPoint mp                = ImPlot::GetPlotMousePos();
                            speed_drag_start_mouse_s_     = mp.x;
                            speed_drag_start_mouse_speed_ = mp.y;
                            speed_drag_start_point_s_     = traj.speed_profile_.points_[static_cast<size_t>(idx)].s;
                            speed_drag_start_point_speed_ = traj.speed_profile_.points_[static_cast<size_t>(idx)].speed;
                        }
                        else if (speed_selected_entity_name_ == name)
                        {
                            speed_point_selected_ = false;
                            speed_selected_point_index_ = -1;
                            speed_selected_entity_name_.clear();
                        }
                    }
                }

                // Insert Point / Delete Point context menu (Trajectory_Editing.md 8.2), populated from the
                // right-click above. Must be opened/rendered while still inside BeginPlot()/EndPlot(): ImPlot's
                // plot area is itself a child window, which pushes its own ID scope, so a BeginPopup() called
                // only after EndPlot() would resolve to a DIFFERENT id than the OpenPopup() above (called
                // while still inside the child) and would never actually find/open it. Scoped under this
                // entity's PushID(name), so each entity's plot gets its own independent popup instance.
                if (ImGui::BeginPopup("SpeedProfileContextMenu"))
                {
                    bool clicked_on_point = speed_context_point_index_ >= 0;
                    bool clicked_on_anchor = clicked_on_point &&
                                            (speed_context_point_index_ == 0 ||
                                             speed_context_point_index_ == static_cast<int>(traj.speed_profile_.points_.size()) - 1);

                    if (clicked_on_point)
                    {
                        if (clicked_on_anchor)
                        {
                            ImGui::BeginDisabled(true);
                            ImGui::MenuItem("Delete Point (start/end anchor, fixed)");
                            ImGui::EndDisabled();
                        }
                        else if (ImGui::MenuItem("Delete Point"))
                        {
                            traj.speed_profile_.RemovePoint(speed_context_point_index_);
                            profile_changed = true;
                            if (speed_selected_entity_name_ == name && speed_selected_point_index_ == speed_context_point_index_)
                            {
                                speed_point_selected_       = false;
                                speed_selected_point_index_ = -1;
                                speed_selected_entity_name_.clear();
                            }
                        }
                    }
                    else if (ImGui::MenuItem("Insert Point"))
                    {
                        traj.speed_profile_.InsertPoint(speed_context_insert_s_, speed_context_insert_speed_);
                        profile_changed = true;
                        if (speed_selected_entity_name_ == name)
                        {
                            speed_point_selected_       = false;
                            speed_selected_point_index_ = -1;
                            speed_selected_entity_name_.clear();
                        }
                    }
                    ImGui::EndPopup();
                }
                }  // traj.show_speed_points_

                ImPlot::EndPlot();

                // Skip re-sorting/anchor-sync while a Ctrl-drag is actively moving this point's s, so the
                // point's index stays stable across frames for the drag above; it's finalized on release
                // (the frame speed_point_drag_active_ turns false, which happens before this check runs).
                // This is also precisely the right condition under which to commit an undo step (Trajectory_
                // Editing.md section 11.5): either a discrete Insert/Delete Point action, or the exact frame a
                // drag gesture ends - never a mid-drag intermediate frame.
                if (profile_changed && !(speed_point_drag_active_ && this_is_drag_target))
                {
                    // An Insert Point (or an out-of-order Ctrl-drag) could leave points_ unsorted;
                    // EvaluateSpeed() assumes ascending s, so restore that invariant after any edit.
                    std::sort(traj.speed_profile_.points_.begin(),
                             traj.speed_profile_.points_.end(),
                             [](const SpeedProfilePoint& a, const SpeedProfilePoint& b) { return a.s < b.s; });
                    traj.SyncSpeedProfileEndpoints();
                    ResolveEntityKeyframes(name);  // keyframes stay authoritative over manual speed edits
                    data_model_.PushTrajectoryUndoState(CaptureTrajectorySelectionSnapshot());
                }
                if (profile_changed)
                {
                    data_model_.trajectories_modified_ = true;
                    trajectory_renderer_.MarkDirty(name);
                }
            }

            // Precise numeric editing table, and the only way to delete a speed profile point. The first and
            // last rows are the fixed start/end anchors: their s is read-only and they cannot be deleted, so
            // there are always at least 2 points spanning [0, path length]. Collapsed by default; "Add Point"
            // sits on the header's own row so it stays reachable without expanding the table. The header
            // itself stays interactive even when show_speed_points_ is off, so the table can still be opened
            // to *view* the values - only the actual editing widgets (Add Point, per-row inputs/Delete) are
            // grayed out/disabled in that case, per the Trajectories tab's Show Speed Point checkbox.
            bool speed_points_enabled = traj.show_speed_points_;
            bool speed_table_open     = ImGui::CollapsingHeader("Speed Profile Points");
            ImGui::SameLine(ImGui::GetWindowWidth() - 90.0f);
            if (!speed_points_enabled)
                ImGui::BeginDisabled(true);
            if (ImGui::SmallButton("Add Point"))
            {
                // Insert at the midpoint between the last two points (rather than appending after the end
                // anchor, which would leave it no longer the last / highest-s point).
                size_t n = traj.speed_profile_.points_.size();
                double new_s     = 0.0;
                double new_speed = 0.0;
                if (n >= 2)
                {
                    new_s     = 0.5 * (traj.speed_profile_.points_[n - 2].s + traj.speed_profile_.points_[n - 1].s);
                    new_speed = 0.5 * (traj.speed_profile_.points_[n - 2].speed + traj.speed_profile_.points_[n - 1].speed);
                }
                traj.speed_profile_.InsertPoint(new_s, new_speed);
                traj.SyncSpeedProfileEndpoints();
                data_model_.trajectories_modified_ = true;
                trajectory_renderer_.MarkDirty(name);
                if (speed_selected_entity_name_ == name)
                {
                    speed_point_selected_ = false;
                    speed_selected_point_index_ = -1;
                    speed_selected_entity_name_.clear();
                }
                ResolveEntityKeyframes(name);
                data_model_.PushTrajectoryUndoState(CaptureTrajectorySelectionSnapshot());
            }
            if (!speed_points_enabled)
                ImGui::EndDisabled();

            int  point_to_delete     = -1;
            bool commit_numeric_edit = false;  // set when an InputFloat below finishes an edit (see IsItemDeactivatedAfterEdit)
            if (speed_table_open && ImGui::BeginTable("##speed_profile_table", 3, ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableSetupColumn("s (m)");
                ImGui::TableSetupColumn("speed (m/s)");
                ImGui::TableSetupColumn("");
                ImGui::TableHeadersRow();

                if (!speed_points_enabled)
                    ImGui::BeginDisabled(true);

                for (size_t i = 0; i < traj.speed_profile_.points_.size(); i++)
                {
                    bool is_anchor  = (i == 0) || (i == traj.speed_profile_.points_.size() - 1);
                    bool is_selected = speed_point_selected_ && speed_selected_entity_name_ == name &&
                                       speed_selected_point_index_ == static_cast<int>(i);

                    ImGui::PushID(static_cast<int>(i));
                    ImGui::TableNextRow();
                    if (is_selected)
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(120, 40, 40, 255));

                    ImGui::TableSetColumnIndex(0);
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    float s_value = static_cast<float>(traj.speed_profile_.points_[i].s);
                    if (is_anchor)
                    {
                        ImGui::BeginDisabled(true);
                        ImGui::InputFloat("##s", &s_value, 0.0f, 0.0f, "%.2f");
                        ImGui::EndDisabled();
                    }
                    else
                    {
                        if (ImGui::InputFloat("##s", &s_value, 0.0f, 0.0f, "%.2f"))
                        {
                            traj.speed_profile_.points_[i].s   = std::min(static_cast<float>(s_axis_max), std::max(0.0f, s_value));
                            data_model_.trajectories_modified_ = true;
                            trajectory_renderer_.MarkDirty(name);
                        }
                        // Commit one undo step per finished edit (Trajectory_Editing.md 11.5), not per keystroke;
                        // the actual push is deferred to after the re-sort below so it captures the final state.
                        if (ImGui::IsItemDeactivatedAfterEdit())
                            commit_numeric_edit = true;
                    }

                    ImGui::TableSetColumnIndex(1);
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    float speed_value = static_cast<float>(traj.speed_profile_.points_[i].speed);
                    if (ImGui::InputFloat("##speed", &speed_value, 0.0f, 0.0f, "%.2f"))
                    {
                        traj.speed_profile_.points_[i].speed = std::min(static_cast<float>(kSpeedAxisMax), std::max(0.0f, speed_value));
                        data_model_.trajectories_modified_    = true;
                        trajectory_renderer_.MarkDirty(name);
                    }
                    if (ImGui::IsItemDeactivatedAfterEdit())
                        commit_numeric_edit = true;

                    ImGui::TableSetColumnIndex(2);
                    if (is_anchor)
                    {
                        ImGui::BeginDisabled(true);
                        ImGui::SmallButton("Delete");
                        ImGui::EndDisabled();
                    }
                    else if (ImGui::SmallButton("Delete"))
                    {
                        point_to_delete = static_cast<int>(i);
                    }

                    ImGui::PopID();
                }
                if (!speed_points_enabled)
                    ImGui::EndDisabled();
                ImGui::EndTable();
            }

            if (point_to_delete >= 0)
            {
                traj.speed_profile_.RemovePoint(point_to_delete);
                traj.SyncSpeedProfileEndpoints();
                data_model_.trajectories_modified_ = true;
                trajectory_renderer_.MarkDirty(name);
                if (speed_selected_entity_name_ == name)
                {
                    speed_point_selected_ = false;
                    speed_selected_point_index_ = -1;
                    speed_selected_entity_name_.clear();
                }
                if (speed_drag_entity_name_ == name)
                {
                    speed_point_drag_active_ = false;
                    speed_drag_point_index_  = -1;
                    speed_drag_entity_name_.clear();
                }
                ResolveEntityKeyframes(name);
                data_model_.PushTrajectoryUndoState(CaptureTrajectorySelectionSnapshot());
            }
            else
            {
                // Re-sort after a numeric edit may have moved a point past a neighbor (see above).
                std::sort(traj.speed_profile_.points_.begin(),
                         traj.speed_profile_.points_.end(),
                         [](const SpeedProfilePoint& a, const SpeedProfilePoint& b) { return a.s < b.s; });
                traj.SyncSpeedProfileEndpoints();
                if (commit_numeric_edit)
                {
                    ResolveEntityKeyframes(name);
                    data_model_.PushTrajectoryUndoState(CaptureTrajectorySelectionSnapshot());
                }
            }

            // Keyframes (Trajectory_Editing_Enhancement.md 7.4): the arrival-time constraints recorded by
            // dragging the ghost vehicle along its path in the map view. t is editable here (same re-solve
            // path as dragging, finalized decision 9); s is edited by dragging the ghost only. Collapsed by
            // default, same as the speed profile points table above.
            if (ImGui::CollapsingHeader("Keyframes"))
            {
            if (traj.keyframes_.empty())
            {
                ImGui::TextDisabled("No keyframes. Scrub the timeline, then Ctrl-drag the vehicle along its path to add one.");
            }
            else
            {
                int  kf_to_delete   = -1;
                bool kf_t_committed = false;

                if (ImGui::BeginTable("##keyframes_table", 4, ImGuiTableFlags_SizingStretchProp))
                {
                    ImGui::TableSetupColumn("t (s)");
                    ImGui::TableSetupColumn("s (m)");
                    ImGui::TableSetupColumn("status");
                    ImGui::TableSetupColumn("");
                    ImGui::TableHeadersRow();

                    for (size_t i = 0; i < traj.keyframes_.size(); i++)
                    {
                        TrajectoryKeyframe& kf = traj.keyframes_[i];
                        ImGui::PushID(static_cast<int>(i) + 1000);
                        ImGui::TableNextRow();

                        ImGui::TableSetColumnIndex(0);
                        ImGui::SetNextItemWidth(-FLT_MIN);
                        float t_value = static_cast<float>(kf.t);
                        if (ImGui::InputFloat("##kf_t", &t_value, 0.0f, 0.0f, "%.2f"))
                            kf.t = std::max(0.0, static_cast<double>(t_value));
                        if (ImGui::IsItemDeactivatedAfterEdit())
                            kf_t_committed = true;

                        ImGui::TableSetColumnIndex(1);
                        ImGui::Text("%.2f", kf.s);

                        ImGui::TableSetColumnIndex(2);
                        if (kf.feasible)
                            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "OK");
                        else
                            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "clamped, act. %.2fs", kf.achieved_t);

                        ImGui::TableSetColumnIndex(3);
                        if (ImGui::SmallButton("Go"))
                            data_model_.virtual_time_ = static_cast<float>(traj.start_time_ + kf.t);  // kf.t is local time; virtual_time_ is global
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Delete"))
                            kf_to_delete = static_cast<int>(i);

                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }

                if (kf_to_delete >= 0)
                {
                    // RemoveKeyframe also deletes the keyframe's pin node and re-solves the remaining chain.
                    traj.RemoveKeyframe(kf_to_delete);
                    data_model_.trajectories_modified_ = true;
                    trajectory_renderer_.MarkDirty(name);
                    data_model_.PushTrajectoryUndoState(CaptureTrajectorySelectionSnapshot());
                }
                else if (kf_t_committed)
                {
                    traj.ResolveKeyframes();  // same solve path as a ghost drag (finalized decision 9)
                    data_model_.trajectories_modified_ = true;
                    trajectory_renderer_.MarkDirty(name);
                    data_model_.PushTrajectoryUndoState(CaptureTrajectorySelectionSnapshot());
                }
            }
            }  // Keyframes CollapsingHeader
        }
        ImGui::PopID();
    }

    if (!entity_to_delete.empty())
    {
        DeleteEntityTrajectory(entity_to_delete);
    }
    else if (!rename_from.empty())
    {
        RenameEntityTrajectory(rename_from, rename_to);
    }
}

void StudioGui::HandleExitConfirmDialog()
{
    if (exit_confirm_dialog_active_)
        ImGui::OpenPopup("Unsaved Changes");

    ImGuiIO& io     = ImGui::GetIO();
    ImVec2   center = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("Unsaved Changes", &exit_confirm_dialog_active_, ImGuiWindowFlags_AlwaysAutoResize))
    {
        // Merged prompt covering both independent save entry points (Trajectory_Editing.md 5.4/13): no need to
        // ask separately about OpenSCENARIO vs. Trajectories changes.
        ImGui::TextUnformatted("OpenSCENARIO and/or Trajectories have unsaved changes.");
        ImGui::TextUnformatted("Do you want to save before exiting?");

        if (ImGui::Button("Save All && Exit", ImVec2(140, 0)))
        {
            if (data_model_.modified_ && !data_model_.xosc_path_.empty())
                data_model_.SaveXoscXml(data_model_.xosc_path_);
            if (data_model_.trajectories_modified_ && !data_model_.traj_json_path_.empty())
                data_model_.SaveTrajJson(data_model_.traj_json_path_);
            exit_confirm_dialog_active_      = false;
            data_model_.mode_                = StudioMode::ZOMBIE;
            viewer_->osgViewer_->setDone(true);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard && Exit", ImVec2(140, 0)))
        {
            exit_confirm_dialog_active_ = false;
            data_model_.mode_           = StudioMode::ZOMBIE;
            viewer_->osgViewer_->setDone(true);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0)) || ImGui::IsKeyPressedMap(ImGuiKey_Escape))
        {
            exit_confirm_dialog_active_ = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void StudioGui::HandleEsminiSettingsDialog()
{
    if (data_model_.esmini_settings_dialog_to_open_)
    {
        data_model_.esmini_settings_dialog_to_open_ = false;
        ImGui::OpenPopup("Esmini Settings");
    }

    ImGuiIO& io     = ImGui::GetIO();
    ImVec2   center = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("Esmini Settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::InputInt("Seed", &data_model_.esmini_seed_);
        ImGui::InputFloat("Timestep", &data_model_.esmini_timestep_);

        static char path_buffer[4096];
        if (ImGui::IsWindowAppearing())
        {
            strncpy(path_buffer, data_model_.esmini_resource_paths_.c_str(), sizeof(path_buffer));
            path_buffer[sizeof(path_buffer) - 1] = 0;
        }

        ImGui::InputTextMultiline("Resource Paths", path_buffer, sizeof(path_buffer), ImVec2(300, 100));
        ImGui::TextDisabled("(Separate multiple paths with newlines)");

        ImGui::Separator();

        if (ImGui::Button("Save", ImVec2(120, 0)) || ImGui::IsKeyPressedMap(ImGuiKey_Enter))
        {
            data_model_.esmini_resource_paths_ = path_buffer;
            data_model_.SaveConfig();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SetItemDefaultFocus();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0)) || ImGui::IsKeyPressedMap(ImGuiKey_Escape))
        {
            data_model_.LoadConfig();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void StudioGui::RenderMenuBar()
{
    std::string backup_path;

    if (ImGui::BeginMainMenuBar())
    {
        bool in_composer_mode = (data_model_.mode_ == StudioMode::COMPOSER);
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Clear", nullptr, false, in_composer_mode))
            {
                data_model_.Clear();
                positions_extracted_       = false;
                to_reset_camera_pos_       = true;
                scenario_object_map_dirty_ = true;

                // File > Clear also resets the independent path+speed-profile trajectories (Trajectory_
                // Editing.md 5.4) - otherwise it would look like Clear only affects the OpenSCENARIO document,
                // leaving stale trajectories (and their renderer/undo state) behind.
                data_model_.entity_trajectories_.clear();
                data_model_.trajectories_modified_ = false;
                ResetTrajectoryInteractionStateAndUndoStacks();
            }
            if (ImGui::MenuItem("Open an OpenSCENARIO File ...", nullptr, false, in_composer_mode))
            {
                auto result = pfd::open_file("Choose an OpenSCENARIO File", "", scenario_filter).result();
                if (!result.empty())
                {
                    if (data_model_.LoadXoscXml(result[0]))
                    {
                        LOG("Successfully the OpenSCENARIO file: [%s].", result[0].c_str());
                        scenario_object_map_dirty_ = true;  // Mark mapping as dirty when XML is loaded
                        if (data_model_.mode_ != StudioMode::COMPOSER)
                            SwitchToComposer();
                        to_reset_camera_pos_ = true;

                        // Automatically try to load the OpenDRIVE file referenced in the scenario
                        TryLoadOpenDriveFromScenario();

                        // Load the independent trajectory sidecar file if one exists next to the scenario
                        // (Trajectory_Editing.md 5.4). entity_trajectories_ has no relationship to xml_doc_'s
                        // entities, so this is purely "load whatever was saved before", no generation/sync.
                        data_model_.traj_json_path_ = DeriveTrajJsonPath(result[0]);
                        data_model_.LoadTrajJson(data_model_.traj_json_path_);
                        // Fresh file: don't allow undoing back into whatever was loaded before (section 11.5),
                        // and seed the new baseline so the first real edit after this can still be undone.
                        data_model_.ClearTrajectoryUndoRedoStacks();
                        data_model_.PushTrajectoryUndoState(TrajectorySelectionSnapshot());
                    }
                }
            }
            if (ImGui::MenuItem("Save OpenSCENARIO", "Ctrl+S"))
                to_save_file = true;
            if (ImGui::MenuItem("Save As..."))
            {
                to_save_file = true;
                backup_path  = data_model_.xosc_path_;
                data_model_.xosc_path_.clear();
            }
            if (ImGui::MenuItem("Load Trajectories ...", nullptr, false, in_composer_mode))
            {
                auto result = pfd::open_file("Choose a Trajectory File", "", traj_json_filter).result();
                if (!result.empty())
                {
                    if (data_model_.LoadTrajJson(result[0]))
                    {
                        LOG("Successfully loaded the trajectory file: [%s].", result[0].c_str());
                        // Loading replaces entity_trajectories_ wholesale (Trajectory_Editing.md 5.4); any
                        // selection/drag/picking state referring to the previous set is now stale.
                        ResetTrajectoryInteractionStateAndUndoStacks();
                    }
                    else
                    {
                        LOG("Failed to load trajectory file: %s", result[0].c_str());
                    }
                }
            }
            if (ImGui::MenuItem("Save Trajectories", "Ctrl+S", false, !data_model_.entity_trajectories_.empty() || data_model_.trajectories_modified_))
            {
                std::string path = data_model_.traj_json_path_;
                if (path.empty())
                    path = DeriveTrajJsonPath(data_model_.xosc_path_.empty() ? "scenario.xosc" : data_model_.xosc_path_);
                data_model_.traj_json_path_ = path;
                data_model_.SaveTrajJson(path);
            }
            if (ImGui::MenuItem("Export Trajectories as CSV ...", nullptr, false, !data_model_.entity_trajectories_.empty()))
            {
                auto file_path = pfd::save_file("Export Trajectories as CSV", "trajectories.csv", traj_csv_filter).result();
                if (!file_path.empty())
                {
                    if (data_model_.ExportTrajectoriesCsv(file_path))
                        LOG("Successfully exported trajectories to CSV: [%s]", file_path.c_str());
                    else
                        LOG("Failed to export trajectories to CSV: [%s]", file_path.c_str());
                }
            }
            if (ImGui::MenuItem("Import CSV as Trajectories ...", nullptr, false, in_composer_mode))
            {
                auto result = pfd::open_file("Import CSV as Trajectories", "", traj_csv_filter).result();
                if (!result.empty())
                {
                    if (data_model_.ImportTrajectoriesCsv(result[0]))
                    {
                        LOG("Successfully imported trajectories from CSV: [%s]", result[0].c_str());
                        // Imported entities are new, but importing can add many at once; treat it like the
                        // wholesale-replace load above for undo purposes (Trajectory_Editing.md section 11.5),
                        // since it's not a single small discrete edit.
                        data_model_.ClearTrajectoryUndoRedoStacks();
                        data_model_.PushTrajectoryUndoState(TrajectorySelectionSnapshot());
                    }
                    else
                    {
                        LOG("Failed to import CSV as trajectories: %s", result[0].c_str());
                    }
                }
            }
            if (ImGui::MenuItem("Open an OpenDRIVE File ...", nullptr, false, in_composer_mode))
            {
                auto result = pfd::open_file("Choose an OpenDRIVE File", "", map_filter).result();
                if (!result.empty())
                {
                    if (viewer_->LoadOpenDrive(result[0]))
                    {
                        positions_extracted_ = false;
                        to_reset_camera_pos_ = true;
                        data_model_.ClearUndoRedoStacks();
                    }
                    else
                    {
                        LOG("Failed to load OpenDRIVE file: %s", result[0].c_str());
                    }
                }
            }
            if (ImGui::MenuItem("Exit"))
                Exit();
            ImGui::EndMenu();
        }

        if (to_save_file)
        {
            if (data_model_.xosc_path_.empty())
            {
                auto file_path = pfd::save_file("Save as OpenSCENARIO", "untitled.xosc", scenario_filter).result();
                if (file_path.empty())
                {  // Cancelled
                    data_model_.xosc_path_ = backup_path;
                    to_save_file           = false;
                }
                else
                {
                    data_model_.xosc_path_ = file_path;
                    // A brand-new OpenSCENARIO path was just picked (first Save, or Save As), so any
                    // previously-derived/loaded traj_json_path_ (which could be left over from an earlier,
                    // differently-named scenario, e.g. from clicking "Save Trajectories" while xosc_path_ was
                    // still empty) no longer matches it and must be re-derived below rather than reused as-is.
                    data_model_.traj_json_path_.clear();
                }
            }
            if (to_save_file)
                data_model_.SaveXoscXml(data_model_.xosc_path_);
            // Ctrl+S / Save OpenSCENARIO / Save As all save the trajectory sidecar too (Trajectory_Editing.md
            // 5.4/13): the two files are independent, but a single save shortcut should not leave one behind.
            if (to_save_file && (!data_model_.entity_trajectories_.empty() || data_model_.trajectories_modified_))
            {
                std::string traj_path = data_model_.traj_json_path_;
                if (traj_path.empty())
                    traj_path = DeriveTrajJsonPath(data_model_.xosc_path_.empty() ? "scenario.xosc" : data_model_.xosc_path_);
                data_model_.traj_json_path_ = traj_path;
                data_model_.SaveTrajJson(traj_path);
            }
            to_save_file = false;
        }

        if (ImGui::BeginMenu("Edit"))
        {
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, (data_model_.CanUndo() || data_model_.CanUndoTrajectories()) && in_composer_mode))
            {
                Undo();
            }
            if (ImGui::MenuItem("Redo", "Ctrl+Shift+Z", false, (data_model_.CanRedo() || data_model_.CanRedoTrajectories()) && in_composer_mode))
            {
                Redo();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Validate Scenario", nullptr, false, in_composer_mode))
            {
                data_model_.ValidateScenario();
                show_validation_window_ = true;
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Settings"))
        {
            if (ImGui::MenuItem("Esmini Settings"))
            {
                data_model_.esmini_settings_dialog_to_open_ = true;
            }
            if (ImGui::MenuItem("Reset Windows"))
            {
                data_model_.ResetWindowSizes();
            }
            ImGui::EndMenu();
        }
    }
    ImGui::EndMainMenuBar();
}

TrajectorySelectionSnapshot StudioGui::CaptureTrajectorySelectionSnapshot() const
{
    TrajectorySelectionSnapshot sel;
    sel.path_point_selected  = trajectory_point_selected_;
    sel.path_point_entity    = trajectory_selected_entity_name_;
    sel.path_point_index     = trajectory_selected_point_index_;
    sel.speed_point_selected = speed_point_selected_;
    sel.speed_point_entity   = speed_selected_entity_name_;
    sel.speed_point_index    = speed_selected_point_index_;
    return sel;
}

void StudioGui::ApplyTrajectorySelectionSnapshot(const TrajectorySelectionSnapshot& sel)
{
    trajectory_point_selected_       = sel.path_point_selected;
    trajectory_selected_entity_name_ = sel.path_point_entity;
    trajectory_selected_point_index_ = sel.path_point_index;
    speed_point_selected_            = sel.speed_point_selected;
    speed_selected_entity_name_      = sel.speed_point_entity;
    speed_selected_point_index_      = sel.speed_point_index;

    // Defensive: end any drag that might still (in theory) be active - Undo()/Redo() already refuse to run
    // while a gesture is in progress, this just guards against a dangling drag-start reference after the
    // underlying data changed out from under it.
    trajectory_point_drag_active_ = false;
    speed_point_drag_active_      = false;

    // ImGui's InputFloat caches its own edit buffer while actively focused and won't re-read the float* every
    // frame; if undo/redo just changed the value behind a currently-focused numeric field, force it to let go
    // so it re-syncs from the (now updated) underlying data next frame instead of briefly showing a stale value.
    ImGui::ClearActiveID();
}

void StudioGui::Undo()
{
    if (data_model_.mode_ != StudioMode::COMPOSER)
        return;
    // Ignore Ctrl+Z while a trajectory gesture is in progress, rather than trying to reconcile an undo with a
    // not-yet-committed drag/picking session (Trajectory_Editing.md section 11.4).
    if (trajectory_picking_active_ || trajectory_point_drag_active_ || speed_point_drag_active_ || ghost_keyframe_drag_active_)
        return;

    bool xml_is_newer = data_model_.CanUndo() &&
                        (!data_model_.CanUndoTrajectories() || data_model_.LastXmlUndoSeq() > data_model_.LastTrajectoryUndoSeq());
    if (xml_is_newer)
    {
        data_model_.Undo();
    }
    else if (data_model_.CanUndoTrajectories())
    {
        ApplyTrajectorySelectionSnapshot(data_model_.UndoTrajectories());
        trajectory_renderer_.SetSelectedPoint(trajectory_point_selected_ ? trajectory_selected_entity_name_ : std::string(),
                                              trajectory_point_selected_ ? trajectory_selected_point_index_ : -1);
    }

    scenario_object_map_dirty_ = true;
    positions_extracted_       = false;
}

void StudioGui::Redo()
{
    if (data_model_.mode_ != StudioMode::COMPOSER)
        return;
    if (trajectory_picking_active_ || trajectory_point_drag_active_ || speed_point_drag_active_ || ghost_keyframe_drag_active_)
        return;

    bool xml_is_newer = data_model_.CanRedo() &&
                        (!data_model_.CanRedoTrajectories() || data_model_.LastXmlRedoSeq() > data_model_.LastTrajectoryRedoSeq());
    if (xml_is_newer)
    {
        data_model_.Redo();
    }
    else if (data_model_.CanRedoTrajectories())
    {
        ApplyTrajectorySelectionSnapshot(data_model_.RedoTrajectories());
        trajectory_renderer_.SetSelectedPoint(trajectory_point_selected_ ? trajectory_selected_entity_name_ : std::string(),
                                              trajectory_point_selected_ ? trajectory_selected_point_index_ : -1);
    }

    scenario_object_map_dirty_ = true;
    positions_extracted_       = false;
}

void StudioGui::TryLoadOpenDriveFromScenario()
{
    // Extract the filepath from OpenSCENARIO/RoadNetwork/LogicFile/@filepath
    pugi::xml_node root_node = data_model_.RootNode();
    if (root_node.empty())
        return;

    pugi::xml_node road_network = root_node.child("RoadNetwork");
    if (road_network.empty())
        return;

    pugi::xml_node logic_file = road_network.child("LogicFile");
    if (logic_file.empty())
        return;

    pugi::xml_attribute filepath_attr = logic_file.attribute("filepath");
    if (filepath_attr.empty())
        return;

    std::string filepath = filepath_attr.value();
    if (filepath.empty())
        return;

    // Extract filename from filepath (ignore path part)
    std::string filename = FileNameOf(filepath);

    // Get currently loaded OpenDRIVE filename
    roadmanager::OpenDrive* odr = roadmanager::Position::GetOpenDrive();
    if (odr != nullptr)
    {
        std::string current_odr_filename = FileNameOf(odr->GetOpenDriveFilename());

        // If filenames match, no need to reload
        if (current_odr_filename == filename)
        {
            LOG("OpenDRIVE file [%s] is already loaded.", filename.c_str());
            return;
        }
    }

    // Try to load the OpenDRIVE file
    bool        loaded = false;
    std::string loaded_path;

    // Check if filepath is absolute
    if (FileExists(filepath.c_str()))
    {
        // Absolute path exists, try to load directly
        if (viewer_->LoadOpenDrive(filepath))
        {
            loaded      = true;
            loaded_path = filepath;
            LOG("Successfully loaded OpenDRIVE from absolute path: [%s]", filepath.c_str());
        }
    }
    else
    {
        // Try relative paths using SE_Env search paths
        const std::vector<std::string>& search_paths = SE_Env::Inst().GetPaths();

        for (const auto& search_path : search_paths)
        {
            std::string candidate_path = search_path + "/" + filepath;
            candidate_path             = normalize_path(candidate_path);

            if (FileExists(candidate_path.c_str()))
            {
                if (viewer_->LoadOpenDrive(candidate_path))
                {
                    loaded      = true;
                    loaded_path = candidate_path;
                    LOG("Successfully loaded OpenDRIVE from: [%s]", candidate_path.c_str());
                    break;
                }
            }
        }
    }

    // If loading failed, prompt user to manually select the file
    if (!loaded)
    {
        LOG("Failed to automatically load OpenDRIVE file: [%s]", filepath.c_str());
        LOG("Please manually select the OpenDRIVE file.");

        filepath   = normalize_path(filepath);
        size_t cur = filepath.rfind('/');
        if (cur != std::string::npos)
            filepath = filepath.substr(cur + 1);
        std::vector<std::string> filter = {filepath, filepath};
        std::string              title  = "Choose the OpenDRIVE File[";
        title += filepath + "]";
        auto result = pfd::open_file(title, "", filter).result();
        if (!result.empty())
        {
            if (viewer_->LoadOpenDrive(result[0]))
            {
                LOG("Successfully loaded OpenDRIVE file: [%s]", result[0].c_str());
                positions_extracted_ = false;
                to_reset_camera_pos_ = true;
                data_model_.ClearUndoRedoStacks();
                SE_Env::Inst().AddPath(DirNameOf(result[0]));
            }
            else
            {
                LOG("Failed to load OpenDRIVE file: [%s]", result[0].c_str());
            }
        }
    }
    else
    {
        // Successfully loaded, update viewer state
        positions_extracted_ = false;
        to_reset_camera_pos_ = true;
        data_model_.ClearUndoRedoStacks();
    }
}

void StudioGui::RenderTimeline()
{
    ImGui::SetNextWindowPos(ImVec2(0, data_model_.viewport_height_ - data_model_.time_bar_height_));
    ImGui::SetNextWindowSize((ImVec2(data_model_.viewport_width_, data_model_.time_bar_height_)));
    ImGui::SetNextWindowBgAlpha(1.0f);
    ImGui::Begin("##timebar", nullptr, ImGuiWindowFlags_NoDecoration);
    ImGui::PushItemWidth(-1);
    data_model_.virtual_time_max_value_ = std::max(data_model_.virtual_time_, data_model_.virtual_time_max_value_);

    // The slider is enabled in COMPOSER too (Trajectory_Editing_Enhancement.md 7.1): there it acts as the
    // ghost preview scrubber (virtual_time_ only drives the trajectory ghost markers, no player is running),
    // which is also where ghost keyframes are recorded from. Extend its range to cover the slowest ghost so
    // every trajectory can be scrubbed end to end.
    if (data_model_.mode_ == StudioMode::COMPOSER)
    {
        for (const auto& entry : data_model_.entity_trajectories_)
        {
            double total_time = entry.second.speed_profile_.EvaluateTimeAtS(entry.second.path_.GetTotalLength());
            double end_time   = entry.second.start_time_ + total_time;  // scrub range must reach each ghost's actual end, not just its duration from 0
            data_model_.virtual_time_max_value_ = std::max(data_model_.virtual_time_max_value_, static_cast<float>(end_time));
        }
    }

    if (ImGui::SliderFloat("##virtual_time",
                           &data_model_.virtual_time_,
                           0.0f,
                           data_model_.virtual_time_max_value_,
                           "Virtual Time: %.2f s",
                           ImGuiSliderFlags_NoInput))
    {
        data_model_.virtual_time_             = std::round(data_model_.virtual_time_ / 0.05) * 0.05;
        data_model_.virtual_time_manipulated_ = true;
    }
    ImGui::End();
}

bool StudioGui::handle(const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter& aa)
{
    if (!initialized_)
    {
        auto* osg_view = aa.asView();
        if (osg_view)
        {
            auto* camera = osg_view->getCamera();
            camera->addPreDrawCallback(new ImGuiNewFrameCallback(*this));
            camera->addPostDrawCallback(new ImGuiRenderCallback(*this));
            initialized_ = true;
        }
    }

    ImGuiIO&   io                  = ImGui::GetIO();
    const bool wantCaptureMouse    = io.WantCaptureMouse;
    const bool wantCaptureKeyboard = io.WantCaptureKeyboard;

    switch (ea.getEventType())
    {
        case osgGA::GUIEventAdapter::KEYDOWN:
        case osgGA::GUIEventAdapter::KEYUP:
        {
            bool isKeyDown = ea.getEventType() == osgGA::GUIEventAdapter::KEYDOWN;
            int  c         = ea.getKey();
            auto mod       = ea.getModKeyMask();

            // Handle Ctrl+Letter issues where OSG might return 1-26 for A-Z
            if ((mod & osgGA::GUIEventAdapter::MODKEY_CTRL) && c >= 1 && c <= 26)
            {
                // Remap Ctrl+A (1) through Ctrl+Z (26) to 'a'-'z'
                c = c + 'a' - 1;
            }

            // Try to map to ImGuiKey
            ImGuiKey imgui_key = ImGuiKey_None;

            // Check special keys first
            int special_key = ConvertOSGKeyToImGuiKey(c);
            if (special_key != -1)
                imgui_key = (ImGuiKey)special_key;
            else
            {
                // Map ASCII characters to ImGui Keys
                if (c >= 'a' && c <= 'z')
                    imgui_key = (ImGuiKey)(ImGuiKey_A + (c - 'a'));
                else if (c >= 'A' && c <= 'Z')
                    imgui_key = (ImGuiKey)(ImGuiKey_A + (c - 'A'));
                else if (c >= '0' && c <= '9')
                    imgui_key = (ImGuiKey)(ImGuiKey_0 + (c - '0'));
            }

            if (mod & osgGA::GUIEventAdapter::MODKEY_CTRL)
            {
                io.AddKeyEvent(ImGuiMod_Ctrl, isKeyDown);
                ctrl_pressed_ = isKeyDown;
            }
            else
            {
                io.AddKeyEvent(ImGuiMod_Ctrl, false);
                ctrl_pressed_ = false;
            }

            if (mod & osgGA::GUIEventAdapter::MODKEY_SHIFT)
            {
                io.AddKeyEvent(ImGuiMod_Shift, isKeyDown);
                shift_pressed_ = isKeyDown;
            }
            else
            {
                io.AddKeyEvent(ImGuiMod_Shift, false);
                shift_pressed_ = false;
            }
            if (mod & osgGA::GUIEventAdapter::MODKEY_ALT)
            {
                io.AddKeyEvent(ImGuiMod_Alt, isKeyDown);
                alt_pressed_ = isKeyDown;
            }
            else
            {
                io.AddKeyEvent(ImGuiMod_Alt, false);
                alt_pressed_ = isKeyDown;
            }

            // Send key event if we mapped it
            if (imgui_key != ImGuiKey_None)
            {
                io.AddKeyEvent(imgui_key, isKeyDown);
            }

            // Send character input for text fields
            // Don't send if Ctrl is pressed (avoids sending control codes as text)
            if (isKeyDown && c > 0 && c < 0xFF && !(mod & osgGA::GUIEventAdapter::MODKEY_CTRL))
            {
                io.AddInputCharacter((unsigned short)c);
            }
            else if (isKeyDown)
            {
                // Handle Keypad input characters
                // Note: OSG might not map keypad keys to ASCII automatically, so we do it manually
                // We also check for NumLock where appropriate, although OSG usually distinguishes
                // between KP_Insert (0) and KP_0 based on NumLock state.
                char kp_char = 0;
                switch (c)
                {
                    case osgGA::GUIEventAdapter::KEY_KP_0:
                        kp_char = '0';
                        break;
                    case osgGA::GUIEventAdapter::KEY_KP_1:
                        kp_char = '1';
                        break;
                    case osgGA::GUIEventAdapter::KEY_KP_2:
                        kp_char = '2';
                        break;
                    case osgGA::GUIEventAdapter::KEY_KP_3:
                        kp_char = '3';
                        break;
                    case osgGA::GUIEventAdapter::KEY_KP_4:
                        kp_char = '4';
                        break;
                    case osgGA::GUIEventAdapter::KEY_KP_5:
                        kp_char = '5';
                        break;
                    case osgGA::GUIEventAdapter::KEY_KP_6:
                        kp_char = '6';
                        break;
                    case osgGA::GUIEventAdapter::KEY_KP_7:
                        kp_char = '7';
                        break;
                    case osgGA::GUIEventAdapter::KEY_KP_8:
                        kp_char = '8';
                        break;
                    case osgGA::GUIEventAdapter::KEY_KP_9:
                        kp_char = '9';
                        break;
                    case osgGA::GUIEventAdapter::KEY_KP_Decimal:
                        kp_char = '.';
                        break;
                    case osgGA::GUIEventAdapter::KEY_KP_Divide:
                        kp_char = '/';
                        break;
                    case osgGA::GUIEventAdapter::KEY_KP_Multiply:
                        kp_char = '*';
                        break;
                    case osgGA::GUIEventAdapter::KEY_KP_Subtract:
                        kp_char = '-';
                        break;
                    case osgGA::GUIEventAdapter::KEY_KP_Add:
                        kp_char = '+';
                        break;
                    case osgGA::GUIEventAdapter::KEY_KP_Equal:
                        kp_char = '=';
                        break;
                }

                if (kp_char != 0)
                {
                    io.AddInputCharacter((unsigned short)kp_char);
                }
            }

            // Handle global shortcuts (legacy handling, might want to move to ImGui shortcuts eventually)
            if (!isKeyDown && c == osgGA::GUIEventAdapter::KEY_Space && !wantCaptureKeyboard)
            {
                HandleSpaceKey();
            }
            else if (!isKeyDown && c == osgGA::GUIEventAdapter::KEY_Escape && !wantCaptureKeyboard)
            {
                if (ghost_keyframe_drag_active_)
                    EndGhostKeyframeDrag(false);  // cancel: ghost snaps back, nothing is committed
                else if (trajectory_picking_active_)
                    CancelTrajectoryPickingPoint();
                else if (heading_operation_active_)
                    CancelHeadingOperation();
                else if (data_model_.mode_ != StudioMode::COMPOSER)
                    SwitchToComposer();
            }
            else if (!isKeyDown && !wantCaptureKeyboard && trajectory_picking_active_ &&
                     (c == osgGA::GUIEventAdapter::KEY_Return || c == osgGA::GUIEventAdapter::KEY_KP_Enter))
            {
                FinishTrajectoryPicking();
            }
            else if (!isKeyDown && !wantCaptureKeyboard && c == osgGA::GUIEventAdapter::KEY_Delete)
            {
                // Delete the currently selected path point or speed profile point (whichever is selected),
                // mirroring the corresponding right-click "Delete Point" menu items/table row buttons.
                if (trajectory_point_selected_)
                {
                    auto it = data_model_.entity_trajectories_.find(trajectory_selected_entity_name_);
                    if (it != data_model_.entity_trajectories_.end() && trajectory_selected_point_index_ >= 0 &&
                        static_cast<size_t>(trajectory_selected_point_index_) < it->second.path_.points_.size() &&
                        it->second.path_.points_.size() > 1)  // never leave a path with 0 points
                    {
                        std::string entity_name = trajectory_selected_entity_name_;
                        it->second.path_.RemovePoint(trajectory_selected_point_index_);
                        it->second.SyncSpeedProfileEndpoints();  // removing a point changes the path's total length
                        data_model_.trajectories_modified_ = true;
                        trajectory_renderer_.MarkDirty(entity_name);
                        trajectory_point_selected_       = false;
                        trajectory_selected_point_index_ = -1;
                        trajectory_selected_entity_name_.clear();
                        ResolveEntityKeyframes(entity_name);  // arc lengths changed
                        data_model_.PushTrajectoryUndoState(CaptureTrajectorySelectionSnapshot());
                    }
                }
                else if (speed_point_selected_)
                {
                    auto it = data_model_.entity_trajectories_.find(speed_selected_entity_name_);
                    if (it != data_model_.entity_trajectories_.end() && speed_selected_point_index_ >= 0 &&
                        static_cast<size_t>(speed_selected_point_index_) < it->second.speed_profile_.points_.size())
                    {
                        size_t index     = static_cast<size_t>(speed_selected_point_index_);
                        bool   is_anchor = (index == 0) || (index == it->second.speed_profile_.points_.size() - 1);
                        if (!is_anchor)  // the start/end anchors can never be deleted (Trajectory_Editing.md 8.2)
                        {
                            std::string entity_name = speed_selected_entity_name_;
                            it->second.speed_profile_.RemovePoint(speed_selected_point_index_);
                            it->second.SyncSpeedProfileEndpoints();
                            data_model_.trajectories_modified_ = true;
                            trajectory_renderer_.MarkDirty(entity_name);
                            speed_point_selected_       = false;
                            speed_selected_point_index_ = -1;
                            speed_selected_entity_name_.clear();
                            ResolveEntityKeyframes(entity_name);
                            data_model_.PushTrajectoryUndoState(CaptureTrajectorySelectionSnapshot());
                        }
                    }
                }
            }

            // Legacy Ctrl shortcuts handling (can be removed if ImGui handles them, but keeping for safety)
            if (ea.getEventType() == osgGA::GUIEventAdapter::KEYDOWN && (mod & osgGA::GUIEventAdapter::MODKEY_CTRL))
            {
                if (c == 'z' && (mod & osgGA::GUIEventAdapter::MODKEY_SHIFT) &&
                    (data_model_.CanRedo() || data_model_.CanRedoTrajectories()))  // Ctrl + SHIFT + Z
                {
                    Redo();
                }
                else if (c == 'z' && (data_model_.CanUndo() || data_model_.CanUndoTrajectories()))  // Ctrl + Z
                {
                    Undo();
                }
                else if (c == 's')  // Ctrl + S
                {
                    to_save_file = true;
                }
            }

            return wantCaptureKeyboard;
        }
        case osgGA::GUIEventAdapter::RELEASE:
        case osgGA::GUIEventAdapter::PUSH:
        {
            io.MousePos           = ImVec2(ea.getX(), io.DisplaySize.y - ea.getY());
            left_mouse_pressed_   = ea.getButtonMask() & osgGA::GUIEventAdapter::LEFT_MOUSE_BUTTON;
            right_mouse_pressed_  = ea.getButtonMask() & osgGA::GUIEventAdapter::RIGHT_MOUSE_BUTTON;
            middle_mouse_pressed_ = ea.getButtonMask() & osgGA::GUIEventAdapter::MIDDLE_MOUSE_BUTTON;

            // Handle mouse click events for vehicle/object picking
            if (!wantCaptureMouse && ea.getEventType() == osgGA::GUIEventAdapter::PUSH)
            {
                if (data_model_.mode_ == StudioMode::COMPOSER)
                {
                    if (ea.getButtonMask() & osgGA::GUIEventAdapter::LEFT_MOUSE_BUTTON)
                    {
                        if (trajectory_picking_active_)
                        {
                            // Continuous point-picking for a new trajectory (Trajectory_Editing.md 6.1)
                            CommitTrajectoryPickingPoint();
                        }
                        else if (!heading_operation_active_ && !move_operation_active_ && HandleTrajectoryPointClick())
                        {
                            // Trajectory point selection/drag consumed the click (Trajectory_Editing.md 7.4):
                            // either a point was just (re)selected, or the drag on an already-selected point's
                            // gizmo just started. Either way, do not fall through to xosc entity picking below.
                        }
                        else if (!heading_operation_active_ && !move_operation_active_ && HandleGhostKeyframeClick())
                        {
                            // Pressing on a ghost vehicle starts the slide-along-path keyframe drag
                            // (Trajectory_Editing_Enhancement.md section 7.2). Path control points take
                            // priority above since they are much smaller targets.
                        }
                        else if (heading_operation_active_)
                        {
                            // Left click confirms the heading modification
                            ConfirmHeadingOperation();
                        }
                        else
                        {
                            // Left mouse button - existing functionality
                            auto* pos_info = PickPosition();
                            if (!pos_info)
                            {
                                ClearXmlHighlights();
                                data_model_.markers_to_update_ = true;
                            }
                            // Clear any existing move operation if right-clicking empty space
                            if (move_operation_active_)
                            {
                                EndMoveOperation();
                                SetModified();
                            }
                        }
                    }
                    else if (ea.getButtonMask() & osgGA::GUIEventAdapter::RIGHT_MOUSE_BUTTON)
                    {
                        if (heading_operation_active_)
                        {
                            // Right click cancels the heading modification and restores the original value
                            CancelHeadingOperation();
                        }
                        else if (!move_operation_active_)
                        {
                            // Defer opening the context menu until release, so a right-drag (camera pan)
                            // does not trigger it. The menu opens on release only if the mouse did not move.
                            right_click_candidate_ = true;
                            right_press_x_         = ea.getX();
                            right_press_y_         = ea.getY();
                        }
                    }
                }
            }

            // Open the right-click context menu on release, but only for a genuine click (not a pan)
            if (!wantCaptureMouse && ea.getEventType() == osgGA::GUIEventAdapter::RELEASE &&
                ea.getButton() == osgGA::GUIEventAdapter::RIGHT_MOUSE_BUTTON)
            {
                if (right_click_candidate_ && data_model_.mode_ == StudioMode::COMPOSER && !heading_operation_active_ && !move_operation_active_)
                {
                    // Trajectory path points take priority over xosc entity picking (Trajectory_Editing.md
                    // 7.4/8.2): only fall through to Move/Add Vehicle if the click wasn't on/near a path.
                    if (!HandleTrajectoryPointRightClick())
                    {
                        auto* pos_info = PickPosition();
                        if (pos_info)
                            move_context_menu_to_open_ = true;
                        else
                            add_vehicle_context_menu_to_open_ = true;
                    }
                }
                right_click_candidate_ = false;
            }

            // End a trajectory point drag on left-button release (Trajectory_Editing.md 7.4)
            if (!wantCaptureMouse && ea.getEventType() == osgGA::GUIEventAdapter::RELEASE &&
                ea.getButton() == osgGA::GUIEventAdapter::LEFT_MOUSE_BUTTON && trajectory_point_drag_active_)
            {
                EndTrajectoryPointDrag();
            }

            // Commit a ghost keyframe drag on left-button release (Trajectory_Editing_Enhancement.md 7.2)
            if (ea.getEventType() == osgGA::GUIEventAdapter::RELEASE && ea.getButton() == osgGA::GUIEventAdapter::LEFT_MOUSE_BUTTON &&
                ghost_keyframe_drag_active_)
            {
                EndGhostKeyframeDrag(true);
            }
        }
        case osgGA::GUIEventAdapter::DRAG:
        case osgGA::GUIEventAdapter::MOVE:
        {
            io.MousePos = ImVec2(ea.getX(), io.DisplaySize.y - ea.getY());

            // A right-button drag beyond a small threshold is a camera pan, not a click: cancel the pending menu
            if (right_click_candidate_)
            {
                const float click_move_threshold = 4.0f;  // pixels
                float       dx                    = ea.getX() - right_press_x_;
                float       dy                    = ea.getY() - right_press_y_;
                if (dx * dx + dy * dy > click_move_threshold * click_move_threshold)
                    right_click_candidate_ = false;
            }

            // Update move operation during drag if active
            if (move_operation_active_)
            {
                UpdateMoveOperation();
            }

            // Update a trajectory point drag if active (Trajectory_Editing.md 7.4)
            if (trajectory_point_drag_active_)
            {
                UpdateTrajectoryPointDrag();
            }

            // Update a ghost keyframe drag if active (Trajectory_Editing_Enhancement.md 7.2)
            if (ghost_keyframe_drag_active_)
            {
                UpdateGhostKeyframeDrag();
            }

            // Track the mouse direction while modifying a heading
            if (heading_operation_active_)
            {
                UpdateHeadingOperation();
            }

            return wantCaptureMouse;
        }
        case osgGA::GUIEventAdapter::SCROLL:
        {
            mouse_wheel_ = ea.getScrollingMotion() == osgGA::GUIEventAdapter::SCROLL_UP ? 1.0 : -1.0;
            return wantCaptureMouse;
        }
        default:
        {
            return false;
        }
    }

    return false;
}

inline int find_enum_index(const std::vector<std::string>& enums, const char* value)
{
    for (size_t i = 0; i < enums.size(); ++i)
        if (enums[i] == value)
            return (int)i;
    return -1;
}

const char* get_enum(void* user_data, int idx)
{
    auto enums = static_cast<std::vector<std::string>*>(user_data);
    return (*enums)[idx].c_str();
}

void StudioGui::RenderXmlSubTree(pugi::xml_node node,
                                 int            level /* = 0*/,
                                 bool           force_expand /* = false */,
                                 bool           suppress_context_menu /* = false */,
                                 int            level_to_expand /* = 0 */,
                                 int            level_to_collapse /* = 0 */,
                                 bool           parent_matches_filter /* = false */)
{
    // Filter logic
    bool node_matches_filter     = false;
    bool has_matching_descendant = false;

    if (filter_active_)
    {
        // In isolation mode, descendants of matched nodes should be shown but not highlighted
        // So we don't propagate parent_matches_filter for highlighting purposes
        std::string name       = node.name();
        std::string name_lower = name;
        std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), ::tolower);

        std::string filter_lower = filter_text_;
        std::transform(filter_lower.begin(), filter_lower.end(), filter_lower.begin(), ::tolower);

        if (!filter_lower.empty() && name_lower.find(filter_lower) != std::string::npos)
        {
            node_matches_filter = true;
        }
        else
        {
            // Check descendants
            std::function<bool(pugi::xml_node)> check_descendants = [&](pugi::xml_node n) -> bool
            {
                for (pugi::xml_node child = n.first_child(); child; child = child.next_sibling())
                {
                    std::string child_name       = child.name();
                    std::string child_name_lower = child_name;
                    std::transform(child_name_lower.begin(), child_name_lower.end(), child_name_lower.begin(), ::tolower);

                    if (child_name_lower.find(filter_lower) != std::string::npos)
                        return true;

                    if (check_descendants(child))
                        return true;
                }
                return false;
            };
            has_matching_descendant = check_descendants(node);
        }

        if (filter_active_ && filter_isolate_)
        {
            // In isolation mode: show matched nodes and their descendants, plus ancestors of matches
            // parent_matches_filter indicates this node is a descendant of a matched node
            bool is_descendant_of_match = parent_matches_filter;

            if (!node_matches_filter && !has_matching_descendant && !is_descendant_of_match)
                return;  // Hide nodes that don't match and aren't related to matches

            if (node_matches_filter)
                force_expand = true;  // Expand matched nodes to show their children
            else if (has_matching_descendant)
                force_expand = true;  // Expand to show path to descendant matches
        }
    }

    // Check if this node should be highlighted
    bool isHighlighted            = (highlighted_nodes_.end() != std::find(highlighted_nodes_.begin(), highlighted_nodes_.end(), node));
    bool isSearchHighlighted      = (node == search_highlight_node_ && !search_highlight_attr_);
    bool scroll_to_node           = (node_to_scroll_to_ == node);
    bool scroll_to_node_and_attrs = (node_and_attrs_to_scroll_to_ == node);
    bool is_node_invalid          = (data_model_.invalid_nodes_.count(node) > 0);

    if (isHighlighted)
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 255, 0, 255));  // Green text
    else if (node_matches_filter && filter_active_)
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(100, 149, 237, 255));  // CornflowerBlue for filter match

    if (isSearchHighlighted)
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 64, 64, 255));  // Red text

    if (nodes_to_expand_.empty() && nodes_to_collapse_.empty())
    {
        if (level_to_expand > 0)
            ImGui::SetNextItemOpen(true);
        if (level_to_collapse > 0)
            ImGui::SetNextItemOpen(false);
    }
    else
    {
        if (!nodes_to_expand_.empty())
        {
            auto iter = std::find_if(nodes_to_expand_.begin(), nodes_to_expand_.end(), [&](pugi::xml_node node2) { return node == node2; });
            if (iter != nodes_to_expand_.end())
            {
                ImGui::PushID(node.name());
                int id = GImGui->CurrentWindow->IDStack.back();
                GImGui->CurrentWindow->DC.StateStorage->SetInt(id, 1);
                nodes_to_expand_.erase(iter);
                ImGui::PopID();
            }
        }
    }

    bool disabled2 = (data_model_.mode_ != StudioMode::COMPOSER);
    if (disabled2)
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, GImGui->Style.Alpha * GImGui->Style.DisabledAlpha);

    if (is_node_invalid)
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.1f, 0.1f, 1.0f));

    bool expanded = ImGui::TreeNodeEx(node.name());
    auto rect_min = ImGui::GetItemRectMin();
    auto rect_max = ImGui::GetItemRectMax();

    if (is_node_invalid)
        ImGui::PopStyleColor();

    if (disabled2)
        ImGui::PopStyleVar();

    // Pop filter highlight immediately after TreeNodeEx to avoid highlighting children/attributes
    if (node_matches_filter && filter_active_ && !isHighlighted)
    {
        ImGui::PopStyleColor();
    }

    if (scroll_to_node)
    {
        ImGui::ScrollToRect(ImGui::GetCurrentWindow(), ImRect(rect_min, rect_max), ImGuiScrollFlags_KeepVisibleCenterY);
        node_to_scroll_to_ = pugi::xml_node();
    }
    if (isSearchHighlighted)
        ImGui::PopStyleColor();  // Pop the two colors we pushed IMMEDIATELY after the item is drawn

    if (!nodes_to_collapse_.empty())
    {
        auto iter = std::find_if(nodes_to_collapse_.begin(), nodes_to_collapse_.end(), [&](pugi::xml_node node2) { return node == node2; });
        if (iter != nodes_to_collapse_.end())
        {
            int id = ImGui::GetItemID();
            GImGui->CurrentWindow->DC.StateStorage->SetInt(id, 0);
            nodes_to_collapse_.erase(iter);
        }
    }

    if (ImGui::IsItemClicked() && ImGui::IsItemToggledOpen() && ImGui::GetIO().KeyShift)
        level_to_expand = 5;
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen() && ImGui::GetIO().KeyShift)
        level_to_collapse = 5;

    if (!suppress_context_menu && ImGui::IsItemClicked(ImGuiMouseButton_Right))
    {
        // LOG("%s is right clicked", node.name());
        xml_node_modify_menu_to_open = true;
        modify_menu_context          = node.name();
        modify_menu_context_node     = node;
    }

    if (strcmp(node.name(), "WorldPosition") == 0 || strcmp(node.name(), "LanePosition") == 0 || strcmp(node.name(), "RelativeLanePosition") == 0)
    {
        bool disabled3 = (data_model_.mode_ != StudioMode::COMPOSER);
        if (disabled3)
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, GImGui->Style.Alpha * GImGui->Style.DisabledAlpha);

        ImGui::SameLine();
        bool find_it = (ImGui::SmallButton("Find It") && !disabled3);
        ImGui::SameLine();
        bool place_it = (ImGui::SmallButton("Move It") && !disabled3);

        if (disabled3)
            ImGui::PopStyleVar();

        if (find_it || place_it)
        {
            auto iter =
                std::find_if(extracted_positions_.begin(), extracted_positions_.end(), [=](const PositionInfo& info) { return info.node == node; });
            if (iter != extracted_positions_.end())
            {
                for (auto& info : extracted_positions_)
                    info.selected = false;
                iter->selected = true;
                viewer_->SetCameraDistance(EGO_ZOOM_DIST);
                viewer_->MoveCameraWithoutOriginOffset(iter->x, iter->y);
                MoveMouse(iter->x, iter->y);
                if (place_it)
                {
                    last_picked_position_info_     = *iter;
                    data_model_.markers_to_update_ = true;
                    pending_move_count_            = 5;
                }
            }
        }
    }

    if (expanded)
    {
        bool disabled = (data_model_.mode_ != StudioMode::COMPOSER);
        if (disabled)
            ImGui::BeginDisabled(true);

        int attr_ui_id = 0;
        for (auto attr = node.first_attribute(); attr; attr = attr.next_attribute(), ++attr_ui_id)
        {
            if (!attr.name())
                continue;

            std::vector<std::string> enums;

            ImGui::PushID(attr_ui_id);
            auto        type                    = QueryValueTypeFromSchema(data_model_.xml_schema_, node.name(), attr.name(), &enums);
            std::string attr_name               = attr.name();
            bool        isAttrSearchHighlighted = (attr == search_highlight_attr_);
            bool        scroll_to_attr          = (attr_to_scroll_to_ == attr);
            bool        is_attr_invalid         = (data_model_.invalid_attrs_.count(attr) > 0);

            if (isAttrSearchHighlighted)
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 64, 64, 255));

            if (is_attr_invalid)
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.6f, 0.0f, 0.0f, 0.6f));

            if (strstr(attr.value(), "$") != nullptr)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 64, 255));
                std::string buf(attr.value());
                buf.resize(1024);
                if (ImGui::InputText((std::string(attr.name()) + " (EXPRESSION)").c_str(), buf.data(), buf.size()))
                {
                    data_model_.UpdateAttribute(node, attr.name(), buf.c_str());
                    SetModified();
                }
                ImGui::PopStyleColor();

                // Evaluate and display result
                std::string eval_result = data_model_.EvaluateString(attr.value());
                if (!eval_result.empty())
                {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), " = %s", eval_result.c_str());
                }
                else if (strlen(attr.value()) > 0)
                {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), " [Error]");
                }
            }
            else if (attr_name == "entityRef")
            {
                auto scenario_names = data_model_.GetScenarioObjectNames();
                if (!scenario_names.empty())
                {
                    std::string current_value = attr.value();
                    int         current_idx   = find_enum_index(scenario_names, current_value.c_str());
                    if (current_idx < 0)
                    {
                        scenario_names.insert(scenario_names.begin(), current_value);
                        current_idx = 0;
                    }
                    std::vector<const char*> values;
                    for (auto& name : scenario_names)
                        values.push_back(name.c_str());
                    if (ImGui::Combo(attr.name(), &current_idx, values.data(), scenario_names.size()))
                    {
                        attr.set_value(scenario_names[current_idx].c_str());
                        SetModified();
                    }
                }
                else
                {
                    std::string buf(attr.value());
                    buf.resize(512);
                    if (ImGui::InputText(attr.name(), buf.data(), buf.size()))
                    {
                        attr.set_value(buf.c_str());
                        SetModified();
                    }
                }
            }
            else if (!enums.empty())
            {
                int                      enum_idx = find_enum_index(enums, attr.value());
                std::vector<const char*> values;
                for (auto& e : enums)
                    values.push_back(e.c_str());
                if (ImGui::Combo(attr.name(), &enum_idx, values.data(), enums.size()))
                {
                    attr.set_value(enums[enum_idx].c_str());
                    SetModified();
                }
            }
            else if (type == "UnsignedInt" || type == "UnsignedShort")
            {
                // step/step_fast set to 0 hides the +/- buttons and gives a plain keyboard-editable field
                int v = attr.as_int();
                if (ImGui::InputInt(attr.name(), &v, 0, 0))
                {
                    if (v < 0)
                        v = 0;
                    attr.set_value(v);
                    SetModified();
                }
            }
            else if (type == "Int")
            {
                int v = attr.as_int();
                if (ImGui::InputInt(attr.name(), &v, 0, 0))
                {
                    attr.set_value(v);
                    SetModified();
                }
            }
            else if (type == "Double" || type == "Float")
            {
                double v = attr.as_double();
                if (ImGui::InputDouble(attr.name(), &v, 0.0, 0.0, "%g", ImGuiInputTextFlags_CharsScientific))
                {
                    attr.set_value(v);
                    SetModified();
                }
            }
            else if (type == "Boolean")
            {
                bool v = attr.as_bool();
                if (ImGui::Checkbox(attr.name(), &v))
                {
                    attr.set_value(v);
                    SetModified();
                }
            }
            else
            {
                std::string buf(attr.value());
                buf.resize(512);

                // Check if this is a path or filepath attribute
                bool is_path_attr     = (attr_name == "path");
                bool is_filepath_attr = (attr_name == "filepath");

                if (ImGui::InputText(attr.name(), buf.data(), buf.size()))
                {
                    std::string new_value(buf.c_str());
                    std::string old_value = attr.value();
                    if (attr_name == "name")
                    {
                        if (new_value.empty())
                        {
                            new_value = data_model_.GetDefaultNodeName(node);
                        }
                        else
                        {
                            new_value = data_model_.EnsureUniqueName(new_value, node);
                        }
                        if (data_model_.IsScenarioObjectNode(node))
                        {
                            data_model_.UpdateEntityRefs(old_value, new_value);
                        }
                    }
                    attr.set_value(new_value.c_str());
                    SetModified();
                }

                // Add "choose" button for path/filepath attributes
                if (is_path_attr || is_filepath_attr)
                {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("choose"))
                    {
                        std::string selected_path;
                        if (is_path_attr)
                        {
                            // Use folder selection dialog
                            auto result = pfd::select_folder("Select Folder", "").result();
                            if (!result.empty())
                            {
                                selected_path = normalize_path(result);
                            }
                        }
                        else  // is_filepath_attr
                        {
                            // Use file selection dialog
                            std::vector<std::string> empty;
                            bool                     is_map_path = (strcmp(node.name(), "LogicFile") == 0);
                            is_map_path &= node.parent() && strcmp(node.parent().name(), "RoadNetwork") == 0;
                            is_map_path &= node.parent() && node.parent().parent() && strcmp(node.parent().parent().name(), "OpenSCENARIO") == 0;
                            auto result = pfd::open_file("Select File", "", is_map_path ? map_filter : empty).result();
                            if (!result.empty())
                            {
                                selected_path = normalize_path(result[0]);
                            }
                        }

                        if (!selected_path.empty())
                        {
                            attr.set_value(selected_path.c_str());
                            SetModified();
                        }
                    }
                }
            }
            if (scroll_to_attr)
            {
                ImGui::ScrollToRect(ImGui::GetCurrentWindow(),
                                    ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()),
                                    ImGuiScrollFlags_KeepVisibleCenterY);
                attr_to_scroll_to_ = pugi::xml_attribute();
            }
            if (isAttrSearchHighlighted)
                ImGui::PopStyleColor();
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                ImGui::OpenPopup("Attr Context Menu");
            if (ImGui::BeginPopup("Attr Context Menu"))
            {
                if (strstr(attr.value(), "$"))
                {
                    if (ImGui::MenuItem("Reset to Value"))
                    {
                        attr.set_value("");
                        SetModified();
                        ImGui::CloseCurrentPopup();
                    }
                }
                else if (ImGui::MenuItem("Convert to Expression"))
                {
                    attr.set_value((std::string("${") + attr.value() + std::string("}")).c_str());
                    SetModified();
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::IsKeyPressedMap(ImGuiKey_Escape))
                {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            if (is_attr_invalid)
                ImGui::PopStyleColor();

            ImGui::PopID();
        }
        if (disabled)
            ImGui::EndDisabled();

        int node_ui_id = 0;
        for (pugi::xml_node child = node.first_child(); child; child = child.next_sibling(), ++node_ui_id)
        {
            ImGui::PushID(node_ui_id);
            RenderXmlSubTree(child, level + 1, force_expand, suppress_context_menu, level_to_expand - 1, level_to_collapse - 1, node_matches_filter);
            ImGui::PopID();
        }

        ImGui::TreePop();
    }

    if (scroll_to_node_and_attrs)
    {
        auto rect_max2 = ImGui::GetItemRectMax();
        ImGui::ScrollToRect(ImGui::GetCurrentWindow(), ImRect(rect_min, rect_max2), ImGuiScrollFlags_KeepVisibleCenterY);
        node_and_attrs_to_scroll_to_ = pugi::xml_node();
    }

    if (isHighlighted)
        ImGui::PopStyleColor();

    if (level == 0)
    {
        nodes_to_expand_.clear();
        nodes_to_collapse_.clear();
    }
}

void StudioGui::HandleElementContextMenu()
{
    // Check for node modify menu to open
    if (xml_node_modify_menu_to_open)
    {
        xml_node_modify_menu_to_open = false;

        attrs_to_create = QueryOptionalAttrsFromSchema(data_model_.xml_schema_, modify_menu_context);

        // Refresh element lists after ensuring required children exist
        elements_to_create = QuerySubelementsWithModelInfo(data_model_.xml_schema_, modify_menu_context);

        ExcludeExistingAttrs(modify_menu_context_node, &attrs_to_create, &attrs_to_delete);
        ExcludeExistingElements(data_model_.xml_schema_, modify_menu_context_node, &elements_to_create);

        // Filter out deprecated elements and attributes from create menus (but keep them for delete menus)
        FilterDeprecatedSubelements(&elements_to_create);
        FilterDeprecatedAttributes(&attrs_to_create, data_model_.xml_schema_, modify_menu_context_node.name());

        ImGui::OpenPopup("modify_menu");
    }

    // Render the node modify menu
    if (ImGui::BeginPopup("modify_menu"))
    {
        bool           can_delete_node = false;
        pugi::xml_node parent          = modify_menu_context_node.parent();
        if (!parent.empty() && !parent.path().empty())
        {
            std::string parentName = parent.name();
            std::string childName  = modify_menu_context_node.name();
            auto        subInfos   = QuerySubelementsWithModelInfo(data_model_.xml_schema_, parentName);
            auto        iter = std::find_if(subInfos.begin(), subInfos.end(), [&](const SubelementInfo& info) { return info.name == childName; });
            if (iter != subInfos.end())
                can_delete_node = (CountSiblingByName(parent, childName) > iter->min_occurs);
        }

        bool delete_this_node =
            (ImGui::MenuItem("Delete Node", nullptr, false, can_delete_node || data_model_.invalid_nodes_.count(modify_menu_context_node) > 0));

        std::vector<std::string>    deletable_attrs    = attrs_to_delete;
        std::vector<std::string>    creatable_attrs    = attrs_to_create;
        std::vector<SubelementInfo> creatable_elements = elements_to_create;

        auto required_attrs   = QueryRequiredAttrsFromSchema(data_model_.xml_schema_,
                                                           QueryTypeByNameFromSchema(data_model_.xml_schema_, modify_menu_context_node.name()));
        auto is_required_attr = [&](const std::string& attr)
        { return std::find(required_attrs.begin(), required_attrs.end(), attr) != required_attrs.end(); };
        deletable_attrs.erase(std::remove_if(deletable_attrs.begin(), deletable_attrs.end(), is_required_attr), deletable_attrs.end());

        if (!deletable_attrs.empty() && ImGui::BeginMenu("Delete Attribute"))
        {
            for (auto& attr : deletable_attrs)
            {
                if (ImGui::MenuItem(attr.c_str()))
                {
                    data_model_.DeleteAttribute(modify_menu_context_node, attr);
                    SetModified();
                    LOG("Attribute [%s] deleted from node [%s]", attr.c_str(), modify_menu_context_node.name());
                }
            }
            ImGui::EndMenu();
        }

        new_attr_value.reserve(1024);

        if (!creatable_attrs.empty() && ImGui::BeginMenu("Create Attribute"))
        {
            for (auto& attr : creatable_attrs)
            {
                if (ImGui::MenuItem(attr.c_str()))
                {
                    attr_to_create_name = attr;
                    if (attr == "name")
                    {
                        new_attr_value = data_model_.GetDefaultNodeName(modify_menu_context_node);
                    }
                    else if (attr == "entityRef")
                    {
                        auto scenario_names = data_model_.GetScenarioObjectNames();
                        if (!scenario_names.empty())
                        {
                            new_attr_value = scenario_names.front();
                        }
                        else
                        {
                            new_attr_value.clear();
                        }
                    }
                    else
                    {
                        new_attr_value = GetDefaultValueForAttribute(data_model_.xml_schema_, modify_menu_context, attr);
                    }
                    attr_value_dialog_to_open = true;
                }
            }
            ImGui::EndMenu();
        }

        if (!creatable_elements.empty() && ImGui::BeginMenu("Create Element"))
        {
            for (auto& element : creatable_elements)
            {
                int  count     = CountSiblingByName(modify_menu_context_node, element.name);
                bool canCreate = (element.is_max_unbounded || (element.max_occurs >= 0 && count < element.max_occurs));

                if (ImGui::MenuItem(element.name.c_str(), nullptr, false, canCreate))
                {
                    node_to_create_name          = element.name;
                    node_creation_dialog_to_open = true;
                }
            }
            ImGui::EndMenu();
        }

        pugi::xml_node prev_sibling = modify_menu_context_node.previous_sibling();
        if (ImGui::MenuItem("Move Node Up", nullptr, nullptr, prev_sibling))
            data_model_.MoveNodeUp(modify_menu_context_node);
        pugi::xml_node next_sibling = modify_menu_context_node.next_sibling();
        if (ImGui::MenuItem("Move Node Down", nullptr, nullptr, next_sibling))
            data_model_.MoveNodeDown(modify_menu_context_node);

        bool can_duplicate_node = false;
        if (!parent.empty() && !parent.path().empty())
        {
            auto infos = QuerySubelementsWithModelInfo(data_model_.xml_schema_, parent.name());
            auto iter  = std::find_if(infos.begin(),
                                     infos.end(),
                                     [&](const SubelementInfo& info) { return info.name == std::string(modify_menu_context_node.name()); });
            if (iter->is_max_unbounded || iter->max_occurs < 0)
            {
                can_duplicate_node = true;
            }
            else
            {
                int count = 0;
                for (auto sibling = parent.child(modify_menu_context_node.name()); sibling;
                     sibling      = sibling.next_sibling(modify_menu_context_node.name()))
                    ++count;
                if (count < iter->max_occurs)
                    can_duplicate_node = true;
            }
        }
        if (ImGui::MenuItem("Duplicate Node", nullptr, false, can_duplicate_node))
        {
            auto new_node = data_model_.DuplicateNode(modify_menu_context_node);
            QueueSubtreeExpansion(new_node);
        }

        if (delete_this_node)
        {
            if (!modify_menu_context_node.empty())
            {
                data_model_.DeleteNode(modify_menu_context_node);
            }
        }

        ImGui::EndPopup();
    }
}

void StudioGui::QueueNodeExpansion(pugi::xml_node node)
{
    GetNodesAbove(node, &nodes_to_expand_);
}

void StudioGui::QueueSubtreeExpansion(pugi::xml_node node)
{
    GetNodesBelow(node, &nodes_to_expand_);
}

void StudioGui::MoveMouse(double x, double y)
{
    viewer_->MoveMouse(x, y);
}

void StudioGui::SetModified()
{
    data_model_.SetModified();
    // search_highlight_node_ = pugi::xml_node();
    // search_highlight_attr_ = pugi::xml_attribute();
    scenario_object_map_dirty_ = true;  // Mark mapping as dirty when XML is modified
}

void StudioGui::SwitchToViewer()
{
    if (data_model_.mode_ == StudioMode::VIEWER)
        return;

    if (data_model_.mode_ == StudioMode::COMPOSER)
        viewer_->BackupViewSetting();

    data_model_.prev_mode_ = data_model_.mode_;
    data_model_.mode_      = StudioMode::VIEWER;
    viewer_->osgViewer_->setDone(true);
    ClearPositionMarkers();
}

void StudioGui::SwitchToComposer()
{
    if (data_model_.mode_ == StudioMode::COMPOSER)
        return;
    data_model_.prev_mode_ = data_model_.mode_;
    data_model_.mode_      = StudioMode::COMPOSER;
    viewer_->osgViewer_->setDone(true);
    viewer_->RestoreViewSetting();
    data_model_.markers_to_update_ = true;
}

void StudioGui::HandleSpaceKey()
{
    // Handle space key based on current mode
    if (data_model_.mode_ == StudioMode::COMPOSER)
    {
        // COMPOSER -> VIEWER: Save and switch to viewer
        if (data_model_.SaveXoscXml(data_model_.tmp_xosc_path_))
            SwitchToViewer();
    }
    else
    {
        viewer::KeyEvent event;
        event.down_       = false;
        event.key_        = static_cast<int>(KeyType::KEY_Space);
        event.modKeyMask_ = 0;
        FetchKeyEvent(&event, nullptr);
    }
}

// Log window implementation
void StudioGui::AddLogMessage(const char* message)
{
    if (message && strlen(message) > 0)
    {
        if (!log_messages_.empty() && log_messages_.back() == message)
            return;

        log_messages_.push_back(std::string(message));

        // Keep only the latest max_log_messages_ messages
        if (log_messages_.size() > static_cast<size_t>(max_log_messages_))
        {
            log_messages_.pop_front();
        }
    }
}

void StudioGui::RenderLogWindow()
{
    float log_height = std::floor(data_model_.viewport_height_ * data_model_.log_height_ratio_);
    ImGui::SetNextWindowPos(ImVec2(0.0f, data_model_.viewport_height_ - data_model_.time_bar_height_ - log_height));
    ImGui::SetNextWindowSize(ImVec2(data_model_.viewport_width_ - data_model_.xml_panel_width_, log_height));
    ImGui::SetNextWindowBgAlpha(1.0f);

    ImGui::Begin("Log", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize);

    // Window header with controls
    if (ImGui::Button("Clear"))
    {
        log_messages_.clear();
        log_text_buffer_.clear();
    }
    ImGui::SameLine();
    if (ImGui::Button("Copy All"))
    {
        ImGui::SetClipboardText(log_text_buffer_.c_str());
    }
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &log_auto_scroll_);
    ImGui::SameLine(ImGui::GetWindowWidth() - 50);
    if (ImGui::Button("^"))
    {
        data_model_.log_height_ratio_ = std::min(0.8, data_model_.log_height_ratio_ + 0.15);
        data_model_.SaveConfig();
    }
    ImGui::SameLine();
    if (ImGui::Button("v"))
    {
        data_model_.log_height_ratio_ = std::max(0.1, data_model_.log_height_ratio_ - 0.15);
        data_model_.SaveConfig();
    }
    ImGui::Separator();

    // Rebuild log text buffer if messages changed
    static size_t last_log_count           = 0;
    static int    scroll_to_bottom_counter = 0;
    static int    total_line_count         = 0;

    if (log_messages_.size() != last_log_count)
    {
        log_text_buffer_.clear();
        total_line_count = 0;
        for (const auto& log_msg : log_messages_)
        {
            log_text_buffer_ += log_msg;
            log_text_buffer_ += "\n";
            total_line_count++;
            // Also count newlines inside the message if any
            for (char c : log_msg)
            {
                if (c == '\n')
                    total_line_count++;
            }
        }
        last_log_count = log_messages_.size();

        // Mark that we need to scroll to bottom
        if (log_auto_scroll_)
        {
            scroll_to_bottom_counter = 2;
        }
    }

    // Display log messages in a scrollable child window
    // Enable horizontal scrollbar for the child window
    ImGui::BeginChild("log_scrolling_region", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

    // Calculate required height for the text area
    // We add extra padding (2 lines) to ensure the last line is fully visible and avoid scrollbar trigger
    float line_height      = ImGui::GetTextLineHeight();
    float required_height  = (total_line_count + 2) * line_height;
    float available_height = ImGui::GetContentRegionAvail().y;

    // Use the larger of required height or available height
    // This forces InputTextMultiline to expand and effectively disables its internal vertical scrollbar
    // allowing the parent Child window to handle scrolling
    float input_height = std::max(required_height, available_height);

    // Use InputTextMultiline for selectable, read-only text
    ImGuiInputTextFlags flags = ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_NoHorizontalScroll;

    // Force hide scrollbar by setting its size to 0
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 0.0f);

    // Set width to -FLT_MIN to fill width, but allow parent horizontal scroll if needed
    ImGui::InputTextMultiline("##log_text",
                              const_cast<char*>(log_text_buffer_.c_str()),
                              log_text_buffer_.size() + 1,
                              ImVec2(-FLT_MIN, input_height),
                              flags);

    // Handle Space key to switch mode (which is otherwise captured by InputTextMultiline)
    if (ImGui::IsItemActive() && ImGui::IsKeyReleased(ImGuiKey_Space))
    {
        HandleSpaceKey();
    }

    ImGui::PopStyleVar();

    // Save selection state before opening context menu
    static std::string saved_selection;
    static bool        has_saved_selection = false;

    // Check if user is right-clicking
    bool is_right_clicking = ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right);

    if (is_right_clicking)
    {
        // Save current selection before context menu opens
        ImGuiInputTextState* state = ImGui::GetInputTextState(ImGui::GetID("##log_text"));

        if (state && state->HasSelection())
        {
            int select_start = state->Stb.select_start;
            int select_end   = state->Stb.select_end;
            if (select_start > select_end)
                std::swap(select_start, select_end);

            saved_selection     = log_text_buffer_.substr(select_start, select_end - select_start);
            has_saved_selection = true;
        }
        else
        {
            has_saved_selection = false;
        }

        ImGui::OpenPopup("log_context_menu");
    }

    // Handle right-click context menu for copy
    if (ImGui::BeginPopup("log_context_menu"))
    {
        if (ImGui::MenuItem("Copy Selected", nullptr, false, has_saved_selection))
        {
            if (has_saved_selection)
            {
                ImGui::SetClipboardText(saved_selection.c_str());
            }
        }
        if (ImGui::MenuItem("Copy All"))
        {
            ImGui::SetClipboardText(log_text_buffer_.c_str());
        }
        ImGui::EndPopup();
    }

    // Auto-scroll: scroll parent window to bottom
    if (scroll_to_bottom_counter > 0)
    {
        ImGui::SetScrollHereY(1.0f);
        scroll_to_bottom_counter--;
    }

    ImGui::EndChild();
    ImGui::End();
}

void StudioGui::UpdateMousePositionFromWorld()
{
    auto& pos          = viewer_->GetMousePosition();
    hud_mouse_world_x_ = pos.x();
    hud_mouse_world_y_ = pos.y();
    hud_mouse_world_z_ = 0.0;
    ConvertWorldPosToLanePos(pos.x(),
                             pos.y(),
                             0.0,
                             0.0,
                             /*align_to_lane=*/true,
                             &hud_mouse_road_id_,
                             &hud_mouse_lane_id_,
                             &hud_mouse_s_,
                             &hud_mouse_lane_offset_,
                             &hud_mouse_lane_h_);
}

void StudioGui::RenderRealTimeHUD()
{
    if (!hud_show_position_)
        return;

    // Set up HUD window
    ImGui::SetNextWindowPos(ImVec2(0, data_model_.menu_bar_height_), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(200, 170), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.5f);

    // Create semi-transparent HUD window
    ImGui::Begin("Mouse Position HUD",
                 &hud_show_position_,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    ImGui::Text("Mouse Position (World):");
    ImGui::Text("X: %.2f m", hud_mouse_world_x_);
    ImGui::Text("Y: %.2f m", hud_mouse_world_y_);
    ImGui::Text("Z: %.2f m", hud_mouse_world_z_);

    if (hud_mouse_road_id_ >= 0)
    {
        ImGui::Separator();
        ImGui::Text("Lane Position:");
        ImGui::Text("Road ID: %d", hud_mouse_road_id_);
        ImGui::Text("Lane ID: %d", hud_mouse_lane_id_);
        ImGui::Text("S: %.2f m", hud_mouse_s_);
        ImGui::Text("Lane Offset: %.2f m", hud_mouse_lane_offset_);
    }
    else
    {
        ImGui::Separator();
        ImGui::Text("Offroad");
    }

    ImGui::End();
}

void StudioGui::ExtractPositionsFromXml()
{
    // Ensure scenario object mapping is up-to-date
    if (scenario_object_map_dirty_)
        UpdateScenarioObjectMapping();

    extracted_positions_.clear();  // Clear any previous results
    data_model_.ExtractPositionsRecursive(data_model_.xml_doc_, "", &extracted_positions_);
}

void StudioGui::ClearPositionMarkers()
{
    if (!viewer_->rootnode_)
        return;

    if (position_markers_group)
        position_markers_group->removeChildren(0, position_markers_group->getNumChildren());
}

void StudioGui::DrawPositionMarkers()
{
    // Get the scene root node
    osg::MatrixTransform* rootNode = viewer_->rootnode_;
    if (!rootNode)
    {
        LOG("Error: Cannot get scene root node");
        return;
    }

    if (!position_markers_group)
    {
        position_markers_group = new osg::Group();
        position_markers_group->setName("PositionMarkers");
        rootNode->addChild(position_markers_group);
    }
    else
    {  // Clear existing markers
        position_markers_group->removeChildren(0, position_markers_group->getNumChildren());
    }

    // Create geometric objects once and cache them
    static osg::ref_ptr<osg::Node> green_sphere_node;
    static osg::ref_ptr<osg::Node> blue_box_node;
    static osg::ref_ptr<osg::Node> red_cylinder_node;
    static osg::ref_ptr<osg::Node> yellow_cone_node;

    // Create different marker geometries if not already created
    if (!green_sphere_node)
        green_sphere_node = CreateGreenSphereGeometry(1.0, 16, 16);  // radius, latitude, longitude

    if (!blue_box_node)
        blue_box_node = CreateBlueBoxGeometry(2.0, 1.0, 1.0);  // width, height, depth

    if (!red_cylinder_node)
        red_cylinder_node = CreateRedCylinderGeometry(0.8, 2.0, 16);  // radius, height, segments

    if (!yellow_cone_node)
        yellow_cone_node = CreateYellowConeGeometry(1.0, 2.5, 16);  // radius, height, segments

    // Process each extracted position
    for (size_t i = 0; i < extracted_positions_.size(); i++)
    {
        PositionInfo& pos = extracted_positions_[i];

        // Handle RelativeLanePosition by calculating absolute world coordinates
        if (pos.type == PositionType::RELATIVE_LANE_POSITION && !pos.entity_ref.empty())
        {
            // Find the reference entity's position (can be LanePosition or WorldPosition)
            bool found = false;
            for (const auto& ref_pos : extracted_positions_)
            {
                if (ref_pos.name == pos.entity_ref && (ref_pos.type == PositionType::LANE_POSITION || ref_pos.type == PositionType::WORLD_POSITION))
                {
                    ConvertRelLanePosToWorldPos(ref_pos, &pos);
                    found = true;
                    break;
                }
            }

            if (!found)
            {
                LOG((std::string("Reference entity not found for RelativeLanePosition: ") + pos.entity_ref).c_str());
                continue;  // Skip if reference not found or no road manager
            }
        }

        // Create transformation node for this position
        // Use PositionAttitudeTransform to simplify position and attitude settings
        osg::ref_ptr<osg::PositionAttitudeTransform> marker_transform = new osg::PositionAttitudeTransform();
        marker_transform->setName("PositionMarker_" + std::to_string(i));
        marker_transform->setPosition(osg::Vec3(static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(pos.z)));
        marker_transform->setAttitude(osg::Quat(pos.absolute_h, osg::Vec3(0.0, 0.0, 1.0)));

        if (pos.object_type == "Vehicle" || pos.object_type == "Pedestrian" || pos.object_type == "MiscObject")
        {
            // Vehicle initialization positions - use blue box or OSGB model
            osg::ref_ptr<osg::Node> vehicle_model;
            if (!pos.name.empty())
            {
                // Try to get the specific model for this scenario object
                vehicle_model = GetOSGBModelForScenarioObject(pos.name);

                if (!vehicle_model)
                {
                    LOG(("No specific OSGB model found for vehicle: " + pos.name + " - using blue box marker").c_str());
                    // Fallback to blue box if no specific model is available
                    vehicle_model = blue_box_node;
                }
                else
                {
                    // LOG(("Loaded OSGB model for vehicle: " + pos.vehicle_name).c_str());
                }
            }
            else
            {
                vehicle_model = blue_box_node;
            }

            // Add the vehicle model to the marker
            if (vehicle_model)
                marker_transform->addChild(vehicle_model);
        }
        else if (pos.object_type == "Position Related")
        {
            // Condition positions - use yellow cone (more visible than green sphere)
            marker_transform->addChild(yellow_cone_node);
        }
        else
        {
            // Default: use green sphere for other types
            marker_transform->addChild(green_sphere_node);
        }

        position_markers_group->addChild(marker_transform);

        // Create and add 3D text label if we have meaningful information
        if (!pos.name.empty())
        {
            osg::ref_ptr<osg::Geode> text_geode;
            if (heading_operation_active_ && pos.node == heading_position_info_.node)
            {
                // Append the heading value being edited, marking whether it is lane-relative or absolute
                char suffix[64];
                snprintf(suffix, sizeof(suffix), " (%s h=%.1f deg)", heading_is_relative_ ? "rel" : "abs", heading_preview_h_ * 180.0 / M_PI);
                PositionInfo labeled_pos = pos;
                labeled_pos.name += suffix;
                text_geode = CreateTextLabelGeode(labeled_pos);
            }
            else
            {
                text_geode = CreateTextLabelGeode(pos);
            }
            if (text_geode)
            {
                // Position text slightly offset from the marker
                osg::ref_ptr<osg::MatrixTransform> text_transform = new osg::MatrixTransform();
                osg::Matrix text_matrix = osg::Matrix::translate(pos.x + 2.0, pos.y + 2.0, pos.z + 30.0);  // Offset text position
                text_transform->setMatrix(text_matrix);
                text_transform->addChild(text_geode);
                position_markers_group->addChild(text_transform);
            }
        }
    }

    // Draw trajectory/path lines connecting vertices
    // Group positions by their parent Trajectory/Polyline
    std::map<pugi::xml_node, std::vector<const PositionInfo*>> trajectory_groups;

    for (const auto& pos : extracted_positions_)
    {
        // Check if this position is part of a Vertex in a Polyline/Trajectory
        pugi::xml_node current = pos.node;
        pugi::xml_node vertex_node;
        pugi::xml_node polyline_node;

        // Walk up to find Vertex and Polyline nodes
        while (current)
        {
            std::string node_name = current.name();
            if (node_name == "Vertex")
            {
                vertex_node = current;
            }
            else if (node_name == "Polyline" || node_name == "Clothoid" || node_name == "Nurbs")
            {
                polyline_node = current;
                break;
            }
            current = current.parent();
        }

        // If this position is part of a trajectory, add it to the group
        if (vertex_node && polyline_node)
            trajectory_groups[polyline_node].push_back(&pos);
    }

    // Draw lines for each trajectory group
    for (const auto& [polyline_node, positions] : trajectory_groups)
    {
        if (positions.size() < 2)
            continue;  // Need at least 2 points to draw a line

        auto line_geode = CreateLineStripGeode(positions);
        position_markers_group->addChild(line_geode);
    }

    // Draw an auxiliary line from the entity towards the mouse while its heading is being modified
    if (heading_operation_active_)
    {
        PositionInfo line_start = heading_position_info_;
        PositionInfo line_end;
        line_end.x = hud_mouse_world_x_;
        line_end.y = hud_mouse_world_y_;
        line_end.z = heading_position_info_.z;
        std::vector<const PositionInfo*> line_points = {&line_start, &line_end};
        position_markers_group->addChild(CreateLineStripGeode(line_points));
    }
}

std::string StudioGui::GetEntryNameFromScenarioObject(const std::string& scenario_object_name) const
{
    // Access the cached mapping directly without updating (const method)
    auto it = scenario_object_name_to_entryname_.find(scenario_object_name);
    if (it != scenario_object_name_to_entryname_.end())
    {
        return it->second;
    }
    return "";
}

void StudioGui::UpdateScenarioObjectMapping()
{
    // Save old mapping to detect changes
    std::map<std::string, std::string> old_mapping = scenario_object_name_to_entryname_;
    scenario_object_name_to_entryname_.clear();
    scenario_object_map_dirty_ = false;

    if (data_model_.xml_doc_.empty())
        return;

    // Use ScenarioReader to parse the entire scenario
    // This populates temp_entities_ with fully resolved objects

    // 1. Reset ScenarioReader state
    // Clear entities (ScenarioReader appends to them)
    // Note: We need to manually delete objects if we own them, but ScenarioReader/Entities destructor handles it?
    // Entities destructor deletes objects in object_ and object_pool_.
    // But we are reusing the Entities instance.
    // We should clear and delete contents.
    for (auto* obj : temp_entities_->object_)
        delete obj;
    temp_entities_->object_.clear();
    for (auto* obj : temp_entities_->object_pool_)
        delete obj;
    temp_entities_->object_pool_.clear();

    // 2. Load XML into ScenarioReader
    PrepareReader();
    scenario_reader_->loadOSCMem(data_model_.xml_doc_);

    // 3. Parse Catalogs and Entities
    scenario_reader_->parseGlobalParameterDeclarations();
    scenario_reader_->parseCatalogs();
    try
    {
        scenario_reader_->parseEntities();
    }
    catch (std::runtime_error& e)
    {
        LOG("Failed to parse entities. %s", e.what());
    }

    // 4. Update mapping and detect changes
    std::vector<std::string> models_to_invalidate;

    for (auto* obj : temp_entities_->object_)
    {
        std::string object_name = obj->GetName();
        if (object_name.empty())
            continue;

        // Use the resolved model filename as the key for caching
        std::string model_key = obj->GetModelFileName();
        if (model_key.empty() && obj->model_id_ >= 0)
            model_key = SE_Env::Inst().GetModelFilenameById(obj->model_id_);

        scenario_object_name_to_entryname_[object_name] = model_key;

        // Check if model changed
        auto old_it = old_mapping.find(object_name);
        if (old_it != old_mapping.end())
        {
            if (old_it->second != model_key)
            {
                // Add old model key to invalidate list
                if (!old_it->second.empty())
                    models_to_invalidate.push_back(old_it->second);
            }
        }
    }

    // Invalidate cached models that are no longer valid
    if (!models_to_invalidate.empty())
    {
        auto it =
            std::remove_if(osgb_model_cache_.begin(),
                           osgb_model_cache_.end(),
                           [&models_to_invalidate](const OSGBModel& model)
                           { return std::find(models_to_invalidate.begin(), models_to_invalidate.end(), model.name) != models_to_invalidate.end(); });
        osgb_model_cache_.erase(it, osgb_model_cache_.end());
    }
}

osg::ref_ptr<osg::Node> StudioGui::GetOSGBModelForScenarioObject(const std::string& scenario_object_name)
{
    // Ensure mapping is up to date
    if (scenario_object_map_dirty_)
        UpdateScenarioObjectMapping();

    // Look up object in parsed entities
    scenarioengine::Object* obj = temp_entities_->GetObjectByName(scenario_object_name);
    if (!obj)
        return nullptr;

    // Get model filename directly from the object
    std::string osgb_filename = obj->GetModelFileName();

    if (osgb_filename.empty() && obj->model_id_ >= 0)
        osgb_filename = SE_Env::Inst().GetModelFilenameById(obj->model_id_);

    if (osgb_filename.empty())
    {
        LOG("Failed to determine OSGB filename for: %s", scenario_object_name.c_str());
        return nullptr;
    }

    // Check cache
    std::string model_key = scenario_object_name_to_entryname_[scenario_object_name];
    if (model_key.empty())
        model_key = osgb_filename;

    OSGBModel* cached_model = FindOSGBModelInCache(model_key);
    if (cached_model && cached_model->is_loaded)
        return cached_model->node;

    // Generate possible paths - exactly matching Viewer::CreateEntityModel behavior
    std::vector<std::string> possible_paths;

    // First try to use the direct path as provided - exactly matching esmini behavior
    possible_paths.push_back(osgb_filename);

    // Then check registered paths - exactly matching esmini behavior
    for (size_t i = 0; i < SE_Env::Inst().GetPaths().size(); i++)
    {
        possible_paths.push_back(SE_Env::Inst().GetPaths()[i] + "/" + osgb_filename);
        possible_paths.push_back(SE_Env::Inst().GetPaths()[i] + "/../models/" + osgb_filename);
        possible_paths.push_back(SE_Env::Inst().GetPaths()[i] + "/../resources/models/" + osgb_filename);
        possible_paths.push_back(SE_Env::Inst().GetPaths()[i] + "/" + FileNameOf(osgb_filename));
    }

    // Get object's scale mode - mimicking Viewer::CreateEntityModel behavior
    EntityScaleMode scale_mode = obj->scaleMode_;

    // Set up bounding box - exactly matching Viewer::CreateEntityModel behavior
    OSCBoundingBox bounding_box;

    // Initialize with default car dimensions (same as Viewer::CreateEntityModel)
    bounding_box.dimensions_.width_  = 4.5;  // Default width
    bounding_box.dimensions_.length_ = 1.8;  // Default length
    bounding_box.dimensions_.height_ = 1.5;  // Default height

    // Set center - same as Viewer::CreateEntityModel
    bounding_box.center_.x_ = 1.5;   // Default center X
    bounding_box.center_.y_ = 0.0;   // Default center Y
    bounding_box.center_.z_ = 0.75;  // Default center Z

    // Validate bounding box dimensions
    if (bounding_box.dimensions_.width_ <= 0 || bounding_box.dimensions_.length_ <= 0 || bounding_box.dimensions_.height_ <= 0)
    {
        LOG("Invalid bounding box dimensions for %s, using defaults", scenario_object_name.c_str());
        bounding_box.dimensions_.width_  = 4.5;
        bounding_box.dimensions_.length_ = 1.8;
        bounding_box.dimensions_.height_ = 1.5;
        bounding_box.center_.x_          = 1.5;
        bounding_box.center_.y_          = 0.0;
        bounding_box.center_.z_          = 0.75;
    }

    osg::ref_ptr<osg::Node> loaded_node = nullptr;

    // Try to load from all possible paths - exactly matching Viewer::CreateEntityModel behavior
    for (size_t i = 0; i < possible_paths.size(); i++)
    {
        if (FileExists(possible_paths[i].c_str()))
        {
            loaded_node = LoadOSGBModelWithScale(possible_paths[i], scale_mode, bounding_box);
            if (loaded_node)
            {
                AddOSGBModelToCache(model_key, osgb_filename, loaded_node);
                return loaded_node;
            }
        }
    }

    // If all paths failed, create a default virtual model - exactly matching Viewer::CreateEntityModel behavior
    if (osgb_filename.empty())
    {
        LOG("No filename specified for model! - creating a dummy model");
    }
    else
    {
        LOG("Failed to load visual model %s. %s", osgb_filename.c_str(), possible_paths.size() > 1 ? "Also tried the following paths:" : "");
        for (size_t i = 1; i < possible_paths.size(); i++)
            LOG("    %s", possible_paths[i].c_str());
        LOG("Creating a dummy model instead");
    }

    // Define color arrays to match esmini's viewer.cpp
    float color_light_gray[3] = {0.7f, 0.7f, 0.7f};

    // Determine color based on entity index - exactly matching esmini's logic
    float* color;
    float  b = 1.0;  // brightness

    // Use a static counter to simulate entity index behavior
    static int entity_counter = 0;
    int        index          = entity_counter % 4;
    entity_counter++;

    if (index == 0)
        color = color_light_gray;
    else if (index == 1)
        color = color_red;
    else if (index == 2)
        color = color_blue;
    else
        color = color_yellow;

    // Create material with correct colors
    osg::ref_ptr<osg::Material> material = new osg::Material();
    material->setDiffuse(osg::Material::FRONT, osg::Vec4(b * color[0], b * color[1], b * color[2], 1.0f));
    material->setAmbient(osg::Material::FRONT, osg::Vec4(b * color[0], b * color[1], b * color[2], 1.0f));

    // Create groups and transforms to match esmini structure
    osg::ref_ptr<osg::Group>           group      = new osg::Group;
    osg::ref_ptr<osg::MatrixTransform> modeltx    = new osg::MatrixTransform;
    osg::ref_ptr<osg::Group>           modelgroup = new osg::Group;
    osg::ref_ptr<osg::Group>           bbGroup    = new osg::Group;

    // Create box geometry with center position matching esmini
    osg::ref_ptr<osg::Geode> bbGeode = new osg::Geode;
    bbGeode->addDrawable(new osg::ShapeDrawable(new osg::Box(osg::Vec3(bounding_box.center_.x_, bounding_box.center_.y_, bounding_box.center_.z_),
                                                             bounding_box.dimensions_.length_,
                                                             bounding_box.dimensions_.width_,
                                                             bounding_box.dimensions_.height_)));

    //// Draw only wireframe - match esmini
    // osg::PolygonMode* polygonMode = new osg::PolygonMode;
    // polygonMode->setMode(osg::PolygonMode::FRONT_AND_BACK, osg::PolygonMode::LINE);
    // osg::ref_ptr<osg::StateSet> stateset = bbGeode->getOrCreateStateSet();
    // stateset->setAttributeAndModes(polygonMode, osg::StateAttribute::OVERRIDE | osg::StateAttribute::ON);
    // stateset->setMode(GL_LIGHTING, osg::StateAttribute::OFF | osg::StateAttribute::OVERRIDE);
    //
    //// Set node mask - assuming NODE_MASK_ENTITY_BB is defined in scenariostudio
    //// If not, use a default value that makes sense in the context
    // const unsigned int NODE_MASK_ENTITY_BB = 0x1000;
    // bbGeode->setNodeMask(NODE_MASK_ENTITY_BB);
    //
    //// Add center point marker - match esmini
    // osg::ref_ptr<osg::Geode> center = new osg::Geode;
    // center->addDrawable(new osg::ShapeDrawable(new osg::Sphere(osg::Vec3(0.0f, 0.0f, 0.0f), 0.2f)));
    // center->setNodeMask(NODE_MASK_ENTITY_BB);

    // Apply material to bounding box group
    bbGroup->addChild(bbGeode);
    // bbGroup->addChild(center);
    bbGroup->getOrCreateStateSet()->setAttribute(material);
    bbGroup->setName("BoundingBox");

    // Build the node hierarchy to match esmini
    modeltx->addChild(modelgroup);
    group->addChild(modeltx);
    group->addChild(bbGroup);
    group->setName(model_key);

    // Configure transparency and depth test - match esmini
    osg::ref_ptr<osg::BlendColor> blend_color = new osg::BlendColor(osg::Vec4(1, 1, 1, 1));
    blend_color->setDataVariance(osg::Object::DYNAMIC);
    osg::ref_ptr<osg::StateSet> state_set = group->getOrCreateStateSet();

    // Configure state set attributes
    osg::BlendFunc* bf = new osg::BlendFunc(osg::BlendFunc::CONSTANT_ALPHA, osg::BlendFunc::ONE_MINUS_CONSTANT_ALPHA);
    state_set->setAttributeAndModes(blend_color);
    state_set->setAttributeAndModes(bf);
    state_set->setMode(GL_DEPTH_TEST, osg::StateAttribute::ON);
    state_set->setRenderingHint(osg::StateSet::TRANSPARENT_BIN);

    loaded_node = group;
    AddOSGBModelToCache(model_key, osgb_filename, loaded_node);
    return loaded_node;
}

// Load OSGB model with EntityScaleMode support - mimicking Viewer::CreateEntityModel behavior
osg::ref_ptr<osg::Node> StudioGui::LoadOSGBModelWithScale(const std::string&    file_path,
                                                          EntityScaleMode       scale_mode,
                                                          const OSCBoundingBox& bounding_box)
{
    // Check if file exists - mimicking Viewer::CreateEntityModel behavior
    std::ifstream file(file_path.c_str());
    if (!file.good())
    {
        LOG(("OSGB file not found: " + file_path).c_str());
        // Create a virtual model when file not found - same as Viewer::CreateEntityModel
        return CreateBoxGeometry(bounding_box.dimensions_.width_,
                                 bounding_box.dimensions_.length_,
                                 bounding_box.dimensions_.height_,
                                 osg::Vec4(0.8f, 0.2f, 0.2f, 1.0f));
    }
    file.close();

    try
    {
        // Create OSG reader for OSGB files
        osg::ref_ptr<osg::Node> node = osgDB::readNodeFile(file_path);

        if (node)
        {
            LOG(("Successfully loaded OSGB model: " + file_path).c_str());

            // Apply scale based on EntityScaleMode - mimicking Viewer::CreateEntityModel behavior
            if (scale_mode == EntityScaleMode::MODEL_TO_BB && bounding_box.dimensions_.width_ > 0 && bounding_box.dimensions_.length_ > 0 &&
                bounding_box.dimensions_.height_ > 0)
            {
                // Calculate scaling factors to match the requested bounding box
                // Since we can't use ComputeBoundsVisitor, we'll use the default model dimensions
                // that Viewer::CreateEntityModel would use as reference
                double ref_width  = 4.0;  // Default model width reference
                double ref_length = 2.0;  // Default model length reference
                double ref_height = 1.5;  // Default model height reference

                // Calculate scale factors
                double scale_x = bounding_box.dimensions_.width_ / ref_width;
                double scale_y = bounding_box.dimensions_.length_ / ref_length;
                double scale_z = bounding_box.dimensions_.height_ / ref_height;

                // Build the log message correctly
                std::string log_message = "Applying MODEL_TO_BB scaling: x=" + std::to_string(scale_x) + ", y=" + std::to_string(scale_y) +
                                          ", z=" + std::to_string(scale_z);
                LOG(log_message.c_str());

                // Create a transform node to apply scaling - same as Viewer::CreateEntityModel
                osg::ref_ptr<osg::PositionAttitudeTransform> transform = new osg::PositionAttitudeTransform;
                transform->setScale(osg::Vec3(scale_x, scale_y, scale_z));
                transform->addChild(node);

                return transform;
            }
            else if (scale_mode == EntityScaleMode::BB_TO_MODEL)
            {
                // For BB_TO_MODEL mode, we don't scale the model, but would use the model's bounds
                // to update the entity's bounding box. However, since we're only returning the model here,
                // we just return the node without scaling - matching Viewer::CreateEntityModel behavior
                LOG("Using BB_TO_MODEL mode - returning model without scaling");
                return node;
            }
            else
            {
                // Default case - return model without scaling
                return node;
            }
        }
        else
        {
            LOG(("Failed to parse OSGB model: " + file_path).c_str());
            // Create a virtual model when parsing fails - mimicking Viewer::CreateEntityModel behavior
            return CreateBoxGeometry(bounding_box.dimensions_.width_,
                                     bounding_box.dimensions_.length_,
                                     bounding_box.dimensions_.height_,
                                     osg::Vec4(0.8f, 0.2f, 0.2f, 1.0f));
        }
    }
    catch (const std::exception& e)
    {
        LOG(("Exception loading OSGB model " + file_path + ": " + e.what()).c_str());
        // Create a virtual model when exception occurs - mimicking Viewer::CreateEntityModel behavior
        return CreateBoxGeometry(bounding_box.dimensions_.width_,
                                 bounding_box.dimensions_.length_,
                                 bounding_box.dimensions_.height_,
                                 osg::Vec4(0.8f, 0.2f, 0.2f, 1.0f));
    }
    catch (...)
    {
        LOG(("Unknown exception loading OSGB model: " + file_path).c_str());
        // Catch all other exceptions - mimicking robust error handling in Viewer::CreateEntityModel
        return CreateBoxGeometry(bounding_box.dimensions_.width_,
                                 bounding_box.dimensions_.length_,
                                 bounding_box.dimensions_.height_,
                                 osg::Vec4(0.8f, 0.2f, 0.2f, 1.0f));
    }
}

StudioGui::OSGBModel* StudioGui::FindOSGBModelInCache(const std::string& model_name)
{
    for (auto& model : osgb_model_cache_)
    {
        if (model.name == model_name)
        {
            return &model;
        }
    }
    return nullptr;
}

void StudioGui::AddOSGBModelToCache(const std::string& model_name, const std::string& file_path, osg::ref_ptr<osg::Node> node)
{
    // Remove existing entry if it exists
    auto it = std::remove_if(osgb_model_cache_.begin(),
                             osgb_model_cache_.end(),
                             [&model_name](const OSGBModel& model) { return model.name == model_name; });
    osgb_model_cache_.erase(it, osgb_model_cache_.end());

    // Add new entry
    OSGBModel new_model;
    new_model.name      = model_name;
    new_model.file_path = file_path;
    new_model.node      = node;
    new_model.is_loaded = (node != nullptr);

    osgb_model_cache_.push_back(new_model);

    // LOG(("Added OSGB model to cache: " + model_name + " (" + file_path + ")").c_str());
}

int StudioGui::GetModelIdFromCatalogEntry(const std::string& catalog_name, const std::string& entry_name)
{
    // Step 1: Find CatalogLocations to determine catalog directory
    pugi::xml_node catalog_locations = data_model_.RootNode().child("CatalogLocations");
    if (!catalog_locations)
    {
        LOG("GetModelIdFromCatalogEntry: CatalogLocations not found");
        return -1;
    }

    // Find the specific catalog directory
    std::string catalog_dir;
    for (pugi::xml_node catalog_node : catalog_locations.children())
    {
        if (std::string(catalog_node.name()) == catalog_name)
        {
            pugi::xml_node dir_node = catalog_node.child("Directory");
            if (dir_node)
            {
                catalog_dir = dir_node.attribute("path").as_string("");
                break;
            }
        }
    }

    if (catalog_dir.empty())
    {
        LOG(("GetModelIdFromCatalogEntry: Catalog directory not found for: " + catalog_name).c_str());
        return -1;
    }

    // Step 2: Construct catalog file path
    std::vector<std::string> catalog_paths;

    // Relative to current scenario file
    std::string scenario_dir = DirNameOf(data_model_.xosc_path_);
    if (!scenario_dir.empty())
        catalog_paths.push_back(scenario_dir + "/" + catalog_dir + "/" + entry_name + ".xosc");

    // Direct path
    catalog_paths.push_back(catalog_dir + "/" + entry_name + ".xosc");

    // Relative to registered paths
    for (const auto& env_path : SE_Env::Inst().GetPaths())
        catalog_paths.push_back(env_path + "/" + catalog_dir + "/" + entry_name + ".xosc");

    // Step 3: Try to load catalog file
    pugi::xml_document catalog_doc;
    bool               loaded = false;

    for (const auto& path : catalog_paths)
    {
        if (FileExists(path.c_str()))
        {
            pugi::xml_parse_result result = catalog_doc.load_file(path.c_str());
            if (result)
            {
                loaded = true;
                LOG(("Loaded catalog entry from: " + path).c_str());
                break;
            }
        }
    }

    if (!loaded)
    {
        LOG(("GetModelIdFromCatalogEntry: Failed to load catalog file for entry: " + entry_name).c_str());
        return -1;
    }

    // Step 4: Parse catalog to find the entry
    pugi::xml_node catalog_root = catalog_doc.child("OpenSCENARIO");
    if (!catalog_root)
    {
        LOG("GetModelIdFromCatalogEntry: Catalog file has no OpenSCENARIO root");
        return -1;
    }

    pugi::xml_node catalog = catalog_root.child("Catalog");
    if (!catalog)
    {
        LOG("GetModelIdFromCatalogEntry: Catalog file has no Catalog node");
        return -1;
    }

    // Find the entry
    pugi::xml_node entity_node;
    std::string    entity_type;

    for (pugi::xml_node entry : catalog.children())
    {
        std::string name_attr = entry.attribute("name").as_string("");
        if (name_attr == entry_name)
        {
            // Found the entry, find the actual entity definition inside
            pugi::xml_node vehicle     = entry.child("Vehicle");
            pugi::xml_node pedestrian  = entry.child("Pedestrian");
            pugi::xml_node misc_object = entry.child("MiscObject");

            if (vehicle)
            {
                entity_node = vehicle;
                entity_type = "Vehicle";
            }
            else if (pedestrian)
            {
                entity_node = pedestrian;
                entity_type = "Pedestrian";
            }
            else if (misc_object)
            {
                entity_node = misc_object;
                entity_type = "MiscObject";
            }
            break;
        }
    }

    if (!entity_node)
    {
        LOG(("GetModelIdFromCatalogEntry: Entry not found in catalog: " + entry_name).c_str());
        return -1;
    }

    // Step 5: Extract model_id from Properties
    pugi::xml_node props = entity_node.child("Properties");
    if (props)
    {
        for (pugi::xml_node prop : props.children("Property"))
        {
            if (std::string(prop.attribute("name").as_string()) == "model_id")
            {
                int model_id = prop.attribute("value").as_int(-1);
                LOG(("Found model_id=" + std::to_string(model_id) + " in catalog entry: " + entry_name).c_str());
                return model_id;
            }
        }
    }

    // Step 6: If no model_id found, try to determine default based on category
    std::string category =
        entity_node
            .attribute(entity_type == "Vehicle" ? "vehicleCategory" : (entity_type == "Pedestrian" ? "pedestrianCategory" : "miscObjectCategory"))
            .as_string("");

    if (!category.empty())
    {
        // Use ScenarioReader to parse the entitynode directly to get model_id
        // This ensures exact esmini logic
        int default_id = -1;

        if (entity_type == "Vehicle")
        {
            // For catalog entries, we assume first vehicle (index 0) since we don't have full context
            temp_entities_->object_.clear();
            temp_entities_->object_pool_.clear();

            scenarioengine::Vehicle* parsed_vehicle = scenario_reader_->parseOSCVehicle(entity_node);
            if (parsed_vehicle)
            {
                default_id = parsed_vehicle->model_id_;
                delete parsed_vehicle;
            }

            temp_entities_->object_.clear();
            temp_entities_->object_pool_.clear();
        }
        else if (entity_type == "Pedestrian")
        {
            scenarioengine::Pedestrian* parsed_pedestrian = scenario_reader_->parseOSCPedestrian(entity_node);
            if (parsed_pedestrian)
            {
                default_id = parsed_pedestrian->model_id_;
                delete parsed_pedestrian;
            }
        }
        else if (entity_type == "MiscObject")
        {
            scenarioengine::MiscObject* parsed_misc = scenario_reader_->parseOSCMiscObject(entity_node);
            if (parsed_misc)
            {
                default_id = parsed_misc->model_id_;
                delete parsed_misc;
            }
        }

        if (default_id >= 0)
        {
            LOG(("Using default model_id=" + std::to_string(default_id) + " for " + entity_type + " category=" + category +
                 " in catalog entry: " + entry_name)
                    .c_str());
            return default_id;
        }
    }

    LOG(("GetModelIdFromCatalogEntry: No model_id found for entry: " + entry_name).c_str());
    return -1;
}

// Vehicle/object picking function using distance calculation
PositionInfo* StudioGui::PickPosition()
{
    // Update mouse world position first
    UpdateMousePositionFromWorld();

    if (extracted_positions_.empty())
        return nullptr;

    double mouse_x = hud_mouse_world_x_;
    double mouse_y = hud_mouse_world_y_;
    double mouse_z = hud_mouse_world_z_;

    PositionInfo* best_position = nullptr;
    double        min_distance  = 3.0;  // Maximum distance threshold in meters

    for (auto& pos : extracted_positions_)
    {
        pos.selected = false;
        // Calculate distance to this position
        double dx       = mouse_x - pos.x;
        double dy       = mouse_y - pos.y;
        double dz       = mouse_z - pos.z;
        double distance = std::sqrt(dx * dx + dy * dy + dz * dz);

        // Check if this is a vehicle, object, or condition position and within threshold
        if (distance < min_distance)
        {
            min_distance  = distance;
            best_position = &pos;
        }
    }

    if (best_position)
    {
        // Store additional info for menu distinction
        best_position->selected        = true;
        data_model_.markers_to_update_ = true;
        last_picked_position_info_     = *best_position;

        // Highlight the specific position node instead of just the ScenarioObject
        HighlightXmlNodeForPosition(*best_position);
    }

    return best_position;
}

void StudioGui::ClearXmlHighlights()
{
    highlighted_nodes_.clear();
}

void StudioGui::HighlightXmlNodeForPosition(const PositionInfo& posInfo)
{
    ClearXmlHighlights();
    highlighted_nodes_.push_back(posInfo.node);
    node_and_attrs_to_scroll_to_ = posInfo.node;
    QueueNodeExpansion(posInfo.node);
    QueueSubtreeExpansion(posInfo.node);
}

void StudioGui::StartMoveOperation(PositionInfo* position_info)
{
    if (!position_info)
    {
        LOG("Error: No position selected for move operation");
        return;
    }

    move_operation_active_ = true;

    // Create a deep copy of the position info to ensure stability during move operation
    // This prevents pointer invalidation when XML is re-parsed
    static PositionInfo locked_position_copy;
    locked_position_copy = *position_info;  // Copy all data including xml_node pointer

    // Store the copy pointer - this will remain stable during the entire move operation
    selected_position_for_move_ = &locked_position_copy;
}

void StudioGui::UpdateMoveOperation()
{
    if (!move_operation_active_ || !selected_position_for_move_)
        return;

    // Check if left mouse button is pressed to end the move operation
    if (!left_mouse_pressed_)
    {
        // CRITICAL: Update position from current mouse world coordinates WITHOUT re-evaluating distance
        // Use the locked target position info that was set when move operation started
        UpdatePositionFromLockedTarget(selected_position_for_move_);

        // LOG(("Position updated during move operation (locked target: " + current_locked_target + ")").c_str());
    }
    else
    {
        // Left mouse button pressed - end the move operation
        // LOG("Left mouse pressed - ending move operation");
        EndMoveOperation();
    }
}

void StudioGui::UpdatePositionFromLockedTarget(PositionInfo* position_info)
{
    if (!position_info)
    {
        LOG("Error: No locked target position info provided");
        return;
    }

    // Get current mouse position in world coordinates
    UpdateMousePositionFromWorld();

    // Update the PositionInfo with new values
    position_info->x           = hud_mouse_world_x_;
    position_info->y           = hud_mouse_world_y_;
    position_info->z           = hud_mouse_world_z_;
    position_info->h           = 0.0;
    position_info->road_id     = hud_mouse_road_id_;
    position_info->lane_id     = hud_mouse_lane_id_;
    position_info->s           = hud_mouse_s_;
    position_info->lane_offset = hud_mouse_lane_offset_;

    // Special handling for RelativeLanePosition
    if (position_info->type == PositionType::RELATIVE_LANE_POSITION && !position_info->entity_ref.empty())
    {
        // Find the reference entity's position (can be LanePosition or WorldPosition)
        bool found = false;
        for (const auto& ref_pos : extracted_positions_)
        {
            if (ref_pos.name == position_info->entity_ref &&
                (ref_pos.type == PositionType::LANE_POSITION || ref_pos.type == PositionType::WORLD_POSITION))
            {
                ConvertWorldPosToRelLanePos(ref_pos, position_info);
                found = true;
                break;
            }
        }

        if (!found)
        {
            LOG((std::string("Warning: Reference entity not found for RelativeLanePosition: ") + position_info->entity_ref).c_str());
        }
    }
    // Update HUD mouse lane information
    hud_mouse_road_id_     = position_info->road_id;
    hud_mouse_lane_id_     = position_info->lane_id;
    hud_mouse_s_           = position_info->s;
    hud_mouse_lane_offset_ = position_info->lane_offset;

    UpdateXmlPositionNode(position_info->node, *position_info);
    // SetModified();  // Mark scenario as modified
    //  LOG("XML position node updated successfully");

    data_model_.markers_to_update_ = true;  // Force marker refresh
    ExtractPositionsFromXml();              // Re-extract positions to update markers
}

void StudioGui::EndMoveOperation()
{
    if (!move_operation_active_)
        return;

    move_operation_active_      = false;
    selected_position_for_move_ = nullptr;
}

void StudioGui::StartHeadingOperation(const PositionInfo& position_info)
{
    if (position_info.node.empty() ||
        (position_info.type != PositionType::LANE_POSITION && position_info.type != PositionType::WORLD_POSITION))
    {
        LOG("StartHeadingOperation: only LanePosition and WorldPosition are supported");
        return;
    }

    heading_operation_active_ = true;
    heading_position_info_    = position_info;
    heading_is_relative_      = false;
    heading_base_h_           = 0.0;
    heading_preview_h_        = position_info.h;

    // Snapshot the original orientation state so that cancel can restore it
    if (position_info.type == PositionType::WORLD_POSITION)
    {
        pugi::xml_attribute h_attr        = position_info.node.attribute("h");
        heading_orig_attr_present_        = !h_attr.empty();
        heading_orig_h_value_             = h_attr.value();
        heading_orig_orientation_present_ = false;
    }
    else  // LanePosition
    {
        pugi::xml_node      orientation   = position_info.node.child("Orientation");
        pugi::xml_attribute h_attr        = orientation.attribute("h");
        heading_orig_orientation_present_ = !orientation.empty();
        heading_orig_attr_present_        = !h_attr.empty();
        heading_orig_h_value_             = h_attr.value();

        // A LanePosition without Orientation is lane-aligned, i.e. relative with h=0
        heading_is_relative_ = position_info.orientation_is_relative;
        if (heading_is_relative_)
        {
            // Lane direction at the position, needed to convert the absolute mouse direction
            // into the lane-relative h value stored in the file
            heading_base_h_ = GetAngleInInterval2PI(position_info.absolute_h - position_info.h);
        }
    }

    data_model_.markers_to_update_ = true;
}

void StudioGui::UpdateHeadingOperation()
{
    if (!heading_operation_active_)
        return;

    UpdateMousePositionFromWorld();

    double dx = hud_mouse_world_x_ - heading_position_info_.x;
    double dy = hud_mouse_world_y_ - heading_position_info_.y;
    if (dx * dx + dy * dy < 0.25)
        return;  // mouse too close to the entity, direction would be unstable

    // Absolute (world) heading pointing from the entity towards the mouse. For a lane-relative
    // orientation convert it into a lane-relative value, so the resulting absolute heading still
    // matches the mouse direction after the lane direction is added back at playback
    double target_h    = GetAngleInInterval2PI(atan2(dy, dx));
    double h           = heading_is_relative_ ? GetAngleInInterval2PI(target_h - heading_base_h_) : target_h;
    heading_preview_h_ = h;

    if (heading_position_info_.type == PositionType::WORLD_POSITION)
    {
        pugi::xml_attribute h_attr = heading_position_info_.node.attribute("h");
        if (h_attr.empty())
            h_attr = heading_position_info_.node.append_attribute("h");
        h_attr.set_value(std::to_string(h).c_str());
    }
    else
    {
        pugi::xml_node orientation = heading_position_info_.node.child("Orientation");
        if (orientation.empty())
        {
            orientation = heading_position_info_.node.append_child("Orientation");
            orientation.append_attribute("type").set_value("relative");
        }
        pugi::xml_attribute h_attr = orientation.attribute("h");
        if (h_attr.empty())
            h_attr = orientation.append_attribute("h");
        h_attr.set_value(std::to_string(h).c_str());
    }

    // No SetModified() during the preview: the document hash change already triggers re-extraction
    // and marker redraw, a single undo state is pushed on confirm
    data_model_.markers_to_update_ = true;
}

void StudioGui::ConfirmHeadingOperation()
{
    if (!heading_operation_active_)
        return;

    heading_operation_active_ = false;
    SetModified();  // single undo step for the whole heading change
    data_model_.markers_to_update_ = true;
}

void StudioGui::CancelHeadingOperation()
{
    if (!heading_operation_active_)
        return;

    heading_operation_active_ = false;

    // Restore the original orientation state
    pugi::xml_node node = heading_position_info_.node;
    if (heading_position_info_.type == PositionType::WORLD_POSITION)
    {
        if (heading_orig_attr_present_)
            node.attribute("h").set_value(heading_orig_h_value_.c_str());
        else
            node.remove_attribute(node.attribute("h"));
    }
    else
    {
        pugi::xml_node orientation = node.child("Orientation");
        if (!heading_orig_orientation_present_)
            node.remove_child(orientation);
        else if (heading_orig_attr_present_)
            orientation.attribute("h").set_value(heading_orig_h_value_.c_str());
        else
            orientation.remove_attribute(orientation.attribute("h"));
    }

    data_model_.markers_to_update_ = true;
}

void StudioGui::RenderValidationReport()
{
    if (!show_validation_window_)
        return;

    ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Validation Report", &show_validation_window_))
    {
        if (data_model_.validation_errors_.empty())
        {
            ImGui::TextColored(ImVec4(0, 1, 0, 1), "No errors were found. Scenario is valid.");
        }
        else
        {
            ImGui::TextColored(ImVec4(1, 0, 0, 1), "Found %d errors:", (int)data_model_.validation_errors_.size());
            ImGui::Separator();

            if (ImGui::BeginTable("ValidationErrors", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
            {
                ImGui::TableSetupColumn("Message", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Node", ImGuiTableColumnFlags_WidthFixed, 150.0f);
                ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 120.0f);
                ImGui::TableHeadersRow();

                for (const auto& error : data_model_.validation_errors_)
                {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextWrapped("%s", error.message.c_str());

                    ImGui::TableNextColumn();
                    ImGui::Text("%s", error.node.name());
                    if (!error.attr_name.empty())
                    {
                        ImGui::SameLine();
                        ImGui::TextDisabled("(%s)", error.attr_name.c_str());
                    }

                    ImGui::TableNextColumn();
                    std::string id_suffix = std::to_string((uintptr_t)error.node.internal_object()) + error.attr_name;
                    if (ImGui::Button(("Jump##" + id_suffix).c_str()))
                    {
                        // Highlight and jump to node
                        QueueNodeExpansion(error.node);
                        search_highlight_node_ = error.node;
                        search_highlight_attr_ = error.attr;
                        node_to_scroll_to_     = error.node;
                        attr_to_scroll_to_     = error.attr;
                    }

                    if (error.can_be_fixed)
                    {
                        ImGui::SameLine();
                        if (ImGui::Button(("Fix It##" + id_suffix).c_str()))
                        {
                            pugi::xml_node      node = error.node;
                            pugi::xml_attribute attr = node.attribute(error.attr_name.c_str());
                            if (!attr)
                            {
                                attr = node.append_attribute(error.attr_name.c_str());
                            }
                            std::string default_val = GetDefaultValueForAttribute(data_model_.xml_schema_, node.name(), error.attr_name);
                            attr.set_value(default_val.c_str());
                            SetModified();
                        }
                    }
                }
                ImGui::EndTable();
            }
        }
    }
    ImGui::End();
}
