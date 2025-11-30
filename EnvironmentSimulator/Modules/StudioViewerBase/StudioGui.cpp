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
#include <experimental/filesystem>
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

    std::vector<std::string> scenario_filter = {"OpenSCENARIO Files", "*.xosc"};

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
    xml_panel_status = ImGui::Begin("XML Tree", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize);
    if (xml_panel_status)
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

        ImGui::BeginChild("node", ImVec2(-FLT_MIN, -FLT_MIN), ImGuiChildFlags_Border);
        ImGui::PushItemWidth(TREE_VIEW_WIDTH);
        RenderXmlSubTree(data_model_.RootNode(), 0);
        ImGui::PopItemWidth();
        ImGui::EndChild();

        HandleElementContextMenu();
        HandleAttrDialog();
        HandleNodeDialog();
        HandleMovePositionMenu();
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
                if (ImGui::Combo(attr_to_create_name.c_str(), &current_idx, get_enum, &scenario_names, scenario_names.size()))
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
            if (ImGui::Combo(attr_to_create_name.c_str(), &enum_idx, get_enum, &enums, enums.size()))
            {
                new_attr_value = enums[enum_idx];
            }
            value_handled = true;
        }

        if (!value_handled && type == "UnsignedInt")
        {
            int v = new_attr_value.empty() ? 0 : atoi(new_attr_value.c_str());
            if (ImGui::DragInt(attr_to_create_name.c_str(), &v, 0.05, 0, 20))
            {
                new_attr_value = std::to_string(v);
            }
            value_handled = true;
        }
        if (!value_handled && type == "Int")
        {
            int v = new_attr_value.empty() ? 0 : atoi(new_attr_value.c_str());
            if (ImGui::DragInt(attr_to_create_name.c_str(), &v, 0.05, -6, 6))
            {
                new_attr_value = std::to_string(v);
            }
            value_handled = true;
        }
        if (!value_handled && type == "Double")
        {
            double v = new_attr_value.empty() ? 0.0 : atof(new_attr_value.c_str());
            if (ImGui::InputDouble(attr_to_create_name.c_str(), &v, 0.1, 0.0, "%g", ImGuiInputTextFlags_CharsDecimal))
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

        bool ok_confirmed = ImGui::Button("OK", ImVec2(ok_width, 0.0f));
        if (ImGui::IsKeyPressedMap(ImGuiKey_Enter))
            ok_confirmed = true;
        pugi::xml_node source_node = temp_doc.child("temp").first_child();
        bool           is_pos_node = (strcmp(source_node.name(), "LanePosition") == 0 || strcmp(source_node.name(), "WorldPosition") == 0);
        if (ok_confirmed || is_pos_node)
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
            if (is_pos_node)
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
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Clear"))
            {
                data_model_.Clear();
                positions_extracted_       = false;
                to_reset_camera_pos_       = true;
                scenario_object_map_dirty_ = true;
            }
            if (ImGui::MenuItem("Open..."))
            {
                auto result = pfd::open_file("Choose an OpenSCENARIO file", "", scenario_filter).result();
                if (!result.empty())
                {
                    if (data_model_.LoadXoscXml(result[0]))
                    {
                        LOG("Successfully the OpenSCENARIO file: [%s].", result[0].c_str());
                        scenario_object_map_dirty_ = true;  // Mark mapping as dirty when XML is loaded
                        if (data_model_.mode_ != StudioMode::COMPOSER)
                            SwitchToComposer();
                        to_reset_camera_pos_ = true;
                    }
                }
            }
            if (ImGui::MenuItem("Save", "Ctrl+S"))
                to_save_file = true;
            if (ImGui::MenuItem("Save as..."))
            {
                to_save_file = true;
                backup_path  = data_model_.xosc_path_;
                data_model_.xosc_path_.clear();
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
                }
            }
            if (to_save_file)
                data_model_.SaveXoscXml(data_model_.xosc_path_);
            to_save_file = false;
        }

        if (ImGui::BeginMenu("Edit"))
        {
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, data_model_.CanUndo()))
            {
                Undo();
            }
            if (ImGui::MenuItem("Redo", "Ctrl+Shift+Z", false, data_model_.CanRedo()))
            {
                Redo();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Validate Scenario"))
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

void StudioGui::Undo()
{
    data_model_.Undo();
    scenario_object_map_dirty_ = true;
    positions_extracted_       = false;
}

void StudioGui::Redo()
{
    data_model_.Redo();
    scenario_object_map_dirty_ = true;
    positions_extracted_       = false;
}

void StudioGui::RenderTimeline()
{
    ImGui::SetNextWindowPos(ImVec2(0, data_model_.viewport_height_ - data_model_.time_bar_height_));
    ImGui::SetNextWindowSize((ImVec2(data_model_.viewport_width_, data_model_.time_bar_height_)));
    ImGui::SetNextWindowBgAlpha(1.0f);
    ImGui::Begin("##timebar", nullptr, ImGuiWindowFlags_NoDecoration);
    ImGui::PushItemWidth(-1);
    data_model_.virtual_time_max_value_ = std::max(data_model_.virtual_time_, data_model_.virtual_time_max_value_);

    if (data_model_.mode_ == StudioMode::COMPOSER)
        ImGui::BeginDisabled(true);
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
    if (data_model_.mode_ == StudioMode::COMPOSER)
        ImGui::EndDisabled();
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
                if (data_model_.mode_ != StudioMode::COMPOSER)
                    SwitchToComposer();
            }

            // Legacy Ctrl shortcuts handling (can be removed if ImGui handles them, but keeping for safety)
            if (ea.getEventType() == osgGA::GUIEventAdapter::KEYDOWN && (mod & osgGA::GUIEventAdapter::MODKEY_CTRL))
            {
                if (c == 'z' && (mod & osgGA::GUIEventAdapter::MODKEY_SHIFT) && data_model_.CanRedo())  // Ctrl + SHIFT + Z
                {
                    Redo();
                }
                else if (c == 'z' && data_model_.CanUndo())  // Ctrl + Z
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
                if (ea.getButtonMask() & osgGA::GUIEventAdapter::LEFT_MOUSE_BUTTON)
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
                else if (ea.getButtonMask() & osgGA::GUIEventAdapter::RIGHT_MOUSE_BUTTON)
                {
                    if (!move_operation_active_)
                    {
                        // Right mouse button - new functionality for move context menu
                        auto* pos_info = PickPosition();
                        if (pos_info)
                            move_context_menu_to_open_ = true;
                    }
                }
            }
        }
        case osgGA::GUIEventAdapter::DRAG:
        case osgGA::GUIEventAdapter::MOVE:
        {
            io.MousePos = ImVec2(ea.getX(), io.DisplaySize.y - ea.getY());

            // Update move operation during drag if active
            if (move_operation_active_)
            {
                UpdateMoveOperation();
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
        ImGui::SameLine();
        bool find_it = ImGui::SmallButton("Find It");
        ImGui::SameLine();
        bool place_it = ImGui::SmallButton("Move It");
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
                    if (ImGui::Combo(attr.name(), &current_idx, get_enum, &scenario_names, scenario_names.size()))
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
                int enum_idx = find_enum_index(enums, attr.value());
                if (ImGui::Combo(attr.name(), &enum_idx, get_enum, &enums, enums.size()))
                {
                    attr.set_value(enums[enum_idx].c_str());
                    SetModified();
                }
            }
            else if (type == "UnsignedInt")
            {
                int v = attr.as_int();
                if (ImGui::DragInt(attr.name(), &v, 0.05, 0, 20))
                {
                    attr.set_value(v);
                    SetModified();
                }
            }
            else if (type == "Int")
            {
                int v = attr.as_int();
                if (ImGui::DragInt(attr.name(), &v, 0.05, -6, 6))
                {
                    attr.set_value(v);
                    SetModified();
                }
            }
            else if (type == "Double")
            {
                double v = attr.as_double();
                if (ImGui::InputDouble(attr.name(), &v, 0.1, 0.0, "%g", ImGuiInputTextFlags_CharsDecimal))
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
            osg::ref_ptr<osg::Geode> text_geode = CreateTextLabelGeode(pos);
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

void StudioGui::RenderValidationReport()
{
    if (!show_validation_window_)
        return;

    ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Validation Report", &show_validation_window_))
    {
        if (data_model_.validation_errors_.empty())
        {
            ImGui::TextColored(ImVec4(0, 1, 0, 1), "No errors found. Scenario is valid.");
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
