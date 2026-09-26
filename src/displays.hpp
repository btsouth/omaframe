/** @fileoverview Human names for displays, instead of connector names. */
#pragma once

#include <QList>
#include <QRect>
#include <QString>

namespace Displays {
/** One output: its connector `name`, EDID make and model, logical `bounds` in
 *  the desktop layout, and native `pixels`. */
struct Info {
  QString name, make, model;
  QRect bounds;
  QSize pixels;
};
/** `label` is for pickers ("Left · Dell S2721DGF · 2560 × 1440"); `place` reads
 *  inside a sentence ("left display"). */
struct Names {
  QString label, place;
};
/** Names for `displays` in the same order. Position comes from the desktop
 *  layout, so two identical monitors are still told apart. */
QList<Names> describe(const QList<Info> &displays);
/** The connected screens, in QGuiApplication::screens() order. */
QList<Info> fromScreens();
} // namespace Displays
