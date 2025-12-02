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

#include "PosUtil.hpp"
#include "CommonMini.hpp"
#include "RoadManager.hpp"

auto POS_QUERY_FLAG = roadmanager::Position::LookAheadMode::LOOKAHEADMODE_AT_CURRENT_LATERAL_OFFSET;

void ConvertWorldPosToLanePos(double  x,
                              double  y,
                              double  z,
                              double  h,
                              bool    align_to_lane,
                              int*    road_id,
                              int*    lane_id,
                              double* s,
                              double* offset,
                              double* rel_h)
{
    roadmanager::RoadProbeInfo probe_data;
    roadmanager::Position      pos_pivot = roadmanager::Position(x, y, z, h, 0.0, 0.0);
    if (align_to_lane)
        pos_pivot.SetHeadingRelative(0);
    pos_pivot.GetProbeInfo(0.0, &probe_data, POS_QUERY_FLAG);
    auto& info = probe_data.road_lane_info;
    *road_id   = info.roadId;
    *lane_id   = info.laneId;
    *s         = info.s;
    *offset    = info.laneOffset;
    *rel_h     = pos_pivot.GetHRelativeDrivingDirection();
}

void ConvertLanePosToWorldPos(int road_id, int lane_id, double s, double offset, double rel_h, double* x, double* y, double* z, double* h)
{
    roadmanager::Position pos = roadmanager::Position(road_id, lane_id, s, offset);

    roadmanager::OpenDrive* odr = roadmanager::Position::GetOpenDrive();
    if (odr)
    {
        roadmanager::Road* road = odr->GetRoadById(road_id);
        if (road)
        {
            if ((lane_id < 0 && road->GetRule() == roadmanager::Road::RoadRule::RIGHT_HAND_TRAFFIC) ||
                (lane_id > 0 && road->GetRule() == roadmanager::Road::RoadRule::LEFT_HAND_TRAFFIC))
            {
                pos.SetHeadingRelative(rel_h);
            }
            else
            {
                pos.SetHeadingRelative(rel_h + M_PI);
            }
        }
    }
    *x = pos.GetX();
    *y = pos.GetY();
    *z = pos.GetZ();
    *h = pos.GetH();
}

void ConvertRoadPosToWorldPos(int road_id, double s, double t, PositionInfo* pos_info)
{
    // Try to convert to world coordinates if road manager is available
    try
    {
        roadmanager::Position pos;
        if (pos.SetTrackPos(road_id, s, t) == roadmanager::Position::ReturnCode::OK)
        {
            pos_info->x = pos.GetX();
            pos_info->y = pos.GetY();
            pos_info->z = pos.GetZ();
            pos_info->h = pos.GetH();
        }
    }
    catch (...)
    {
        // Conversion failed, keep original world_coords
    }
}

void ConvertRelLanePosToWorldPos(const PositionInfo& ref, PositionInfo* pos)
{
    int    ref_road_id     = 0;
    int    ref_lane_id     = 0;
    double ref_s           = 0.0;
    double ref_lane_offset = 0.0;
    double ref_lane_h      = 0.0;

    // Get reference position in lane coordinates
    if (ref.type == PositionType::LANE_POSITION)
    {
        // Reference is already in lane coordinates
        ref_road_id     = ref.road_id;
        ref_lane_id     = ref.lane_id;
        ref_s           = ref.s;
        ref_lane_offset = ref.lane_offset;
    }
    else if (ref.type == PositionType::WORLD_POSITION)
    {
        ConvertWorldPosToLanePos(ref.x, ref.y, ref.z, 0.0, /*align_to_lane=*/true, &ref_road_id, &ref_lane_id, &ref_s, &ref_lane_offset, &ref_lane_h);
    }

    // Calculate target lane position: reference lane + dLane offset
    int    target_lane_id     = ref_lane_id;
    double target_s           = ref_s;
    double target_lane_offset = ref_lane_offset;

    target_lane_id += static_cast<int>(pos->dLane);
    target_s += pos->ds;
    target_lane_offset += pos->dlane_offset;

    ConvertLanePosToWorldPos(ref_road_id, target_lane_id, target_s, target_lane_offset, pos->h, &pos->x, &pos->y, &pos->z, &pos->absolute_h);
    if (!pos->orientation_is_relative)
        pos->absolute_h = pos->h;

    pos->road_id     = ref_road_id;
    pos->lane_id     = target_lane_id;
    pos->s           = target_s;
    pos->lane_offset = target_lane_offset;
}

