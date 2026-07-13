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
