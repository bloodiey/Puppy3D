#!/usr/bin/env bash
set -euo pipefail

if ! command -v apt-get >/dev/null 2>&1; then
    echo "This setup script requires Ubuntu or a compatible apt-based distribution." >&2
    exit 1
fi

if [[ ${EUID} -eq 0 ]]; then
    SUDO=()
else
    command -v sudo >/dev/null 2>&1 || { echo "Please run as root or install sudo." >&2; exit 1; }
    SUDO=(sudo)
fi

"${SUDO[@]}" apt-get update
"${SUDO[@]}" apt-get install -y \
    build-essential pkg-config libgtk-3-dev libsdl2-dev libsdl2-mixer-dev \
    liblua5.4-dev nlohmann-json3-dev libepoxy-dev libfluidsynth3 libgl1-mesa-dev

echo "Dependencies are ready. Build this project with: make"
