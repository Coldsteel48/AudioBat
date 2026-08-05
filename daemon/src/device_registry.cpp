// RamkolFX
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of RamkolFX, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#include "device_registry.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <limits>

#include <pipewire/extensions/metadata.h>
#include <pipewire/pipewire.h>
#include <spa/utils/dict.h>

#include "virtual_sink.hpp"

namespace ramkolfx
{

namespace
{

void OnGlobalAdded(void* UserData, uint32 Id, uint32 Permissions, const char* Type,
                    uint32 Version, const spa_dict* Props)
{
    (void)Permissions;
    (void)Version;
    static_cast<DeviceRegistry*>(UserData)->HandleGlobalAdded(Id, Type, Props);
}

void OnGlobalRemoved(void* UserData, uint32 Id)
{
    static_cast<DeviceRegistry*>(UserData)->HandleGlobalRemoved(Id);
}

const struct pw_registry_events RegistryEvents = []
{
    struct pw_registry_events Events{};
    Events.version = PW_VERSION_REGISTRY_EVENTS;
    Events.global = OnGlobalAdded;
    Events.global_remove = OnGlobalRemoved;
    return Events;
}();

// WaitForInitialSync's roundtrip state: a pointer to this struct is the
// pw_core_events 'done' listener's user data.
struct CoreSyncState
{
    int PendingSeq = -1;
    bool bDone = false;
};

void OnCoreDone(void* UserData, uint32 Id, int Seq)
{
    auto* State = static_cast<CoreSyncState*>(UserData);
    if (Id == PW_ID_CORE && Seq == State->PendingSeq)
    {
        State->bDone = true;
    }
}

const struct pw_core_events CoreEvents = []
{
    struct pw_core_events Events{};
    Events.version = PW_VERSION_CORE_EVENTS;
    Events.done = OnCoreDone;
    return Events;
}();

} // namespace

DeviceRegistry::DeviceRegistry(pw_loop* InLoop) : Loop(InLoop)
{
}

DeviceRegistry::~DeviceRegistry()
{
    if (DefaultMetadata)
    {
        pw_proxy_destroy(reinterpret_cast<pw_proxy*>(DefaultMetadata));
    }
    if (Registry)
    {
        spa_hook_remove(&RegistryListener);
        pw_proxy_destroy(reinterpret_cast<pw_proxy*>(Registry));
    }
    if (Core)
    {
        pw_core_disconnect(Core);
    }
    if (Context)
    {
        pw_context_destroy(Context);
    }
}

bool DeviceRegistry::Start()
{
    Context = pw_context_new(Loop, nullptr, 0);
    if (!Context)
    {
        fprintf(stderr, "[ramkolfxd] failed to create device registry context\n");
        return false;
    }

    Core = pw_context_connect(Context, nullptr, 0);
    if (!Core)
    {
        fprintf(stderr, "[ramkolfxd] failed to connect device registry core\n");
        return false;
    }

    Registry = pw_core_get_registry(Core, PW_VERSION_REGISTRY, 0);
    if (!Registry)
    {
        fprintf(stderr, "[ramkolfxd] failed to get PipeWire registry\n");
        return false;
    }

    pw_registry_add_listener(Registry, &RegistryListener, &RegistryEvents, this);
    return true;
}

bool DeviceRegistry::WaitForInitialSync(int TimeoutMs)
{
    CoreSyncState State;
    spa_hook CoreListener{};
    pw_core_add_listener(Core, &CoreListener, &CoreEvents, &State);
    State.PendingSeq = pw_core_sync(Core, PW_ID_CORE, 0);

    const auto Deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(TimeoutMs);
    while (!State.bDone && std::chrono::steady_clock::now() < Deadline)
    {
        pw_loop_iterate(Loop, 50);
    }

    spa_hook_remove(&CoreListener);
    if (!State.bDone)
    {
        fprintf(stderr, "[ramkolfxd] device registry initial sync timed out after %dms\n", TimeoutMs);
    }
    return State.bDone;
}

void DeviceRegistry::HandleGlobalAdded(uint32 Id, const char* Type, const spa_dict* Props)
{
    if (!Props)
    {
        return;
    }

    if (std::strcmp(Type, PW_TYPE_INTERFACE_Metadata) == 0)
    {
        // The session-wide "default" metadata object - there's exactly
        // one, holding default.configured.audio.sink/source among other
        // things. Bind it once and immediately claim the virtual sink.
        const char* MetadataName = spa_dict_lookup(Props, PW_KEY_METADATA_NAME);
        if (!DefaultMetadata && MetadataName && std::strcmp(MetadataName, "default") == 0)
        {
            DefaultMetadata = reinterpret_cast<pw_metadata*>(
                pw_registry_bind(Registry, Id, Type, PW_VERSION_METADATA, 0));
            if (DefaultMetadata)
            {
                ClaimVirtualSinkAsDefault();
            }
        }
        return;
    }

    if (std::strcmp(Type, PW_TYPE_INTERFACE_Node) != 0)
    {
        return;
    }

    const char* MediaClass = spa_dict_lookup(Props, PW_KEY_MEDIA_CLASS);
    if (!MediaClass || std::strcmp(MediaClass, "Audio/Sink") != 0)
    {
        return;
    }

    const char* NodeName = spa_dict_lookup(Props, PW_KEY_NODE_NAME);
    if (!NodeName || std::strcmp(NodeName, VirtualSink::NodeName) == 0)
    {
        // No name to key on, or this is our own virtual sink - selecting
        // ourselves as the hardware output target would loop the signal
        // back into the capture side.
        return;
    }

    const char* Description = spa_dict_lookup(Props, PW_KEY_NODE_DESCRIPTION);

    AudioDeviceInfo Info;
    Info.Name = NodeName;
    Info.Description = Description ? Description : NodeName;

    std::lock_guard<std::mutex> Lock(DevicesMutex);
    DevicesById[Id] = std::move(Info);
}

void DeviceRegistry::ClaimVirtualSinkAsDefault()
{
    // Same key/value shape `wpctl set-default` writes. Re-sending it even
    // when it's already the persisted default is the point: PipeWire's
    // metadata implementation broadcasts a property event on every
    // set_property() call regardless of whether the value actually
    // changed, and that's what nudges WirePlumber's default-nodes policy
    // to reconcile any stream that's been sitting unlinked since before
    // this daemon (re)started.
    char Value[128];
    std::snprintf(Value, sizeof(Value), "{ \"name\": \"%s\" }", VirtualSink::NodeName);
    pw_metadata_set_property(DefaultMetadata, PW_ID_CORE, "default.configured.audio.sink",
                              "Spa:String:JSON", Value);
}

void DeviceRegistry::HandleGlobalRemoved(uint32 Id)
{
    std::lock_guard<std::mutex> Lock(DevicesMutex);
    DevicesById.erase(Id);
}

std::vector<AudioDeviceInfo> DeviceRegistry::GetDevices() const
{
    std::lock_guard<std::mutex> Lock(DevicesMutex);
    std::vector<AudioDeviceInfo> Result;
    Result.reserve(DevicesById.size());
    for (const auto& [Id, Info] : DevicesById)
    {
        Result.push_back(Info);
    }
    return Result;
}

bool DeviceRegistry::HasDevice(const std::string& Name) const
{
    std::lock_guard<std::mutex> Lock(DevicesMutex);
    for (const auto& [Id, Info] : DevicesById)
    {
        if (Info.Name == Name)
        {
            return true;
        }
    }
    return false;
}

std::optional<std::string> DeviceRegistry::PickAnyDevice() const
{
    std::lock_guard<std::mutex> Lock(DevicesMutex);
    uint32 LowestId = std::numeric_limits<uint32>::max();
    const AudioDeviceInfo* Picked = nullptr;
    for (const auto& [Id, Info] : DevicesById)
    {
        if (Id < LowestId)
        {
            LowestId = Id;
            Picked = &Info;
        }
    }
    return Picked ? std::make_optional(Picked->Name) : std::nullopt;
}

} // namespace ramkolfx
