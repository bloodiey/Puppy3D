#!/usr/bin/env bash
set -euo pipefail

if ! command -v zypper >/dev/null 2>&1; then
    echo "This setup script requires openSUSE or a compatible zypper-based distribution." >&2
    exit 1
fi

if [[ ${EUID} -eq 0 ]]; then
    SUDO=()
else
    command -v sudo >/dev/null 2>&1 || { echo "Please run as root or install sudo." >&2; exit 1; }
    SUDO=(sudo)
fi

"${SUDO[@]}" zypper --non-interactive install \
    gcc-c++ make pkg-config gtk3-devel libSDL2-devel libSDL2_mixer-devel \
    lua54-devel nlohmann_json-devel libepoxy-devel libfluidsynth3 Mesa-libGL-devel

echo "Dependencies are ready. Build this project with: make"
