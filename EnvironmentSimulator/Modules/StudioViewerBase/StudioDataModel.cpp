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

#include "CommonMini.hpp"
#include "StudioDataModel.hpp"
#include "PosUtil.hpp"
#include "XmlUtil.hpp"
#include "simple_expr.h"
#include <algorithm>
#include <functional>
#include <set>
#include <map>
#include <sstream>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <nlohmann/json.hpp>

#ifdef __linux__
#include <unistd.h>
#else
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

extern std::string              g_xosc_path;
extern std::string              g_seed;
extern std::string              g_timestep;
extern std::vector<std::string> g_paths;
extern int                      g_path_count_from_cmd;

static void ReplaceStringInPlace(std::string& subject, const std::string& search, const std::string& replace)
{
    size_t pos = 0;
    while ((pos = subject.find(search, pos)) != std::string::npos)
    {
        subject.replace(pos, search.length(), replace);
        pos += replace.length();
    }
}

void strip(std::string* s)
{
    auto start_it = std::find_if_not(s->begin(), s->end(), [](unsigned char c) { return std::isspace(c); });
    s->erase(s->begin(), start_it);

    auto end_it = std::find_if_not(s->rbegin(), s->rend(), [](unsigned char c) { return std::isspace(c); }).base();
    s->erase(end_it, s->end());
}

std::string GetConfigFilePath()
{
    // Get the path to the executable
    char buffer[MAX_PATH];
#ifdef _WIN32
    GetModuleFileNameA(NULL, buffer, MAX_PATH);
#else
    ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (len != -1)
    {
        buffer[len] = '\0';
    }
#endif

    std::string exe_path(buffer);
    size_t      last_slash = exe_path.find_last_of("\\/");
    std::string exe_dir    = exe_path.substr(0, last_slash);

    return exe_dir + "/config.ini";
}

bool LoadConfigValue(const std::string& key, int& value)
{
    std::string   config_path = GetConfigFilePath();
    std::ifstream config_file(config_path);

    if (!config_file.is_open())
        return false;

    std::string line;
    while (std::getline(config_file, line))
    {
        size_t eq_pos = line.find('=');
        if (eq_pos == std::string::npos)
            continue;

        std::string line_key   = line.substr(0, eq_pos);
        std::string line_value = line.substr(eq_pos + 1);

        // Trim whitespace
        line_key.erase(0, line_key.find_first_not_of(" \t\r\n"));
        line_key.erase(line_key.find_last_not_of(" \t\r\n") + 1);
        line_value.erase(0, line_value.find_first_not_of(" \t\r\n"));
        line_value.erase(line_value.find_last_not_of(" \t\r\n") + 1);

        if (line_key == key)
        {
            try
            {
                value = std::stoi(line_value);
                config_file.close();
                return true;
            }
            catch (const std::exception&)
            {
                config_file.close();
                return false;
            }
        }
    }

    config_file.close();
    return false;
}

bool LoadConfigValue(const std::string& key, float& value)
{
    std::string   config_path = GetConfigFilePath();
    std::ifstream config_file(config_path);

    if (!config_file.is_open())
        return false;

    std::string line;
    while (std::getline(config_file, line))
    {
        size_t eq_pos = line.find('=');
        if (eq_pos == std::string::npos)
            continue;

        std::string line_key   = line.substr(0, eq_pos);
        std::string line_value = line.substr(eq_pos + 1);

        // Trim whitespace
        line_key.erase(0, line_key.find_first_not_of(" \t\r\n"));
        line_key.erase(line_key.find_last_not_of(" \t\r\n") + 1);
        line_value.erase(0, line_value.find_first_not_of(" \t\r\n"));
        line_value.erase(line_value.find_last_not_of(" \t\r\n") + 1);

        if (line_key == key)
        {
            try
            {
                value = std::stof(line_value);
                config_file.close();
                return true;
            }
            catch (const std::exception&)
            {
                config_file.close();
                return false;
            }
        }
    }

    config_file.close();
    return false;
}

bool LoadConfigValue(const std::string& key, std::string& value)
{
    std::string   config_path = GetConfigFilePath();
    std::ifstream config_file(config_path);

    if (!config_file.is_open())
        return false;

    std::string line;
    while (std::getline(config_file, line))
    {
        size_t eq_pos = line.find('=');
        if (eq_pos == std::string::npos)
            continue;

        std::string line_key   = line.substr(0, eq_pos);
        std::string line_value = line.substr(eq_pos + 1);

        // Trim whitespace
        line_key.erase(0, line_key.find_first_not_of(" \t\r\n"));
        line_key.erase(line_key.find_last_not_of(" \t\r\n") + 1);
        line_value.erase(0, line_value.find_first_not_of(" \t\r\n"));
        line_value.erase(line_value.find_last_not_of(" \t\r\n") + 1);

        if (line_key == key)
        {
            value = line_value;
            config_file.close();
            return true;
        }
    }

    config_file.close();
    return false;
}

StudioDataModel::StudioDataModel()
{
    // Creation rules for commonly used nodes
    node_rules_ = {
        {"ParameterDeclarations", {{"OpenSCENARIO"}}},
        {"CatalogLocations", {{"OpenSCENARIO"}}},
        {"VehicleCatalog", {{"CatalogLocations"}}},
        {"RoadNetwork", {{"OpenSCENARIO"}}, 0},
        {"LogicFile", {{"RoadNetwork"}}},
        {"Entities", {{"OpenSCENARIO"}}},
        {"ScenarioObject", {{"Entities"}}},
        {"CatalogReference", {{"ScenarioObject"}}},
        {"Storyboard", {{"OpenSCENARIO"}}},
        {"Private", {{"Actions"}, {"Init"}}},
        {"TeleportAction", {{"PrivateAction", 0}, {"Private"}, {"Actions"}, {"Init"}}},
        {"LanePosition", {{"Position"}}},
        {"Orientation", {{"LanePosition"}}},
        {"Orientation", {{"RelativeLanePosition"}}},
        {"PrivateAction", {{"Private"}, {"Actions"}, {"Init"}}, 1},
        {"LongitudinalAction", {{"PrivateAction", 1}, {"Private"}, {"Actions"}, {"Init"}}},
        {"SpeedAction", {{"LongitudinalAction"}, {"PrivateAction", 1}, {"Private"}, {"Actions"}, {"Init"}}},
        {"AbsoluteTargetSpeed",
         {{"SpeedActionTarget"}, {"SpeedAction"}, {"LongitudinalAction"}, {"PrivateAction", 1}, {"Private"}, {"Actions"}, {"Init"}}},
        {"Story", {{"Storyboard"}}},
        {"EntityRef", {{"Actors"}}},
        {"Maneuver", {{"ManeuverGroup"}}},
        {"PrivateAction", {{"Action"}, {"Event"}}},
        {"ConditionGroup", {{"StartTrigger"}}},
        {"StopTrigger", {{"Storyboard"}}},
        {"ConditionGroup", {{"StopTrigger"}}},
        {"ByValueCondition", {{"Condition", 0}, {"ConditionGroup", 0}, {"StopTrigger"}, {"Storyboard"}}},
        {"SimulationTimeCondition", {{"ByValueCondition", 0}, {"Condition", 0}, {"ConditionGroup", 0}, {"StopTrigger"}, {"Storyboard"}}},
    };

    // Creation rules for commonly used attributes
    attr_rules_ = {
        {"author", "Scenario Studio", {{"FileHeader"}}},
        {"revMajor", "1", {{"FileHeader"}}},
        {"revMinor", "3", {{"FileHeader"}}},
        {"path", "../xosc/Catalogs/Vehicles", {{"Directory"}, {"VehicleCatalog"}, {"CatalogLocations"}}},
        {"name", "ego", {{"ScenarioObject", 0}, {"Entities"}}},
        {"catalogName", "VehicleCatalog", {{"CatalogReference"}, {"ScenarioObject", 0}, {"Entities"}}},
        {"entryName", "car_white", {{"CatalogReference"}, {"ScenarioObject", 0}, {"Entities"}}},
        {"entityRef", "ego", {{"Private", 0}, {"Actions"}, {"Init"}}},
        {"dynamicsShape",
         "step",
         {{"SpeedActionDynamics"}, {"SpeedAction"}, {"LongitudinalAction"}, {"PrivateAction", 1}, {"Private"}, {"Actions"}, {"Init"}}},
        {"value", "0", {{"SpeedActionDynamics"}, {"SpeedAction"}, {"LongitudinalAction"}, {"PrivateAction", 1}, {"Private"}, {"Actions"}, {"Init"}}},
        {"dynamicsDimension",
         "time",
         {{"SpeedActionDynamics"}, {"SpeedAction"}, {"LongitudinalAction"}, {"PrivateAction", 1}, {"Private"}, {"Actions"}, {"Init"}}},
        {"value", "16.67", {{"AbsoluteTargetSpeed"}, {"SpeedActionTarget"}}},
        {"maximumExecutionCount", "1", {{"ManeuverGroup"}}},
        {"entityRef", "ego", {{"EntityRef"}, {"Actors"}, {"ManeuverGroup", 0}, {"Act", 0}, {"Story", 0}, {"Storyboard"}}},
        {"rule",
         "greaterThan",
         {{"SimulationTimeCondition"}, {"ByValueCondition"}, {"Condition", 0}, {"ConditionGroup", 0}, {"StopTrigger"}, {"Storyboard"}}},
        {"value",
         "30.0",
         {{"SimulationTimeCondition"}, {"ByValueCondition"}, {"Condition", 0}, {"ConditionGroup", 0}, {"StopTrigger"}, {"Storyboard"}}},
        {"conditionEdge", "rising", {{"Condition"}}},
        {"type", "relative", {{"Orientation"}, {"LanePosition"}}},
        {"h", "0", {{"Orientation"}, {"LanePosition"}}},
        {"type", "relative", {{"Orientation"}, {"RelativeLanePosition"}}},
        {"h", "0", {{"Orientation"}, {"RelativeLanePosition"}}},
    };

    mode_ = StudioMode::COMPOSER;
    if (!xml_schema_.load_file("OpenSCENARIO.xsd")) {
        printf("schema loading failed\n");
        exit(1);
    }

    Clear();
    if (!g_xosc_path.empty())
        LoadXoscXml(g_xosc_path);
}

void StudioDataModel::SetModified()
{
    modified_ = true;
    PushUndoState();
    RebuildParameters();
    if (!invalid_nodes_.empty() || !invalid_attrs_.empty() || !validation_errors_.empty())
        ValidateScenario();
}

static void PrintNode(pugi::xml_node node, int level = 0)
{
    for (int i = 0; i < level; ++i)
        printf("  ");
    printf("[%s]\n", node.name());
    for (auto attr = node.first_attribute(); attr; attr = attr.next_attribute())
    {
        if (attr.name())
        {
            for (int i = 0; i < level + 1; ++i)
                printf("  ");
            printf("%s:%s\n", attr.name(), attr.value());
        }
    }
    for (pugi::xml_node entities_child = node.first_child(); entities_child; entities_child = entities_child.next_sibling())
        PrintNode(entities_child, level + 1);
}

bool StudioDataModel::LoadXoscXml(const std::string& path)
{
    Clear();
    auto result = xml_doc_.load_file(path.c_str());
    if (!result)
    {
        LOG("%s at offset (character position): %d", result.description(), result.offset);
        return false;
    }

    RebuildParameters();
    xosc_path_ = path;
    modified_  = false;
    ClearUndoRedoStacks();
    PushUndoState();
    return true;
}

bool StudioDataModel::SaveXoscXml(const std::string& path)
{
    auto result = xml_doc_.save_file(path.c_str());
    if (!result)
    {
        LOG("Failed to save [%s]", path.c_str());
        return false;
    }
    modified_ = false;
    LOG("File saved: [%s]", path.c_str());

    return true;
}

void StudioDataModel::UpdateEsminiSettings()
{
    g_timestep = std::to_string(esmini_timestep_);
    g_seed     = std::to_string(esmini_seed_);
    std::stringstream ss(esmini_resource_paths_);
    std::string       line;
    g_paths.resize(g_path_count_from_cmd);
    while (std::getline(ss, line))
    {
        strip(&line);
        if (!line.empty())
            g_paths.push_back(line);
    }
}

void StudioDataModel::LoadConfig()
{
    std::string config_path = GetConfigFilePath();

    // Use static helper functions to load values
    LoadConfigValue("xml_panel_width", xml_panel_width_);
    LoadConfigValue("menu_bar_height", menu_bar_height_);
    LoadConfigValue("time_bar_height", time_bar_height_);
    LoadConfigValue("log_height_ratio", log_height_ratio_);
    LoadConfigValue("viewport_width", viewport_width_);
    LoadConfigValue("viewport_height", viewport_height_);

    // Load Esmini settings
    LoadConfigValue("esmini_seed", esmini_seed_);
    LoadConfigValue("esmini_timestep", esmini_timestep_);
    LoadConfigValue("esmini_resource_paths", esmini_resource_paths_);
    UpdateEsminiSettings();
}

