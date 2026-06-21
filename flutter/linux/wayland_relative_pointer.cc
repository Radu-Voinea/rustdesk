// Wayland relative pointer / pointer lock implementation.
// See wayland_relative_pointer.h for the high-level description.

#include "wayland_relative_pointer.h"

#if defined(GDK_WINDOWING_WAYLAND) && defined(HAS_WAYLAND_RELATIVE_POINTER)

#include <cstring>
#include <gdk/gdkwayland.h>
#include <wayland-client.h>

#include "pointer-constraints-unstable-v1-client-protocol.h"
#include "relative-pointer-unstable-v1-client-protocol.h"

// Protocol globals (bound once for the process; valid for the compositor
// connection lifetime).
static struct zwp_pointer_constraints_v1* g_constraints = nullptr;
static struct zwp_relative_pointer_manager_v1* g_rel_manager = nullptr;

// Per-session state. There is a single active relative-mouse session at a time.
static struct zwp_locked_pointer_v1* g_locked = nullptr;
static struct zwp_relative_pointer_v1* g_rel_pointer = nullptr;
static FlMethodChannel* g_delta_channel = nullptr;  // ref held while active
static GtkWindow* g_window = nullptr;               // for cursor restore
static bool g_active = false;

// Sub-pixel accumulator: relative-motion deltas are fixed-point doubles but the
// wire format uses integers, so carry the fractional remainder across events to
// preserve slow/fine movement.
static double g_acc_x = 0.0;
static double g_acc_y = 0.0;

// --- Registry: bind the constraints + relative-pointer managers --------------

static void registry_handle_global(void* /*data*/, struct wl_registry* registry,
                                   uint32_t name, const char* interface,
                                   uint32_t /*version*/) {
  if (strcmp(interface, zwp_pointer_constraints_v1_interface.name) == 0) {
    g_constraints = static_cast<zwp_pointer_constraints_v1*>(wl_registry_bind(
        registry, name, &zwp_pointer_constraints_v1_interface, 1));
  } else if (strcmp(interface,
                    zwp_relative_pointer_manager_v1_interface.name) == 0) {
    g_rel_manager = static_cast<zwp_relative_pointer_manager_v1*>(
        wl_registry_bind(registry, name,
                         &zwp_relative_pointer_manager_v1_interface, 1));
  }
}

static void registry_handle_global_remove(void* /*data*/,
                                          struct wl_registry* /*registry*/,
                                          uint32_t /*name*/) {}

static const struct wl_registry_listener registry_listener = {
    registry_handle_global,
    registry_handle_global_remove,
};

// Bind the protocol managers once. Returns true if both are available.
static bool ensure_globals(struct wl_display* display) {
  if (g_constraints != nullptr && g_rel_manager != nullptr) {
    return true;
  }
  struct wl_registry* registry = wl_display_get_registry(display);
  if (registry == nullptr) {
    return false;
  }
  wl_registry_add_listener(registry, &registry_listener, nullptr);
  wl_display_roundtrip(display);
  wl_registry_destroy(registry);
  return g_constraints != nullptr && g_rel_manager != nullptr;
}

// --- Relative motion ---------------------------------------------------------

static void relative_pointer_handle_relative_motion(
    void* /*data*/, struct zwp_relative_pointer_v1* /*rel*/,
    uint32_t /*utime_hi*/, uint32_t /*utime_lo*/, wl_fixed_t dx, wl_fixed_t dy,
    wl_fixed_t /*dx_unaccel*/, wl_fixed_t /*dy_unaccel*/) {
  if (!g_active || g_delta_channel == nullptr) {
    return;
  }

  // Use the accelerated deltas so the pointer feel matches the local desktop.
  g_acc_x += wl_fixed_to_double(dx);
  g_acc_y += wl_fixed_to_double(dy);

  int ix = static_cast<int>(g_acc_x);  // truncate towards zero
  int iy = static_cast<int>(g_acc_y);
  if (ix == 0 && iy == 0) {
    return;
  }
  g_acc_x -= ix;
  g_acc_y -= iy;

  g_autoptr(FlValue) args = fl_value_new_map();
  fl_value_set_string_take(args, "dx", fl_value_new_int(ix));
  fl_value_set_string_take(args, "dy", fl_value_new_int(iy));
  // Fire-and-forget; the callback runs on the GTK main thread (GDK dispatches
  // Wayland events there), so invoking the channel here is safe.
  fl_method_channel_invoke_method(g_delta_channel, "onMouseDelta", args, nullptr,
                                  nullptr, nullptr);
}

static const struct zwp_relative_pointer_v1_listener relative_pointer_listener =
    {
        relative_pointer_handle_relative_motion,
};

// --- Cursor hiding -----------------------------------------------------------

