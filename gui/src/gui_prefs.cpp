// RamkolFX
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of RamkolFX, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#include "gui_prefs.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <unordered_map>

#include <sys/stat.h>

namespace ramkolfx::gui
{

namespace
{

// $XDG_CONFIG_HOME/ramkolfx, falling back to ~/.config/ramkolfx - same
// directory the daemon's SettingsStore uses, but a distinct filename so
// this file's writes never race with or clobber the daemon's own.
std::string GuiPrefsPath()
{
    std::string ConfigDir;
    if (const char* XdgConfigHome = std::getenv("XDG_CONFIG_HOME"); XdgConfigHome && *XdgConfigHome)
    {
        ConfigDir = XdgConfigHome;
    }
    else if (const char* Home = std::getenv("HOME"); Home && *Home)
    {
        ConfigDir = std::string(Home) + "/.config";
    }
    else
    {
        ConfigDir = "/tmp";
    }
    ConfigDir += "/ramkolfx";
    mkdir(ConfigDir.c_str(), 0700);
    return ConfigDir + "/gui_prefs.conf";
}

} // namespace

GuiPreferences LoadGuiPreferences()
{
    GuiPreferences Result;

    std::ifstream File(GuiPrefsPath());
    if (!File.is_open())
    {
        return Result;
    }

    std::unordered_map<std::string, std::string> Fields;
    std::string Line;
    while (std::getline(File, Line))
    {
        const size_t Eq = Line.find('=');
        if (Eq == std::string::npos)
        {
            continue;
        }
        Fields.emplace(Line.substr(0, Eq), Line.substr(Eq + 1));
    }

    if (const auto It = Fields.find("mirror_left_right"); It != Fields.end())
    {
        Result.bMirrorModeEnabled = It->second == "1";
    }
    if (const auto It = Fields.find("advanced_eq_mode"); It != Fields.end())
    {
        Result.bAdvancedEqMode = It->second == "1";
    }
    if (const auto It = Fields.find("advanced_bass_mode"); It != Fields.end())
    {
        Result.bAdvancedBassMode = It->second == "1";
    }
    return Result;
}

void SaveGuiPreferences(const GuiPreferences& InPreferences)
{
    const std::string Path = GuiPrefsPath();

    // Write-to-temp-then-rename, same as SettingsStore::Save: rename() is
    // atomic within a directory, so a crash mid-write can't corrupt the
    // prefs file.
    const std::string TempPath = Path + ".tmp";
    std::ofstream File(TempPath, std::ios::trunc);
    if (!File.is_open())
    {
        fprintf(stderr, "[ramkolfx-gui] failed to write preferences to %s\n", TempPath.c_str());
        return;
    }
    File << "mirror_left_right=" << (InPreferences.bMirrorModeEnabled ? 1 : 0) << "\n";
    File << "advanced_eq_mode=" << (InPreferences.bAdvancedEqMode ? 1 : 0) << "\n";
    File << "advanced_bass_mode=" << (InPreferences.bAdvancedBassMode ? 1 : 0) << "\n";
    File.close();

    if (std::rename(TempPath.c_str(), Path.c_str()) != 0)
    {
        fprintf(stderr, "[ramkolfx-gui] failed to save preferences to %s\n", Path.c_str());
    }
}

} // namespace ramkolfx::gui