void StudioDataModel::SaveConfig()
{
    UpdateEsminiSettings();

    std::string   config_path = GetConfigFilePath();
    std::ofstream config_file(config_path);

    if (!config_file.is_open())
    {
        LOG(("Failed to save config to: " + config_path).c_str());
        return;
    }

    config_file << "xml_panel_width=" << xml_panel_width_ << "\n";
    config_file << "menu_bar_height=" << menu_bar_height_ << "\n";
    config_file << "time_bar_height=" << time_bar_height_ << "\n";
    config_file << "log_height_ratio=" << log_height_ratio_ << "\n";
    config_file << "viewport_width=" << viewport_width_ << "\n";
    config_file << "viewport_height=" << viewport_height_ << "\n";

    // Save Esmini settings
    config_file << "esmini_seed=" << esmini_seed_ << "\n";
    config_file << "esmini_timestep=" << esmini_timestep_ << "\n";
    config_file << "esmini_resource_paths=" << esmini_resource_paths_ << "\n";

    config_file.close();
}

void StudioDataModel::ResetWindowSizes()
{
    xml_panel_width_  = DEFAULT_XML_PANEL_WIDTH;
    menu_bar_height_  = DEFAULT_MENU_BAR_HEIGHT;
    time_bar_height_  = DEFAULT_TIME_BAR_HEIGHT;
    log_height_ratio_ = DEFAULT_LOG_HEIGHT_RATIO;
    viewport_width_   = DEFAULT_VIEWPORT_WIDTH;
    viewport_height_  = DEFAULT_VIEWPORT_HEIGHT;

    SaveConfig();
}

pugi::xml_node StudioDataModel::CreateNode(pugi::xml_node parent, const std::string& node_name, pugi::xml_node node_to_plug)
{
    if (parent.empty())
        return pugi::xml_node();

    pugi::xml_node new_node = parent.append_child(node_name.c_str());

    PopulateRequiredStructure(new_node, 0, node_to_plug);

    return new_node;
}

void StudioDataModel::DeleteNode(pugi::xml_node node)
{
    if (node.empty())
        return;

    std::string node_name;
    if (node.attribute("name"))
    {
        node_name = node.attribute("name").value();
    }

    node.parent().remove_child(node);

    if (!node_name.empty())
    {
        UpdateEntityRefs(node_name, "");
    }
    SetModified();
}

bool StudioDataModel::MoveNodeUp(pugi::xml_node node)
{
    pugi::xml_node prev_sibling = node.previous_sibling();
    if (prev_sibling.empty())
        return false;

    pugi::xml_node parent = node.parent();
    parent.insert_move_before(node, prev_sibling);
    SetModified();
    return true;
}

bool StudioDataModel::MoveNodeDown(pugi::xml_node node)
{
    pugi::xml_node next_sibling = node.next_sibling();
    if (next_sibling.empty())
        return false;

    pugi::xml_node parent = node.parent();
    parent.insert_move_after(node, next_sibling);
    SetModified();
    return true;
}

pugi::xml_node StudioDataModel::DuplicateNode(pugi::xml_node node)
{
    pugi::xml_node new_node = node.parent().append_copy(node);
    EnsureUniqueNamesForSubtree(new_node);
    SetModified();
    return new_node;
}

static bool IsRelativePositionNode(const std::string& node_name)
{
    return node_name == "RelativeLanePosition" || node_name == "RelativeWorldPosition" || node_name == "RelativeObjectPosition" ||
           node_name == "RelativeRoadPosition";
}

// Collect the nodes in the subtree that act on the given entity, i.e. carry entityRef=entity_name
// (typically Private under Init/Actions, or EntityAction inside a GlobalAction). entityRef on
// relative position nodes refers to a reference entity, not the acting one, so those are skipped.
static void CollectActionNodesForEntity(pugi::xml_node node, const std::string& entity_name, std::vector<pugi::xml_node>* out)
{
    for (pugi::xml_node child : node.children())
    {
        if (IsRelativePositionNode(child.name()))
            continue;

        if (child.attribute("entityRef").value() == entity_name)
        {
            out->push_back(child);
            continue;  // whole subtree belongs to this entity, no need to descend
        }

        CollectActionNodesForEntity(child, entity_name, out);
    }
}

// Set entityRef attributes matching old_name to new_name within a subtree, skipping relative
// position nodes (their entityRef points at a reference entity that must stay unchanged)
static void RetargetEntityRefsInSubtree(pugi::xml_node node, const std::string& old_name, const std::string& new_name)
{
    if (IsRelativePositionNode(node.name()))
        return;

    pugi::xml_attribute attr = node.attribute("entityRef");
    if (attr && attr.value() == old_name)
        attr.set_value(new_name.c_str());

    for (pugi::xml_node child : node.children())
        RetargetEntityRefsInSubtree(child, old_name, new_name);
}

bool StudioDataModel::DeleteEntity(const std::string& entity_name)
{
    if (entity_name.empty())
        return false;

    bool changed = false;

    // Delete all actions under Storyboard > Init > Actions referring to the entity. When a removal
    // leaves an ancestor without children, remove the ancestor too, up to but excluding Actions.
    pugi::xml_node init_actions = RootNode().child("Storyboard").child("Init").child("Actions");
    if (init_actions)
    {
        std::vector<pugi::xml_node> action_nodes;
        CollectActionNodesForEntity(init_actions, entity_name, &action_nodes);
        for (pugi::xml_node action_node : action_nodes)
        {
            pugi::xml_node parent = action_node.parent();
            parent.remove_child(action_node);
            changed = true;

            while (parent != init_actions && !parent.first_child())
            {
                pugi::xml_node grandparent = parent.parent();
                grandparent.remove_child(parent);
                parent = grandparent;
            }
        }
    }

    // Delete the entity declaration under Entities
    pugi::xml_node entities = RootNode().child("Entities");
    for (pugi::xml_node obj = entities.child("ScenarioObject"); obj;)
    {
        pugi::xml_node next = obj.next_sibling("ScenarioObject");
        if (obj.attribute("name").value() == entity_name)
        {
            entities.remove_child(obj);
            changed = true;
        }
        obj = next;
    }

    if (!changed)
    {
        LOG(("DeleteEntity: nothing to delete for entity [" + entity_name + "]").c_str());
        return false;
    }

    // Clear any remaining references to the deleted entity (e.g. in stories or conditions)
    UpdateEntityRefs(entity_name, "");
    SetModified();
    return true;
}

std::string StudioDataModel::CloneEntity(const std::string& entity_name)
{
    if (entity_name.empty())
        return "";

    pugi::xml_node entities = RootNode().child("Entities");
    pugi::xml_node source_object;
    for (pugi::xml_node obj : entities.children("ScenarioObject"))
    {
        if (obj.attribute("name").value() == entity_name)
        {
            source_object = obj;
            break;
        }
    }

    if (source_object.empty())
    {
        LOG(("CloneEntity: no ScenarioObject named [" + entity_name + "] found under Entities").c_str());
        return "";
    }

    // Duplicate the declaration and give the copy a unique name
    pugi::xml_node new_object = entities.insert_copy_after(source_object, source_object);
    EnsureUniqueNamesForSubtree(new_object);
    std::string new_name = new_object.attribute("name").value();
    if (new_name.empty() || new_name == entity_name)
    {
        entities.remove_child(new_object);
        LOG(("CloneEntity: failed to give the clone of [" + entity_name + "] a unique name").c_str());
        return "";
    }

    // Copy all Init actions of the source entity and retarget them to the clone
    pugi::xml_node init_actions = RootNode().child("Storyboard").child("Init").child("Actions");
    if (init_actions)
    {
        std::vector<pugi::xml_node> action_nodes;
        CollectActionNodesForEntity(init_actions, entity_name, &action_nodes);
        for (pugi::xml_node action_node : action_nodes)
        {
            pugi::xml_node copy = action_node.parent().insert_copy_after(action_node, action_node);
            RetargetEntityRefsInSubtree(copy, entity_name, new_name);
        }
    }

    SetModified();
    return new_name;
}

std::string StudioDataModel::AddVehicle(const std::string& name,
                                        const std::string& catalog_name,
                                        const std::string& entry_name,
                                        double             init_speed)
{
    if (name.empty())
    {
        LOG("AddVehicle: empty name provided");
        return "";
    }

    pugi::xml_node root     = RootNode();
    pugi::xml_node entities = root.child("Entities");
    if (!entities)
    {
        LOG("AddVehicle: Entities node not found");
        return "";
    }

    // Make sure the entity name is unique across the whole document
    std::string unique_name = EnsureUniqueName(name, pugi::xml_node());

    // Create the ScenarioObject declaration under Entities
    pugi::xml_node object                    = entities.append_child("ScenarioObject");
    object.append_attribute("name")          = unique_name.c_str();
    pugi::xml_node catalog_ref               = object.append_child("CatalogReference");
    catalog_ref.append_attribute("catalogName") = catalog_name.c_str();
    catalog_ref.append_attribute("entryName")   = entry_name.c_str();

    // Create the Init actions (TeleportAction + SpeedAction with AbsoluteTargetSpeed)
    pugi::xml_node init_actions = root.child("Storyboard").child("Init").child("Actions");
    if (init_actions)
    {
        pugi::xml_node priv           = init_actions.append_child("Private");
        priv.append_attribute("entityRef") = unique_name.c_str();

        // TeleportAction with a default LanePosition (the actual position is set by the drag operation)
        pugi::xml_node teleport_action = priv.append_child("PrivateAction").append_child("TeleportAction");
        pugi::xml_node lane_position   = teleport_action.append_child("Position").append_child("LanePosition");
        lane_position.append_attribute("laneId") = "-1";
        lane_position.append_attribute("roadId") = "0";
        lane_position.append_attribute("s")      = "0";
        lane_position.append_attribute("offset") = "0";
        pugi::xml_node orientation               = lane_position.append_child("Orientation");
        orientation.append_attribute("type")     = "relative";
        orientation.append_attribute("h")        = "0";

        // SpeedAction with the requested initial speed
        pugi::xml_node speed_action    = priv.append_child("PrivateAction").append_child("LongitudinalAction").append_child("SpeedAction");
        pugi::xml_node dynamics        = speed_action.append_child("SpeedActionDynamics");
        dynamics.append_attribute("dynamicsDimension") = "time";
        dynamics.append_attribute("dynamicsShape")     = "step";
        dynamics.append_attribute("value")             = "0";

        std::ostringstream speed_stream;
        speed_stream << init_speed;
        pugi::xml_node abs_target = speed_action.append_child("SpeedActionTarget").append_child("AbsoluteTargetSpeed");
        abs_target.append_attribute("value") = speed_stream.str().c_str();
    }
    else
    {
        LOG("AddVehicle: Storyboard/Init/Actions node not found, only the entity declaration was created");
    }

    SetModified();
    return unique_name;
}

bool StudioDataModel::RenameEntity(const std::string& old_name, const std::string& new_name)
{
    if (old_name.empty() || new_name.empty() || new_name == old_name)
        return false;

    pugi::xml_node entities = RootNode().child("Entities");
    pugi::xml_node object;
    for (pugi::xml_node obj : entities.children("ScenarioObject"))
    {
        if (obj.attribute("name").value() == old_name)
        {
            object = obj;
            break;
        }
    }

    if (object.empty())
    {
        LOG(("RenameEntity: no ScenarioObject named [" + old_name + "] found under Entities").c_str());
        return false;
    }

    // Update the declaration and every reference to the entity in the document
    object.attribute("name").set_value(new_name.c_str());
    UpdateEntityRefs(old_name, new_name);
    SetModified();
    return true;
}

void StudioDataModel::AddAttribute(pugi::xml_node node, const std::string& attr_name, const std::string& attr_value)
{
    if (node.empty() || attr_name.empty())
        return;

    pugi::xml_attribute attr = node.attribute(attr_name.c_str());
    if (attr)
    {
        attr.set_value(attr_value.c_str());
    }
    else
    {
        node.append_attribute(attr_name.c_str()).set_value(attr_value.c_str());
    }

    SetModified();
}

void StudioDataModel::UpdateAttribute(pugi::xml_node node, const std::string& attr_name, const std::string& attr_value)
{
    if (node.empty() || attr_name.empty())
        return;

    pugi::xml_attribute attr = node.attribute(attr_name.c_str());
    if (!attr)
        return;

    if (attr_name == "name")
    {
        std::string old_name = attr.value();
        std::string new_name = attr_value;

        new_name = EnsureUniqueName(new_name, node);

        attr.set_value(new_name.c_str());

        if (old_name != new_name)
        {
            UpdateEntityRefs(old_name, new_name);
        }
    }
    else
    {
        attr.set_value(attr_value.c_str());
    }

    SetModified();
}

void StudioDataModel::DeleteAttribute(pugi::xml_node node, const std::string& attr_name)
{
    if (node.empty() || attr_name.empty())
        return;

    pugi::xml_attribute attr = node.attribute(attr_name.c_str());
    if (!attr)
        return;

    if (attr_name == "name")
    {
        std::string name = attr.value();
        UpdateEntityRefs(name, "");
    }

    node.remove_attribute(attr);
    SetModified();
}

bool StudioDataModel::RecursiveNameExists(const std::string& name, pugi::xml_node current, pugi::xml_node exclude_node) const
{
    if (!current)
        return false;

    pugi::xml_attribute name_attr = current.attribute("name");
    if (name_attr && name_attr.value() == name)
    {
        if (!(exclude_node && current == exclude_node))
            return true;
    }

    for (auto child : current.children())
    {
        if (RecursiveNameExists(name, child, exclude_node))
            return true;
    }

    return false;
}

