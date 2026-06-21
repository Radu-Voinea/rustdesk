# Relative Mouse Mode (gaming mode) — build, install & usage

Relative mouse mode sends the remote raw mouse **deltas** instead of absolute
positions, so the on‑screen pointer never hits the edge of the window. It is
what you want for games, 3D/CAD apps, and anything that uses raw mouse input.

This document covers the version with three additions:

1. **A global default** you can turn on *before* connecting (Settings → Display
   → *Other default options* → **Relative mouse mode**). It auto‑enables on every
   new desktop connection.
2. **A rendered cursor** while the mode is active. The local OS cursor is
   hidden/locked, so the remote cursor is drawn as a software overlay (with a
   synthetic centered cursor shown until the remote reports its first position).
3. **Wayland support** (e.g. Kubuntu 26.04 / KWin). On Wayland the cursor cannot
   be warped, so the client locks the pointer and reads raw deltas using the
   `zwp_pointer_constraints_v1` + `zwp_relative_pointer_v1` protocols.

> The feature is **Flutter‑only** (not the legacy Sciter UI). The Wayland piece
> is native C in `flutter/linux/` and **must be built on Linux** — it cannot be
> compiled on Windows/macOS.

---

## 1. Prerequisites (Kubuntu / Ubuntu 26.04)

### Build tools and libraries

```sh
sudo apt update
sudo apt install -y \
  zip g++ gcc git curl wget nasm yasm libgtk-3-dev clang libxcb-randr0-dev \
  libxdo-dev libxfixes-dev libxcb-shape0-dev libxcb-xfixes0-dev libasound2-dev \
  libpulse-dev cmake make libclang-dev ninja-build pkg-config libssl-dev \
  libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libpam0g-dev \
  libwayland-dev libwayland-bin wayland-protocols
```

> **GCC 15 note (Ubuntu 26.04):** the bundled `libwebm` (in the `rust-webm`
> dependency) uses `uint32_t` without including `<cstdint>`, which the very new
> GCC rejects. Export `CXXFLAGS="-include cstdint"` before building (the
> `build_flutter_linux.sh` script does this for you). Harmless on older GCC.

The **last line** is required for relative mouse mode on Wayland:

| Package | Provides |
| --- | --- |
| `libwayland-dev` | `wayland-client` headers/.pc, the `wayland-scanner.pc` used by CMake |
| `libwayland-bin` | the `wayland-scanner` code generator |
| `wayland-protocols` | the `pointer-constraints-unstable-v1.xml` and `relative-pointer-unstable-v1.xml` definitions |

If these are missing at build time, CMake silently skips the native module
(`HAS_WAYLAND_RELATIVE_POINTER` is undefined) and relative mode will not engage
on Wayland.

### Rust

```sh
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
source "$HOME/.cargo/env"
```

### Flutter 3.24.5 (the version RustDesk CI builds with)

```sh
git clone https://github.com/flutter/flutter.git -b 3.24.5 "$HOME/flutter"
export PATH="$HOME/flutter/bin:$PATH"     # add to ~/.bashrc to persist
flutter --version                          # should report 3.24.5
```

### vcpkg + media codecs

```sh
git clone https://github.com/microsoft/vcpkg "$HOME/vcpkg"
"$HOME/vcpkg/bootstrap-vcpkg.sh"
export VCPKG_ROOT="$HOME/vcpkg"            # add to ~/.bashrc to persist
"$HOME/vcpkg/vcpkg" install libvpx libyuv opus aom
```

---

## 2. Get the source

```sh
git clone --recurse-submodules <your-fork-or-remote> rustdesk
cd rustdesk
# if you already cloned without submodules:
git submodule update --init --recursive
```

`libs/hbb_common` is a submodule; the build will not compile without it.

---

## 3. Build

### Option A — convenience script (unpacked bundle)

```sh
./build_flutter_linux.sh
```

It initializes submodules, installs `flutter_rust_bridge_codegen` 1.80.1,
generates the FFI bridge, builds `liblibrustdesk.so`, and runs
`flutter build linux --release`. Output:

```
flutter/build/linux/x64/release/bundle/      # rustdesk + lib/ + data/
```

### Option B — installable `.deb`

```sh
# one-time: the FFI bridge (generated_bridge.dart is gitignored)
cargo install flutter_rust_bridge_codegen --version 1.80.1 --features "uuid" --locked
flutter_rust_bridge_codegen --rust-input ./src/flutter_ffi.rs \
                            --dart-output ./flutter/lib/generated_bridge.dart

python3 ./build.py --flutter        # add --hwcodec for hardware codecs
# -> produces flutter/rustdesk.deb
```

### Option C — fully manual

