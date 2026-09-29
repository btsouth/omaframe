#include "navigation.hpp"
#include <utility>

void Navigation::setDirty(bool dirty) {
  if (m_dirty == dirty)
    return;
  m_dirty = dirty;
  emit changed();
}
void Navigation::request(const QString &command, const QUrl &file) {
  if (command.isEmpty() || pending())
    return;
  if (!m_dirty) {
    emit proceed(command, file);
    return;
  }
  m_command = command;
  m_file = file;
  emit changed();
  emit confirmationRequested();
}
void Navigation::cancel() {
  if (!pending() || m_saving)
    return;
  m_command.clear();
  m_file = QUrl();
  emit changed();
}
void Navigation::discard() {
  if (pending() && !m_saving)
    continueRequest();
}
void Navigation::save() {
  if (!pending() || m_saving)
    return;
  m_saving = true;
  emit changed();
  emit saveRequested();
}
void Navigation::saveSucceeded() {
  if (m_saving)
    continueRequest();
}
void Navigation::saveFailed() {
  if (!m_saving)
    return;
  m_saving = false;
  emit changed();
  emit confirmationRequested();
}
void Navigation::continueRequest() {
  const QString command = std::exchange(m_command, {});
  const QUrl file = std::exchange(m_file, {});
  m_saving = false;
  emit changed();
  emit proceed(command, file);
}