bool StudioDataModel::NameExists(const std::string& name, pugi::xml_node exclude_node) const
{
    if (name.empty())
        return false;

    return RecursiveNameExists(name, RootNode(), exclude_node);
}

std::string StudioDataModel::GenerateUniqueName(const std::string& base, pugi::xml_node exclude_node) const
{
    std::string name    = base;
    int         counter = 1;

    while (NameExists(name, exclude_node))
    {
        size_t pos = base.find_last_of('_');
        if (pos != std::string::npos && pos + 1 < base.length())
        {
            bool is_number = true;
            for (size_t i = pos + 1; i < base.length(); ++i)
            {
                if (!isdigit(base[i]))
                {
                    is_number = false;
                    break;
                }
            }

            if (is_number)
            {
                name = base.substr(0, pos + 1) + std::to_string(counter++);
            }
            else
            {
                name = base + "_" + std::to_string(counter++);
            }
        }
        else
        {
            name = base + "_" + std::to_string(counter++);
        }
    }

    return name;
}

std::string StudioDataModel::EnsureUniqueName(const std::string& desired, pugi::xml_node exclude_node) const
{
    if (!NameExists(desired, exclude_node))
        return desired;

    return GenerateUniqueName(desired, exclude_node);
}

void StudioDataModel::EnsureUniqueNamesForSubtree(pugi::xml_node node)
{
    if (node.empty())
        return;

    if (node.attribute("name") && strlen(node.attribute("name").value()) > 0)
    {
        std::string old_name = node.attribute("name").value();
        std::string new_name = EnsureUniqueName(old_name, node);
        if (old_name != new_name)
        {
            node.attribute("name").set_value(new_name.c_str());
        }
    }

    for (auto child : node.children())
    {
        EnsureUniqueNamesForSubtree(child);
    }
}

void StudioDataModel::UpdateEntityRefs(const std::string& old_name, const std::string& new_name)
{
    if (old_name.empty())
        return;

    std::function<void(pugi::xml_node)> update_refs = [&](pugi::xml_node node)
    {
        if (node.empty())
            return;

        for (pugi::xml_attribute attr = node.first_attribute(); attr; attr = attr.next_attribute())
        {
            std::string attr_name = attr.name();
            // masterEntityRef (e.g. on SynchronizeAction) references entities as well
            if ((attr_name == "entityRef" || attr_name == "masterEntityRef") && attr.value() == old_name)
            {
                if (!new_name.empty())
                {
                    attr.set_value(new_name.c_str());
                }
                else
                {
                    attr.set_value("");
                }
            }
        }

        for (pugi::xml_node child = node.first_child(); child; child = child.next_sibling())
        {
            update_refs(child);
        }
    };

    update_refs(RootNode());
}

bool StudioDataModel::IsScenarioObjectNode(pugi::xml_node node) const
{
    if (node.empty())
        return false;

    if (std::string(node.name()) != "ScenarioObject")
        return false;

    pugi::xml_node parent = node.parent();
    return parent && std::string(parent.name()) == "Entities";
}

std::vector<std::string> StudioDataModel::GetScenarioObjectNames() const
{
    std::vector<std::string> names;
    pugi::xml_node           entities = RootNode().child("Entities");
    if (!entities)
        return names;

    for (pugi::xml_node scenario_object : entities.children("ScenarioObject"))
    {
        pugi::xml_attribute name_attr = scenario_object.attribute("name");
        if (name_attr && name_attr.value()[0] != '\0')
        {
            names.push_back(name_attr.value());
        }
    }

    return names;
}

bool StudioDataModel::IsNodeInMainDocument(pugi::xml_node node) const
{
    if (node.empty())
        return false;

    pugi::xml_node current = node;
    while (current && current.parent())
    {
        current = current.parent();
    }

    return current == xml_doc_;
}

void StudioDataModel::AssignDefaultName(pugi::xml_node node)
{
    if (node.empty() || !node.attribute("name"))
        return;

    if (std::string(node.attribute("name").value()).empty())
    {
        std::string node_name    = node.name();
        int         count        = 1;
        std::string default_name = node_name + "_" + std::to_string(count);

        while (NameExists(default_name, node))
        {
            ++count;
            default_name = node_name + "_" + std::to_string(count);
        }

        node.attribute("name").set_value(default_name.c_str());
    }
}

void StudioDataModel::PopulateRequiredStructure(pugi::xml_node node, int depth, pugi::xml_node node_to_plug)
{
    if (node.empty())
        return;

    if (depth > 30)
        return;  // Prevent infinite recursion

    AddRequiredAttributes(node, node.name());

    pugi::xml_attribute name_attr = node.attribute("name");
    if (name_attr && strlen(name_attr.value()) == 0)
    {
        AssignDefaultName(node);
    }

    std::string node_name    = node.name();
    auto        sub_elements = QuerySubelementsWithModelInfo(xml_schema_, node_name);

    for (const auto& element : sub_elements)
    {
        if (element.min_occurs <= 0 || element.has_choice)
            continue;

        int existing_count = 0;
        for (auto child = node.child(element.name.c_str()); child; child = child.next_sibling(element.name.c_str()))
        {
            ++existing_count;
        }

        while (existing_count < element.min_occurs)
        {
            pugi::xml_node new_child = node.append_child(element.name.c_str());
            PopulateRequiredStructure(new_child, depth + 1, node_to_plug);
            ++existing_count;
        }
    }

    // Create commonly used nodes
    for (auto& [name, parent_rule, idx] : node_rules_)
    {
        auto parent            = node;
        bool node_to_plug_used = false;
        for (size_t i = 0; parent && i < parent_rule.size(); ++i)
        {
            if (strcmp(parent.name(), parent_rule[i].name.c_str()) != 0)
                break;
            auto next_parent = parent.parent();
            if (strcmp(next_parent.name(), "temp") == 0 && !node_to_plug_used)
            {
                node_to_plug_used = true;
                next_parent       = node_to_plug;
            }
            if (parent_rule[i].idx != -1)
            {
                int  j     = 0;
                auto child = next_parent.first_child();
                for (; child; child = child.next_sibling())
                {
                    if (parent == child)
                        break;
                    if (strcmp(child.name(), parent.name()) != 0)
                        continue;
                    ++j;
                }
                if (j != parent_rule[i].idx)
                    break;
            }
            if (i + 1 == parent_rule.size())
            {  // parent chain matched
                if (idx != -1)
                {
                    int k = 0;
                    for (auto child = node.first_child(); child; child = child.next_sibling())
                    {
                        if (strcmp(child.name(), name.c_str()) == 0)
                            ++k;
                    }
                    if (idx != k)  // check whether CommonlyUsedNodeRule::idx matched
                        break;
                }
                pugi::xml_node new_child = node.append_child(name.c_str());
                PopulateRequiredStructure(new_child, depth + 1, node_to_plug);
            }
            parent = next_parent;
        }
    }

    // Create commonly used attributes
    for (auto& [name, default_value, parent_rule] : attr_rules_)
    {
        auto parent            = node;
        bool node_to_plug_used = false;
        for (size_t i = 0; parent && i < parent_rule.size(); ++i)
        {
            if (strcmp(parent.name(), parent_rule[i].name.c_str()) != 0)
                break;
            auto next_parent = parent.parent();
            if (strcmp(next_parent.name(), "temp") == 0 && !node_to_plug_used)
            {
                node_to_plug_used = true;
                next_parent       = node_to_plug;
            }
            if (parent_rule[i].idx != -1)
            {
                int  j     = 0;
                auto child = next_parent.first_child();
                for (; child; child = child.next_sibling())
                {
                    if (parent == child)
                        break;
                    if (strcmp(child.name(), parent.name()) != 0)
                        continue;
                    ++j;
                }
                if (j != parent_rule[i].idx)
                    break;
            }
            if (i + 1 == parent_rule.size())
            {  // parent chain matched
                auto attr = node.attribute(name.c_str());
                if (attr)
                    attr.set_value(default_value.c_str());
                else
                    node.append_attribute(name.c_str()) = default_value.c_str();
            }
            parent = next_parent;
        }
    }
}

void StudioDataModel::Clear()
{
    invalid_nodes_.clear();
    invalid_attrs_.clear();
    validation_errors_.clear();
    xml_doc_.reset();
    CreateNode(xml_doc_, "OpenSCENARIO", pugi::xml_node());

    modified_ = false;
    ClearUndoRedoStacks();
    PushUndoState();
}

std::string StudioDataModel::GetDefaultNodeName(pugi::xml_node node) const
{
    return GenerateUniqueName(node.name(), node);
}

void StudioDataModel::SetNodeDefaultName(pugi::xml_node node)
{
    pugi::xml_attribute name_attr = node.attribute("name");
    if (!name_attr || name_attr.empty())
    {
        std::string default_name = GenerateUniqueName(node.name(), node);
        name_attr                = node.append_attribute("name");
        name_attr.set_value(default_name.c_str());
    }
}

void StudioDataModel::AddRequiredAttributes(pugi::xml_node node, const std::string& node_name)
{
    if (node.empty())
        return;

    std::string type_name = QueryTypeByNameFromSchema(xml_schema_, node_name);

    auto required_attrs = QueryRequiredAttrsFromSchema(xml_schema_, type_name);

    for (const auto& attr : required_attrs)
    {
        if (!node.attribute(attr.c_str()))
        {
            std::string default_val = GetDefaultValueForAttribute(xml_schema_, node_name, attr);
            node.append_attribute(attr.c_str()).set_value(default_val.c_str());
        }
    }
}

// Undo/Redo implementation
void StudioDataModel::PushUndoState()
{
    std::ostringstream oss;
    xml_doc_.save(oss, "", 0);  // save without indents
    std::string new_state = oss.str();

    // If this is the first state (initial load), just initialize current_snapshot_
    if (current_snapshot_.empty())
    {
        current_snapshot_ = new_state;
        return;
    }

    // Compute diff: How to go from New -> Old (Reverse Patch) for Undo
    // We want to store: Apply(New, Patch) -> Old
    // ComputeDiff(Old, New) gives patches to go Old -> New
    // ComputeDiff(New, Old) gives patches to go New -> Old

    std::vector<DiffChunk> undo_diff = ComputeDiff(new_state, current_snapshot_);

    // Only push if there are actual changes
    if (!undo_diff.empty())
    {
        undo_stack_.push_back(undo_diff);
        undo_seq_stack_.push_back(++edit_sequence_counter_);

        // size_t count = 0;
        // for(auto& chunk : undo_diff)
        //     for(auto &line : chunk.insert_lines)
        //     count += line.size();
        // LOG("undo diff: %d vs %d", current_snapshot_.size(), count);

        if (undo_stack_.size() > MAX_UNDO_STACK_SIZE)
        {
            undo_stack_.pop_front();
            undo_seq_stack_.pop_front();
        }

        // Clear redo stack when a new action is performed
        redo_stack_.clear();
        redo_seq_stack_.clear();

        // Update current snapshot
        current_snapshot_ = new_state;
    }
}

void StudioDataModel::ClearUndoRedoStacks()
{
    undo_stack_.clear();
    redo_stack_.clear();
    undo_seq_stack_.clear();
    redo_seq_stack_.clear();
    current_snapshot_.clear();
}

void StudioDataModel::Undo()
{
    if (undo_stack_.empty())
        return;

    // Get the undo patch (New -> Old)
    std::vector<DiffChunk> undo_diff = undo_stack_.back();
    undo_stack_.pop_back();
    undo_seq_stack_.pop_back();

    // We need to compute the redo patch (Old -> New) before applying undo
    // Current state is New. We want to go to Old.
    // Redo patch should go Old -> New.
    // So Redo Patch = ComputeDiff(Old, New)
    // But we don't have Old yet. We have New and Diff(New -> Old).
    // So Old = ApplyDiff(New, undo_diff).

    std::string old_state = ApplyDiff(current_snapshot_, undo_diff);

    // Compute Redo patch: Old -> New
    std::vector<DiffChunk> redo_diff = ComputeDiff(old_state, current_snapshot_);
    redo_stack_.push_back(redo_diff);
    redo_seq_stack_.push_back(++undo_sequence_counter_);

    // Apply undo
    current_snapshot_ = old_state;

    if (xml_doc_.load_string(current_snapshot_.c_str()))
    {
        modified_ = true;
    }
    else
    {
        LOG("Failed to restore undo state");
    }

    if (!invalid_nodes_.empty() || !invalid_attrs_.empty())
        ValidateScenario();
}

void StudioDataModel::Redo()
{
    if (redo_stack_.empty())
        return;

    // Get redo patch (Old -> New)
    std::vector<DiffChunk> redo_diff = redo_stack_.back();
    redo_stack_.pop_back();
    redo_seq_stack_.pop_back();

    // We need to compute undo patch (New -> Old) for the undo stack
    // Current is Old. Target is New.
    // New = ApplyDiff(Old, redo_diff)

    std::string new_state = ApplyDiff(current_snapshot_, redo_diff);

    // Compute Undo patch: New -> Old
    std::vector<DiffChunk> undo_diff = ComputeDiff(new_state, current_snapshot_);
    undo_stack_.push_back(undo_diff);
    undo_seq_stack_.push_back(++edit_sequence_counter_);

    // Apply redo
    current_snapshot_ = new_state;

    if (xml_doc_.load_string(current_snapshot_.c_str()))
    {
        modified_ = true;
    }
    else
    {
        LOG("Failed to restore redo state");
    }

    if (!invalid_nodes_.empty() || !invalid_attrs_.empty())
        ValidateScenario();
}

