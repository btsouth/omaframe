#pragma once
#include <QQuickWindow>
#include <functional>

namespace CaptureDismissal {
inline const QString scope = QStringLiteral("omaframe-capture-countdown");
// Install before mapping. A failed compositor rule must not start a timer.
bool prepare(QString &error);
// Destroy the platform surface, acknowledge its removal, then cross two
// compositor frames. Failure is bounded and never falls through to a grab.
void clear(QQuickWindow *badge, QObject *owner, std::function<void(bool)> done);
} // namespace CaptureDismissal
