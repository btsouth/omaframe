#pragma once
#include <QJsonArray>
#include <QStringList>
#include <QVariantList>

namespace WindowTargets {
// Coordinates are normalized to each monitor's logical rectangle, matching
// the selection overlay even when the output has a fractional scale.
QVariantList fromHyprland(const QJsonArray &monitors, const QJsonArray &clients,
                          const QStringList &requested);
// The displays Hyprland reports as powered off (DPMS).
QStringList dark(const QJsonArray &monitors);
// Drops displays that Hyprland reports as powered off (DPMS). A dark output
// never produces a frame, so waiting on it would fail the whole capture. If
// every requested display is dark, all of them are kept.
QStringList awake(const QJsonArray &monitors, const QStringList &requested);
}