void StudioDataModel::PushTrajectoryUndoState(const TrajectorySelectionSnapshot& selection_after_edit)
{
    std::string new_data = TrajectoriesToJsonString();

    // If this is the first state (initial load / first edit this session), just initialize the baseline.
    if (current_trajectory_entry_.data_json.empty())
    {
        current_trajectory_entry_ = {new_data, selection_after_edit};
        return;
    }

    // No actual data change (e.g. a click that only changed selection, not any point) - don't spend an undo
    // slot on it.
    if (new_data == current_trajectory_entry_.data_json)
        return;

    trajectory_undo_stack_.push_back(current_trajectory_entry_);
    trajectory_undo_seq_stack_.push_back(++edit_sequence_counter_);
    if (trajectory_undo_stack_.size() > MAX_TRAJECTORY_UNDO_STACK_SIZE)
    {
        trajectory_undo_stack_.pop_front();
        trajectory_undo_seq_stack_.pop_front();
    }

    trajectory_redo_stack_.clear();
    trajectory_redo_seq_stack_.clear();
    current_trajectory_entry_ = {new_data, selection_after_edit};
    trajectories_modified_    = true;
}

TrajectorySelectionSnapshot StudioDataModel::UndoTrajectories()
{
    if (trajectory_undo_stack_.empty())
        return current_trajectory_entry_.selection;

    TrajectoryUndoEntry old_entry = trajectory_undo_stack_.back();
    trajectory_undo_stack_.pop_back();
    trajectory_undo_seq_stack_.pop_back();

    trajectory_redo_stack_.push_back(current_trajectory_entry_);
    trajectory_redo_seq_stack_.push_back(++undo_sequence_counter_);

    current_trajectory_entry_ = old_entry;
    TrajectoriesFromJsonString(old_entry.data_json);
    trajectories_modified_ = true;
    return old_entry.selection;
}

TrajectorySelectionSnapshot StudioDataModel::RedoTrajectories()
{
    if (trajectory_redo_stack_.empty())
        return current_trajectory_entry_.selection;

    TrajectoryUndoEntry new_entry = trajectory_redo_stack_.back();
    trajectory_redo_stack_.pop_back();
    trajectory_redo_seq_stack_.pop_back();

    trajectory_undo_stack_.push_back(current_trajectory_entry_);
    trajectory_undo_seq_stack_.push_back(++edit_sequence_counter_);

    current_trajectory_entry_ = new_entry;
    TrajectoriesFromJsonString(new_entry.data_json);
    trajectories_modified_ = true;
    return new_entry.selection;
}

void StudioDataModel::ClearTrajectoryUndoRedoStacks()
{
    trajectory_undo_stack_.clear();
    trajectory_redo_stack_.clear();
    trajectory_undo_seq_stack_.clear();
    trajectory_redo_seq_stack_.clear();
    current_trajectory_entry_ = TrajectoryUndoEntry();
}

void StudioDataModel::ValidateScenario()
{
    invalid_nodes_.clear();
    invalid_attrs_.clear();
    validation_errors_.clear();

    auto                  scenario_objects = GetScenarioObjectNames();
    std::set<std::string> scenario_object_set(scenario_objects.begin(), scenario_objects.end());

    std::function<void(pugi::xml_node)> validate_node = [&](pugi::xml_node node)
    {
        if (node.empty())
            return;
        std::string node_name = node.name();

        // 1. Check Required Attributes
        std::string type_name = QueryTypeByNameFromSchema(xml_schema_, node_name);
        if (!type_name.empty())
        {
            auto required_attrs = QueryRequiredAttrsFromSchema(xml_schema_, type_name);
            for (const auto& attr_name : required_attrs)
            {
                pugi::xml_attribute attr = node.attribute(attr_name.c_str());
                if (!attr)
                {
                    validation_errors_.push_back(
                        {"Missing required attribute: " + attr_name, node, pugi::xml_attribute(), attr_name, /*can_be_fixed = */ true});
                }
                else if (strlen(attr.value()) == 0)
                {
                    std::vector<std::string> enums;
                    std::string              val_type = QueryValueTypeFromSchema(xml_schema_, node_name, attr_name, &enums);
                    if (val_type != "xsd:string" && val_type != "String")
                    {
                        invalid_attrs_.insert(attr);
                        validation_errors_.push_back(
                            {"Invalid attribute value (empty): " + attr_name, node, attr, attr_name, /*can_be_fixed = */ true});
                    }
                }
            }

            // 1.5 Check Attribute Data Types and Enumerations
            for (pugi::xml_attribute attr = node.first_attribute(); attr; attr = attr.next_attribute())
            {
                std::string attr_name = attr.name();
                std::string attr_val  = attr.value();
                if (attr_val.empty())
                    continue;

                // Check for parameter expressions
                if (attr_val.find('$') != std::string::npos)
                {
                    std::string eval_val = EvaluateAttribute(node, attr_name);
                    if (eval_val.empty() && !attr_val.empty())
                    {
                        validation_errors_.push_back({"Expression evaluation failed: " + attr_val, node, attr, attr_name});
                        continue;
                    }
                    attr_val = eval_val;
                }

                // Check Deprecated Attributes
                if (IsAttributeDeprecated(xml_schema_, node_name, attr_name))
                {
                    invalid_attrs_.insert(attr);
                    validation_errors_.push_back({"Deprecated attribute: " + attr_name, node, attr, attr_name});
                }

                std::vector<std::string> enums;
                std::string              val_type = QueryValueTypeFromSchema(xml_schema_, node_name, attr_name, &enums);

                // Check Enumerations
                if (!enums.empty())
                {
                    bool found = false;
                    for (const auto& enum_val : enums)
                    {
                        if (enum_val == attr_val)
                        {
                            found = true;
                            break;
                        }
                    }
                    if (!found)
                    {
                        // Check if it is a deprecated enum value
                        if (IsEnumDeprecated(xml_schema_, val_type, attr_val))
                        {
                            invalid_attrs_.insert(attr);
                            validation_errors_.push_back({"Deprecated enum value: " + attr_val, node, attr, attr_name});
                        }
                        else
                        {
                            std::string valid_values;
                            for (size_t i = 0; i < enums.size(); ++i)
                                valid_values += (i > 0 ? ", " : "") + enums[i];
                            invalid_attrs_.insert(attr);
                            validation_errors_.push_back(
                                {"Invalid enum value '" + attr_val + "'. Expected one of: " + valid_values, node, attr, attr_name});
                        }
                    }
                }
                // Check Basic Data Types
                else if (!val_type.empty())
                {
                    bool type_error = false;
                    if (val_type == "xsd:double" || val_type == "xsd:float" || val_type == "Double")
                    {
                        try
                        {
                            size_t pos;
                            std::stod(attr_val, &pos);
                            if (pos != attr_val.length())
                                type_error = true;
                        }
                        catch (...)
                        {
                            type_error = true;
                        }
                    }
                    else if (val_type == "xsd:int" || val_type == "xsd:integer" || val_type == "xsd:unsignedInt" || val_type == "Int" ||
                             val_type == "UnsignedInt")
                    {
                        try
                        {
                            size_t pos;
                            std::stoi(attr_val, &pos);
                            if (pos != attr_val.length())
                                type_error = true;

                            if ((val_type == "xsd:unsignedInt" || val_type == "UnsignedInt") && attr_val.find('-') != std::string::npos)
                                type_error = true;
                        }
                        catch (...)
                        {
                            type_error = true;
                        }
                    }
                    else if (val_type == "xsd:boolean" || val_type == "Boolean")
                    {
                        if (attr_val != "true" && attr_val != "false" && attr_val != "0" && attr_val != "1")
                            type_error = true;
                    }

                    if (type_error)
                    {
                        invalid_attrs_.insert(attr);
                        validation_errors_.push_back(
                            {"Invalid value '" + attr_val + "' for type " + val_type, node, attr, attr_name, /*can_be_fixed = */ true});
                    }
                }
            }
        }

        // 2. Check Entity References
        for (pugi::xml_attribute attr = node.first_attribute(); attr; attr = attr.next_attribute())
        {
            std::string attr_name = attr.name();
            if (attr_name == "entityRef" || attr_name == "masterEntityRef")
            {
                std::string ref_value = attr.value();
                if (!ref_value.empty() && scenario_object_set.find(ref_value) == scenario_object_set.end())
                {
                    invalid_attrs_.insert(attr);
                    validation_errors_.push_back({"Reference to non-existent entity: " + ref_value, node, attr, attr_name});
                }
            }
        }

        // 3. Check Deprecated Elements (Optional)
        if (IsElementDeprecated(node))
        {
            invalid_nodes_.insert(node);
            validation_errors_.push_back({"Deprecated element: " + node_name, node, pugi::xml_attribute(), ""});
        }

        // 4. Check Element Structure and Quantity
        auto sub_elements = QuerySubelementsWithModelInfo(xml_schema_, node_name);
        if (!sub_elements.empty())
        {
            std::map<std::string, int> element_counts;
            std::set<std::string>      allowed_elements;
            // choice_id -> set of present branch_ids
            std::map<std::string, std::set<std::string>> choice_branches_present;

            for (const auto& info : sub_elements)
            {
                allowed_elements.insert(info.name);
            }

            // Count actual children
            for (pugi::xml_node child = node.first_child(); child; child = child.next_sibling())
            {
                std::string child_name = child.name();
                element_counts[child_name]++;

                // Check for unknown elements
                if (allowed_elements.find(child_name) == allowed_elements.end())
                {
                    if (child.type() == pugi::node_element)
                    {
                        invalid_nodes_.insert(child);
                        validation_errors_.push_back({"Unexpected child element: " + child_name, node, pugi::xml_attribute(), ""});
                    }
                }
                else
                {
                    // Check choice constraints
                    // Find which choice group this element belongs to
                    for (const auto& info : sub_elements)
                    {
                        if (info.name == child_name && !info.choice_id.empty())
                        {
                            choice_branches_present[info.choice_id].insert(info.choice_branch_id);
                        }
                    }
                }
            }

            // Check Min/Max Occurs
            for (const auto& info : sub_elements)
            {
                int count = element_counts[info.name];

                // If element is part of a choice, we skip the standard minOccurs check here.
                // The choice logic handles "one of these must exist" if the choice itself is required.
                // However, if the element IS present (count > 0), it must satisfy its own minOccurs (usually 1).
                if (info.choice_id.empty())
                {
                    if (count < info.min_occurs)
                    {
                        invalid_nodes_.insert(node);
                        validation_errors_.push_back({"Missing required element: " + info.name + " (Found " + std::to_string(count) +
                                                          ", Expected min " + std::to_string(info.min_occurs) + ")",
                                                      node,
                                                      pugi::xml_attribute(),
                                                      ""});
                    }
                }
                else
                {
                    // For choice elements, if they appear, they must meet their min requirement (if > 0)
                    // But if they don't appear, it's fine (as long as choice constraint is met)
                    if (count > 0 && count < info.min_occurs)
                    {
                        invalid_nodes_.insert(node);
                        validation_errors_.push_back({"Incomplete element occurrence: " + info.name + " (Found " + std::to_string(count) +
                                                          ", Expected min " + std::to_string(info.min_occurs) + ")",
                                                      node,
                                                      pugi::xml_attribute(),
                                                      ""});
                    }
                }

                if (info.max_occurs != -1 && count > info.max_occurs)
                {
                    invalid_nodes_.insert(node);
                    validation_errors_.push_back({"Too many elements: " + info.name + " (Found " + std::to_string(count) + ", Expected max " +
                                                      std::to_string(info.max_occurs) + ")",
                                                  node,
                                                  pugi::xml_attribute(),
                                                  ""});
                }
            }

            // Check Choice Groups
            for (const auto& entry : choice_branches_present)
            {
                const std::string&           choice_id        = entry.first;
                const std::set<std::string>& present_branches = entry.second;

                // Find the choice constraints from one of the elements
                int choice_max = 1;
                int choice_min = 1;
                for (const auto& info : sub_elements)
                {
                    if (info.choice_id == choice_id)
                    {
                        choice_max = info.choice_max_occurs;
                        choice_min = info.choice_min_occurs;
                        break;
                    }
                }

                if (choice_max != -1 && present_branches.size() > (size_t)choice_max)
                {
                    // Construct a helpful message about which elements are conflicting
                    std::string conflicting_elements;
                    for (const auto& info : sub_elements)
                    {
                        if (info.choice_id == choice_id && present_branches.count(info.choice_branch_id))
                        {
                            auto invalid_nodes = FindElements(xml_schema_, node, info.xsd_path);
                            for (auto invalid_node : invalid_nodes)
                                invalid_nodes_.insert(invalid_node);
                            if (element_counts[info.name] > 0)
                                conflicting_elements += info.name + " ";
                        }
                    }
                    validation_errors_.push_back(
                        {"Choice violation: Mutually exclusive elements present (" + conflicting_elements + ")", node, pugi::xml_attribute(), ""});
                }
            }
        }

        for (pugi::xml_node child = node.first_child(); child; child = child.next_sibling())
        {
            validate_node(child);
        }
    };

    validate_node(RootNode());
}

