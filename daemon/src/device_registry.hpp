// AudioBat
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of AudioBat, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#pragma once

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <spa/utils/hook.h>

#include "audiobat/protocol.hpp"

struct pw_loop;
struct pw_context;
struct pw_core;
struct pw_registry;
struct pw_metadata;
struct spa_dict;

namespace audiobat
{

// Maintains a live list of real hardware playback sinks (PipeWire nodes
// with media.class "Audio/Sink") by listening for registry global-add/
// remove events on its own context/core connection - separate from the
// VirtualSink/HardwareOutput streams so device discovery keeps working
// independently of whether those streams are currently connected.
//
// Also claims the PipeWire virtual sink as the system default output as
// soon as the session's "default" metadata object appears in the
// registry (see HandleGlobalAdded/ClaimVirtualSinkAsDefault): this isn't
// just convenience routing - re-asserting default.configured.audio.sink
// nudges WirePlumber's default-nodes policy to reconcile any client
// stream that got created before it had resolved a target and was
// otherwise sitting unlinked (silently producing no sound) until
// something touched that metadata, which is what manually opening a
// system volume mixer was observed to fix.
//
// Registry events land on the PipeWire main-loop thread (the same thread
// AudioEngine::Run() drives); GetDevices() is called from ControlServer's
// per-client threads, so the device map is behind a mutex.
class DeviceRegistry
{
public:
    explicit DeviceRegistry(pw_loop* InLoop);
    ~DeviceRegistry();

    DeviceRegistry(const DeviceRegistry&) = delete;
    DeviceRegistry& operator=(const DeviceRegistry&) = delete;

    // Connects to PipeWire and starts listening for registry events.
    // Returns false on failure.
    bool Start();

    // Safe to call from any thread.
    std::vector<AudioDeviceInfo> GetDevices() const;

    // pw_registry callback trampoline targets; not for external use.
    void HandleGlobalAdded(uint32_t Id, const char* Type, const spa_dict* Props);
    void HandleGlobalRemoved(uint32_t Id);

private:
    // Sets default.configured.audio.sink to VirtualSink::NodeName via the
    // bound "default" metadata object. Called once, as soon as that
    // object is bound (see HandleGlobalAdded) - re-sent every time the
    // daemon (re)starts and this registry connection reconnects, since a
    // fresh connection re-discovers the metadata global from scratch.
    void ClaimVirtualSinkAsDefault();

    pw_loop* Loop;
    pw_context* Context = nullptr;
    pw_core* Core = nullptr;
    pw_registry* Registry = nullptr;
    spa_hook RegistryListener{};
    pw_metadata* DefaultMetadata = nullptr;

    mutable std::mutex DevicesMutex;
    std::unordered_map<uint32_t, AudioDeviceInfo> DevicesById;
};

} // namespace audiobat
