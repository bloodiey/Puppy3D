#!/usr/bin/env bash
set -euo pipefail

if ! command -v pacman >/dev/null 2>&1; then
    echo "This setup script requires Arch Linux or an Arch-based distribution." >&2
    exit 1
fi

if [[ ${EUID} -eq 0 ]]; then
    SUDO=()
else
    command -v sudo >/dev/null 2>&1 || { echo "Please run as root or install sudo." >&2; exit 1; }
    SUDO=(sudo)
fi

"${SUDO[@]}" pacman -S --needed --noconfirm \
    base-devel pkgconf gtk3 sdl2 sdl2_mixer fluidsynth lua nlohmann-json libepoxy mesa

echo "Dependencies are ready. Build this project with: make"
