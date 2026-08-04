// AudioBat
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of AudioBat, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#pragma once

#include <array>
#include <optional>
#include <string>

#include "audiobat/protocol.hpp"

namespace audiobat
{

// Snapshot of daemon state worth remembering across restarts: spatial
// mode, per-speaker layout (azimuth/distance/mute), the near-field
// toggle, the pinned output device, and the active HRTF source.
// Deliberately excludes test-noise: that's a one-off calibration aid, not
// something anyone wants blasting on every daemon start.
struct PersistedSettings
{
    SpatialMode Mode = SpatialMode::Off;
    std::array<float, SpeakerCount> SpeakerAzimuthDegrees{};
    std::array<float, SpeakerCount> SpeakerDistanceMeters{};
    std::array<bool, SpeakerCount> SpeakerMuted{};
    bool bNearFieldEnabled = false;
    std::string OutputDeviceName;

    // Matched against RuntimeHrtfCatalog entries by display name at load
    // time (see AudioEngine::Run), not by index - the catalog's contents
    // and order can shift between builds (e.g. a SADIE subject file going
    // missing), and a stale index would silently resolve to the wrong
    // entry instead of just failing to match.
    std::string ActiveHrtfDisplayName;
};

// Loads/saves PersistedSettings as a small text file under
// $XDG_CONFIG_HOME/audiobat (falling back to ~/.config/audiobat), so user
// choices made through the control protocol survive a daemon restart.
// Not realtime-safe - only ever called from AudioEngine::Run() (startup
// load) and HandleControlCommand() (save after a mutating command, on a
// control-client worker thread), never the audio thread.
class SettingsStore
{
public:
    SettingsStore();

    // Returns nullopt if no settings file exists yet, or it couldn't be
    // opened - callers should fall back to built-in defaults in that
    // case. Fields missing from an otherwise-readable file (e.g. an older
    // version's file, or partial corruption) fall back individually to
    // PersistedSettings' own defaults rather than failing the whole load.
    std::optional<PersistedSettings> Load() const;

    // Writes InSettings to disk, replacing whatever was there. Writes to
    // a temp file and renames over the target so a crash or power loss
    // mid-write can't corrupt the settings file. Best-effort: logs to
    // stderr and returns on failure rather than throwing.
    void Save(const PersistedSettings& InSettings) const;

private:
    std::string SettingsPath;
};

} // namespace audiobat
