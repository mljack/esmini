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

#include "StudioViewer.hpp"

#include <osg/Group>
#include <osgGA/KeySwitchMatrixManipulator>

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "StudioGui.hpp"

#define PERSP_FOV 30.0
#define ORTHO_FOV 1.0

osg::ref_ptr<osg::Group> g_tmp_lines = nullptr;

class ImGuiInitOperation : public osg::Operation
{
public:
    ImGuiInitOperation() : osg::Operation("ImGuiInitOperation", false)
    {
    }

    void operator()(osg::Object* object) override
    {
        osg::GraphicsContext* context = dynamic_cast<osg::GraphicsContext*>(object);
        if (!context)
            return;

        static bool once = false;
        if (once)
            ImGui_ImplOpenGL3_Shutdown();
        once = true;

        if (!ImGui_ImplOpenGL3_Init())
        {
            printf("Failed in ImGui_ImplOpenGL3_Init()\n");
        }
    }
};

class ImGuiDestroyOperation : public osg::Operation
{
public:
    ImGuiDestroyOperation() : osg::Operation("ImGuiDestroyOperation", false)
    {
    }

    void operator()(osg::Object* object) override
    {
        // ImGui_ImplOpenGL3_Shutdown();
    }
};

using namespace viewer;

StudioViewer::StudioViewer(roadmanager::OpenDrive* odrManager,
                           const char*             modelFilename,
                           const char*             scenarioFilename,
                           const char*             exe_path,
                           osg::ArgumentParser     arguments,
                           SE_Options*             opt)
    : Viewer(odrManager, modelFilename, scenarioFilename, exe_path, arguments, opt, false, false)
{
    data_model_ = nullptr;
    osgViewer_->setRealizeOperation(new ImGuiInitOperation);
    osgViewer_->setCleanUpOperation(new ImGuiDestroyOperation);
    auto* gui   = new StudioGui(this);
    data_model_ = gui->GetModel();
    osgViewer_->addEventHandler(gui);
    studio_gui_ = gui;

    osgViewer_->addEventHandler(new StudioViewerEventHandler(this));

    topViewManipulator_                                                  = new osgGA::TopViewManipulator(origin_);
    osg::ref_ptr<osgGA::KeySwitchMatrixManipulator> keyswitchManipulator = new osgGA::KeySwitchMatrixManipulator;
    keyswitchManipulator->addMatrixManipulator('\0', "topview", topViewManipulator_.get());
    osgViewer_->setCameraManipulator(keyswitchManipulator.get());
    camMode_ = osgGA::TopViewManipulator::RB_MODE_TOP;
    topViewManipulator_->setMode(static_cast<unsigned int>(osgGA::TopViewManipulator::RB_MODE_TOP));

    auto fov = ORTHO_FOV;

    osg::ref_ptr<osg::GraphicsContext>         gc = osgViewer_->getCamera()->getGraphicsContext();
    osg::ref_ptr<osg::GraphicsContext::Traits> traits =
        const_cast<osg::GraphicsContext::Traits*>(osgViewer_->getCamera()->getGraphicsContext()->getTraits());

    osgViewer_->getCamera()->setProjectionMatrixAsPerspective(fov,
                                                              static_cast<double>(traits->width) / traits->height,
                                                              1.0 * PERSP_FOV / fov,
                                                              1E5 * PERSP_FOV / fov);

    osgViewer_->getCamera()->setLODScale(static_cast<float>(fov / PERSP_FOV));

    osg::BoundingSphere bounding_sphere;
    if (environment_ != nullptr)
    {
        bounding_sphere            = environment_->getBound();
        bounding_sphere._center[2] = 0.0;
        bounding_sphere.radius() *= 1.1;
    }
    else
    {
        // If no environment model, set a default bounding sphere
        bounding_sphere.set(osg::Vec3(0.0, 0.0, 0.0), 1000.0);
    }
    topViewManipulator_->setBound(bounding_sphere);
}

void StudioViewer::Cleanup()
{
    while (entities_.size() > 0)
    {
        RemoveCar((int)(entities_.size() - 1));
    }
    polyLine_.clear();
    routewaypoints_->removeChildren(0, routewaypoints_->getNumChildren());
    trajectoryLines_->removeChildren(0, trajectoryLines_->getNumChildren());
}

const osg::Vec2d& StudioViewer::GetMousePosition() const
{
    return topViewManipulator_->getMousePos();
}

bool StudioViewer::MoveCameraToMapCenter()
{
    if (environment_ != nullptr)
    {
        auto c = environment_->getBound().center();
        return MoveCamera(c[0], c[1]);
    }
    else
    {
        // If no environment model, try using the default origin
        return MoveCamera(0.0, 0.0);
    }
}

