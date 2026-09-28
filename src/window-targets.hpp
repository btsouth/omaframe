#pragma once
#include <QJsonArray>
#include <QStringList>
#include <QVariantList>

namespace WindowTargets {
// Coordinates are normalized to each monitor's logical rectangle, matching
// the selection overlay even when the output has a fractional scale.
QVariantList fromHyprland(const QJsonArray &monitors, const QJsonArray &clients,
                          const QStringList &requested);
}
