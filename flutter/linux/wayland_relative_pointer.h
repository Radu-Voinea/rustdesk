// Wayland relative pointer / pointer lock support.
//
// Implements native relative mouse mode for the Wayland client using the
// zwp_pointer_constraints_v1 (lock_pointer) and zwp_relative_pointer_v1
// protocols. This replaces the X11 cursor-warp approach (which Wayland forbids):
// the compositor locks the pointer in place and delivers raw relative-motion
// deltas, which we forward to Dart over the 'org.rustdesk.rustdesk/host'
// method channel as `onMouseDelta` (the same path macOS uses).

#ifndef WAYLAND_RELATIVE_POINTER_H_
#define WAYLAND_RELATIVE_POINTER_H_

#include <flutter_linux/flutter_linux.h>
#include <gtk/gtk.h>

#if defined(GDK_WINDOWING_WAYLAND) && defined(HAS_WAYLAND_RELATIVE_POINTER)

// Lock the pointer for `window` and start streaming relative deltas to `channel`
// via `onMouseDelta`. There is a single active session at a time (matching the
// Dart RelativeMouseModel._activeNativeModel constraint); calling enable again
// re-points delta delivery at the new channel.
//
// Returns true on success, false if the display is not Wayland or the required
// protocols are unavailable (the Dart side then keeps the cursor unlocked).
//
// `channel` is ref'd for the lifetime of the session and released on disable.
bool wayland_relative_pointer_enable(FlMethodChannel* channel,
                                     GtkWindow* window);

// Release the pointer lock and stop streaming deltas. Safe to call when not
// active. Restores the window cursor.
void wayland_relative_pointer_disable();

#endif  // defined(GDK_WINDOWING_WAYLAND) && defined(HAS_WAYLAND_RELATIVE_POINTER)

#endif  // WAYLAND_RELATIVE_POINTER_H_
