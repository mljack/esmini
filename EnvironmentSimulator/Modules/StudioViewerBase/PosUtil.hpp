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

#include "XmlUtil.hpp"

void ConvertWorldPosToLanePos(double  x,
                              double  y,
                              double  z,
                              double  h,
                              bool    align_to_lane,
                              int*    road_id,
                              int*    lane_id,
                              double* s,
                              double* offset,
                              double* rel_h);
void ConvertLanePosToWorldPos(int road_id, int lane_id, double s, double offset, double rel_h, double* x, double* y, double* z, double* h);
void ConvertRelLanePosToWorldPos(const PositionInfo& ref, PositionInfo* pos);
void ConvertWorldPosToRelLanePos(const PositionInfo& ref, PositionInfo* pos);
void ConvertRoadPosToWorldPos(int road_id, double s, double t, PositionInfo* pos_info);

void UpdateXmlPositionNode(pugi::xml_node position_node, const PositionInfo& position_info);