static void set_blank_cursor(GtkWindow* window, bool blank) {
  GdkWindow* gdk_window = gtk_widget_get_window(GTK_WIDGET(window));
  if (gdk_window == nullptr) {
    return;
  }
  if (!blank) {
    // Restore the inherited cursor.
    gdk_window_set_cursor(gdk_window, nullptr);
    return;
  }
  GdkDisplay* display = gtk_widget_get_display(GTK_WIDGET(window));
  GdkCursor* cursor = gdk_cursor_new_for_display(display, GDK_BLANK_CURSOR);
  if (cursor != nullptr) {
    gdk_window_set_cursor(gdk_window, cursor);
    g_object_unref(cursor);
  }
}

// --- Public API --------------------------------------------------------------

bool wayland_relative_pointer_enable(FlMethodChannel* channel,
                                     GtkWindow* window) {
  if (channel == nullptr || window == nullptr) {
    return false;
  }

  GdkDisplay* display = gtk_widget_get_display(GTK_WIDGET(window));
  if (!GDK_IS_WAYLAND_DISPLAY(display)) {
    return false;
  }

  // Already active: just re-point delta delivery at the new caller's channel
  // (mirrors the macOS behavior).
  if (g_active) {
    if (g_delta_channel != channel) {
      if (g_delta_channel != nullptr) {
        g_object_unref(g_delta_channel);
      }
      g_delta_channel = FL_METHOD_CHANNEL(g_object_ref(channel));
    }
    return true;
  }

  struct wl_display* wl_display = gdk_wayland_display_get_wl_display(display);
  if (wl_display == nullptr || !ensure_globals(wl_display)) {
    return false;
  }

  GdkWindow* gdk_window = gtk_widget_get_window(GTK_WIDGET(window));
  if (gdk_window == nullptr) {
    return false;
  }
  struct wl_surface* surface = gdk_wayland_window_get_wl_surface(gdk_window);
  if (surface == nullptr) {
    return false;
  }

  GdkSeat* gdk_seat = gdk_display_get_default_seat(display);
  GdkDevice* gdk_pointer =
      gdk_seat != nullptr ? gdk_seat_get_pointer(gdk_seat) : nullptr;
  if (gdk_pointer == nullptr) {
    return false;
  }
  struct wl_pointer* pointer = gdk_wayland_device_get_wl_pointer(gdk_pointer);
  if (pointer == nullptr) {
    return false;
  }

  // Lock the pointer in place over the whole surface (region = NULL). PERSISTENT
  // lifetime keeps the constraint across focus changes so it re-activates when
  // the window regains pointer focus.
  g_locked = zwp_pointer_constraints_v1_lock_pointer(
      g_constraints, surface, pointer, nullptr,
      ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
  if (g_locked == nullptr) {
    return false;
  }

  g_rel_pointer =
      zwp_relative_pointer_manager_v1_get_relative_pointer(g_rel_manager, pointer);
  if (g_rel_pointer == nullptr) {
    zwp_locked_pointer_v1_destroy(g_locked);
    g_locked = nullptr;
    return false;
  }
  zwp_relative_pointer_v1_add_listener(g_rel_pointer,
                                       &relative_pointer_listener, nullptr);

  g_delta_channel = FL_METHOD_CHANNEL(g_object_ref(channel));
  g_window = window;
  g_acc_x = 0.0;
  g_acc_y = 0.0;
  g_active = true;

  // Hide the frozen OS cursor; the Dart side renders its own software cursor.
  set_blank_cursor(window, true);

  wl_display_flush(wl_display);
  return true;
}

void wayland_relative_pointer_disable() {
  if (!g_active) {
    return;
  }
  g_active = false;

  if (g_rel_pointer != nullptr) {
    zwp_relative_pointer_v1_destroy(g_rel_pointer);
    g_rel_pointer = nullptr;
  }
  if (g_locked != nullptr) {
    zwp_locked_pointer_v1_destroy(g_locked);
    g_locked = nullptr;
  }
  if (g_window != nullptr) {
    set_blank_cursor(g_window, false);
    g_window = nullptr;
  }
  if (g_delta_channel != nullptr) {
    g_object_unref(g_delta_channel);
    g_delta_channel = nullptr;
  }
  g_acc_x = 0.0;
  g_acc_y = 0.0;

  GdkDisplay* display = gdk_display_get_default();
  if (display != nullptr && GDK_IS_WAYLAND_DISPLAY(display)) {
    struct wl_display* wl_display = gdk_wayland_display_get_wl_display(display);
    if (wl_display != nullptr) {
      wl_display_flush(wl_display);
    }
  }
}

#endif  // defined(GDK_WINDOWING_WAYLAND) && defined(HAS_WAYLAND_RELATIVE_POINTER)
