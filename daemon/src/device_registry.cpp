// AudioBat
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of AudioBat, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#include "device_registry.hpp"

#include <cstdio>
#include <cstring>

#include <pipewire/pipewire.h>
#include <spa/utils/dict.h>

#include "virtual_sink.hpp"

namespace audiobat
{

namespace
{

void OnGlobalAdded(void* UserData, uint32_t Id, uint32_t Permissions, const char* Type,
                    uint32_t Version, const spa_dict* Props)
{
    (void)Permissions;
    (void)Version;
    static_cast<DeviceRegistry*>(UserData)->HandleGlobalAdded(Id, Type, Props);
}

void OnGlobalRemoved(void* UserData, uint32_t Id)
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

} // namespace

DeviceRegistry::DeviceRegistry(pw_loop* InLoop) : Loop(InLoop)
{
}

DeviceRegistry::~DeviceRegistry()
{
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
        fprintf(stderr, "[audiobatd] failed to create device registry context\n");
        return false;
    }

    Core = pw_context_connect(Context, nullptr, 0);
    if (!Core)
    {
        fprintf(stderr, "[audiobatd] failed to connect device registry core\n");
        return false;
    }

    Registry = pw_core_get_registry(Core, PW_VERSION_REGISTRY, 0);
    if (!Registry)
    {
        fprintf(stderr, "[audiobatd] failed to get PipeWire registry\n");
        return false;
    }

    pw_registry_add_listener(Registry, &RegistryListener, &RegistryEvents, this);
    return true;
}

void DeviceRegistry::HandleGlobalAdded(uint32_t Id, const char* Type, const spa_dict* Props)
{
    if (!Props || std::strcmp(Type, PW_TYPE_INTERFACE_Node) != 0)
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

void DeviceRegistry::HandleGlobalRemoved(uint32_t Id)
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

} // namespace audiobat
