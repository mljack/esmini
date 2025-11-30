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

#include <osgViewer/ViewerEventHandlers>

#include <string>

#include "TopViewManipulator.hpp"
#include "RoadManager.hpp"
#include "CommonMini.hpp"
#include "roadgeom.hpp"
#include "viewer.hpp"
#include "Entities.hpp"

class StudioGui;

#include <string>
#include <vector>

using namespace scenarioengine;

class StudioDataModel;

namespace viewer
{
    class StudioViewer : public viewer::Viewer
    {
    public:
        StudioViewer(roadmanager::OpenDrive* odrManager,
                     const char*             modelFilename,
                     const char*             scenarioFilename,
                     const char*             exe_path,
                     osg::ArgumentParser     arguments,
                     SE_Options*             opt = 0);

        void              SetWindowTitle(const std::string& title);
        void              Cleanup();
        const osg::Vec2d& GetMousePosition() const;
        bool              MoveCameraToMapCenter();
        bool              MoveCamera(double x, double y);
        bool              MoveCameraWithoutOriginOffset(double x, double y);
        void              MoveMouse(double x, double y);
        ViewSetting       GetViewSetting();
        void              SetViewSetting(const ViewSetting& settings);
        void              BackupViewSetting();
        void              RestoreViewSetting();
        void              SetCameraDistance(double d);
        void              SetViewportSize(double width, double height);
        void              SetViewportCenterPixelOffset(double x, double y);
        StudioDataModel*  GetDataModel()
        {
            return data_model_;
        }

        class StudioGui* GetStudioGui()
        {
            return studio_gui_;
        }

    private:
        osg::ref_ptr<osgGA::TopViewManipulator> topViewManipulator_;
        StudioDataModel*                        data_model_;
        class StudioGui*                        studio_gui_;
    };

    class StudioViewerEventHandler : public osgGA::GUIEventHandler
    {
    public:
        StudioViewerEventHandler(Viewer* viewer) : viewer_(viewer)
        {
        }

        using osgGA::GUIEventHandler::handle;
        bool handle(const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter&) override;

    private:
        Viewer* viewer_;
    };
}  // namespace viewer
