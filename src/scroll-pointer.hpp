/** @fileoverview A virtual pointer that scrolls whatever is under a point on
 *  one display, for scrolling capture. Uses zwlr_virtual_pointer_v1, which
 *  Hyprland provides. Every call must come from the thread that opened it. */
#pragma once
#include <QPointF>
#include <QString>
#include <memory>

class ScrollPointer {
public:
  ScrollPointer();
  ~ScrollPointer();
  ScrollPointer(const ScrollPointer &) = delete;
  ScrollPointer &operator=(const ScrollPointer &) = delete;
  /** Connects and creates a pointer bound to `outputName`. */
  bool open(const QString &outputName, QString &error);
  bool isOpen() const;
  /** Moves the pointer to `fraction` of the display (0..1 on each axis) and
   *  nudges it so the compositor re-checks what is under it. */
  bool park(QPointF fraction);
  /** Sends `notches` wheel steps. Positive scrolls down (or right). */
  bool scroll(int notches, bool horizontal = false);
  void close();
  struct State;

private:
  std::unique_ptr<State> state_;
};