void ConvertWorldPosToRelLanePos(const PositionInfo& ref, PositionInfo* pos)
{
    int    ref_road_id     = 0;
    int    ref_lane_id     = 0;
    double ref_s           = 0.0;
    double ref_lane_offset = 0.0;
    double ref_lane_h      = 0.0;

    // Get reference position in lane coordinates
    if (ref.type == PositionType::LANE_POSITION)
    {
        // Reference is already in lane coordinates
        ref_road_id     = ref.road_id;
        ref_lane_id     = ref.lane_id;
        ref_s           = ref.s;
        ref_lane_offset = ref.lane_offset;
    }
    else if (ref.type == PositionType::WORLD_POSITION)
    {
        ConvertWorldPosToLanePos(ref.x, ref.y, ref.z, 0.0, /*align_to_lane=*/true, &ref_road_id, &ref_lane_id, &ref_s, &ref_lane_offset, &ref_lane_h);
    }

    pos->dLane        = pos->lane_id - ref_lane_id;
    pos->ds           = pos->s - ref_s;
    pos->dlane_offset = pos->lane_offset - ref_lane_offset;
}

void UpdateXmlPositionNode(pugi::xml_node position_node, const PositionInfo& position_info)
{
    std::string position_type = position_node.name();

    // Update position attributes based on the position type
    if (position_type == "WorldPosition")
    {
        // Update world coordinates
        if (position_node.attribute("x"))
            position_node.attribute("x").set_value(std::to_string(position_info.x).c_str());
        else
            position_node.append_attribute("x") = std::to_string(position_info.x).c_str();

        if (position_node.attribute("y"))
            position_node.attribute("y").set_value(std::to_string(position_info.y).c_str());
        else
            position_node.append_attribute("y") = std::to_string(position_info.y).c_str();
        if (position_node.attribute("z"))
            position_node.attribute("z").set_value(std::to_string(position_info.z).c_str());
        else
            position_node.append_attribute("z") = std::to_string(position_info.z).c_str();

        // Update orientation if available
        if (position_node.attribute("h"))
            position_node.attribute("h") = std::to_string(position_info.h).c_str();
        else
            position_node.append_attribute("h") = std::to_string(position_info.h).c_str();

        if (position_node.attribute("p"))
            position_node.attribute("p") = std::to_string(position_info.p).c_str();
        else
            position_node.append_attribute("p") = std::to_string(position_info.p).c_str();

        if (position_node.attribute("r"))
            position_node.attribute("r") = std::to_string(position_info.r).c_str();
        else
            position_node.append_attribute("r") = std::to_string(position_info.r).c_str();
    }
    else if (position_type == "LanePosition")
    {
        // Update lane position coordinates
        if (position_node.attribute("roadId"))
            position_node.attribute("roadId").set_value(std::to_string(position_info.road_id).c_str());
        else
            position_node.append_attribute("roadId") = std::to_string(position_info.road_id).c_str();

        if (position_node.attribute("laneId"))
            position_node.attribute("laneId").set_value(std::to_string((int)position_info.lane_id).c_str());
        else
            position_node.append_attribute("laneId") = std::to_string((int)position_info.lane_id).c_str();

        if (position_node.attribute("s"))
            position_node.attribute("s").set_value(std::to_string(position_info.s).c_str());
        else
            position_node.append_attribute("s") = std::to_string(position_info.s).c_str();

        if (position_node.attribute("offset"))
            position_node.attribute("offset").set_value(std::to_string(position_info.lane_offset).c_str());
        else
            position_node.append_attribute("offset") = std::to_string(position_info.lane_offset).c_str();
    }
    else if (position_type == "RelativeWorldPosition")
    {
        // Update relative world position
        if (position_node.attribute("entityRef"))
            position_node.attribute("entityRef") = position_info.entity_ref.c_str();
        else
            position_node.append_attribute("entityRef") = position_info.entity_ref.c_str();

        if (position_node.attribute("dx"))
            position_node.attribute("dx") = std::to_string(position_info.dx).c_str();
        else
            position_node.append_attribute("dx") = std::to_string(position_info.dx).c_str();

        if (position_node.attribute("dy"))
            position_node.attribute("dy") = std::to_string(position_info.dy).c_str();
        else
            position_node.append_attribute("dy") = std::to_string(position_info.dy).c_str();

        if (position_node.attribute("dz"))
            position_node.attribute("dz") = std::to_string(position_info.dz).c_str();
        else
            position_node.append_attribute("dz") = std::to_string(position_info.dz).c_str();

        if (position_node.attribute("dh"))
            position_node.attribute("dh") = std::to_string(position_info.h).c_str();
        else
            position_node.append_attribute("dh") = std::to_string(position_info.h).c_str();
    }
    else if (position_type == "RelativeLanePosition")
    {
        // Update relative lane position
        if (position_node.attribute("entityRef"))
            position_node.attribute("entityRef") = position_info.entity_ref.c_str();
        else
            position_node.append_attribute("entityRef") = position_info.entity_ref.c_str();

        // Always update dLane (required attribute)
        if (position_node.attribute("dLane"))
            position_node.attribute("dLane") = std::to_string(static_cast<int>(position_info.dLane)).c_str();
        else
            position_node.append_attribute("dLane") = std::to_string(static_cast<int>(position_info.dLane)).c_str();

        // Always update ds (required attribute)
        if (position_node.attribute("ds"))
            position_node.attribute("ds") = std::to_string(position_info.ds).c_str();
        else
            position_node.append_attribute("ds") = std::to_string(position_info.ds).c_str();

        // Always update offset (optional but should be updated when moving)
        if (position_node.attribute("offset"))
            position_node.attribute("offset") = std::to_string(position_info.dlane_offset).c_str();
        else
            position_node.append_attribute("offset") = std::to_string(position_info.dlane_offset).c_str();

        // Update dsLane if present (optional attribute)
        if (position_info.ds != 0.0 && position_node.attribute("dsLane"))
            position_node.attribute("dsLane") = std::to_string(position_info.ds).c_str();
    }
    else if (position_type == "RoadPosition")
    {
        // Update road position
        if (position_node.attribute("roadId"))
            position_node.attribute("roadId").set_value(std::to_string(position_info.road_id).c_str());
        else
            position_node.append_attribute("roadId") = std::to_string(position_info.road_id).c_str();

        if (position_node.attribute("s"))
            position_node.attribute("s").set_value(std::to_string(position_info.s).c_str());
        else
            position_node.append_attribute("s") = std::to_string(position_info.s).c_str();

        if (position_node.attribute("t"))
            position_node.attribute("t").set_value(std::to_string(position_info.t).c_str());
        else
            position_node.append_attribute("t") = std::to_string(position_info.t).c_str();
    }
    else
    {
        // Unknown position type - just update basic world coordinates if available
        if (position_node.attribute("x"))
            position_node.attribute("x").set_value(std::to_string(position_info.x).c_str());
        else
            position_node.append_attribute("x") = std::to_string(position_info.x).c_str();

        if (position_node.attribute("y"))
            position_node.attribute("y").set_value(std::to_string(position_info.y).c_str());
        else
            position_node.append_attribute("y") = std::to_string(position_info.y).c_str();

        if (position_node.attribute("z"))
            position_node.attribute("z").set_value(std::to_string(position_info.z).c_str());
        else
            position_node.append_attribute("z") = std::to_string(position_info.z).c_str();
    }
}