bool StudioViewer::MoveCamera(double x, double y)
{
    if (topViewManipulator_)
    {
        x += origin_[0];
        y += origin_[1];
        return topViewManipulator_->moveCameraTo(x, y);
    }
    else
    {
        LOG("Warning: topViewManipulator_ is null, cannot move camera");
        return false;
    }
}

bool StudioViewer::MoveCameraWithoutOriginOffset(double x, double y)
{
    if (topViewManipulator_)
    {
        return topViewManipulator_->moveCameraTo(x, y);
    }
    else
    {
        LOG("Warning: topViewManipulator_ is null, cannot move camera");
        return false;
    }
}

void StudioViewer::MoveMouse(double x, double y)
{
    if (topViewManipulator_ && osgViewer_.valid())
    {
        topViewManipulator_->MoveMouse(*osgViewer_, x, y);
    }
}

ViewSetting StudioViewer::GetViewSetting()
{
    return topViewManipulator_->getViewSetting();
}

void StudioViewer::SetViewSetting(const ViewSetting& setting)
{
    topViewManipulator_->setViewSetting(setting);
}

void StudioViewer::BackupViewSetting()
{
    topViewManipulator_->backupViewSetting();
}

void StudioViewer::RestoreViewSetting()
{
    topViewManipulator_->restoreViewSetting();
}

void StudioViewer::SetCameraDistance(double d)
{
    topViewManipulator_->setCameraDistance(d);
}

void StudioViewer::SetWindowTitle(const std::string& title)
{
    Viewer::SetWindowTitle(title.c_str());
    if (gw_)
        gw_->setWindowName(title.c_str());
}

bool StudioViewerEventHandler::handle(const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter&)
{
    if (ea.getKey() != osgGA::GUIEventAdapter::KEY_Escape && ea.getHandled())
        return false;

    switch (ea.getEventType())
    {
        case (osgGA::GUIEventAdapter::NONE):
            break;
        case (osgGA::GUIEventAdapter::PUSH):
            break;
        case (osgGA::GUIEventAdapter::RELEASE):
            break;
        case (osgGA::GUIEventAdapter::DOUBLECLICK):
            break;
        case (osgGA::GUIEventAdapter::DRAG):
            break;
        case (osgGA::GUIEventAdapter::MOVE):
            break;
        case (osgGA::GUIEventAdapter::KEYDOWN):
            break;
        case (osgGA::GUIEventAdapter::KEYUP):
            break;
        case (osgGA::GUIEventAdapter::FRAME):
            break;
        case (osgGA::GUIEventAdapter::SCROLL):
            break;
        case (osgGA::GUIEventAdapter::PEN_PRESSURE):
            break;
        case (osgGA::GUIEventAdapter::PEN_ORIENTATION):
            break;
        case (osgGA::GUIEventAdapter::PEN_PROXIMITY_ENTER):
            break;
        case (osgGA::GUIEventAdapter::PEN_PROXIMITY_LEAVE):
            break;
        case (osgGA::GUIEventAdapter::USER):
            break;
        case (osgGA::GUIEventAdapter::RESIZE):
            viewer_->infoTextCamera->setProjectionMatrix(osg::Matrix::ortho2D(0, ea.getWindowWidth(), 0, ea.getWindowHeight()));
            break;
        case (osgGA::GUIEventAdapter::CLOSE_WINDOW):
            viewer_->renderSemaphore.Release();  // no more rendering will happen
            viewer_->SetQuitRequest(true);
            break;
        case (osgGA::GUIEventAdapter::QUIT_APPLICATION):
            viewer_->SetQuitRequest(true);
            break;
    }

    // Send key event to registered callback subscribers
    if (ea.getKey() > 0)
    {
        for (size_t i = 0; i < viewer_->callback_.size(); i++)
        {
            KeyEvent ke = {ea.getKey(), ea.getModKeyMask(), ea.getEventType() & osgGA::GUIEventAdapter::KEYDOWN ? true : false};
            viewer_->callback_[i].func(&ke, viewer_->callback_[i].data);
        }
    }

    if (ea.getKey() == osgGA::GUIEventAdapter::KEY_Space)  // prevent OSG "view reset" action on space key
    {
        return false;
    }
    else
    {
        // forward all other key events to OSG
        return false;
    }
}

void StudioViewer::SetViewportSize(double width, double height)
{
    topViewManipulator_->setViewportSize(width, height);
}

void StudioViewer::SetViewportCenterPixelOffset(double x, double y)
{
    topViewManipulator_->setViewportCenterPixelOffset(x, y);
}