void StudioDataModel::RebuildParameters()
{
    parameters_.Clear();
    pugi::xml_node param_decls = RootNode().child("ParameterDeclarations");
    if (param_decls)
    {
        parameters_.parseGlobalParameterDeclarations(param_decls);
    }
}

std::string StudioDataModel::EvaluateAttribute(pugi::xml_node node, const std::string& attr_name)
{
    if (node.empty() || attr_name.empty())
        return "";

    pugi::xml_attribute attr = node.attribute(attr_name.c_str());
    if (!attr)
        return "";

    std::string attr_val = attr.value();

    if (attr_val.find('$') != std::string::npos)
    {
        if (attr_val.length() > 2 && attr_val[0] == '$' && attr_val[1] == '{')
        {
            std::string expr  = attr_val;
            std::size_t found = expr.find('}', 2);
            if (found != std::string::npos)
            {
                expr = expr.substr(2, found - 2);
                try
                {
                    expr = parameters_.ResolveParametersInString(expr);
                }
                catch (...)
                {
                    return "";  // Failed to resolve parameters
                }

                ReplaceStringInPlace(expr, "not ", "!");
                ReplaceStringInPlace(expr, "not(", "!(");
                ReplaceStringInPlace(expr, "and ", "&& ");
                ReplaceStringInPlace(expr, "or ", "|| ");
                ReplaceStringInPlace(expr, "true ", "1 ");
                ReplaceStringInPlace(expr, "false ", "0 ");

                ExprReturnStruct rs = eval_expr(expr.c_str());

                if (rs.type == EXPR_RETURN_DOUBLE)
                {
                    return std::to_string(rs._double);
                }
                else if (rs.type == EXPR_RETURN_STRING)
                {
                    std::string res = rs._string.string;
                    free(rs._string.string);
                    return res;
                }
                else
                {
                    return "";
                }
            }
        }
        else
        {
            try
            {
                return parameters_.getParameter(attr_val);
            }
            catch (...)
            {
                return "";
            }
        }
    }
    return attr_val;
}

double StudioDataModel::EvaluateAttributeDouble(pugi::xml_node node, const std::string& attr_name, double default_val)
{
    std::string val_str = EvaluateAttribute(node, attr_name);
    if (val_str.empty())
        return default_val;
    try
    {
        return std::stod(val_str);
    }
    catch (...)
    {
        return default_val;
    }
}

int StudioDataModel::EvaluateAttributeInt(pugi::xml_node node, const std::string& attr_name, int default_val)
{
    std::string val_str = EvaluateAttribute(node, attr_name);
    if (val_str.empty())
        return default_val;
    try
    {
        return std::stoi(val_str);
    }
    catch (...)
    {
        return default_val;
    }
}

std::string StudioDataModel::EvaluateString(const std::string& str)
{
    if (str.empty())
        return "";

    std::string expr = str;

    // Check if it's an expression
    if (expr.find('$') != std::string::npos)
    {
        if (expr.length() > 2 && expr[0] == '$' && expr[1] == '{')
        {
            std::size_t found = expr.find('}', 2);
            if (found != std::string::npos)
            {
                expr = expr.substr(2, found - 2);
                try
                {
                    expr = parameters_.ResolveParametersInString(expr);
                }
                catch (...)
                {
                    return "";  // Failed to resolve parameters
                }

                ReplaceStringInPlace(expr, "not ", "!");
                ReplaceStringInPlace(expr, "not(", "!(");
                ReplaceStringInPlace(expr, "and ", "&& ");
                ReplaceStringInPlace(expr, "or ", "|| ");
                ReplaceStringInPlace(expr, "true ", "1 ");
                ReplaceStringInPlace(expr, "false ", "0 ");

                ExprReturnStruct rs = eval_expr(expr.c_str());

                if (rs.type == EXPR_RETURN_DOUBLE)
                {
                    return std::to_string(rs._double);
                }
                else if (rs.type == EXPR_RETURN_STRING)
                {
                    std::string res = rs._string.string;
                    free(rs._string.string);
                    return res;
                }
            }
        }
        else
        {
            // Parameter reference
            try
            {
                return parameters_.getParameter(expr);
            }
            catch (...)
            {
                return "";
            }
        }
    }
    return expr;  // Return as is if not expression
}

void StudioDataModel::ExtractPositionsRecursive(pugi::xml_node node, const std::string& context, std::vector<PositionInfo>* infos)
{
    std::string node_name = node.name();

    // Check for all OpenSCENARIO position types according to XSD 1750-1763
    bool         is_position_node = false;
    PositionInfo pos_info;

    // Store pointer to the XML node for direct access
    pos_info.node = node;

    // WorldPosition - already in world coordinates
    if (node_name == "WorldPosition")
    {
        is_position_node = true;
        pos_info.type    = PositionType::WORLD_POSITION;
        pos_info.x       = EvaluateAttributeDouble(node, "x", 0.0);
        pos_info.y       = EvaluateAttributeDouble(node, "y", 0.0);
        pos_info.z       = EvaluateAttributeDouble(node, "z", 0.0);

        if (!node.attribute("h").empty())
        {
            pos_info.h = EvaluateAttributeDouble(node, "h", 0.0);
            pos_info.p = EvaluateAttributeDouble(node, "p", 0.0);
            pos_info.r = EvaluateAttributeDouble(node, "r", 0.0);

            pos_info.absolute_h = pos_info.h;
        }
    }

    // RelativeWorldPosition - relative to world coordinates
    else if (node_name == "RelativeWorldPosition")
    {
        is_position_node     = true;
        pos_info.type        = PositionType::RELATIVE_WORLD_POSITION;
        pos_info.dx          = EvaluateAttributeDouble(node, "dx", 0.0);
        pos_info.dy          = EvaluateAttributeDouble(node, "dy", 0.0);
        pos_info.dz          = EvaluateAttributeDouble(node, "dz", 0.0);
        pos_info.relative_to = EvaluateAttribute(node, "relativeTo");
        // TODO: heading?
    }

    // RelativeObjectPosition - relative to another object
    else if (node_name == "RelativeObjectPosition")
    {
        is_position_node     = true;
        pos_info.type        = PositionType::RELATIVE_OBJECT_POSITION;
        pos_info.dx          = EvaluateAttributeDouble(node, "dx", 0.0);
        pos_info.dy          = EvaluateAttributeDouble(node, "dy", 0.0);
        pos_info.dz          = EvaluateAttributeDouble(node, "dz", 0.0);
        pos_info.entity_ref  = EvaluateAttribute(node, "entityRef");
        pos_info.relative_to = "object:" + pos_info.entity_ref;
        // TODO: heading?
    }

    // RoadPosition - road coordinate system
    else if (node_name == "RoadPosition")
    {
        is_position_node = true;
        pos_info.type    = PositionType::ROAD_POSITION;
        pos_info.road_id = EvaluateAttributeInt(node, "roadId", 0);
        pos_info.s       = EvaluateAttributeDouble(node, "s", 0.0);
        pos_info.t       = EvaluateAttributeDouble(node, "t", 0.0);
        ConvertRoadPosToWorldPos(pos_info.road_id, pos_info.s, pos_info.t, &pos_info);
        // TODO: heading?
    }

    // RelativeRoadPosition - relative road coordinates
    else if (node_name == "RelativeRoadPosition")
    {
        is_position_node     = true;
        pos_info.type        = PositionType::RELATIVE_ROAD_POSITION;
        pos_info.ds          = EvaluateAttributeDouble(node, "ds", 0.0);
        pos_info.dt          = EvaluateAttributeDouble(node, "dt", 0.0);
        pos_info.entity_ref  = EvaluateAttribute(node, "entityRef");
        pos_info.relative_to = "object:" + pos_info.entity_ref + " road coordinates";
        // TODO: heading?
    }

    // LanePosition - lane coordinate system
    else if (node_name == "LanePosition")
    {
        is_position_node     = true;
        pos_info.type        = PositionType::LANE_POSITION;
        pos_info.road_id     = EvaluateAttributeInt(node, "roadId", 0);
        pos_info.lane_id     = EvaluateAttributeInt(node, "laneId", 0);
        pos_info.s           = EvaluateAttributeDouble(node, "s", 0.0);
        pos_info.lane_offset = EvaluateAttributeDouble(node, "offset", 0.0);

        // Parse Orientation element if present (for relative orientation handling)
        for (pugi::xml_node child : node.children())
        {
            std::string child_name = std::string(child.name());

            if (child_name == "Orientation")
            {
                pos_info.h                       = EvaluateAttributeDouble(child, "h", 0.0);
                pos_info.p                       = EvaluateAttributeDouble(child, "p", 0.0);
                pos_info.r                       = EvaluateAttributeDouble(child, "r", 0.0);
                std::string orientation_type     = child.attribute("type").as_string("");
                pos_info.orientation_is_relative = (orientation_type == "relative");
                pos_info.orientation_reference   = "lane";
                if (pos_info.orientation_is_relative)
                    pos_info.absolute_h = pos_info.h;
                break;
            }
        }
        ConvertLanePosToWorldPos(pos_info.road_id,
                                 pos_info.lane_id,
                                 pos_info.s,
                                 pos_info.lane_offset,
                                 pos_info.h,
                                 &pos_info.x,
                                 &pos_info.y,
                                 &pos_info.z,
                                 &pos_info.absolute_h);
        if (!pos_info.orientation_is_relative)
            pos_info.absolute_h = pos_info.h;
    }

    // RelativeLanePosition - relative lane coordinates
    else if (node_name == "RelativeLanePosition")
    {
        is_position_node      = true;
        pos_info.type         = PositionType::RELATIVE_LANE_POSITION;
        pos_info.dLane        = EvaluateAttributeInt(node, "dLane", 0);
        pos_info.dlane_offset = EvaluateAttributeDouble(node, "offset", 0.0);
        pos_info.ds           = EvaluateAttributeDouble(node, "ds", 0.0);
        pos_info.entity_ref   = EvaluateAttribute(node, "entityRef");
        pos_info.relative_to  = "object:" + pos_info.entity_ref + " lane coordinates";
        // Parse Orientation element if present (for relative orientation handling)
        for (pugi::xml_node child : node.children())
        {
            std::string child_name = std::string(child.name());

            if (child_name == "Orientation")
            {
                pos_info.h                       = EvaluateAttributeDouble(child, "h", 0.0);
                pos_info.p                       = EvaluateAttributeDouble(child, "p", 0.0);
                pos_info.r                       = EvaluateAttributeDouble(child, "r", 0.0);
                std::string orientation_type     = child.attribute("type").as_string("");
                pos_info.orientation_is_relative = (orientation_type == "relative");
                pos_info.orientation_reference   = "lane";
                if (pos_info.orientation_is_relative)
                    pos_info.absolute_h = pos_info.h;
                break;
            }
        }
    }

    // RoutePosition - position along a route
    else if (node_name == "RoutePosition")
    {
        is_position_node   = true;
        pos_info.type      = PositionType::ROUTE_POSITION;
        pos_info.route_ref = EvaluateAttribute(node, "routeRef");

        // Check for InRoutePosition child
        for (pugi::xml_node child : node.children())
        {
            if (strcmp(child.name(), "InRoutePosition") == 0)
            {
                pos_info.s = EvaluateAttributeDouble(child, "s", 0.0);
                pos_info.t = EvaluateAttributeDouble(child, "t", 0.0);
                break;
            }
        }
    }

    // GeoPosition - geographical coordinates
    else if (node_name == "GeoPosition")
    {
        is_position_node   = true;
        pos_info.type      = PositionType::GEO_POSITION;
        pos_info.latitude  = EvaluateAttributeDouble(node, "latitude", 0.0);
        pos_info.longitude = EvaluateAttributeDouble(node, "longitude", 0.0);
        pos_info.altitude  = EvaluateAttributeDouble(node, "altitude", 0.0);
    }

    // TrajectoryPosition - position along a trajectory
    else if (node_name == "TrajectoryPosition")
    {
        is_position_node        = true;
        pos_info.type           = PositionType::TRAJECTORY_POSITION;
        pos_info.trajectory_ref = EvaluateAttribute(node, "trajectoryRef");
        pos_info.s              = EvaluateAttributeDouble(node, "s", 0.0);
        pos_info.t              = EvaluateAttributeDouble(node, "t", 0.0);
    }

    if (is_position_node)
    {
        std::string object_type, entity_name, condition_name;
        ExtractObjectType(node, &object_type, &entity_name, &condition_name);
        pos_info.object_type = object_type;
        pos_info.name        = entity_name;
        infos->push_back(pos_info);
    }

    // Recursively search child nodes
    // Note: Don't add every node to the context path - only meaningful ones
    std::string new_context = context;

    // Only add to context if this is a meaningful OpenSCENARIO element
    bool is_meaningful_element = (node_name == "OpenSCENARIO" || node_name == "Storyboard" || node_name == "Init" || node_name == "Story" ||
                                  node_name == "Act" || node_name == "StartTrigger" || node_name == "StopTrigger" || node_name == "ConditionGroup" ||
                                  node_name == "Condition" || node_name == "ActionGroup" || node_name == "PrivateActionGroup" ||
                                  node_name == "Private" || node_name == "PrivateAction" || node_name == "TeleportAction" ||
                                  node_name == "AcquirePositionAction" || node_name == "Event" || node_name == "Action" || node_name == "Position");

    if (is_meaningful_element)
    {
        if (!context.empty())
            new_context += " -> ";
        new_context += node_name;
    }

    for (pugi::xml_node child : node.children())
        ExtractPositionsRecursive(child, new_context, infos);
}

