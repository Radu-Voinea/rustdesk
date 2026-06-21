#!/usr/bin/env bash
#
# Build the RustDesk Flutter desktop client on Linux (X11 or Wayland).
#
# This produces an unpacked bundle you can run directly. It is the dev-friendly
# subset of `python3 ./build.py --flutter` (which additionally packages a .deb).
#
# Run from the repository root:
#     ./build_flutter_linux.sh
#
# Pinned tool versions match RustDesk CI (.github/workflows):
#     Flutter                    3.24.5
#     flutter_rust_bridge_codegen 1.80.1
#
# Prerequisites are documented in docs/RELATIVE_MOUSE_MODE.md.

set -euo pipefail

FRB_VERSION="1.80.1"

# --- sanity checks -----------------------------------------------------------
if [[ ! -f "Cargo.toml" || ! -d "flutter" ]]; then
  echo "error: run this from the RustDesk repository root." >&2
  exit 1
fi

if [[ -z "${VCPKG_ROOT:-}" ]]; then
  echo "error: VCPKG_ROOT is not set. Install vcpkg and run:" >&2
  echo "         vcpkg install libvpx libyuv opus aom" >&2
  echo "       then: export VCPKG_ROOT=\$HOME/vcpkg" >&2
  exit 1
fi

for tool in cargo flutter pkg-config cmake ninja; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "error: '$tool' not found on PATH (see docs/RELATIVE_MOUSE_MODE.md)." >&2
    exit 1
  fi
done

# --- submodules --------------------------------------------------------------
echo ">> ensuring git submodules (hbb_common etc.) are initialized"
git submodule update --init --recursive

# --- flutter_rust_bridge codegen (generated_bridge.dart is gitignored) -------
if ! command -v flutter_rust_bridge_codegen >/dev/null 2>&1; then
  echo ">> installing flutter_rust_bridge_codegen ${FRB_VERSION}"
  cargo install flutter_rust_bridge_codegen --version "${FRB_VERSION}" --features "uuid" --locked
fi

if [[ ! -f "flutter/lib/generated_bridge.dart" || "${1:-}" == "--regen-bridge" ]]; then
  echo ">> generating Dart/Rust FFI bridge"
  flutter_rust_bridge_codegen \
    --rust-input ./src/flutter_ffi.rs \
    --dart-output ./flutter/lib/generated_bridge.dart
fi

# --- flutter deps ------------------------------------------------------------
echo ">> flutter pub get"
( cd flutter && flutter pub get )

# --- Rust shared library (must precede 'flutter build linux') ----------------
# The Flutter CMake bundle copies target/release/liblibrustdesk.so into the
# bundle as lib/librustdesk.so, so build it first.
echo ">> building Rust library (liblibrustdesk.so)"
cargo build --locked --features flutter --lib --release

# --- Flutter Linux bundle ----------------------------------------------------
echo ">> flutter build linux --release"
( cd flutter && flutter build linux --release )

BUNDLE="flutter/build/linux/x64/release/bundle"
echo
echo "Build complete."
echo "  Bundle:  ${BUNDLE}"
echo "  Run it:  ( cd ${BUNDLE} && ./rustdesk )"
echo
echo "To confirm the Wayland relative-pointer module was compiled in, check the"
echo "CMake configure output above for the wayland-protocols pointer-constraints"
echo "and relative-pointer files. If they were missing, install:"
echo "  sudo apt install libwayland-dev libwayland-bin wayland-protocols"
