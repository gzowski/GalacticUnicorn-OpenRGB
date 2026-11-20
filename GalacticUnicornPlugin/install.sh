#!/usr/bin/env bash
# One-shot installer: Galactic Unicorn plugin for OpenRGB 1.1 (Linux)
#
#   ./install.sh                 install deps, clone OpenRGB release_1.1, build, install plugin
#   ./install.sh --qt5|--qt6     force the Qt major version (must match your OpenRGB binary)
#   ./install.sh --no-deps       skip the package-manager step
#   ./install.sh --uninstall     remove the installed plugin
#   ./install.sh --clean         delete the build directory
#   ./install.sh --plugin-dir D  install somewhere else than ~/.config/OpenRGB/plugins

set -euo pipefail

TAG="release_1.1"
EXPECTED_API="5"
REPO_URL="https://codeberg.org/OpenRGB/OpenRGB.git"

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$HERE/build"
SRC_DIR="$BUILD_DIR/OpenRGB"
OUT_DIR="$BUILD_DIR/plugin-build"
PLUGIN_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/OpenRGB/plugins"
PLUGIN_FILE="libGalacticUnicornPlugin.so"

QT_MAJOR=""
DO_DEPS=1

c_g=$'\e[32m'; c_y=$'\e[33m'; c_r=$'\e[31m'; c_0=$'\e[0m'
info() { echo "${c_g}==>${c_0} $*"; }
warn() { echo "${c_y}WARN:${c_0} $*" >&2; }
die()  { echo "${c_r}ERROR:${c_0} $*" >&2; exit 1; }

while [[ $# -gt 0 ]]; do
  case "$1" in
    --qt5) QT_MAJOR=5 ;;
    --qt6) QT_MAJOR=6 ;;
    --no-deps) DO_DEPS=0 ;;
    --plugin-dir) shift; PLUGIN_DIR="$1" ;;
    --uninstall) rm -fv "$PLUGIN_DIR/$PLUGIN_FILE"; exit 0 ;;
    --clean) rm -rf "$BUILD_DIR"; info "Build directory removed"; exit 0 ;;
    -h|--help) sed -n '2,12p' "$0"; exit 0 ;;
    *) die "Unknown option: $1" ;;
  esac
  shift
done

# ---------------------------------------------------------------- Qt version
detect_qt() {
  [[ -n "$QT_MAJOR" ]] && return
  local bin; bin="$(command -v openrgb || command -v OpenRGB || true)"
  if [[ -n "$bin" ]] && command -v ldd >/dev/null 2>&1; then
    if ldd "$bin" 2>/dev/null | grep -q 'libQt6Core'; then QT_MAJOR=6
    elif ldd "$bin" 2>/dev/null | grep -q 'libQt5Core'; then QT_MAJOR=5
    fi
  fi
  if [[ -z "$QT_MAJOR" ]]; then
    warn "Could not detect which Qt your OpenRGB uses (AppImage/Flatpak/not in PATH?)."
    warn "Defaulting to Qt6. If OpenRGB uses Qt5, re-run with --qt5."
    QT_MAJOR=6
  fi
  info "Using Qt$QT_MAJOR"
}

# ---------------------------------------------------------------- dependencies
install_deps() {
  [[ $DO_DEPS -eq 1 ]] || { info "Skipping dependency install"; return; }
  local SUDO=""; [[ $EUID -ne 0 ]] && SUDO="sudo"

  if command -v apt-get >/dev/null; then
    local qt; [[ $QT_MAJOR == 6 ]] && qt="qt6-base-dev qt6-serialport-dev" || qt="qtbase5-dev libqt5serialport5-dev"
    info "Installing build dependencies (apt)"
    $SUDO apt-get update
    $SUDO apt-get install -y git build-essential pkg-config $qt libgl1-mesa-dev \
      libusb-1.0-0-dev libhidapi-dev
  elif command -v dnf >/dev/null; then
    local qt; [[ $QT_MAJOR == 6 ]] && qt="qt6-qtbase-devel qt6-qtserialport-devel" || qt="qt5-qtbase-devel qt5-qtserialport-devel"
    info "Installing build dependencies (dnf)"
    $SUDO dnf install -y git gcc-c++ make pkgconf-pkg-config $qt mesa-libGL-devel \
      libusb1-devel hidapi-devel
  elif command -v pacman >/dev/null; then
    local qt; [[ $QT_MAJOR == 6 ]] && qt="qt6-base qt6-serialport" || qt="qt5-base qt5-serialport"
    info "Installing build dependencies (pacman)"
    $SUDO pacman -S --needed --noconfirm git base-devel pkgconf $qt libusb hidapi
  else
    warn "Unknown package manager - install git, a C++17 compiler, make and Qt$QT_MAJOR dev packages yourself."
  fi
}

find_qmake() {
  local cands=()
  if [[ $QT_MAJOR == 6 ]]; then
    cands=(qmake6 qmake-qt6 /usr/lib/qt6/bin/qmake /usr/lib64/qt6/bin/qmake /usr/lib/x86_64-linux-gnu/qt6/bin/qmake qmake)
  else
    cands=(qmake-qt5 qmake /usr/lib/qt5/bin/qmake /usr/lib64/qt5/bin/qmake /usr/lib/x86_64-linux-gnu/qt5/bin/qmake qmake5)
  fi
  local q v
  for q in "${cands[@]}"; do
    if command -v "$q" >/dev/null 2>&1; then
      v="$("$q" -query QT_VERSION 2>/dev/null || true)"
      if [[ "$v" == $QT_MAJOR.* ]]; then QMAKE="$(command -v "$q")"; info "qmake: $QMAKE (Qt $v)"; return; fi
    fi
  done
  die "No qmake for Qt$QT_MAJOR found. Install the Qt$QT_MAJOR dev package or pass --qt5/--qt6."
}