void StudioDataModel::ExtractObjectType(pugi::xml_node node, std::string* object_type, std::string* entity_name, std::string* condition_name)
{
    // Initialize output parameters
    *object_type    = "Unknown";
    *entity_name    = "";
    *condition_name = "";

    // Walk up the tree to find the parent context
    std::string context = "";
    for (auto parent = node.parent(); parent; parent = parent.parent())
    {
        std::string parentName = parent.name();
        if (!parentName.empty())
        {
            if (context.empty())
            {
                context = parentName;
            }
            else
            {
                context = parentName + " -> " + context;
            }
        }

        // Check for specific OpenSCENARIO element patterns
        if (parentName == "Init" || parentName == "Initialize")
        {
            *object_type = "Vehicle Initialization";
            *entity_name = FindEntityNameFromNode(node);
        }
        else if (parentName == "TeleportAction")
        {
            *object_type = "Vehicle Teleportation";
            *entity_name = FindEntityNameFromNode(node);
        }
        else if (parentName == "Condition")
        {
            *object_type = "Condition Related";
            // Extract condition name from the Condition element
            *condition_name = parent.attribute("name").as_string("");
            *entity_name    = FindEntityNameFromNode(node);
        }
        else if (parentName == "Event")
        {
            *object_type = "Event Action";
            *entity_name = FindEntityNameFromNode(node);
        }
        else if (parentName == "Action")
        {
            *object_type = "Action Related";
            *entity_name = FindEntityNameFromNode(node);
        }
        else if (parentName == "Route")
        {
            *object_type = "Route Definition";
        }
        else if (parentName == "Vertex")
        {
            *object_type = "Trajectory Vertex";
        }
        else if (parentName == "CatalogReference")
        {
            *object_type = "Catalog Reference";
        }
        else
        {
            continue;
        }
        break;
    }

    // If no specific classification found, return the immediate context
    pugi::xml_node immediate_parent = node.parent();
    if (immediate_parent)
    {
        std::string immediate_parentname = immediate_parent.name();
        if (!immediate_parentname.empty())
        {
            *object_type = immediate_parentname + " Related";
            if (immediate_parentname == "Condition")
            {
                *condition_name = immediate_parent.attribute("name").as_string("");
            }
            *entity_name = FindEntityNameFromNode(node);
        }
    }

    // Determine entity type by finding the ScenarioObject
    if (!entity_name->empty())
    {
        // Find Entities node
        pugi::xml_node entities = RootNode().child("Entities");
        if (entities)
        {
            // Search for ScenarioObject with matching name
            for (pugi::xml_node scenario_obj : entities.children("ScenarioObject"))
            {
                if (scenario_obj.attribute("name").as_string("") == *entity_name)
                {
                    // Determine entity type
                    std::string entity_type = "Unknown";
                    if (scenario_obj.child("Vehicle"))
                        entity_type = "Vehicle";
                    else if (scenario_obj.child("Pedestrian"))
                        entity_type = "Pedestrian";
                    else if (scenario_obj.child("MiscObject"))
                        entity_type = "MiscObject";
                    else if (pugi::xml_node catalog_ref = scenario_obj.child("CatalogReference"))
                    {
                        std::string catalog_name = catalog_ref.attribute("catalogName").as_string("");
                        if (catalog_name.find("Vehicle") != std::string::npos)
                            entity_type = "Vehicle";
                        else if (catalog_name.find("Pedestrian") != std::string::npos)
                            entity_type = "Pedestrian";
                        else if (catalog_name.find("MiscObject") != std::string::npos)
                            entity_type = "MiscObject";
                    }

                    // Append entity type to objectType
                    *object_type = entity_type;
                    break;
                }
            }
        }
    }
}

// Helper function to find vehicle name from a node by looking for ScenarioObject or entityRef
std::string StudioDataModel::FindEntityNameFromNode(pugi::xml_node node)
{
    // First try to find entityRef in the position node or its parents
    pugi::xml_node current = node;
    while (current)
    {
        std::string name = current.name();
        // Skip entityRef on relative position nodes as they refer to the reference entity, not the subject
        bool is_relative_pos =
            (name == "RelativeLanePosition" || name == "RelativeWorldPosition" || name == "RelativeObjectPosition" || name == "RelativeRoadPosition");

        if (!is_relative_pos)
        {
            // Check for entityRef attribute
            pugi::xml_attribute entityRef = current.attribute("entityRef");
            if (entityRef && strlen(entityRef.value()) > 0)
            {
                return std::string(entityRef.value());
            }
        }
        current = current.parent();
    }

    // If no entityRef found, look for ScenarioObject in the document
    current = node;
    while (current)
    {
        if (std::string(current.name()) == "ScenarioObject")
        {
            pugi::xml_attribute nameAttr = current.attribute("name");
            if (nameAttr && strlen(nameAttr.value()) > 0)
            {
                return std::string(nameAttr.value());
            }
        }
        current = current.parent();
    }

    return "";  // No vehicle name found
}

// ---------------------------------------------------------------------------------------------------------------
// Trajectory editing data model (see Trajectory_Editing.md)
// ---------------------------------------------------------------------------------------------------------------

void EntityPose::SyncFromWorld(bool align_to_lane)
{
    ConvertWorldPosToLanePos(x, y, z, h, align_to_lane, &road_id, &lane_id, &s, &lane_offset, &relative_h);
    source = SourceRepr::WORLD;
}

void EntityPose::SyncFromLane()
{
    ConvertLanePosToWorldPos(road_id, lane_id, s, lane_offset, relative_h, &x, &y, &z, &h);
    source = SourceRepr::LANE;
}

