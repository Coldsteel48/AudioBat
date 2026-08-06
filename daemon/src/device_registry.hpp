// RamkolFX
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of RamkolFX, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <spa/utils/hook.h>

#include "ramkolfx/protocol.hpp"
#include "ramkolfx/types.hpp"

struct pw_loop;
struct pw_context;
struct pw_core;
struct pw_registry;
struct pw_metadata;
struct spa_dict;

namespace ramkolfx
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
// system volume mixer was observed to fix. Whatever default was
// configured beforehand is captured at the same time (see
// HandleMetadataProperty) so RestorePreviousDefaultSink() can hand it
// back to the system once the daemon shuts down.
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

    // Blocks (pumping Loop directly) until a pw_core_sync roundtrip
    // confirms every registry event pending at the time of the call has
    // been dispatched - in particular, the 'global' events for sinks that
    // already existed when Start() connected, which otherwise only arrive
    // once something else drives Loop (normally pw_main_loop_run(), not
    // yet running at the call site this exists for). Only safe to call
    // before the loop is being driven by another thread. Gives up and
    // returns false after TimeoutMs if no sync response arrives.
    bool WaitForInitialSync(int TimeoutMs = 1000);

    // Safe to call from any thread.
    std::vector<AudioDeviceInfo> GetDevices() const;

    // True if a currently-known real hardware sink's node.name matches
    // Name. Safe to call from any thread.
    bool HasDevice(const std::string& Name) const;

    // Returns the node.name of an arbitrary currently-known real hardware
    // sink (the one with the lowest PipeWire global id, so the result is
    // deterministic for a given graph rather than depending on
    // unordered_map iteration order), or nullopt if none are known yet.
    // Safe to call from any thread.
    std::optional<std::string> PickAnyDevice() const;

    // pw_registry callback trampoline targets; not for external use.
    void HandleGlobalAdded(uint32 Id, const char* Type, const spa_dict* Props);
    void HandleGlobalRemoved(uint32 Id);

    // pw_metadata callback trampoline target; not for external use.
    void HandleMetadataProperty(uint32 Subject, const char* Key, const char* Type, const char* Value);

    // Sets default.configured.audio.sink back to whatever it was before
    // ClaimVirtualSinkAsDefault() first overwrote it (captured via
    // HandleMetadataProperty's initial property dump - see
    // PreviousDefaultSinkValue). No-op if that value was never captured
    // (e.g. the "default" metadata object never appeared). Must run while
    // Core/DefaultMetadata are still alive, so callers need to invoke this
    // before tearing this object down - AudioEngine::Teardown() does so
    // explicitly, the same way it stops ControlServer before resetting it.
    void RestorePreviousDefaultSink();

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
    spa_hook MetadataListener{};

    // The pre-existing default.configured.audio.sink value, captured from
    // the first property event the "default" metadata object sends after
    // ClaimVirtualSinkAsDefault()'s listener is added - which is the
    // server's dump of current state, arriving before our own overwrite
    // takes effect (bind and the following set_property are processed in
    // order on the same connection). Empty/unset if never captured.
    bool bCapturedPreviousDefaultSink = false;
    std::string PreviousDefaultSinkValue;

    mutable std::mutex DevicesMutex;
    std::unordered_map<uint32, AudioDeviceInfo> DevicesById;
};

} // namespace ramkolfx
