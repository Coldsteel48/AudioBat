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
struct spa_dict;

namespace audiobat
{

// Maintains a live list of real hardware playback sinks (PipeWire nodes
// with media.class "Audio/Sink") by listening for registry global-add/
// remove events on its own context/core connection - separate from the
// VirtualSink/HardwareOutput streams so device discovery keeps working
// independently of whether those streams are currently connected.
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
    pw_loop* Loop;
    pw_context* Context = nullptr;
    pw_core* Core = nullptr;
    pw_registry* Registry = nullptr;
    spa_hook RegistryListener{};

    mutable std::mutex DevicesMutex;
    std::unordered_map<uint32_t, AudioDeviceInfo> DevicesById;
};

} // namespace audiobat