namespace
{
double NormalizeAngle(double angle)
{
    while (angle > M_PI)
        angle -= 2.0 * M_PI;
    while (angle < -M_PI)
        angle += 2.0 * M_PI;
    return angle;
}

double LerpAngle(double a, double b, double t)
{
    return a + NormalizeAngle(b - a) * t;
}

EntityPose LerpPose(const EntityPose& a, const EntityPose& b, double t)
{
    EntityPose p;
    p.x      = a.x + (b.x - a.x) * t;
    p.y      = a.y + (b.y - a.y) * t;
    p.z      = a.z + (b.z - a.z) * t;
    p.h      = LerpAngle(a.h, b.h, t);
    p.source = EntityPose::SourceRepr::WORLD;
    return p;
}

double CatmullRomComponent(double v0, double v1, double v2, double v3, double t)
{
    double t2 = t * t;
    double t3 = t2 * t;
    return 0.5 * ((2.0 * v1) + (-v0 + v2) * t + (2.0 * v0 - 5.0 * v1 + 4.0 * v2 - v3) * t2 + (-v0 + 3.0 * v1 - 3.0 * v2 + v3) * t3);
}

// Catmull-Rom interpolation between p1 and p2 (t in [0, 1]), using p0/p3 as the neighboring control points.
EntityPose CatmullRomPose(const EntityPose& p0, const EntityPose& p1, const EntityPose& p2, const EntityPose& p3, double t)
{
    EntityPose p;
    p.x      = CatmullRomComponent(p0.x, p1.x, p2.x, p3.x, t);
    p.y      = CatmullRomComponent(p0.y, p1.y, p2.y, p3.y, t);
    p.z      = CatmullRomComponent(p0.z, p1.z, p2.z, p3.z, t);
    p.h      = LerpAngle(p1.h, p2.h, t);
    p.source = EntityPose::SourceRepr::WORLD;
    return p;
}

double Distance3D(const EntityPose& a, const EntityPose& b)
{
    double dx = b.x - a.x;
    double dy = b.y - a.y;
    double dz = b.z - a.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// Interpolate along a fine-grained polyline (fine_poses / cumulative_s, as produced by EntityPath::BuildFineSamples)
// at arc length s. Shared by EntityPath::Evaluate() and EntityPath::RebuildDenseSamples() so the underlying curve
// only needs to be sampled once.
EntityPose SampleAlongFinePoses(const std::vector<EntityPose>& fine_poses, const std::vector<double>& cumulative_s, double s)
{
    if (fine_poses.empty())
        return EntityPose();
    if (fine_poses.size() == 1)
        return fine_poses.front();

    double total = cumulative_s.back();
    s            = std::max(0.0, std::min(s, total));

    size_t idx = 0;
    while (idx + 1 < cumulative_s.size() && cumulative_s[idx + 1] < s)
        idx++;

    if (idx + 1 >= cumulative_s.size())
        return fine_poses.back();

    double seg_len = cumulative_s[idx + 1] - cumulative_s[idx];
    double t       = (seg_len > 1e-9) ? (s - cumulative_s[idx]) / seg_len : 0.0;
    return LerpPose(fine_poses[idx], fine_poses[idx + 1], t);
}
}  // namespace

void EntityPath::BuildFineSamples(std::vector<EntityPose>* out_poses, std::vector<double>* out_cumulative_s) const
{
    out_poses->clear();
    out_cumulative_s->clear();

    if (points_.empty())
        return;

    if (points_.size() == 1)
    {
        out_poses->push_back(points_.front());
        out_cumulative_s->push_back(0.0);
        return;
    }

    // Number of intermediate steps generated per segment. A straight line only needs the two endpoints; curved
    // modes are approximated by a fine polyline. True clothoid fitting is not implemented yet, so CLOTHOID
    // currently falls back to the same Catmull-Rom approximation as "Spline" (see Trajectory_Editing.md 9.1/9.4
    // for the plan to reuse RoadManager's clothoid infrastructure instead, in a later iteration).
    // With fewer than 3 points there isn't enough information for the curve to bend away from a straight line
    // anyway (Catmull-Rom with clamped neighbors degenerates to a straight segment), so render it as an
    // explicit polyline instead of spending 16 substeps on what is geometrically a single straight line.
    const int steps_per_segment = (interp_mode_ == InterpMode::LINEAR || points_.size() < 3) ? 1 : 16;

    out_poses->push_back(points_.front());
    out_cumulative_s->push_back(0.0);

    for (size_t i = 0; i + 1 < points_.size(); i++)
    {
        const EntityPose& p1 = points_[i];
        const EntityPose& p2 = points_[i + 1];

        for (int step = 1; step <= steps_per_segment; step++)
        {
            double t = static_cast<double>(step) / static_cast<double>(steps_per_segment);

            EntityPose sample;
            if (interp_mode_ == InterpMode::LINEAR)
            {
                sample = LerpPose(p1, p2, t);
            }
            else
            {
                const EntityPose& p0 = (i == 0) ? p1 : points_[i - 1];
                const EntityPose& p3 = (i + 2 < points_.size()) ? points_[i + 2] : p2;
                sample                = CatmullRomPose(p0, p1, p2, p3, t);
            }

            double d = Distance3D(out_poses->back(), sample);
            out_poses->push_back(sample);
            out_cumulative_s->push_back(out_cumulative_s->back() + d);
        }
    }

    // Recompute heading from the curve's own tangent direction instead of the per-point stored h (which is
    // aligned to the underlying road/lane at the moment the point was placed, not the shape of the path
    // itself): a vehicle following this path should face along the curve, see Trajectory_Editing.md 9.3.
    size_t sample_count = out_poses->size();
    if (sample_count >= 2)
    {
        for (size_t i = 0; i < sample_count; i++)
        {
            double dx, dy;
            if (i == 0)
            {
                dx = (*out_poses)[1].x - (*out_poses)[0].x;
                dy = (*out_poses)[1].y - (*out_poses)[0].y;
            }
            else if (i == sample_count - 1)
            {
                dx = (*out_poses)[i].x - (*out_poses)[i - 1].x;
                dy = (*out_poses)[i].y - (*out_poses)[i - 1].y;
            }
            else
            {
                // Central difference for a smoother tangent estimate away from the endpoints.
                dx = (*out_poses)[i + 1].x - (*out_poses)[i - 1].x;
                dy = (*out_poses)[i + 1].y - (*out_poses)[i - 1].y;
            }

            if (dx * dx + dy * dy > 1e-9)
                (*out_poses)[i].h = std::atan2(dy, dx);
            else if (i > 0)
                (*out_poses)[i].h = (*out_poses)[i - 1].h;  // coincident samples: keep the previous heading
        }
    }
}

double EntityPath::GetTotalLength() const
{
    std::vector<EntityPose> fine_poses;
    std::vector<double>     cumulative_s;
    BuildFineSamples(&fine_poses, &cumulative_s);
    return cumulative_s.empty() ? 0.0 : cumulative_s.back();
}

EntityPose EntityPath::Evaluate(double s) const
{
    std::vector<EntityPose> fine_poses;
    std::vector<double>     cumulative_s;
    BuildFineSamples(&fine_poses, &cumulative_s);
    return SampleAlongFinePoses(fine_poses, cumulative_s, s);
}

int EntityPath::FindNearestPointIndex(double x, double y) const
{
    if (points_.empty())
        return -1;

    int    best_index    = 0;
    double best_dist_sqr = std::numeric_limits<double>::max();
    for (size_t i = 0; i < points_.size(); i++)
    {
        double dx       = points_[i].x - x;
        double dy       = points_[i].y - y;
        double dist_sqr = dx * dx + dy * dy;
        if (dist_sqr < best_dist_sqr)
        {
            best_dist_sqr = dist_sqr;
            best_index    = static_cast<int>(i);
        }
    }
    return best_index;
}

bool EntityPath::FindNearestPositionOnPath(double x, double y, double* out_s, double* out_x, double* out_y, double* out_z, double* out_dist_sqr) const
{
    if (points_.size() < 2)
        return false;

    double best_dist_sqr = std::numeric_limits<double>::max();
    double best_s = 0.0, best_x = 0.0, best_y = 0.0, best_z = 0.0;
    double cumulative = 0.0;

    for (size_t i = 0; i + 1 < points_.size(); i++)
    {
        const EntityPose& a = points_[i];
        const EntityPose& b = points_[i + 1];

        double abx         = b.x - a.x;
        double aby         = b.y - a.y;
        double seg_len_sqr = abx * abx + aby * aby;
        double t           = 0.0;
        if (seg_len_sqr > 1e-9)
        {
            t = ((x - a.x) * abx + (y - a.y) * aby) / seg_len_sqr;
            t = std::min(1.0, std::max(0.0, t));
        }

        double px       = a.x + t * abx;
        double py       = a.y + t * aby;
        double dx       = px - x;
        double dy       = py - y;
        double dist_sqr = dx * dx + dy * dy;

        double seg_len = Distance3D(a, b);  // matches InsertPoint()'s own cumulative-distance metric below
        if (dist_sqr < best_dist_sqr)
        {
            best_dist_sqr = dist_sqr;
            best_s        = cumulative + t * seg_len;
            best_x        = px;
            best_y        = py;
            best_z        = a.z + t * (b.z - a.z);
        }
        cumulative += seg_len;
    }

    *out_s        = best_s;
    *out_x        = best_x;
    *out_y        = best_y;
    *out_z        = best_z;
    *out_dist_sqr = best_dist_sqr;
    return true;
}

int EntityPath::InsertPoint(double s, const EntityPose& pose)
{
    // Order the new point using the cumulative polyline distance between the existing control points - a
    // light-weight approximation that avoids evaluating the full interpolated curve just to insert a point.
    double cumulative = 0.0;
    size_t insert_at  = points_.size();
    for (size_t i = 0; i < points_.size(); i++)
    {
        if (i > 0)
            cumulative += Distance3D(points_[i - 1], points_[i]);
        if (cumulative >= s)
        {
            insert_at = i;
            break;
        }
    }

    points_.insert(points_.begin() + static_cast<long>(insert_at), pose);
    return static_cast<int>(insert_at);
}

void EntityPath::RemovePoint(int index)
{
    if (index < 0 || static_cast<size_t>(index) >= points_.size())
        return;
    points_.erase(points_.begin() + index);
}

void EntityPath::RebuildDenseSamples(double ds)
{
    dense_samples_.clear();

    std::vector<EntityPose> fine_poses;
    std::vector<double>     cumulative_s;
    BuildFineSamples(&fine_poses, &cumulative_s);

    if (fine_poses.empty())
        return;
    if (fine_poses.size() == 1 || ds <= 1e-6)
    {
        dense_samples_ = fine_poses;
        return;
    }

    double total = cumulative_s.back();
    for (double s = 0.0; s < total; s += ds)
        dense_samples_.push_back(SampleAlongFinePoses(fine_poses, cumulative_s, s));
    dense_samples_.push_back(fine_poses.back());
}

// EntitySpeedProfile's implementation (linear / Fritsch-Carlson monotonic cubic interpolation, time
// integration and its inverse) lives in TrajectorySolver.cpp together with the keyframe solver, keeping the
// whole speed/keyframe math unit-testable without OSG/UI dependencies.

std::string DeriveTrajJsonPath(const std::string& xosc_path)
{
    if (xosc_path.empty())
        return "";

    std::string base       = xosc_path;
    size_t      last_dot   = base.find_last_of('.');
    size_t      last_slash = base.find_last_of("/\\");
    if (last_dot != std::string::npos && (last_slash == std::string::npos || last_dot > last_slash))
        base = base.substr(0, last_dot);

    return base + ".traj.json";
}

// The keyframe chain solver (v2: reachability oracle + pin nodes + Q0-Q3 cascade, see
// Trajectory_Editing_Enhancement.md section 12) lives in TrajectorySolver.cpp;
// EntityTrajectory::ResolveKeyframes()/RemoveKeyframe() in the header delegate straight to it.

namespace
{
std::string PathInterpModeToString(EntityPath::InterpMode mode)
{
    switch (mode)
    {
        case EntityPath::InterpMode::LINEAR:
            return "linear";
        case EntityPath::InterpMode::CLOTHOID:
            return "clothoid";
        case EntityPath::InterpMode::CATMULL_ROM:
        default:
            return "spline";
    }
}

EntityPath::InterpMode StringToPathInterpMode(const std::string& mode)
{
    if (mode == "linear")
        return EntityPath::InterpMode::LINEAR;
    if (mode == "clothoid")
        return EntityPath::InterpMode::CLOTHOID;
    return EntityPath::InterpMode::CATMULL_ROM;
}

std::string SpeedInterpModeToString(EntitySpeedProfile::InterpMode mode)
{
    return mode == EntitySpeedProfile::InterpMode::MONOTONIC_CUBIC ? "monotonic_cubic" : "linear";
}

EntitySpeedProfile::InterpMode StringToSpeedInterpMode(const std::string& mode)
{
    return mode == "monotonic_cubic" ? EntitySpeedProfile::InterpMode::MONOTONIC_CUBIC : EntitySpeedProfile::InterpMode::LINEAR;
}

// Round to a fixed number of decimals before serializing so ".traj.json" diffs stay small and readable across
// saves (see Trajectory_Editing.md section 5.2).
double RoundToDecimals(double value, int decimals)
{
    double scale = std::pow(10.0, decimals);
    return std::round(value * scale) / scale;
}
}  // namespace

EntityTrajectory& StudioDataModel::CreateEntityTrajectory(const std::string& entity_name)
{
    EntityTrajectory traj;
    traj.entity_name_       = entity_name;
    traj.id_                = NextUniqueTrajectoryId();
    auto result             = entity_trajectories_.emplace(entity_name, std::move(traj));
    trajectories_modified_  = true;
    return result.first->second;
}

void StudioDataModel::RemoveEntityTrajectory(const std::string& entity_name)
{
    if (entity_trajectories_.erase(entity_name) > 0)
        trajectories_modified_ = true;
}

int StudioDataModel::NextUniqueTrajectoryId() const
{
    int next_id = 1;
    for (const auto& entry : entity_trajectories_)
        next_id = std::max(next_id, entry.second.id_ + 1);
    return next_id;
}

bool StudioDataModel::SaveTrajJson(const std::string& path)
{
    std::ofstream file(path);
    if (!file.is_open())
    {
        LOG("Failed to open [%s] for writing trajectories", path.c_str());
        return false;
    }
    file << TrajectoriesToJsonString();
    file.close();

    trajectories_modified_ = false;
    LOG("Trajectories saved: [%s]", path.c_str());
    return true;
}

std::string StudioDataModel::TrajectoriesToJsonString() const
{
    nlohmann::json root;
    root["version"]     = 2;
    root["source_xosc"] = FileNameOf(xosc_path_);

    nlohmann::json entities_json = nlohmann::json::object();
    for (const auto& entry : entity_trajectories_)
    {
        const EntityTrajectory& traj = entry.second;

        nlohmann::json path_points_json = nlohmann::json::array();
        for (const auto& p : traj.path_.points_)
        {
            nlohmann::json point_json;
            point_json["x"]           = RoundToDecimals(p.x, 3);
            point_json["y"]           = RoundToDecimals(p.y, 3);
            point_json["z"]           = RoundToDecimals(p.z, 3);
            point_json["h"]           = RoundToDecimals(p.h, 4);
            point_json["road_id"]     = p.road_id;
            point_json["lane_id"]     = p.lane_id;
            point_json["s"]           = RoundToDecimals(p.s, 3);
            point_json["lane_offset"] = RoundToDecimals(p.lane_offset, 3);
            point_json["relative_h"]  = RoundToDecimals(p.relative_h, 4);
            path_points_json.push_back(point_json);
        }

        nlohmann::json speed_points_json = nlohmann::json::array();
        for (const auto& sp : traj.speed_profile_.points_)
        {
            nlohmann::json point_json;
            point_json["s"]     = RoundToDecimals(sp.s, 3);
            point_json["speed"] = RoundToDecimals(sp.speed, 3);
            speed_points_json.push_back(point_json);
        }

        nlohmann::json entity_json;
        entity_json["path"] = {
            {"interp_mode", PathInterpModeToString(traj.path_.interp_mode_)},
            {   "points",                                   path_points_json}
        };
        entity_json["speed_profile"] = {
            {"interp_mode", SpeedInterpModeToString(traj.speed_profile_.interp_mode_)},
            {   "points",                                  speed_points_json}
        };

        // Optional; empty means "use the renderer's fixed default" (backward-compatible with older files).
        if (!traj.vehicle_catalog_entry_name_.empty())
            entity_json["vehicle_catalog_entry"] = traj.vehicle_catalog_entry_name_;

        // Always written: unlike the other optional fields below, id_ has no "default" that would make it
        // safe to omit (0 could clash with a legitimately-assigned id_ of 0).
        entity_json["id"] = traj.id_;

        // Optional; both default to true when absent (backward-compatible with older files, and matching the
        // default for hand-authored trajectories). Only written out when false, to keep normal files unchanged.
        if (!traj.show_path_points_)
            entity_json["show_path_points"] = false;
        if (!traj.show_speed_points_)
            entity_json["show_speed_points"] = false;

        // Optional; hidden defaults to false, alpha defaults to 1.0 (fully opaque) when absent.
        if (traj.hidden_)
            entity_json["hidden"] = true;
        if (traj.alpha_ < 1.0)
            entity_json["alpha"] = RoundToDecimals(traj.alpha_, 2);

        // Time keyframes (Trajectory_Editing_Enhancement.md, schema v2): only (t, s) is persisted; the world
        // position is derived from the path at render time and achieved_t/feasible are recomputed on load.
        if (!traj.keyframes_.empty())
        {
            nlohmann::json keyframes_json = nlohmann::json::array();
            for (const auto& kf : traj.keyframes_)
            {
                nlohmann::json kf_json;
                kf_json["t"] = RoundToDecimals(kf.t, 3);
                kf_json["s"] = RoundToDecimals(kf.s, 3);
                keyframes_json.push_back(kf_json);
            }
            entity_json["keyframes"] = keyframes_json;
        }

        entities_json[entry.first] = entity_json;
    }
    root["entities"] = entities_json;

    return root.dump(2);
}

bool StudioDataModel::TrajectoriesFromJsonString(const std::string& json_text)
{
    nlohmann::json root;
    try
    {
        root = nlohmann::json::parse(json_text);
    }
    catch (const std::exception& e)
    {
        LOG("Failed to parse trajectories JSON: %s", e.what());
        return false;
    }

    std::map<std::string, EntityTrajectory> new_trajectories;

    if (root.contains("entities") && root["entities"].is_object())
    {
        for (auto it = root["entities"].begin(); it != root["entities"].end(); ++it)
        {
            EntityTrajectory traj;
            traj.entity_name_ = it.key();

            const nlohmann::json& entity_json = it.value();

            traj.vehicle_catalog_entry_name_ = entity_json.value("vehicle_catalog_entry", std::string());
            traj.show_path_points_           = entity_json.value("show_path_points", true);
            traj.show_speed_points_          = entity_json.value("show_speed_points", true);
            traj.hidden_                     = entity_json.value("hidden", false);
            traj.alpha_                      = entity_json.value("alpha", 1.0);
            // -1 is a sentinel meaning "no explicit id in this file" (older files predating the id_ field);
            // resolved to an actually-unique value in the post-processing pass below.
            traj.id_ = entity_json.value("id", -1);

            if (entity_json.contains("path") && entity_json["path"].contains("points"))
            {
                traj.path_.interp_mode_ = StringToPathInterpMode(entity_json["path"].value("interp_mode", "linear"));
                for (const auto& point_json : entity_json["path"]["points"])
                {
                    EntityPose pose;
                    pose.x           = point_json.value("x", 0.0);
                    pose.y           = point_json.value("y", 0.0);
                    pose.z           = point_json.value("z", 0.0);
                    pose.h           = point_json.value("h", 0.0);
                    pose.road_id     = point_json.value("road_id", 0);
                    pose.lane_id     = point_json.value("lane_id", 0);
                    pose.s           = point_json.value("s", 0.0);
                    pose.lane_offset = point_json.value("lane_offset", 0.0);
                    pose.relative_h  = point_json.value("relative_h", 0.0);
                    pose.source      = EntityPose::SourceRepr::WORLD;
                    traj.path_.points_.push_back(pose);
                }
            }

            if (entity_json.contains("speed_profile") && entity_json["speed_profile"].contains("points"))
            {
                traj.speed_profile_.interp_mode_ = StringToSpeedInterpMode(entity_json["speed_profile"].value("interp_mode", "linear"));
                for (const auto& point_json : entity_json["speed_profile"]["points"])
                {
                    SpeedProfilePoint sp;
                    sp.s     = point_json.value("s", 0.0);
                    sp.speed = point_json.value("speed", 0.0);
                    traj.speed_profile_.points_.push_back(sp);
                }
            }

            // Optional v2 field; v1 files simply have no keyframes. The persisted profile already reflects the
            // solved state, but achieved_t/feasible are volatile - recompute them so status displays are right.
            if (entity_json.contains("keyframes") && entity_json["keyframes"].is_array())
            {
                for (const auto& kf_json : entity_json["keyframes"])
                {
                    TrajectoryKeyframe kf;
                    kf.t = kf_json.value("t", 0.0);
                    kf.s = kf_json.value("s", 0.0);
                    traj.keyframes_.push_back(kf);
                }
                traj.ResolveKeyframes();
            }

            new_trajectories[traj.entity_name_] = std::move(traj);
        }
    }

    // Assign unique ids to any entity whose file had no explicit "id" (older files predating the id_ field),
    // starting after the highest explicitly-specified id so they don't clash with each other or those.
    int next_auto_id = 1;
    for (const auto& entry : new_trajectories)
        next_auto_id = std::max(next_auto_id, entry.second.id_ + 1);
    for (auto& entry : new_trajectories)
        if (entry.second.id_ < 0)
            entry.second.id_ = next_auto_id++;

    entity_trajectories_ = std::move(new_trajectories);
    return true;
}

bool StudioDataModel::LoadTrajJson(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open())
        return false;

