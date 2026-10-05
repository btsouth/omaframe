/** @fileoverview Omaframe's capture shortcuts on Omarchy/Hyprland.
 *
 * Print takes a screenshot, Alt+Print starts or stops recording, and
 * Alt+Shift+Print pauses or resumes. Setup changes free keys or stock capture
 * bindings only when the user asks. Custom bindings are always left alone. */
#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

namespace Shortcuts {
enum class Action { Screenshot, Record, Pause, Delay };
/** A readable key combination, e.g. "Alt+Print" or "Super+Shift+S". */
QString keyLabel(const QJsonObject &bind);
/** Whether `bind` runs Omaframe for `action`. */
bool runsOmaframe(const QJsonObject &bind, Action action);
/** The key that runs Omaframe for `action`, or empty when none does. */
QString omaframeKey(const QJsonArray &binds, Action action);
/** The default key for `action`: Print, Alt+Print, Alt+Shift+Print, or Shift+Print. */
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
 *  to something else. Previously managed actions with custom overrides are
 *  omitted from the regenerated block. */
bool install(const QString &configDir, const QString &executable,
             const QList<Action> &actions, QString *error,
             QString *backup = nullptr);
} // namespace Shortcuts

/** Live shortcut state for onboarding, settings and recording setup. */
class ShortcutSetup final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString delayKey READ delayKey NOTIFY changed)
  Q_PROPERTY(QString delayState READ delayState NOTIFY changed)
  Q_PROPERTY(QString screenshotKey READ screenshotKey NOTIFY changed)
  Q_PROPERTY(QString recordKey READ recordKey NOTIFY changed)
  Q_PROPERTY(QString pauseKey READ pauseKey NOTIFY changed)
  Q_PROPERTY(QString screenshotState READ screenshotState NOTIFY changed)
  Q_PROPERTY(QString recordState READ recordState NOTIFY changed)
  Q_PROPERTY(QString pauseState READ pauseState NOTIFY changed)
  Q_PROPERTY(bool canSetUp READ canSetUp NOTIFY changed)
  Q_PROPERTY(bool ready READ ready NOTIFY changed)
  Q_PROPERTY(bool checking READ checking NOTIFY changed)
  Q_PROPERTY(bool available READ available NOTIFY changed)
  Q_PROPERTY(QString message READ message NOTIFY changed)
public:
  explicit ShortcutSetup(QObject *parent = nullptr);
  QString delayKey() const { return m_delayKey; }
  QString delayState() const { return m_delayState; }
  QString screenshotKey() const { return m_screenshotKey; }
  QString recordKey() const { return m_recordKey; }
  QString pauseKey() const { return m_pauseKey; }
  QString screenshotState() const { return m_screenshotState; }
  QString recordState() const { return m_recordState; }
  QString pauseState() const { return m_pauseState; }
  bool canSetUp() const;
  bool ready() const {
    return !m_screenshotKey.isEmpty() && !m_recordKey.isEmpty() &&
           !m_pauseKey.isEmpty();
  }
  bool checking() const { return m_checking; }
  /** False when Hyprland could not be asked for its bindings. */
  bool available() const { return m_available; }
  QString message() const { return m_message; }
  Q_INVOKABLE void refresh();
  /** Binds every default key that is free or still Omarchy's stock action. */
  Q_INVOKABLE void setUp();
  Q_INVOKABLE void setUpDelay();
  /** Binds free start/stop and pause keys, for recording setup. */
  Q_INVOKABLE void setUpRecording();
signals:
  void changed();

private:
  void run(const QList<Shortcuts::Action> &actions);
  QString m_delayKey, m_delayState = "unknown";
  QString m_screenshotKey, m_recordKey, m_pauseKey,
      m_screenshotState = "unknown", m_recordState = "unknown",
      m_pauseState = "unknown", m_message;
  bool m_checking = false, m_available = false;
  int m_generation = 0;
};
