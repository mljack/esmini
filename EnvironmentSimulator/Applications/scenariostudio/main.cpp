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

#define _USE_MATH_DEFINES
#include <math.h>
#include <signal.h>

#include "playerbase.hpp"
#include "RoadManager.hpp"
#include "CommonMini.hpp"
#include "Replayer.hpp"
#include "StudioDataModel.hpp"
#include "StudioViewer.hpp"
#include "StudioGui.hpp"

#include <osgUtil/SmoothingVisitor>
#include <filesystem>
#include <chrono>

std::string                           g_xodr_path;
std::string                           g_xosc_path;  // = "scenario.xosc";
std::string                           g_seed                = "0";
std::string                           g_timestep            = "0.1";
std::vector<std::string>              g_paths               = {"."};
int                                   g_path_count_from_cmd = 0;
static std::string                    g_model_filename;
static SE_Options*                    g_opt_ptr = nullptr;
static std::vector<std::string>       g_args;
static unsigned int                   fixed_timestep = 50;
StudioDataModel*                      g_data_model   = nullptr;
std::unique_ptr<viewer::StudioViewer> g_viewer;

const float DEFAULT_TIMELINE_RANGE = 20.0f;

static bool g_quit  = false;
static bool g_pause = false;

// Log callback function to receive log messages and forward to StudioGui
static void log_callback(const char* message)
{
    if (g_viewer && g_viewer->GetStudioGui())
    {
        g_viewer->GetStudioGui()->AddLogMessage(message);
    }
}

template <typename ContainerT, typename PredicateT>
void erase_if(ContainerT& items, const PredicateT& predicate)
{
    for (auto it = items.begin(); it != items.end();)
        if (predicate(*it))
            it = items.erase(it);
        else
            ++it;
}

static void SwitchFromViewerToInspector()
{
    g_data_model->prev_mode_ = g_data_model->mode_;
    g_data_model->mode_      = StudioMode::INSPECTOR;
    g_viewer->osgViewer_->setDone(true);
}

void FetchKeyEvent(viewer::KeyEvent* keyEvent, void*)
{
    if (!g_data_model)
        return;
    if (g_data_model->mode_ == StudioMode::VIEWER)
    {
        if (g_data_model->virtual_time_ > 0.1f && !keyEvent->down_ && keyEvent->key_ == static_cast<int>(KeyType::KEY_Space))
        {
            g_data_model->virtual_time_ -= 0.05;
            SwitchFromViewerToInspector();
        }
    }
    else if (g_data_model->mode_ == StudioMode::INSPECTOR)
    {
        Replay* replay = replayer::GetReplay();
        if (replay)
            replayer::ReportKeyEvent(keyEvent, replay, &g_data_model->virtual_time_);
    }
}

int process_args(int argc, char** argv)
{
    SE_Options& opt = SE_Env::Inst().GetOptions();
    g_opt_ptr       = &opt;
    opt.Reset();

    SE_Env::Inst().AddPath(DirNameOf(argv[0]));  // Add location of exe file to search paths
    SE_Env::Inst().AddPath("../../../../resources");
    SE_Env::Inst().AddPath(".");

    g_args.clear();
    for (int i = 0; i < argc; i++)
        g_args.push_back(argv[i]);

    // use an ArgumentParser object to manage the program arguments.
    opt.AddOption("help", "Show all available command line arguments");
    opt.AddOption("odr", "OpenDRIVE filename", "odr_filename");
    opt.AddOption("osc", "OpenSCENARIO filename", "osc_filename");
    opt.AddOption("model", "3D Model filename", "model_filename");
    opt.AddOption("path", "Search path prefix for assets, e.g. car and sign model files", "path");

    if (opt.ParseArgs(argc, argv) != 0)
    {
        opt.PrintUsage();
        return -1;
    }

    if (opt.GetOptionSet("help"))
    {
        opt.PrintUsage();
        viewer::Viewer::PrintUsage();
        return -1;
    }

    std::string arg_str;

    if (opt.GetOptionSet("disable_stdout"))
        Logger::Inst().SetCallback(0);

    if (opt.GetOptionSet("disable_log"))
    {
        SE_Env::Inst().SetLogFilePath("");
    }
    else if (opt.IsOptionArgumentSet("logfile_path"))
    {
        arg_str = opt.GetOptionArg("logfile_path");
        SE_Env::Inst().SetLogFilePath(arg_str);
        if (arg_str.empty())
            printf("Custom logfile path empty, disable logfile\n");
        else
            printf("Custom logfile path: %s\n", arg_str.c_str());
    }
    Logger::Inst().OpenLogfile(SE_Env::Inst().GetLogFilePath());
    // Register log callback to forward messages to StudioGui Log window
    Logger::Inst().SetCallback(log_callback);
    // Logger::Inst().LogVersion();

    g_xodr_path = opt.GetOptionArg("odr");
    g_xosc_path = opt.GetOptionArg("osc");
    std::string path;
    for (int i = 0; !(path = opt.GetOptionArg("path", i)).empty(); ++i)
    {
        g_paths.push_back(path);
        SE_Env::Inst().AddPath(path);
        LOG("Added path %s", path.c_str());
        g_path_count_from_cmd = i + 1;
    }

    g_model_filename = opt.GetOptionArg("model");

    if (opt.GetOptionSet("use_signs_in_external_model"))
        LOG("Use sign models in external scene graph model, skip creating sign models");

    if (opt.HasUnknownArgs())
    {
        opt.PrintUnknownArgs("Unrecognized arguments:");
        opt.PrintUsage();
    }

    return 0;
}

