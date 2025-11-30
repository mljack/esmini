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
#include <memory>
#include <vector>
#include <string>
#include <functional>

// Diff structures
struct DiffChunk
{
    int                      start_line;    // 0-indexed start line in the original text
    int                      delete_count;  // Number of lines to delete
    std::vector<std::string> insert_lines;  // Lines to insert
};

std::vector<DiffChunk> ComputeDiff(const std::string& old_text, const std::string& new_text);
std::string            ApplyDiff(const std::string& old_text, const std::vector<DiffChunk>& diffs);

// Structure to hold subelement information with its content model type
struct SubelementInfo
{
    std::string                 name;
    bool                        has_choice;
    bool                        is_required;  // For xsd:all, indicates if the element is required
    int                         min_occurs;
    int                         max_occurs;
    bool                        is_max_unbounded;
    std::vector<pugi::xml_node> xsd_path;
    std::string                 choice_id;
    std::string                 choice_branch_id;
    int                         choice_min_occurs;
    int                         choice_max_occurs;

    SubelementInfo(const std::string&                 n,
                   bool                               has_choice1,
                   bool                               req,
                   int                                min_occ,
                   int                                max_occ,
                   bool                               max_unbounded,
                   const std::vector<pugi::xml_node>& path,
                   const std::string&                 c_id        = "",
                   const std::string&                 c_branch_id = "",
                   int                                c_min_occ   = 0,
                   int                                c_max_occ   = 0)
        : name(n),
          has_choice(has_choice1),
          is_required(req),
          min_occurs(min_occ),
          max_occurs(max_occ),
          is_max_unbounded(max_unbounded),
          xsd_path(path),
          choice_id(c_id),
          choice_branch_id(c_branch_id),
          choice_min_occurs(c_min_occ),
          choice_max_occurs(c_max_occ)
    {
    }
};

// Position types
enum class PositionType
{
    WORLD_POSITION,
    RELATIVE_WORLD_POSITION,
    RELATIVE_OBJECT_POSITION,
    ROAD_POSITION,
    RELATIVE_ROAD_POSITION,
    LANE_POSITION,
    RELATIVE_LANE_POSITION,
    ROUTE_POSITION,
    GEO_POSITION,
    TRAJECTORY_POSITION,
    UNKNOWN
};

std::string PositionTypeToString(PositionType type);

// Position extraction data structure
struct PositionInfo
{
    PositionType type = PositionType::UNKNOWN;  // Position type (WorldPosition, LanePosition, etc.)
    std::string  object_type;                   // Object classification (condition, initialization, etc.)

    pugi::xml_node node;

    // Original position data
    double x = 0.0, y = 0.0, z = 0.0;     // World coordinates
    double h = 0.0, p = 0.0, r = 0.0;     // Orientation (heading, pitch, roll) in radians
    double dx = 0.0, dy = 0.0, dz = 0.0;  // Relative coordinates
    double absolute_h = 0;

    // Orientation handling
    bool        orientation_is_relative = true;  // true if orientation is relative to reference
    std::string orientation_reference;           // Reference for relative orientation (e.g., "lane", "road")

    // Road/Lane coordinates
    int    road_id = 0;
    int    lane_id = 0;
    double s = 0.0, t = 0.0;
    double lane_offset = 0.0;
    int    dLane       = 0;
    double ds = 0.0, dt = 0.0, dlane_offset = 0.0;

    // Geographic coordinates
    double latitude = 0.0, longitude = 0.0, altitude = 0.0;

    // References
    std::string entity_ref;
    std::string route_ref;
    std::string trajectory_ref;
    std::string relative_to;

    // Name information
    std::string name;

    bool selected = false;
};

std::string QueryTypeByNameFromSchema(const pugi::xml_document& schema, const std::string& element_name);
std::string QueryNameByTypeFromSchema(const pugi::xml_document& schema, const std::string& element_type);
std::string QueryValueTypeFromSchema(const pugi::xml_document& schema,
                                     const std::string&        element_name,
                                     const std::string&        attribute_name,
                                     std::vector<std::string>* enums);
std::string GetDefaultValueForAttribute(const pugi::xml_document& schema, const std::string& element_name, const std::string& attribute_name);
std::vector<std::string>    QueryAttrsFromSchema(const pugi::xml_document& schema, const std::string& type_name);
std::vector<std::string>    QueryOptionalAttrsFromSchema(const pugi::xml_document& schema, const std::string& type_name);
std::vector<SubelementInfo> QuerySubelementsWithModelInfo(const pugi::xml_document& schema, const std::string& type_name);
std::vector<std::string>    QueryRequiredAttrsFromSchema(const pugi::xml_document& schema, const std::string& type_name);
void ExcludeExistingAttrs(pugi::xml_node node, std::vector<std::string>* attrs_to_create, std::vector<std::string>* attrs_to_delete);
void ExcludeExistingElements(const pugi::xml_document& schema, pugi::xml_node node, std::vector<SubelementInfo>* elements_to_create);
void FindElementsToRemove(const pugi::xml_document&    schema,
                          pugi::xml_node               node,
                          std::vector<SubelementInfo>* schema_elements,
                          const std::string&           new_node_name);
void RemoveElements(const pugi::xml_document& schema, pugi::xml_node node, const std::vector<SubelementInfo>& elements_to_remove);
std::vector<pugi::xml_node> FindElements(const pugi::xml_document& schema, pugi::xml_node node, const std::vector<pugi::xml_node>& xsd_path);
int                         CountSiblingByName(pugi::xml_node node, const std::string& name);

bool IsElementDeprecated(pugi::xml_node node);
bool IsAttributeDeprecated(const pugi::xml_document& schema, const std::string& element_name, const std::string& attribute_name);
bool IsEnumDeprecated(const pugi::xml_document& schema, const std::string& type_name, const std::string& enum_value);
void FilterDeprecatedSubelements(std::vector<SubelementInfo>* elements);
void FilterDeprecatedAttributes(std::vector<std::string>* attributes, const pugi::xml_document& schema, const std::string& element_name);

void           FindPositionNodesRecursive(pugi::xml_node node, std::vector<pugi::xml_node>& position_nodes);
pugi::xml_node FindPositionInNode(pugi::xml_node node, const PositionInfo& position_info);
pugi::xml_node FindScenarioObjectByName(pugi::xml_node root, const std::string& object_name);
bool           DoesPositionNodeMatch(const PositionInfo& position_info, pugi::xml_node position_node);

void ParseOccurs(pugi::xml_node element_node, int* min_occurs, int* max_occurs, bool* max_unbounded);

void GetNodesBelow(pugi::xml_node node, std::vector<pugi::xml_node>* nodes, int depth = 999);
void GetNodesAbove(pugi::xml_node node, std::vector<pugi::xml_node>* nodes, int depth = 999);
void TraverseNodesBelow(pugi::xml_node                      node,
                        std::function<void(pugi::xml_node)> node_push,
                        std::function<void()>               node_pop,
                        std::function<void(int id)>         child_push,
                        std::function<void()>               child_pop);