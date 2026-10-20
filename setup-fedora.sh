#!/usr/bin/env bash
set -euo pipefail

if ! command -v dnf >/dev/null 2>&1; then
    echo "This setup script requires Fedora or a compatible dnf-based distribution." >&2
    exit 1
fi

if [[ ${EUID} -eq 0 ]]; then
    SUDO=()
else
    command -v sudo >/dev/null 2>&1 || { echo "Please run as root or install sudo." >&2; exit 1; }
    SUDO=(sudo)
fi

"${SUDO[@]}" dnf install -y \
    gcc-c++ make pkgconf-pkg-config gtk3-devel SDL2-devel SDL2_mixer-devel \
    lua-devel nlohmann-json-devel libepoxy-devel fluidsynth-libs mesa-libGL-devel

echo "Dependencies are ready. Build this project with: make"