# ---------------------------------------------------------------- OpenRGB source
fetch_openrgb() {
  mkdir -p "$BUILD_DIR"
  if [[ -d "$SRC_DIR/.git" ]]; then
    info "Updating existing OpenRGB checkout to $TAG"
    git -C "$SRC_DIR" fetch --depth 1 origin "refs/tags/$TAG:refs/tags/$TAG" 2>/dev/null || true
    git -C "$SRC_DIR" checkout -q --detach "$TAG"
  else
    info "Cloning OpenRGB $TAG from $REPO_URL"
    git clone --depth 1 --branch "$TAG" "$REPO_URL" "$SRC_DIR"
  fi
  info "Fetching submodules (headers for dependencies)"
  git -C "$SRC_DIR" submodule update --init --depth 1 || warn "Some submodules failed to fetch (usually harmless)"
  info "OpenRGB source at commit $(git -C "$SRC_DIR" rev-parse --short HEAD) ($TAG)"
}

# ---------------------------------------------------------------- preflight
preflight() {
  info "Checking plugin API in the OpenRGB headers"
  local hdr="$SRC_DIR/OpenRGBPluginInterface.h"
  [[ -f "$hdr" ]] || die "$hdr not found - OpenRGB layout is not what this installer expects."

  local api; api="$(grep -Eo 'OPENRGB_PLUGIN_API_VERSION[[:space:]]+[0-9]+' "$hdr" | grep -Eo '[0-9]+$' | head -1 || true)"
  echo "    Plugin API version in headers: ${api:-unknown} (expected $EXPECTED_API)"
  [[ "$api" == "$EXPECTED_API" ]] || warn "API version differs from what this plugin was written for."

  local bad=0
  chk() { grep -q -- "$2" "$1" 2>/dev/null || { warn "'$2' not found in $(basename "$1")"; bad=1; }; }
  chk "$hdr" "GetPluginInfo"
  chk "$hdr" "GetPluginAPIVersion"
  chk "$hdr" "Load("
  chk "$hdr" "GetWidget"
  chk "$hdr" "GetTrayMenu"
  chk "$hdr" "Unload"
  chk "$hdr" "OPENRGB_PLUGIN_LOCATION_TOP"
  chk "$hdr" "TabIconString"
  chk "$SRC_DIR/OpenRGBPluginInterface.h" "GetRGBControllers"
  chk "$SRC_DIR/RGBController/RGBControllerInterface.h" "ZONE_TYPE_MATRIX"
  if [[ $bad -eq 1 ]]; then
    warn "Interface differs from what the plugin expects; the build may fail."
    warn "If it does, send the compiler errors plus the output of:"
    warn "  sed -n '1,200p' $hdr"
  else
    info "Preflight OK"
  fi
}

# ---------------------------------------------------------------- build
write_include_list() {
  info "Generating include paths"
  {
    echo "INCLUDEPATH += \\"
    find "$SRC_DIR" -maxdepth 3 -type d \
      | grep -Ev '/(\.git|Controllers|debian|Documentation|scripts)(/|$)' \
      | sed 's/.*/    "&" \\/'
    echo "    \"$SRC_DIR\""
  } > "$HERE/extra_includes.pri"

  # Optional: system hidapi headers (OpenRGB 1.1 on Linux may want hidapi-hotplug)
  if command -v pkg-config >/dev/null 2>&1; then
    local flags; flags="$(pkg-config --cflags-only-I hidapi-hidraw 2>/dev/null || pkg-config --cflags-only-I hidapi-libusb 2>/dev/null || true)"
    local f
    for f in $flags; do echo "INCLUDEPATH += ${f#-I}" >> "$HERE/extra_includes.pri"; done
  fi
}

build_plugin() {
  info "Building plugin"
  rm -rf "$OUT_DIR"; mkdir -p "$OUT_DIR"
  ( cd "$OUT_DIR"
    OPENRGB_SRC="$SRC_DIR" "$QMAKE" "$HERE/GalacticUnicornPlugin.pro" CONFIG+=release
    make -j"$(nproc 2>/dev/null || echo 2)" )
  [[ -f "$OUT_DIR/$PLUGIN_FILE" ]] || die "Build finished but $PLUGIN_FILE was not produced."
}

install_plugin() {
  mkdir -p "$PLUGIN_DIR"
  cp -v "$OUT_DIR/$PLUGIN_FILE" "$PLUGIN_DIR/$PLUGIN_FILE"
  echo
  info "Installed to $PLUGIN_DIR/$PLUGIN_FILE"
  echo "    Now restart OpenRGB completely (quit it from the tray too)."
  echo "    A 'Galactic Unicorn' tab will appear. Enter the Unicorn's IP and press Connect."
  echo "    Check Settings > Plugins if the tab is missing - it shows load errors."
}

# ---------------------------------------------------------------- main
if command -v openrgb >/dev/null 2>&1; then
  info "Installed OpenRGB reports: $(openrgb --version 2>/dev/null | head -3 | tr '\n' ' ')"
fi

detect_qt
install_deps
find_qmake
fetch_openrgb
preflight
write_include_list
build_plugin
install_plugin
