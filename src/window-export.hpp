#pragma once
#include <QString>
#include <functional>
#include <memory>

class QWindow;

// A window's handle for the portal, so a file chooser opens as a dialog over
// it instead of as a window of its own.
namespace FilePicks {

/** The portal's parent_window string for an exported handle: "wayland:HANDLE",
 * or empty (no parent) when there is no handle. */
QString portalParent(const QString &handle);

class WindowExporter {
public:
  /** The handle, or empty when the window cannot be exported. */
  using Ready = std::function<void(const QString &handle)>;
  virtual ~WindowExporter() = default;
  /** Calls ready once, soon, and never blocks. The export stays alive until
   * release() or the next exportWindow. */
  virtual void exportWindow(QWindow *window, Ready ready) = 0;
  virtual void release() = 0;
};

/** Exports the window with xdg-foreign v2 on Wayland. Anywhere else, with no
 * exporter in the compositor, or when no handle arrives in time, the handle is
 * empty. */
std::unique_ptr<WindowExporter> waylandWindowExporter();

} // namespace FilePicks
