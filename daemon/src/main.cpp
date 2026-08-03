// AudioDock
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of AudioDock, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#include "audio_engine.hpp"

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    audiodock::AudioEngine Engine;
    return Engine.Run();
}
