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

#include "XmlUtil.hpp"
#include "CommonMini.hpp"

#include <set>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <sstream>
#include <iostream>
#include <string.h>
#include <cctype>
#include <ctime>

// Helper to split string into lines
static std::vector<std::string> SplitLines(const std::string& text)
{
    std::vector<std::string> lines;
    std::stringstream        ss(text);
    std::string              line;
    while (std::getline(ss, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        lines.push_back(line);
    }
    return lines;
}

// Simple LCS-based diff implementation
// Optimizations:
// 1. Trim common prefix and suffix
// 2. If middle section is too large, fallback to replace-all to prevent freeze
std::vector<DiffChunk> ComputeDiff(const std::string& old_text, const std::string& new_text)
{
    std::vector<std::string> old_lines = SplitLines(old_text);
    std::vector<std::string> new_lines = SplitLines(new_text);

    int n = (int)old_lines.size();
    int m = (int)new_lines.size();

    // 1. Trim common prefix
    int prefix_len = 0;
    while (prefix_len < n && prefix_len < m && old_lines[prefix_len] == new_lines[prefix_len])
    {
        prefix_len++;
    }

    // 2. Trim common suffix
    int suffix_len = 0;
    while (suffix_len < (n - prefix_len) && suffix_len < (m - prefix_len) && old_lines[n - 1 - suffix_len] == new_lines[m - 1 - suffix_len])
    {
        suffix_len++;
    }

    int old_mid_start = prefix_len;
    int old_mid_end   = n - suffix_len;
    int new_mid_start = prefix_len;
    int new_mid_end   = m - suffix_len;

    int old_mid_len = old_mid_end - old_mid_start;
    int new_mid_len = new_mid_end - new_mid_start;

    std::vector<DiffChunk> diffs;

    // Optimization: If the changed region is too large, just replace the whole block
    // This avoids O(N*M) complexity for large changes
    if (old_mid_len * new_mid_len > 250000)  // Limit to ~500x500 matrix
    {
        DiffChunk chunk;
        chunk.start_line   = old_mid_start;
        chunk.delete_count = old_mid_len;
        for (int i = new_mid_start; i < new_mid_end; ++i)
        {
            chunk.insert_lines.push_back(new_lines[i]);
        }
        if (chunk.delete_count > 0 || !chunk.insert_lines.empty())
        {
            diffs.push_back(chunk);
        }
        return diffs;
    }

    // LCS Algorithm
    std::vector<std::vector<int>> dp(old_mid_len + 1, std::vector<int>(new_mid_len + 1));

    for (int i = 0; i <= old_mid_len; ++i)
    {
        for (int j = 0; j <= new_mid_len; ++j)
        {
            if (i == 0 || j == 0)
                dp[i][j] = 0;
            else if (old_lines[old_mid_start + i - 1] == new_lines[new_mid_start + j - 1])
                dp[i][j] = dp[i - 1][j - 1] + 1;
            else
                dp[i][j] = std::max(dp[i - 1][j], dp[i][j - 1]);
        }
    }

    // Backtrack to find diff
    int i = old_mid_len;
    int j = new_mid_len;

    std::vector<DiffChunk> raw_chunks;

    while (i > 0 || j > 0)
    {
        if (i > 0 && j > 0 && old_lines[old_mid_start + i - 1] == new_lines[new_mid_start + j - 1])
        {
            i--;
            j--;
        }
        else if (j > 0 && (i == 0 || dp[i][j - 1] >= dp[i - 1][j]))
        {
            // Insertion
            DiffChunk chunk;
            chunk.start_line   = old_mid_start + i;  // Insert AT this index (before the line at i)
            chunk.delete_count = 0;
            chunk.insert_lines.push_back(new_lines[new_mid_start + j - 1]);
            raw_chunks.push_back(chunk);
            j--;
        }
        else
        {
            // Deletion
            DiffChunk chunk;
            chunk.start_line   = old_mid_start + i - 1;  // Delete the line at i-1
            chunk.delete_count = 1;
            raw_chunks.push_back(chunk);
            i--;
        }
    }

    // Merge contiguous chunks
    std::reverse(raw_chunks.begin(), raw_chunks.end());

    if (raw_chunks.empty())
        return diffs;

    DiffChunk current = raw_chunks[0];
    for (size_t k = 1; k < raw_chunks.size(); ++k)
    {
        const auto& next   = raw_chunks[k];
        bool        merged = false;

        // Case 1: Extend Deletion
        if (next.delete_count > 0 && next.insert_lines.empty())
        {
            if (next.start_line == current.start_line + current.delete_count)
            {
                current.delete_count += next.delete_count;
                merged = true;
            }
        }
        // Case 2: Extend Insertion
        else if (next.delete_count == 0 && !next.insert_lines.empty())
        {
            if (next.start_line == current.start_line && current.delete_count == 0)
            {
                current.insert_lines.insert(current.insert_lines.end(), next.insert_lines.begin(), next.insert_lines.end());
                merged = true;
            }
            // Handle Replace: Delete at X, Insert at X
            if (!merged && next.start_line == current.start_line && current.delete_count > 0)
            {
                current.insert_lines.insert(current.insert_lines.end(), next.insert_lines.begin(), next.insert_lines.end());
                merged = true;
            }
        }
        // Case 3: Merge Delete and Insert (Replace)
        else if (next.start_line == current.start_line)
        {
            current.delete_count += next.delete_count;
            current.insert_lines.insert(current.insert_lines.end(), next.insert_lines.begin(), next.insert_lines.end());
            merged = true;
        }

        if (!merged)
        {
            diffs.push_back(current);
            current = next;
        }
    }
    diffs.push_back(current);

    return diffs;
}

std::string ApplyDiff(const std::string& old_text, const std::vector<DiffChunk>& diffs)
{
    std::vector<std::string> lines = SplitLines(old_text);
    std::vector<std::string> result;

    int current_line = 0;
    int diff_idx     = 0;

    while (current_line < (int)lines.size() || diff_idx < (int)diffs.size())
    {
        if (diff_idx < (int)diffs.size() && current_line == diffs[diff_idx].start_line)
        {
            const auto& chunk = diffs[diff_idx];
            // Insert new lines
            for (const auto& line : chunk.insert_lines)
            {
                result.push_back(line);
            }
            // Skip deleted lines
            current_line += chunk.delete_count;
            diff_idx++;
        }
        else if (current_line < (int)lines.size())
        {
            result.push_back(lines[current_line]);
            current_line++;
        }
        else
        {
            // Remaining diffs are appends at the end
            if (diff_idx < (int)diffs.size())
            {
                diff_idx++;
            }
        }
    }

    // Join lines
    std::stringstream ss;
    for (size_t i = 0; i < result.size(); ++i)
    {
        ss << result[i];
        if (i + 1 < result.size())
            ss << "\n";
    }

    return ss.str();
}

namespace
{
    pugi::xpath_variable_set vars;
    struct Initializer
    {
        Initializer()
        {
            // Register namespace for XPath queries
            vars.add("xsd", pugi::xpath_type_string);
            vars.set("xsd", "http://www.w3.org/2001/XMLSchema");
        }
    };
    Initializer init;
}  // namespace

std::string QueryTypeByNameFromSchema(const pugi::xml_document& schema, const std::string& element_name)
{
    std::string      xpath1   = "//xsd:element[@name='" + element_name + "']";
    pugi::xpath_node element1 = schema.select_node(xpath1.c_str(), &vars);
    if (!element1)
        return "";
    pugi::xml_attribute type_attr1 = element1.node().attribute("type");
    if (!type_attr1)
        return "";

    return type_attr1.value();
}

std::string QueryNameByTypeFromSchema(const pugi::xml_document& schema, const std::string& element_type)
{
    std::string      xpath1   = "//xsd:element[@type='" + element_type + "']";
    pugi::xpath_node element1 = schema.select_node(xpath1.c_str(), &vars);
    if (!element1)
        return "";
    pugi::xml_attribute type_attr1 = element1.node().attribute("name");
    if (!type_attr1)
        return "";

    return type_attr1.value();
}

std::string QueryValueTypeFromSchema(const pugi::xml_document& schema,
                                     const std::string&        element_name,
                                     const std::string&        attribute_name,
                                     std::vector<std::string>* enums)
{
    std::string type_attr1 = QueryTypeByNameFromSchema(schema, element_name);
    if (type_attr1.empty())
        return "";

    std::string      xpath2     = "//xsd:complexType[@name='" + type_attr1 + "']/xsd:attribute[@name='" + attribute_name + "']";
    pugi::xpath_node attr_node2 = schema.select_node(xpath2.c_str(), &vars);
    if (!attr_node2)
        return "";
    pugi::xml_attribute type_attr2 = attr_node2.node().attribute("type");
    if (!type_attr2)
        return "";

    std::string type_name = type_attr2.value();

    // Helper lambda to extract enums from a simpleType node
    auto extract_enums_from_node = [&](pugi::xml_node type_node)
    {
        std::string xpath_enum = ".//xsd:enumeration";
        auto        enum_nodes = type_node.select_nodes(xpath_enum.c_str(), &vars);
        for (auto& node : enum_nodes)
        {
            // Ignore deprecated enum
            auto appinfo = node.node().select_node("xsd:annotation/xsd:appinfo", &vars);
            if (!appinfo || !appinfo.node().first_child() || strcmp(appinfo.node().first_child().value(), "deprecated") != 0)
            {
                enums->push_back(node.node().attribute("value").value());
            }
        }
    };

    // Find the type definition
    std::string      xpath_type = "//xsd:simpleType[@name='" + type_name + "']";
    pugi::xpath_node type_node  = schema.select_node(xpath_type.c_str(), &vars);

    if (type_node)
    {
        // Check for direct restriction
        if (type_node.node().child("xsd:restriction"))
        {
            extract_enums_from_node(type_node.node());
        }
        // Check for union
        else if (auto union_node = type_node.node().child("xsd:union"))
        {
            // 1. Handle memberTypes attribute
            if (auto member_types_attr = union_node.attribute("memberTypes"))
            {
                std::string              member_types_str = member_types_attr.value();
                std::vector<std::string> member_types;
                std::stringstream        ss(member_types_str);
                std::string              item;
                while (std::getline(ss, item, ' '))
                {
                    if (!item.empty())
                    {
                        // Find the member type definition
                        // Remove namespace prefix if present (e.g., "xsd:string" -> "string", but we need full name lookup)
                        // Actually, schema lookups usually need the name as defined.
                        // If it's a built-in xsd type, we skip enum extraction.
                        if (item.find("xsd:") == 0)
                            continue;

                        std::string      xpath_member = "//xsd:simpleType[@name='" + item + "']";
                        pugi::xpath_node member_node  = schema.select_node(xpath_member.c_str(), &vars);
                        if (member_node)
                        {
                            extract_enums_from_node(member_node.node());
                        }
                    }
                }
            }

            // 2. Handle inline simpleTypes inside union
            for (auto child : union_node.children("xsd:simpleType"))
            {
                extract_enums_from_node(child);
            }
        }
    }

    return type_name;
}

std::string GetDefaultValueForAttribute(const pugi::xml_document& schema, const std::string& element_name, const std::string& attribute_name)
{
    std::vector<std::string> enums;
    std::string              type = QueryValueTypeFromSchema(schema, element_name, attribute_name, &enums);

    if (!enums.empty())
    {
        return enums[0];
    }

    if (type == "Boolean")
    {
        return "false";
    }
    else if (type == "Double" || type == "Float")
    {
        return "0.0";
    }
    else if (type == "Int" || type == "Integer" || type == "UnsignedInt" || type == "UnsignedShort" || type == "UnsignedLong")
    {
        return "0";
    }
    else if (type == "DateTime")
    {
        // Return current time in ISO 8601 format: YYYY-MM-DDThh:mm:ss
        std::time_t t = std::time(nullptr);
        std::tm     now;
#ifdef _WIN32
        localtime_s(&now, &t);
#else
        localtime_r(&t, &now);
#endif
        char buffer[128];
        std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", &now);
        return std::string(buffer);
    }

    return "";
}

std::vector<std::string> QueryAttrsFromSchema(const pugi::xml_document& schema, const std::string& node_name)
{
    std::vector<std::string> ret;
    std::string              type_name = QueryTypeByNameFromSchema(schema, node_name);
    if (!type_name.empty())
    {
        std::string xpath = "//xsd:complexType[@name='" + type_name + "']/xsd:attribute";
        auto        nodes = schema.select_nodes(xpath.c_str(), &vars);
        if (!nodes.empty())
        {
            for (auto& node : nodes)
            {
                ret.push_back(node.node().attribute("name").value());
            }
        }
    }

    return ret;
}

std::vector<std::string> QueryOptionalAttrsFromSchema(const pugi::xml_document& schema, const std::string& node_name)
{
    std::vector<std::string> ret;
    std::string              type_name = QueryTypeByNameFromSchema(schema, node_name);
    if (!type_name.empty())
    {
        std::string xpath = "//xsd:complexType[@name='" + type_name + "']/xsd:attribute";
        auto        nodes = schema.select_nodes(xpath.c_str(), &vars);
        if (!nodes.empty())
        {
            for (auto& node : nodes)
            {
                auto attrName = node.node().attribute("name");
                if (!attrName)
                    continue;
                pugi::xml_attribute useAttr = node.node().attribute("use");
                if (useAttr && strcmp(useAttr.value(), "required") == 0)
                    continue;
                ret.push_back(attrName.value());
            }
        }
    }
    return ret;
}

void HandleXsdNode(const pugi::xml_document&    schema,
                   pugi::xml_node               node,
                   std::vector<pugi::xml_node>* path,
                   std::vector<SubelementInfo>* element_infos,
                   std::string                  current_choice_id = "",
                   std::string                  current_branch_id = "",
                   int                          choice_min        = 0,
                   int                          choice_max        = 0)
{
    path->push_back(node);
    std::string name     = node.name();
    std::string ref_name = node.attribute("ref").value();
    if (name == "xsd:element")
    {
        int  minOccurs, maxOccurs;
        bool maxUnbounded;
        ParseOccurs(node, &minOccurs, &maxOccurs, &maxUnbounded);
        auto element_name = node.attribute("name").value();
        bool has_choice   = std::any_of(path->begin(), path->end(), [&](pugi::xml_node node) { return strcmp(node.name(), "xsd:choice") == 0; });
        element_infos->emplace_back(element_name,
                                    has_choice,
                                    /*isRequired=*/false,
                                    minOccurs,
                                    maxOccurs,
                                    maxUnbounded,
                                    *path,
                                    current_choice_id,
                                    current_branch_id,
                                    choice_min,
                                    choice_max);
    }
    else if (name == "xsd:group" && !ref_name.empty())
    {
        std::string xpath = "//xsd:group[@name='" + ref_name + "']";
        auto        node2 = schema.select_node(xpath.c_str(), &vars);
        HandleXsdNode(schema, node2.node(), path, element_infos, current_choice_id, current_branch_id, choice_min, choice_max);
    }
    else if (name == "xsd:complexType" || name == "xsd:sequence" || name == "xsd:all" || name == "xsd:group")
    {
        for (auto child = node.first_child(); child; child = child.next_sibling())
        {
            HandleXsdNode(schema, child, path, element_infos, current_choice_id, current_branch_id, choice_min, choice_max);
        }
    }
    else if (name == "xsd:choice")
    {
        // Generate a unique ID for this choice node
        std::string new_choice_id = std::to_string((uintptr_t)node.internal_object());

        int  c_min, c_max;
        bool c_unbounded;
        ParseOccurs(node, &c_min, &c_max, &c_unbounded);
        if (c_unbounded)
            c_max = -1;

        for (auto child = node.first_child(); child; child = child.next_sibling())
        {
            // For each direct child of the choice, generate a unique branch ID
            // This branch ID will be passed down to all elements within this branch
            std::string branch_id = std::to_string((uintptr_t)child.internal_object());
            HandleXsdNode(schema, child, path, element_infos, new_choice_id, branch_id, c_min, c_max);
        }
    }
    path->pop_back();
}

void HandleComplexType(const pugi::xml_document& schema, const std::string& name, std::vector<SubelementInfo>* element_infos)
{
    std::vector<pugi::xml_node> path;
    std::string                 xpath = "//xsd:complexType[@name='" + name + "']";
    auto                        node  = schema.select_node(xpath.c_str(), &vars);
    HandleXsdNode(schema, node.node(), &path, element_infos);
}

std::vector<SubelementInfo> QuerySubelementsWithModelInfo(const pugi::xml_document& schema, const std::string& node_name)
{
    std::vector<SubelementInfo> ret;
    std::string                 type_name = QueryTypeByNameFromSchema(schema, node_name);

    HandleComplexType(schema, type_name, &ret);
    return ret;
}

std::vector<std::string> QueryRequiredAttrsFromSchema(const pugi::xml_document& schema, const std::string& type_name)
{
    std::vector<std::string> ret;
    if (!type_name.empty())
    {
        std::string xpath = "//xsd:complexType[@name='" + type_name + "']/xsd:attribute[@use='required']";
        auto        nodes = schema.select_nodes(xpath.c_str(), &vars);
        if (!nodes.empty())
        {
            for (auto& node : nodes)
            {
                ret.push_back(node.node().attribute("name").value());
            }
        }
    }
    return ret;
}

void ExcludeExistingAttrs(pugi::xml_node node, std::vector<std::string>* attrs_to_create, std::vector<std::string>* attrs_to_delete)
{
    std::vector<std::string> ret;
    std::set<std::string>    attr_set;
    attrs_to_delete->clear();
    for (auto& attr : node.attributes())
    {
        attr_set.insert(attr.name());
        attrs_to_delete->push_back(attr.name());
    }
    for (auto& attr : *attrs_to_create)
    {
        if (attr_set.count(attr) == 0)
            ret.push_back(attr);
    }
    *attrs_to_create = ret;
}

std::string BuildXoscQueryPath(const pugi::xml_document& schema, const std::vector<pugi::xml_node>& xsd_path)
{
    std::string path = ".";
    for (auto& node : xsd_path)
    {
        std::string node_name = node.name();
        if (node_name == "xsd:element")
        {
            path += "/";
            path += node.attribute("name").value();
        }
        else if (node_name == "xsd::complexType")
        {
            path += "/";
            path += node.attribute("name").value();
        }
    }
    return path;
}

void ExcludeExistingElements(const pugi::xml_document& schema, pugi::xml_node node, std::vector<SubelementInfo>* elements_to_create)
{
    auto it = std::remove_if(elements_to_create->begin(),
                             elements_to_create->end(),
                             [&schema, &node](const SubelementInfo& info)
                             {
                                 auto path  = BuildXoscQueryPath(schema, info.xsd_path);
                                 auto nodes = node.select_nodes(path.c_str(), &vars);
                                 auto count = nodes.size();
                                 if (!info.is_max_unbounded && info.max_occurs >= 0 && count >= info.max_occurs)
                                     return true;
                                 return false;
                             });

    elements_to_create->erase(it, elements_to_create->end());
}

void FindElementsToRemove(const pugi::xml_document&    schema,
                          pugi::xml_node               node,
                          std::vector<SubelementInfo>* schema_elements,
                          const std::string&           new_node_name)
{
    auto iter = std::find_if(schema_elements->begin(),
                             schema_elements->end(),
                             [&schema, &node, &new_node_name](const SubelementInfo& info)
                             {
                                 if (info.xsd_path.back().attribute("name").value() == new_node_name)
                                     return true;
                                 return false;
                             });

    auto new_node_info = *iter;

    auto it = std::remove_if(schema_elements->begin(),
                             schema_elements->end(),
                             [&schema, &node](const SubelementInfo& info)
                             {
                                 auto path  = BuildXoscQueryPath(schema, info.xsd_path);
                                 auto nodes = node.select_nodes(path.c_str(), &vars);
                                 if (nodes.empty())
                                     return true;
                                 return false;
                             });

    schema_elements->erase(it, schema_elements->end());
    auto it2 =
        std::remove_if(schema_elements->begin(),
                       schema_elements->end(),
                       [&new_node_info](const SubelementInfo& info)
                       {
                           for (size_t i = 0; i + 1 < info.xsd_path.size() && i + 1 < new_node_info.xsd_path.size(); ++i)
                           {
                               if (info.xsd_path[i] == new_node_info.xsd_path[i])
                               {
                                   if (info.xsd_path[i].name() == std::string("xsd:choice") && info.xsd_path[i + 1] != new_node_info.xsd_path[i + 1])
                                   {
                                       return false;
                                   }
                               }
                               else
                               {
                                   return true;
                               }
                           }
                           return true;
                       });
    schema_elements->erase(it2, schema_elements->end());
}

void RemoveElements(const pugi::xml_document& schema, pugi::xml_node node, const std::vector<SubelementInfo>& elements_to_remove)
{
    for (auto& info : elements_to_remove)
    {
        auto path  = BuildXoscQueryPath(schema, info.xsd_path);
        auto nodes = node.select_nodes(path.c_str(), &vars);
        for (auto& child : nodes)
        {
            child.parent().remove_child(child.node());
        }
    }
}

std::vector<pugi::xml_node> FindElements(const pugi::xml_document& schema, pugi::xml_node node, const std::vector<pugi::xml_node>& xsd_path)
{
    std::vector<pugi::xml_node> ret;
    auto                        path  = BuildXoscQueryPath(schema, xsd_path);
    auto                        nodes = node.select_nodes(path.c_str(), &vars);
    for (auto& node2 : nodes)
        ret.push_back(node2.node());
    return ret;
}

int CountSiblingByName(pugi::xml_node node, const std::string& name)
{
    int count = 0;
    for (auto child = node.child(name.c_str()); child; child = child.next_sibling(name.c_str()))
        ++count;
    return count;
}

bool IsElementDeprecated(pugi::xml_node node)
{
    // Check direct element definition
    std::string xpath_element = "xsd:annotation/xsd:appinfo[text()='deprecated']";
    auto        element_node  = node.select_node(xpath_element.c_str(), &vars);
    if (element_node)
        return true;

    // If not a direct element definition, check if deprecated through type definition
    // std::string type_name = query_type_by_name_from_schema(schema, element_name);
    // if (!type_name.empty())
    //{
    //    std::string xpath_type = "//xsd:complexType[@name='" + type_name + "']/xsd:annotation/xsd:appinfo[text()='deprecated']";
    //    auto        type_node  = schema.select_node(xpath_type.c_str(), &vars);
    //    if (type_node)
    //        return true;
    //}

    return false;
}

bool IsAttributeDeprecated(const pugi::xml_document& schema, const std::string& element_name, const std::string& attribute_name)
{
    std::string type_name = QueryTypeByNameFromSchema(schema, element_name);
    if (!type_name.empty())
    {
        std::string xpath = "//xsd:complexType[@name='" + type_name + "']/xsd:attribute[@name='" + attribute_name +
                            "']/xsd:annotation/xsd:appinfo[text()='deprecated']";
        auto attr_node = schema.select_node(xpath.c_str(), &vars);
        return static_cast<bool>(attr_node);
    }
    return false;
}

bool IsEnumDeprecated(const pugi::xml_document& schema, const std::string& type_name, const std::string& enum_value)
{
    if (type_name.empty() || enum_value.empty())
        return false;

    // Helper lambda to check for deprecated enum in a type node
    auto check_enum_node = [&](pugi::xml_node type_node) -> bool
    {
        std::string xpath_enum = ".//xsd:enumeration[@value='" + enum_value + "']";
        auto        enum_node  = type_node.select_node(xpath_enum.c_str(), &vars);
        if (enum_node)
        {
            auto appinfo = enum_node.node().select_node("xsd:annotation/xsd:appinfo", &vars);
            if (appinfo && appinfo.node().first_child() && strcmp(appinfo.node().first_child().value(), "deprecated") == 0)
            {
                return true;
            }
        }
        return false;
    };

    // Find the type definition
    std::string      xpath_type = "//xsd:simpleType[@name='" + type_name + "']";
    pugi::xpath_node type_node  = schema.select_node(xpath_type.c_str(), &vars);

    if (type_node)
    {
        // Check for direct restriction
        if (type_node.node().child("xsd:restriction"))
        {
            if (check_enum_node(type_node.node()))
                return true;
        }
        // Check for union
        else if (auto union_node = type_node.node().child("xsd:union"))
        {
            if (auto member_types_attr = union_node.attribute("memberTypes"))
            {
                std::string       member_types_str = member_types_attr.value();
                std::stringstream ss(member_types_str);
                std::string       item;
                while (std::getline(ss, item, ' '))
                {
                    if (!item.empty())
                    {
                        if (item.find("xsd:") == 0)
                            continue;

                        std::string      xpath_member = "//xsd:simpleType[@name='" + item + "']";
                        pugi::xpath_node member_node  = schema.select_node(xpath_member.c_str(), &vars);
                        if (member_node)
                        {
                            if (check_enum_node(member_node.node()))
                                return true;
                        }
                    }
                }
            }

            // Check inline simpleTypes in union
            for (pugi::xml_node child : union_node.children("xsd:simpleType"))
            {
                if (check_enum_node(child))
                    return true;
            }
        }
    }

    return false;
}

void FilterDeprecatedAttributes(std::vector<std::string>* attributes, const pugi::xml_document& schema, const std::string& element_name)
{
    auto it = std::remove_if(attributes->begin(),
                             attributes->end(),
                             [&schema, &element_name](const std::string& attr) { return IsAttributeDeprecated(schema, element_name, attr); });
    attributes->erase(it, attributes->end());
}

void FilterDeprecatedSubelements(std::vector<SubelementInfo>* elements)
{
    auto it = std::remove_if(elements->begin(),
                             elements->end(),
                             [](const SubelementInfo& element) { return IsElementDeprecated(element.xsd_path.back()); });
    elements->erase(it, elements->end());
}

void FindPositionNodesRecursive(pugi::xml_node node, std::vector<pugi::xml_node>& position_nodes)
{
    std::string node_name = node.name();

    // Check if this node is a position node
    if (node_name == "WorldPosition" || node_name == "LanePosition" || node_name == "RelativeWorldPosition" || node_name == "RelativeLanePosition" ||
        node_name == "RoadPosition")
    {
        position_nodes.push_back(node);
    }

    // Recursively search children
    for (pugi::xml_node child : node.children())
    {
        FindPositionNodesRecursive(child, position_nodes);
    }
}

pugi::xml_node FindPositionInNode(pugi::xml_node node, const PositionInfo& position_info)
{
    std::string node_name = node.name();

    // Check if this node is a position node
    if (node_name == "WorldPosition" || node_name == "LanePosition" || node_name == "RelativeWorldPosition" || node_name == "RelativeLanePosition" ||
        node_name == "RoadPosition")
    {
        return node;
    }

    // Recursively search children
    for (pugi::xml_node child : node.children())
    {
        pugi::xml_node result = FindPositionInNode(child, position_info);
        if (!result.empty())
        {
            return result;
        }
    }

    return pugi::xml_node();
}

pugi::xml_node FindScenarioObjectByName(pugi::xml_node root, const std::string& object_name)
{
    // Search for scenario object with the given name
    for (pugi::xml_node scenario_object : root.children("ScenarioObject"))
    {
        pugi::xml_attribute name_attr = scenario_object.attribute("name");
        if (!name_attr.empty() && name_attr.value() == object_name)
        {
            return scenario_object;
        }

        // Also check entryName if name doesn't match
        pugi::xml_attribute entry_name_attr = scenario_object.attribute("entryName");
        if (!entry_name_attr.empty() && entry_name_attr.value() == object_name)
        {
            return scenario_object;
        }
    }

    return pugi::xml_node();
}

bool DoesPositionNodeMatch(const PositionInfo& position_info, pugi::xml_node position_node)
{
    std::string position_type = position_node.name();

    // Basic matching based on position type
    if (position_type == "LanePosition")
    {
        // Check if road ID and lane ID match
        pugi::xml_attribute road_attr = position_node.attribute("roadId");
        pugi::xml_attribute lane_attr = position_node.attribute("laneId");

        if (!road_attr.empty() && !lane_attr.empty())
        {
            int node_road_id = road_attr.as_int();
            int node_lane_id = lane_attr.as_int();

            return (node_road_id == position_info.road_id && node_lane_id == (int)position_info.lane_id);
        }
    }
    else if (position_type == "WorldPosition")
    {
        // Check if world coordinates are close
        pugi::xml_attribute x_attr = position_node.attribute("x");
        pugi::xml_attribute y_attr = position_node.attribute("y");
        pugi::xml_attribute z_attr = position_node.attribute("z");

        if (!x_attr.empty() && !y_attr.empty() && !z_attr.empty())
        {
            double node_x = x_attr.as_double();
            double node_y = y_attr.as_double();
            double node_z = z_attr.as_double();

            double tolerance = 1.0;  // 1 meter tolerance
            return (std::abs(node_x - position_info.x) < tolerance && std::abs(node_y - position_info.y) < tolerance &&
                    std::abs(node_z - position_info.z) < tolerance);
        }
    }

    // Default fallback - return true for any position node if we can't determine mismatch
    return true;
}

void ParseOccurs(pugi::xml_node element_node, int* min_occurs, int* max_occurs, bool* max_unbounded)
{
    *min_occurs    = 1;
    *max_occurs    = 1;
    *max_unbounded = false;

    if (auto min_attr = element_node.attribute("minOccurs"))
    {
        *min_occurs = min_attr.as_int();
    }

    if (auto max_attr = element_node.attribute("maxOccurs"))
    {
        std::string max_val = max_attr.value();
        if (max_val == "unbounded")
        {
            *max_occurs    = -1;
            *max_unbounded = true;
        }
        else
        {
            *max_occurs = max_attr.as_int();
        }
    }
}

void GetNodesBelow(pugi::xml_node node, std::vector<pugi::xml_node>* nodes, int depth /* = 999*/)
{
    if (depth <= 0)
        return;
    nodes->push_back(node);
    for (pugi::xml_node child = node.first_child(); child; child = child.next_sibling())
        GetNodesBelow(child, nodes, depth - 1);
}

void GetNodesAbove(pugi::xml_node node, std::vector<pugi::xml_node>* nodes, int depth /* = 999*/)
{
    for (; depth > 0 && node; node = node.parent(), --depth)
        nodes->push_back(node);
}

void TraverseNodesBelow(pugi::xml_node                      node,
                        std::function<void(pugi::xml_node)> node_push,
                        std::function<void()>               node_pop,
                        std::function<void(int id)>         child_push,
                        std::function<void()>               child_pop)
{
    node_push(node);
    int i = 0;
    for (pugi::xml_node child = node.first_child(); child; child = child.next_sibling(), ++i)
    {
        child_push(i);
        TraverseNodesBelow(child, node_push, node_pop, child_push, child_pop);
        child_pop();
    }
    node_pop();
}

std::string PositionTypeToString(PositionType type)
{
    switch (type)
    {
        case PositionType::WORLD_POSITION:
            return "WorldPosition";
        case PositionType::RELATIVE_WORLD_POSITION:
            return "RelativeWorldPosition";
        case PositionType::RELATIVE_OBJECT_POSITION:
            return "RelativeObjectPosition";
        case PositionType::ROAD_POSITION:
            return "RoadPosition";
        case PositionType::RELATIVE_ROAD_POSITION:
            return "RelativeRoadPosition";
        case PositionType::LANE_POSITION:
            return "LanePosition";
        case PositionType::RELATIVE_LANE_POSITION:
            return "RelativeLanePosition";
        case PositionType::ROUTE_POSITION:
            return "RoutePosition";
        case PositionType::GEO_POSITION:
            return "GeoPosition";
        case PositionType::TRAJECTORY_POSITION:
            return "TrajectoryPosition";
        default:
            return "Unknown";
    }
}