```sh
# 1. flutter deps FIRST — the codegen runs `flutter pub run ffigen`, which needs
#    flutter/.dart_tool/package_config.json to exist.
( cd flutter && flutter pub get )

# 2. FFI bridge (generated_bridge.dart is gitignored). Needs nightly for cargo-expand.
rustup toolchain install nightly --profile minimal
cargo install cargo-expand --version 1.0.95 --locked
cargo install flutter_rust_bridge_codegen --version 1.80.1 --features "uuid" --locked
flutter_rust_bridge_codegen --rust-input ./src/flutter_ffi.rs \
                            --dart-output ./flutter/lib/generated_bridge.dart \
                            --c-output ./flutter/macos/Runner/bridge_generated.h

# 3. Rust shared lib, then the Flutter bundle (build order matters: the bundle
#    copies target/release/liblibrustdesk.so, so cargo must run first).
export CXXFLAGS="-include cstdint"            # GCC 13+/15 (Ubuntu 26.04); see note above
cargo build --locked --features flutter --lib --release
( cd flutter && flutter build linux --release )
```

**Verify the Wayland module compiled in:** during `flutter build linux`, the
CMake configure step should reference `pointer-constraints-unstable-v1` and
`relative-pointer-unstable-v1`. If it doesn't, re-check the `libwayland-*` /
`wayland-protocols` packages from step 1.

---

## 4. Install / run

**Run the unpacked bundle (Option A/C):**

```sh
cd flutter/build/linux/x64/release/bundle
./rustdesk
```

**Install the .deb (Option B):**

```sh
sudo apt install ./flutter/rustdesk.deb
rustdesk
```

Make sure you launch it under a **Wayland** session (Kubuntu 26.04 default).
You can confirm at runtime: `echo "$XDG_SESSION_TYPE"` should print `wayland`.

---

## 5. How to use

### Turn it on by default (before connecting)

1. Open RustDesk → **Settings** (gear) → **Display**.
2. Under **Other default options**, tick **Relative mouse mode**.
3. Connect to a remote. The mode auto‑engages once the remote image appears:
   - the local cursor disappears and is **locked** in place (no warping);
   - a software cursor is drawn in the remote view;
   - mouse movement drives the remote pointer 1:1 via deltas.

### Toggle per session

In an open session, use the toolbar **mouse menu → Relative mouse mode**.

### Exit the mode

- **Linux / Windows:** `Ctrl + Alt`
- **macOS:** `Cmd + G`

This releases the pointer lock, restores the local cursor, and releases any held
modifiers on the remote.

### Requirements / limits

- The **remote** (controlled) machine must run RustDesk **≥ 1.4.5**.
- Controlling a **remote Linux** host is **not** supported (the option is hidden
  for Linux peers); the controlling client on Linux/Wayland **is** supported.
- Web client is not supported.

---

## 6. Troubleshooting & tuning

**The option/toolbar item doesn't show.**
The remote is Linux, or older than 1.4.5, or you're on the web client. The menu
is gated by `isRelativeMouseModeSupported`.

**On Wayland the mode toggles but the pointer isn't captured.**
The native module probably wasn't built in. Reinstall `libwayland-dev`,
`libwayland-bin`, `wayland-protocols`, delete `flutter/build`, and rebuild. The
relevant code is `flutter/linux/wayland_relative_pointer.cc` and the
`HAS_WAYLAND_RELATIVE_POINTER` block in `flutter/linux/CMakeLists.txt`.

**Pointer speed feels wrong on Wayland.**
The handler forwards the compositor's *accelerated* deltas. To use raw,
unaccelerated deltas (so only the remote applies acceleration), edit
`relative_pointer_handle_relative_motion` in
`flutter/linux/wayland_relative_pointer.cc` to accumulate `dx_unaccel` /
`dy_unaccel` instead of `dx` / `dy`, and rebuild.

**I briefly see a frozen arrow when enabling from the toolbar.**
Flutter hides the cursor over the remote image; the native code also sets a
blank cursor on the window. If you enable while the pointer is over the toolbar,
there can be a brief flash before the lock and blank-cursor take effect. Using
the global default (which engages on connect) avoids this.

---

## 7. Where the code lives

| Area | Files |
| --- | --- |
| Global default option | `flutter/lib/consts.dart`, `flutter/lib/common/widgets/setting_widgets.dart` |
| Auto‑enable on connect | `flutter/lib/models/input_model.dart` (`maybeAutoEnableRelativeMouseMode`) |
| Mode logic / platform gating | `flutter/lib/models/relative_mouse_model.dart` |
| Rendered cursor (client) | `flutter/lib/desktop/pages/remote_page.dart`, `flutter/lib/mobile/pages/remote_page.dart`, `flutter/lib/models/model.dart` |
| Cursor position to controller (server) | `src/server/input_service.rs` (`run_pos`) |
| Native Wayland pointer lock | `flutter/linux/wayland_relative_pointer.{cc,h}`, `flutter/linux/my_application.cc`, `flutter/linux/CMakeLists.txt` |
