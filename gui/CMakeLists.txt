# AudioDock
# Copyright (C) 2026 Roman Levin (Coldsteel48)
#
# This file is part of AudioDock, dual-licensed under the GNU General
# Public License v3.0 (see LICENSE) or a separate commercial license
# (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
# terms of the Contributor License Agreement (see CLA.md).

# Placeholder: GUI toolkit not chosen yet (candidates: Qt, GTK). Not wired
# into the default build - enable with -DAUDIODOCK_BUILD_GUI=ON once there's
# something here to build. The GUI will talk to audiodockd purely over the
# Unix control socket (see common/include/audiodock/protocol.hpp), so it can
# be developed independently of the daemon.
