#pragma once
#include <QObject>
#include <QUrl>

// Holds the first leave request until the user cancels, discards, or saves.
// UI actions and commands from another instance use the same decision.
class Navigation : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool dirty READ dirty WRITE setDirty NOTIFY changed)
  Q_PROPERTY(bool pending READ pending NOTIFY changed)
  Q_PROPERTY(bool saving READ saving NOTIFY changed)
public:
  using QObject::QObject;
  bool dirty() const { return m_dirty; }
  bool pending() const { return !m_command.isEmpty(); }
  bool saving() const { return m_saving; }
  void setDirty(bool dirty);
  Q_INVOKABLE void request(const QString &command, const QUrl &file = {});
  Q_INVOKABLE void cancel();
  Q_INVOKABLE void discard();
  Q_INVOKABLE void save();
  Q_INVOKABLE void saveSucceeded();
  Q_INVOKABLE void saveFailed();
signals:
  void changed();
  void confirmationRequested();
  void saveRequested();
  void proceed(const QString &command, const QUrl &file);

private:
  void continueRequest();
  bool m_dirty = false, m_saving = false;
  QString m_command;
  QUrl m_file;
};