    std::ostringstream buffer;
    buffer << file.rdbuf();
    file.close();

    if (!TrajectoriesFromJsonString(buffer.str()))
        return false;

    traj_json_path_        = path;
    trajectories_modified_ = false;
    LOG("Trajectories loaded: [%s] (%d entities)", path.c_str(), static_cast<int>(entity_trajectories_.size()));
    return true;
}

namespace
{
// Length/Width/Height of an OSC vehicle catalog entry, used to populate the CSV's own Length/Width/Height
// columns. Falls back to generic car dimensions if the catalog or the entry can't be found (e.g. no
// vehicle_catalog_entry_name_ set, or the referenced catalog file has moved).
struct CsvVehicleDimensions
{
    double length = 4.5;
    double width  = 1.8;
    double height = 1.5;
};

CsvVehicleDimensions LookupCsvVehicleDimensions(const std::string& entry_name)
{
    CsvVehicleDimensions dims;
    if (entry_name.empty())
        return dims;

    // Mirrors the VehicleCatalog.xosc search-path logic used elsewhere for the same fixed catalog file
    // (StudioGui::GetVehicleCatalogEntryNames(), EntityTrajectoryRenderer::LoadTrajectoryVehicleModel()).
    std::vector<std::string> candidates = {"resources/xosc/Catalogs/Vehicles/VehicleCatalog.xosc",
                                           "../resources/xosc/Catalogs/Vehicles/VehicleCatalog.xosc"};
    for (const auto& search_path : SE_Env::Inst().GetPaths())
    {
        candidates.push_back(search_path + "/xosc/Catalogs/Vehicles/VehicleCatalog.xosc");
        candidates.push_back(search_path + "/resources/xosc/Catalogs/Vehicles/VehicleCatalog.xosc");
    }

    pugi::xml_document doc;
    bool                loaded = false;
    for (const auto& candidate : candidates)
    {
        if (FileExists(candidate.c_str()) && doc.load_file(candidate.c_str()))
        {
            loaded = true;
            break;
        }
    }
    if (!loaded)
        return dims;

    for (pugi::xml_node vehicle : doc.child("OpenSCENARIO").child("Catalog").children("Vehicle"))
    {
        if (std::string(vehicle.attribute("name").as_string("")) != entry_name)
            continue;
        pugi::xml_node dims_node = vehicle.child("BoundingBox").child("Dimensions");
        if (dims_node)
        {
            dims.length = dims_node.attribute("length").as_double(dims.length);
            dims.width  = dims_node.attribute("width").as_double(dims.width);
            dims.height = dims_node.attribute("height").as_double(dims.height);
        }
        break;
    }
    return dims;
}

// Minimal CSV line splitter: the trajectory CSV format has no quoted/escaped fields (every value is a plain
// number or a short identifier), so a plain comma split is sufficient.
std::vector<std::string> SplitCsvLine(const std::string& line)
{
    std::vector<std::string> fields;
    std::string              field;
    std::istringstream       stream(line);
    while (std::getline(stream, field, ','))
        fields.push_back(field);
    if (!line.empty() && line.back() == ',')
        fields.push_back("");  // a trailing empty field is otherwise dropped by the getline loop above
    return fields;
}
}  // namespace

bool StudioDataModel::ExportTrajectoriesCsv(const std::string& path) const
{
    std::ofstream file(path);
    if (!file.is_open())
    {
        LOG("Failed to open [%s] for writing CSV trajectories", path.c_str());
        return false;
    }

    file << "ID,Time,PositionX,PositionY,PositionZ,Length,Width,Height,Yaw,Pitch,Roll,VX,VY,VZ,AX,AY,AZ,Category,Style,Color,Ego,raw_id\n";
    file << std::setprecision(9);

    const double kSampleDt = 0.1;  // seconds; matches trajectories_test.csv's dominant sampling interval
    int          exported  = 0;

    for (const auto& entry : entity_trajectories_)
    {
        const std::string&      name = entry.first;
        const EntityTrajectory& traj = entry.second;
        if (!traj.HasPath())
            continue;

        double total_length = traj.path_.GetTotalLength();
        double total_time   = traj.speed_profile_.EvaluateTimeAtS(total_length);
        bool   is_ego       = (name == "ego" || name == "Ego" || name == "EGO");
        int    id           = traj.id_;  // the trajectory's own (user-editable, Trajectories tab) id

        CsvVehicleDimensions dims = LookupCsvVehicleDimensions(traj.vehicle_catalog_entry_name_);

        for (double t = 0.0; t <= total_time + 1e-6; t += kSampleDt)
        {
            double     s     = traj.speed_profile_.EvaluateSAtTime(std::min(t, total_time));
            EntityPose pose  = traj.path_.Evaluate(s);
            double     speed = traj.speed_profile_.EvaluateSpeed(s);
            double     vx    = speed * std::cos(pose.h);
            double     vy    = speed * std::sin(pose.h);

            file << id << ',' << t << ',' << pose.x << ',' << pose.y << ',' << pose.z << ',' << dims.length << ',' << dims.width << ','
                << dims.height << ',' << pose.h << ",0,0," << vx << ',' << vy << ",0,0,0,0,vehicle,car,," << (is_ego ? "Y" : "N") << ',' << id
                << "\n";
        }
        exported++;
    }

    file.close();
    LOG("Trajectories exported to CSV: [%s] (%d entities)", path.c_str(), exported);
    return true;
}

bool StudioDataModel::ImportTrajectoriesCsv(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        LOG("Failed to open [%s] for reading CSV trajectories", path.c_str());
        return false;
    }

    std::string header_line;
    if (!std::getline(file, header_line))
    {
        LOG("CSV file [%s] is empty", path.c_str());
        return false;
    }

    std::vector<std::string>      header_fields = SplitCsvLine(header_line);
    std::map<std::string, size_t> column_index;
    for (size_t i = 0; i < header_fields.size(); i++)
        column_index[header_fields[i]] = i;

    auto find_column = [&](const std::string& column_name) -> int
    {
        auto found = column_index.find(column_name);
        return found == column_index.end() ? -1 : static_cast<int>(found->second);
    };

    int id_col   = find_column("ID");
    int time_col = find_column("Time");
    int x_col    = find_column("PositionX");
    int y_col    = find_column("PositionY");
    int z_col    = find_column("PositionZ");
    int yaw_col  = find_column("Yaw");
    int vx_col   = find_column("VX");
    int vy_col   = find_column("VY");
    if (id_col < 0 || time_col < 0 || x_col < 0 || y_col < 0 || z_col < 0 || yaw_col < 0 || vx_col < 0 || vy_col < 0)
    {
        LOG("CSV file [%s] is missing one or more required columns "
            "(ID, Time, PositionX, PositionY, PositionZ, Yaw, VX, VY)",
            path.c_str());
        return false;
    }

    struct CsvSample
    {
        double time = 0.0, x = 0.0, y = 0.0, z = 0.0, yaw = 0.0, vx = 0.0, vy = 0.0;
    };

    std::map<std::string, std::vector<CsvSample>> samples_by_id;
    size_t max_needed_field_count =
        static_cast<size_t>(std::max({id_col, time_col, x_col, y_col, z_col, yaw_col, vx_col, vy_col})) + 1;

    std::string line;
    int         line_number = 1;
    while (std::getline(file, line))
    {
        line_number++;
        if (line.empty())
            continue;

        std::vector<std::string> fields = SplitCsvLine(line);
        if (fields.size() < max_needed_field_count)
        {
            LOG("CSV [%s] line %d: too few fields, skipping", path.c_str(), line_number);
            continue;
        }

        try
        {
            CsvSample sample;
            sample.time = std::stod(fields[static_cast<size_t>(time_col)]);
            sample.x    = std::stod(fields[static_cast<size_t>(x_col)]);
            sample.y    = std::stod(fields[static_cast<size_t>(y_col)]);
            sample.z    = std::stod(fields[static_cast<size_t>(z_col)]);
            sample.yaw  = std::stod(fields[static_cast<size_t>(yaw_col)]);
            sample.vx   = std::stod(fields[static_cast<size_t>(vx_col)]);
            sample.vy   = std::stod(fields[static_cast<size_t>(vy_col)]);
            samples_by_id[fields[static_cast<size_t>(id_col)]].push_back(sample);
        }
        catch (const std::exception&)
        {
            LOG("CSV [%s] line %d: failed to parse numeric fields, skipping", path.c_str(), line_number);
        }
    }

    if (samples_by_id.empty())
    {
        LOG("CSV file [%s] contained no usable trajectory rows", path.c_str());
        return false;
    }

    int imported = 0;
    std::map<int, std::string> imported_id_to_entity;  // for the id/id+900000 pairing pass below
    for (auto& id_entry : samples_by_id)
    {
        std::vector<CsvSample>& samples = id_entry.second;
        if (samples.empty())
            continue;
        std::sort(samples.begin(), samples.end(), [](const CsvSample& a, const CsvSample& b) { return a.time < b.time; });

        // Unique entity name across both the xosc and entity_trajectories_ namespaces (Trajectory_Editing.md 6.1/12).
        std::string base_name   = "csv_" + id_entry.first;
        std::string entity_name = base_name;
        int         suffix      = 1;
        while (NameExists(entity_name, pugi::xml_node()) || entity_trajectories_.count(entity_name) > 0)
            entity_name = base_name + "_" + std::to_string(suffix++);

        EntityTrajectory traj;
        traj.entity_name_       = entity_name;
        traj.path_.interp_mode_ = EntityPath::InterpMode::LINEAR;  // preserve the raw recorded points as-is
        // Too many raw points to sensibly show/edit individually (Trajectory_Editing.md, see also the
        // Trajectories tab's Show Path Point/Show Speed Point checkboxes).
        traj.show_path_points_  = false;
        traj.show_speed_points_ = false;
        // Reuse the CSV's own ID when it parses as an integer, so a round-tripped export/import keeps the
        // same id; fall back to an auto-assigned one if the CSV used a non-numeric identifier.
        try
        {
            traj.id_ = std::stoi(id_entry.first);
        }
        catch (const std::exception&)
        {
            traj.id_ = NextUniqueTrajectoryId();
        }

        double     cumulative_s = 0.0;
        bool       has_prev     = false;
        EntityPose prev_pose;
        for (const CsvSample& sample : samples)
        {
            EntityPose pose;
            pose.x      = sample.x;
            pose.y      = sample.y;
            pose.z      = sample.z;
            pose.h      = sample.yaw;
            pose.source = EntityPose::SourceRepr::WORLD;
            // Deliberately not calling SyncFromWorld()/SyncFromLane(): these are exact recorded world
            // positions and must not be snapped onto a lane centerline the way freshly-picked points are.

            if (has_prev)
                cumulative_s += Distance3D(prev_pose, pose);

            traj.path_.points_.push_back(pose);

            SpeedProfilePoint sp;
            sp.s     = cumulative_s;
            sp.speed = std::sqrt(sample.vx * sample.vx + sample.vy * sample.vy);
            traj.speed_profile_.points_.push_back(sp);

            prev_pose = pose;
            has_prev  = true;
        }
        traj.SyncSpeedProfileEndpoints();

        entity_trajectories_[entity_name] = std::move(traj);
        imported_id_to_entity[entity_trajectories_[entity_name].id_] = entity_name;
        imported++;
    }

    // Some traffic datasets pair a "base" trajectory (id X) with a shifted-id variant (id X + 900000) that
    // represents the same vehicle for a different purpose; when both are present in this same import, dim the
    // base one (id X) to make the pairing visually obvious (Trajectories tab Alpha slider, quantized 0.4).
    for (const auto& id_entry : imported_id_to_entity)
    {
        int id_value = id_entry.first;
        if (id_value <= 900000)
            continue;
        auto base_entry = imported_id_to_entity.find(id_value - 900000);
        if (base_entry == imported_id_to_entity.end())
            continue;
        auto traj_it = entity_trajectories_.find(base_entry->second);
        if (traj_it != entity_trajectories_.end())
            traj_it->second.alpha_ = 0.4;
    }

    if (imported > 0)
        trajectories_modified_ = true;
    LOG("Trajectories imported from CSV: [%s] (%d entities)", path.c_str(), imported);
    return imported > 0;
}

