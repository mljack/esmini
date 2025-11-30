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

#include "OsgUtil.hpp"
#include <osg/Config>
#include <osg/Node>
#include <osg/Geode>

struct PositionInfo;

osg::ref_ptr<osg::Node>  CreateBoxGeometry(double width, double length, double height, const osg::Vec4& color);
osg::ref_ptr<osg::Node>  CreateGreenSphereGeometry(double radius, int latitudeBands, int longitudeBands);
osg::ref_ptr<osg::Node>  CreateBlueBoxGeometry(double width, double height, double depth);
osg::ref_ptr<osg::Node>  CreateRedCylinderGeometry(double radius, double height, int segments);
osg::ref_ptr<osg::Node>  CreateYellowConeGeometry(double radius, double height, int segments);
osg::ref_ptr<osg::Geode> CreateTextLabelGeode(const PositionInfo& pos);
osg::ref_ptr<osg::Geode> CreateLineStripGeode(const std::vector<const PositionInfo*>& positions);
