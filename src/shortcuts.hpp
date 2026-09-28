/** @fileoverview Omaframe's two global shortcuts on Omarchy/Hyprland.
 *
 * Print takes a screenshot and Alt+Print starts or stops a recording. Setup
 * replaces only Omarchy's stock bindings for those keys, and only when the
 * user asks. Custom bindings are always left alone. */
#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

namespace Shortcuts {
enum class Action { Screenshot, Record };
/** A readable key combination, e.g. "Alt+Print" or "Super+Shift+S". */
QString keyLabel(const QJsonObject &bind);
/** Whether `bind` runs Omaframe for `action`. */
bool runsOmaframe(const QJsonObject &bind, Action action);
/** The key that runs Omaframe for `action`, or empty when none does. */
QString omaframeKey(const QJsonArray &binds, Action action);
/** The default key for `action`: Print, or Alt+Print. */
QString defaultKey(Action action);
/** What the default key does now: "omaframe", "stock" (Omarchy's own
 *  action, safe to replace), "none", or "custom". */
QString defaultKeyState(const QJsonArray &binds, Action action);
/** The shell command a shortcut runs, using `executable` unless the same
 *  binary is found on PATH as `omaframe`. */
QString command(Action action, const QString &executable);
/** The Omarchy Lua line that binds the default key for `action`. */
QString luaLine(Action action, const QString &executable);
/** Adds or updates Omaframe's marked block in `configDir/bindings.lua` for
 *  `actions`, after backing the file up. Refuses keys that the file binds
 *  to something else. */
bool install(const QString &configDir, const QString &executable,
             const QList<Action> &actions, QString *error,
             QString *backup = nullptr);
} // namespace Shortcuts

/** Live shortcut state for onboarding, settings and recording setup. */
class ShortcutSetup final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString screenshotKey READ screenshotKey NOTIFY changed)
  Q_PROPERTY(QString recordKey READ recordKey NOTIFY changed)
  Q_PROPERTY(QString screenshotState READ screenshotState NOTIFY changed)
  Q_PROPERTY(QString recordState READ recordState NOTIFY changed)
  Q_PROPERTY(bool canSetUp READ canSetUp NOTIFY changed)
  Q_PROPERTY(bool ready READ ready NOTIFY changed)
  Q_PROPERTY(bool checking READ checking NOTIFY changed)
  Q_PROPERTY(bool available READ available NOTIFY changed)
  Q_PROPERTY(QString message READ message NOTIFY changed)
public:
  explicit ShortcutSetup(QObject *parent = nullptr);
  QString screenshotKey() const { return m_screenshotKey; }
  QString recordKey() const { return m_recordKey; }
  QString screenshotState() const { return m_screenshotState; }
  QString recordState() const { return m_recordState; }
  bool canSetUp() const;
  bool ready() const {
    return !m_screenshotKey.isEmpty() && !m_recordKey.isEmpty();
  }
  bool checking() const { return m_checking; }
  /** False when Hyprland could not be asked for its bindings. */
  bool available() const { return m_available; }
  QString message() const { return m_message; }
  Q_INVOKABLE void refresh();
  /** Binds every default key that is free or still Omarchy's stock action. */
  Q_INVOKABLE void setUp();
  /** Binds only the recording key, for recording setup. */
  Q_INVOKABLE void setUpRecording();
signals:
  void changed();

private:
  void run(const QList<Shortcuts::Action> &actions);
  QString m_screenshotKey, m_recordKey, m_screenshotState = "unknown",
      m_recordState = "unknown", m_message;
  bool m_checking = false, m_available = false;
  int m_generation = 0;
};
