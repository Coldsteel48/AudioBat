// RamkolFX
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of RamkolFX, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#include "settings_store.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <unordered_map>

#include <sys/stat.h>

namespace ramkolfx
{

namespace
{

// $XDG_CONFIG_HOME/ramkolfx, falling back to ~/.config/ramkolfx per the
// XDG base directory spec - unlike the control socket (XDG_RUNTIME_DIR,
// typically tmpfs), settings need to survive a reboot.
std::string DefaultSettingsPath()
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
        ConfigDir = "/tmp"; // last resort; matches DefaultControlSocketPath's own /tmp fallback
    }
    ConfigDir += "/ramkolfx";
    mkdir(ConfigDir.c_str(), 0700);
    return ConfigDir + "/settings.conf";
}

std::string SpeakerKey(size_t Index, const char* Field)
{
    return "speaker." + std::to_string(Index) + "." + Field;
}

std::string HwEqBandKey(size_t Index, const char* Field)
{
    return "hweq." + std::to_string(Index) + "." + Field;
}

} // namespace

SettingsStore::SettingsStore() : SettingsPath(DefaultSettingsPath())
{
}

std::optional<PersistedSettings> SettingsStore::Load() const
{
    std::ifstream File(SettingsPath);
    if (!File.is_open())
    {
        return std::nullopt;
    }

    // Simple `key=value` lines, one per field - deliberately not JSON: the
    // daemon has no JSON dependency, and a settings file this small (a
    // couple dozen scalar fields) doesn't need one.
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

    auto GetString = [&](const std::string& Key, const std::string& Default) -> std::string
    {
        const auto It = Fields.find(Key);
        return It != Fields.end() ? It->second : Default;
    };
    auto GetLong = [&](const std::string& Key, long Default) -> long
    {
        const auto It = Fields.find(Key);
        if (It == Fields.end())
        {
            return Default;
        }
        try
        {
            return std::stol(It->second);
        }
        catch (...)
        {
            return Default;
        }
    };
    auto GetFloat = [&](const std::string& Key, float Default) -> float
    {
        const auto It = Fields.find(Key);
        if (It == Fields.end())
        {
            return Default;
        }
        try
        {
            return std::stof(It->second);
        }
        catch (...)
        {
            return Default;
        }
    };
    auto GetBool = [&](const std::string& Key, bool Default) -> bool
    {
        const auto It = Fields.find(Key);
        return It != Fields.end() ? It->second == "1" : Default;
    };

    PersistedSettings Result;
    Result.Mode = static_cast<SpatialMode>(GetLong("mode", static_cast<long>(Result.Mode)));
    Result.bNearFieldEnabled = GetBool("near_field", Result.bNearFieldEnabled);
    Result.OutputDeviceName = GetString("output_device", Result.OutputDeviceName);
    Result.ActiveHrtfDisplayName = GetString("hrtf_display_name", Result.ActiveHrtfDisplayName);
    for (size_t i = 0; i < SpeakerCount; ++i)
    {
        Result.SpeakerAzimuthDegrees[i] = GetFloat(SpeakerKey(i, "azimuth"), Result.SpeakerAzimuthDegrees[i]);
        Result.SpeakerDistanceMeters[i] = GetFloat(SpeakerKey(i, "distance"), Result.SpeakerDistanceMeters[i]);
        Result.SpeakerMuted[i] = GetBool(SpeakerKey(i, "muted"), Result.SpeakerMuted[i]);
    }
    for (size_t i = 0; i < MaxEqBands; ++i)
    {
        EqBand& Band = Result.HwEqBands[i];
        Band.FilterType = static_cast<EqFilterType>(
            GetLong(HwEqBandKey(i, "type"), static_cast<long>(Band.FilterType)));
        Band.FrequencyHz = GetFloat(HwEqBandKey(i, "freq"), Band.FrequencyHz);
        Band.GainDb = GetFloat(HwEqBandKey(i, "gain"), Band.GainDb);
        Band.Q = GetFloat(HwEqBandKey(i, "q"), Band.Q);
    }
    Result.BassEnhancer.bEnabled = GetBool("bass_enhancer.enabled", Result.BassEnhancer.bEnabled);
    Result.BassEnhancer.CutoffHz = GetFloat("bass_enhancer.cutoff_hz", Result.BassEnhancer.CutoffHz);
    Result.BassEnhancer.Drive = GetFloat("bass_enhancer.drive", Result.BassEnhancer.Drive);
    Result.BassEnhancer.Mix = GetFloat("bass_enhancer.mix", Result.BassEnhancer.Mix);
    return Result;
}

void SettingsStore::Save(const PersistedSettings& InSettings) const
{
    std::ostringstream Out;
    Out << "mode=" << static_cast<int>(InSettings.Mode) << "\n";
    Out << "near_field=" << (InSettings.bNearFieldEnabled ? 1 : 0) << "\n";
    Out << "output_device=" << InSettings.OutputDeviceName << "\n";
    Out << "hrtf_display_name=" << InSettings.ActiveHrtfDisplayName << "\n";
    for (size_t i = 0; i < SpeakerCount; ++i)
    {
        Out << SpeakerKey(i, "azimuth") << "=" << InSettings.SpeakerAzimuthDegrees[i] << "\n";
        Out << SpeakerKey(i, "distance") << "=" << InSettings.SpeakerDistanceMeters[i] << "\n";
        Out << SpeakerKey(i, "muted") << "=" << (InSettings.SpeakerMuted[i] ? 1 : 0) << "\n";
    }
    for (size_t i = 0; i < MaxEqBands; ++i)
    {
        const EqBand& Band = InSettings.HwEqBands[i];
        Out << HwEqBandKey(i, "type") << "=" << static_cast<int>(Band.FilterType) << "\n";
        Out << HwEqBandKey(i, "freq") << "=" << Band.FrequencyHz << "\n";
        Out << HwEqBandKey(i, "gain") << "=" << Band.GainDb << "\n";
        Out << HwEqBandKey(i, "q") << "=" << Band.Q << "\n";
    }
    Out << "bass_enhancer.enabled=" << (InSettings.BassEnhancer.bEnabled ? 1 : 0) << "\n";
    Out << "bass_enhancer.cutoff_hz=" << InSettings.BassEnhancer.CutoffHz << "\n";
    Out << "bass_enhancer.drive=" << InSettings.BassEnhancer.Drive << "\n";
    Out << "bass_enhancer.mix=" << InSettings.BassEnhancer.Mix << "\n";

    // Write-to-temp-then-rename: rename() is atomic within the same
    // directory, so a crash or power loss mid-write leaves either the old
    // settings file or the new one intact, never a half-written one.
    const std::string TempPath = SettingsPath + ".tmp";
    std::ofstream File(TempPath, std::ios::trunc);
    if (!File.is_open())
    {
        fprintf(stderr, "[ramkolfxd] failed to write settings to %s\n", TempPath.c_str());
        return;
    }
    File << Out.str();
    File.close();

    if (std::rename(TempPath.c_str(), SettingsPath.c_str()) != 0)
    {
        fprintf(stderr, "[ramkolfxd] failed to save settings to %s\n", SettingsPath.c_str());
    }
}

} // namespace ramkolfx