// Early config loading to get window size before viewer creation
static int g_window_width  = StudioDataModel::DEFAULT_VIEWPORT_WIDTH;
static int g_window_height = StudioDataModel::DEFAULT_VIEWPORT_HEIGHT;

void add_default_command_args(std::vector<const char*>* args)
{
    // Convert window dimensions to strings (static to keep them in memory)
    static std::string width_str  = std::to_string(g_window_width);
    static std::string height_str = std::to_string(g_window_height);

#ifdef __linux__
    static std::vector<const char*> additional_args = {"--window", "0", "0", width_str.c_str(), height_str.c_str(), "--SingleThreaded"};
#else
    static std::vector<const char*> additional_args =
        {"--window", "0", "30", width_str.c_str(), height_str.c_str(), "--SingleThreaded", "--path", "../../../../resources"};
#endif
    args->insert(args->end(), additional_args.begin(), additional_args.end());
}

int main(int argc, char** argv)
{
    // Load window size from config before creating window
    LoadConfigValue("viewport_app_width", g_window_width);
    LoadConfigValue("viewport_app_height", g_window_height);

    // osg::ArgumentParser modifies given args array so we copy it for safe modification
    std::vector<const char*> args = {argv, std::next(argv, argc)};
    add_default_command_args(&args);
    int                 arg_count = static_cast<int>(args.size());
    osg::ArgumentParser arguments(&arg_count, (char**)args.data());
    if (process_args(arg_count, (char**)args.data()) < 0)
        return -1;

    auto t0 = std::chrono::system_clock::now();

    if (g_xodr_path.empty())
    {
        LOG("Try to load the default OpenDRIVE file...");
        g_xodr_path = "../resources/xodr/e6mini.xodr";
        if (!std::filesystem::exists(g_xodr_path))
            g_xodr_path = "resources/xodr/e6mini.xodr";
        if (!roadmanager::Position::LoadOpenDrive(g_xodr_path.c_str()))
        {
            LOG("Failed to load OpenDRIVE file [%s]\n", g_xodr_path.c_str());
            g_xodr_path.clear();
        }
    }
    else if (!roadmanager::Position::LoadOpenDrive(g_xodr_path.c_str()))
    {
        LOG("Failed to load OpenDRIVE file [%s]\n", g_xodr_path.c_str());
        g_xodr_path.clear();
    }

    if (g_xodr_path.empty())
    {
        exit(-1);
    }
    {
        double duration = std::chrono::duration<double>(std::chrono::system_clock::now() - t0).count();
        LOG("Finished loading OpenDRIVE file [%s] in %.1f seconds.\n", g_xodr_path.c_str(), duration);
    }

    roadmanager::OpenDrive* odrManager = roadmanager::Position::GetOpenDrive();

    // osgViewer_->setThreadingModel(osgViewer::ViewerBase::ThreadingModel::SingleThreaded);

    g_viewer =
        std::make_unique<viewer::StudioViewer>(odrManager, g_model_filename.c_str(), /* scenarioFilename= */ nullptr, argv[0], arguments, g_opt_ptr);
    g_viewer->Realize();
    g_data_model = g_viewer->GetDataModel();
    args.resize(arg_count);

    // Use top view and pause the simulaiton
    g_viewer->SetCameraMode(osgGA::RubberbandManipulator::RB_MODE_TOP);
    // g_pause = true;
    g_viewer->SetNodeMaskBits(viewer::NodeMask::NODE_MASK_INFO_PER_OBJ);

    g_viewer->SetWindowTitle("Scenario Studio");
    g_viewer->SetCameraDistance(viewer::StudioViewer::DEFAULT_ZOOM_DIST);

    std::unique_ptr<ScenarioPlayer> player;

    auto prev_mode                        = g_data_model->mode_;
    g_data_model->virtual_time_max_value_ = DEFAULT_TIMELINE_RANGE;
    while (g_data_model->mode_ != StudioMode::ZOMBIE)
    {
        g_viewer->Cleanup();
        auto old_mode = g_data_model->mode_;
        if (g_data_model->mode_ == StudioMode::COMPOSER)
        {
            g_viewer->osgViewer_->setDone(false);
            g_viewer->SetInfoText("");

            // Disable default Escape key event handler, take over control
            g_viewer->osgViewer_->setKeyEventSetsDone(0);

            g_data_model->virtual_time_ = 0.0f;

            if (g_data_model->prev_mode_ != StudioMode::COMPOSER)
                g_viewer->SetWindowTitle("Scenario Studio (Composer)");

            while (!g_viewer->GetQuitRequest() && !g_viewer->osgViewer_->done() && !g_quit)
            {
                if (!g_pause)
                {
                    SE_sleep(fixed_timestep);
                }

                // Step the simulation
                g_viewer->osgViewer_->frame();
            }
        }
        else if (g_data_model->mode_ == StudioMode::VIEWER)
        {
            bool is_player_valid = true;
            // preview mode with esmini scenario engine
            if (g_data_model->prev_mode_ != StudioMode::INSPECTOR)
            {
                g_data_model->virtual_time_max_value_ = DEFAULT_TIMELINE_RANGE;
                std::vector<const char*> args2        = {"viewer",
                                                         "--fixed_timestep",
                                                         g_timestep.c_str(),
                                                         "--seed",
                                                         g_seed.c_str(),
                                                         "--osc",
                                                         g_data_model->tmp_xosc_path_.c_str(),
                                                         "--record",
                                                         g_data_model->tmp_rec_path_.c_str()};
                for (auto& path : g_paths)
                {
                    args2.push_back("--path");
                    args2.push_back(path.c_str());
                }
                add_default_command_args(&args2);

                int arg_count2 = static_cast<int>(args2.size());
                SE_Env::Inst().GetOptions().Reset();
                player = std::make_unique<ScenarioPlayer>(arg_count2, (char**)args2.data());

                player->RegisterExternalViewer(g_viewer.get());
                try
                {
                    if (player->Init() != 0)
                    {
                        printf("Failed to init ScenarioPlayer!\n");
                        is_player_valid = false;
                    }
                }
                catch (const std::runtime_error& e)
                {
                    LOG("ERROR: Failed to init ScenarioPlayer due to the following error:");
                    LOG("ERROR: \t%s", e.what());
                    is_player_valid = false;
                }
            }

            if (is_player_valid)
            {
                if (g_data_model->prev_mode_ != StudioMode::VIEWER)
                    g_viewer->SetWindowTitle("Scenario Studio (Viewer)");

                g_viewer->osgViewer_->setDone(false);
                g_data_model->virtual_time_manipulated_ = false;

                __int64 time_stamp = 0;
                int     retval     = 0;
                while (!g_viewer->osgViewer_->done() && !g_quit)
                {
                    double dt;
                    if (player->GetFixedTimestep() > SMALL_NUMBER)
                        dt = player->GetFixedTimestep();
                    else
                        dt = SE_getSimTimeStep(time_stamp, player->minStepSize, player->maxStepSize);

                    retval = player->Frame(dt);

                    g_data_model->virtual_time_ = player->scenarioEngine->getSimulationTime();
                    if (g_data_model->mode_ == StudioMode::COMPOSER)
                    {
                        g_data_model->markers_to_update_ = true;
                        break;
                    }

                    if (!player->IsPaused())
                    {
                        for (auto* entity : g_viewer->entities_)
                        {
                            if (entity->name_ == "ego" || entity->name_ == "Ego" || entity->name_ == "EGO")
                            {
                                auto& pos = entity->txNode_->getPosition();
                                g_viewer->MoveCamera(pos.x(), pos.y());
                                break;
                            }
                        }
                    }

                    if (retval)
                        SwitchFromViewerToInspector();

                    if (g_data_model->virtual_time_manipulated_)
                    {
                        g_data_model->virtual_time_manipulated_ = false;
                        SwitchFromViewerToInspector();
                        break;
                    }
                }
            }
            else
            {
                g_data_model->mode_              = StudioMode::COMPOSER;
                g_data_model->markers_to_update_ = true;
            }
        }
        else if (g_data_model->mode_ == StudioMode::INSPECTOR)
        {
            // playback mode with replayer
            std::vector<const char*> args3 = {"inspector", "--fixed_timestep", g_timestep.c_str(), "--file", g_data_model->tmp_rec_path_.c_str()};
            for (auto& path : g_paths)
            {
                args3.push_back("--path");
                args3.push_back(path.c_str());
            }
            add_default_command_args(&args3);

            int arg_count3 = static_cast<int>(args3.size());
            g_viewer->osgViewer_->setDone(false);
            g_viewer->SetWindowTitle("Scenario Studio (Inspector)");
            int ret3 = replayer::Run(arg_count3, (char**)args3.data(), g_viewer.get(), &g_data_model->virtual_time_, &g_quit);
            if (ret3 < 0)
            {
                g_data_model->prev_mode_ = g_data_model->mode_;
                g_data_model->mode_      = StudioMode::VIEWER;
                g_viewer->osgViewer_->setDone(true);
            }
        }

        if (g_data_model->mode_ == old_mode)
            g_data_model->mode_ = StudioMode::ZOMBIE;
        prev_mode = g_data_model->mode_;
    }
    g_viewer.reset();
    return 0;
}
