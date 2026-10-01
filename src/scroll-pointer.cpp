// Wheel injection follows Omasnap's scroll-inject.cpp, copyright 2026 Tobi
// Lütke, MIT. See docs/OMASNAP-LICENSE.
#include "scroll-pointer.hpp"

#include "wlr-virtual-pointer-unstable-v1-client-protocol.h"

#include <wayland-client.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <poll.h>
#include <string>
#include <thread>
#include <vector>

namespace {
/// Coordinates are sent as a fraction of this extent. The compositor maps
/// them onto the bound display's layout box, so fractions survive scale and
/// rotation without knowing the display's pixel size.
constexpr std::uint32_t kExtent = 100000;
/// One wheel notch, in the axis units a physical mouse reports.
constexpr double kNotchValue = 15.0;

struct Output {
  wl_output *handle = nullptr;
  std::string name;
};
} // namespace

struct ScrollPointer::State {
  wl_display *display = nullptr;
  wl_registry *registry = nullptr;
  wl_seat *seat = nullptr;
  zwlr_virtual_pointer_manager_v1 *manager = nullptr;
  zwlr_virtual_pointer_v1 *pointer = nullptr;
  std::vector<std::unique_ptr<Output>> outputs;
  std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();

  std::uint32_t timeMs() const {
    return static_cast<std::uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start)
            .count());
  }
  bool flush() {
    // A full socket buffer must not hang capture; give it a moment, then
    // report failure rather than block.
    for (int attempt = 0; attempt < 20; ++attempt) {
      if (wl_display_flush(display) >= 0)
        return true;
      if (errno != EAGAIN)
        return false;
      pollfd fd{wl_display_get_fd(display), POLLOUT, 0};
      poll(&fd, 1, 10);
    }
    return false;
  }
  ~State() {
    if (pointer)
      zwlr_virtual_pointer_v1_destroy(pointer);
    for (const auto &output : outputs)
      if (output->handle)
        wl_output_release(output->handle);
    if (manager)
      zwlr_virtual_pointer_manager_v1_destroy(manager);
    if (seat)
      wl_seat_destroy(seat);
    if (registry)
      wl_registry_destroy(registry);
    if (display) {
      wl_display_flush(display);
      wl_display_disconnect(display);
    }
  }
};

namespace {
void outputGeometry(void *, wl_output *, int32_t, int32_t, int32_t, int32_t,
                    int32_t, const char *, const char *, int32_t) {}
void outputMode(void *, wl_output *, uint32_t, int32_t, int32_t, int32_t) {}
void outputDone(void *, wl_output *) {}
void outputScale(void *, wl_output *, int32_t) {}
void outputName(void *data, wl_output *, const char *name) {
  static_cast<Output *>(data)->name = name ? name : "";
}
void outputDescription(void *, wl_output *, const char *) {}
constexpr wl_output_listener kOutputListener{
    outputGeometry, outputMode,  outputDone,
    outputScale,    outputName, outputDescription};

void registryGlobal(void *data, wl_registry *registry, uint32_t name,
                    const char *interface, uint32_t version) {
  auto &state = *static_cast<ScrollPointer::State *>(data);
  if (std::strcmp(interface, wl_seat_interface.name) == 0 && !state.seat) {
    state.seat = static_cast<wl_seat *>(
        wl_registry_bind(registry, name, &wl_seat_interface, 1));
  } else if (std::strcmp(interface, wl_output_interface.name) == 0 &&
             version >= 4) {
    auto output = std::make_unique<Output>();
    output->handle = static_cast<wl_output *>(
        wl_registry_bind(registry, name, &wl_output_interface, 4));
    wl_output_add_listener(output->handle, &kOutputListener, output.get());
    state.outputs.push_back(std::move(output));
  } else if (std::strcmp(interface,
                         zwlr_virtual_pointer_manager_v1_interface.name) == 0 &&
             !state.manager) {
    state.manager = static_cast<zwlr_virtual_pointer_manager_v1 *>(
        wl_registry_bind(registry, name,
                         &zwlr_virtual_pointer_manager_v1_interface,
                         std::min(version, 2u)));
  }
}
void registryRemoved(void *, wl_registry *, uint32_t) {}
constexpr wl_registry_listener kRegistryListener{registryGlobal,
                                                 registryRemoved};
} // namespace

