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

    return exe_dir + "/config.json";
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
    if (!xml_schema_.load_file("OpenSCENARIO.xsd"))
        printf("schema loading failed\n");

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
            if (std::string(attr.name()) == "entityRef" && attr.value() == old_name)
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

        // size_t count = 0;
        // for(auto& chunk : undo_diff)
        //     for(auto &line : chunk.insert_lines)
        //     count += line.size();
        // LOG("undo diff: %d vs %d", current_snapshot_.size(), count);

        if (undo_stack_.size() > MAX_UNDO_STACK_SIZE)
        {
            undo_stack_.pop_front();
        }

        // Clear redo stack when a new action is performed
        redo_stack_.clear();

        // Update current snapshot
        current_snapshot_ = new_state;
    }
}

void StudioDataModel::ClearUndoRedoStacks()
{
    undo_stack_.clear();
    redo_stack_.clear();
    current_snapshot_.clear();
}

void StudioDataModel::Undo()
{
    if (undo_stack_.empty())
        return;

    // Get the undo patch (New -> Old)
    std::vector<DiffChunk> undo_diff = undo_stack_.back();
    undo_stack_.pop_back();

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

    // We need to compute undo patch (New -> Old) for the undo stack
    // Current is Old. Target is New.
    // New = ApplyDiff(Old, redo_diff)

    std::string new_state = ApplyDiff(current_snapshot_, redo_diff);

    // Compute Undo patch: New -> Old
    std::vector<DiffChunk> undo_diff = ComputeDiff(new_state, current_snapshot_);
    undo_stack_.push_back(undo_diff);

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