ScrollPointer::ScrollPointer() = default;
ScrollPointer::~ScrollPointer() = default;

bool ScrollPointer::isOpen() const { return state_ && state_->pointer; }

void ScrollPointer::close() { state_.reset(); }

bool ScrollPointer::open(const QString &outputName, QString &error) {
  close();
  auto state = std::make_unique<State>();
  state->display = wl_display_connect(nullptr);
  if (!state->display) {
    error = QStringLiteral("Could not connect to Wayland to scroll.");
    return false;
  }
  state->registry = wl_display_get_registry(state->display);
  wl_registry_add_listener(state->registry, &kRegistryListener, state.get());
  if (wl_display_roundtrip(state->display) < 0 ||
      wl_display_roundtrip(state->display) < 0) {
    error = QStringLiteral("Wayland stopped answering.");
    return false;
  }
  if (!state->manager || !state->seat) {
    error = QStringLiteral("This compositor cannot scroll for Omaframe.");
    return false;
  }
  const std::string wanted = outputName.toStdString();
  const auto match = std::ranges::find_if(
      state->outputs, [&](const auto &output) { return output->name == wanted; });
  if (match == state->outputs.end()) {
    error = QStringLiteral("Display %1 is not available.").arg(outputName);
    return false;
  }
  // Absolute positions map onto the bound display. An unbound pointer maps
  // across every display and would land on the wrong one.
  if (zwlr_virtual_pointer_manager_v1_get_version(state->manager) < 2) {
    error = QStringLiteral("This compositor's virtual pointer is too old.");
    return false;
  }
  state->pointer =
      zwlr_virtual_pointer_manager_v1_create_virtual_pointer_with_output(
          state->manager, state->seat, (*match)->handle);
  if (!state->pointer || wl_display_roundtrip(state->display) < 0) {
    error = QStringLiteral("Could not create a virtual pointer.");
    return false;
  }
  state_ = std::move(state);
  return true;
}

bool ScrollPointer::park(QPointF fraction) {
  if (!isOpen())
    return false;
  auto &s = *state_;
  const auto coordinate = [](double value) {
    return static_cast<std::uint32_t>(
        std::clamp(value, 0.0, 1.0) * (kExtent - 1));
  };
  zwlr_virtual_pointer_v1_motion_absolute(
      s.pointer, s.timeMs(), coordinate(fraction.x()), coordinate(fraction.y()),
      kExtent, kExtent);
  zwlr_virtual_pointer_v1_frame(s.pointer);
  if (!s.flush())
    return false;
  // A tiny relative move afterwards makes the compositor re-check which
  // surface is under the pointer, as a real mouse would.
  std::this_thread::sleep_for(std::chrono::milliseconds(16));
  zwlr_virtual_pointer_v1_motion(s.pointer, s.timeMs(), wl_fixed_from_int(1), 0);
  zwlr_virtual_pointer_v1_frame(s.pointer);
  zwlr_virtual_pointer_v1_motion(s.pointer, s.timeMs(), wl_fixed_from_int(-1), 0);
  zwlr_virtual_pointer_v1_frame(s.pointer);
  return s.flush() && wl_display_roundtrip(s.display) >= 0;
}

bool ScrollPointer::scroll(int notches, bool horizontal) {
  if (!isOpen() || notches == 0)
    return false;
  auto &s = *state_;
  // One discrete event with its source, then a frame. Hyprland ties the
  // source to the most recent axis event, so the source comes after it.
  const std::uint32_t axis = horizontal ? WL_POINTER_AXIS_HORIZONTAL_SCROLL
                                        : WL_POINTER_AXIS_VERTICAL_SCROLL;
  zwlr_virtual_pointer_v1_axis_discrete(s.pointer, s.timeMs(), axis,
                                        wl_fixed_from_double(kNotchValue * notches),
                                        notches);
  zwlr_virtual_pointer_v1_axis_source(s.pointer, WL_POINTER_AXIS_SOURCE_WHEEL);
  zwlr_virtual_pointer_v1_frame(s.pointer);
  return s.flush() && wl_display_roundtrip(s.display) >= 0;
}
