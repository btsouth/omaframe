#include "edit-json.hpp"
#include "video.hpp"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>
#include <utility>

namespace {
QString directory() {
  return QStandardPaths::writableLocation(
             QStandardPaths::AppLocalDataLocation) +
         "/video-drafts";
}
bool validId(const QString &id) {
  static const QRegularExpression format("^[0-9a-f]{64}$");
  return format.match(id).hasMatch();
}
QJsonObject read(const QString &path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly) || file.size() > 2 * 1024 * 1024)
    return {};
  return QJsonDocument::fromJson(file.readAll()).object();
}
QString idFor(const QUrl &source) {
  return QString::fromLatin1(
      QCryptographicHash::hash(source.toEncoded(), QCryptographicHash::Sha256)
          .toHex());
}
} // namespace

QRect Video::cropPixels() const {
  if (m_frameSize.isEmpty())
    return {};
  if (m_frameSize.width() < 2 || m_frameSize.height() < 2)
    return {QPoint(0, 0), m_frameSize};
  const QRectF bounds = m_marks.cropBounds();
  const int left = std::clamp(int(bounds.x() * m_frameSize.width()) / 2 * 2, 0,
                              std::max(0, m_frameSize.width() - 2));
  const int top = std::clamp(int(bounds.y() * m_frameSize.height()) / 2 * 2, 0,
                             std::max(0, m_frameSize.height() - 2));
  const int right =
      std::clamp(int(bounds.right() * m_frameSize.width()) / 2 * 2, left + 2,
                 m_frameSize.width());
  const int bottom =
      std::clamp(int(bounds.bottom() * m_frameSize.height()) / 2 * 2, top + 2,
                 m_frameSize.height());
  return {left, top, right - left, bottom - top};
}
QRectF Video::cropBounds() const {
  const auto pixels = cropPixels();
  if (pixels.isEmpty())
    return {0, 0, 1, 1};
  return {double(pixels.x()) / m_frameSize.width(),
          double(pixels.y()) / m_frameSize.height(),
          double(pixels.width()) / m_frameSize.width(),
          double(pixels.height()) / m_frameSize.height()};
}
bool Video::validEditState(const QVariantMap &state) const {
  const double start = state.value("clipStart").toDouble();
  const double end = state.value("clipEnd").toDouble();
  if (!std::isfinite(start) || !std::isfinite(end) || start < 0 ||
      end > m_duration + 0.05 || end - start < 0.1)
    return false;
  const auto cuts = state.value("cuts").toList();
  if (cuts.size() > 1000)
    return false;
  double previous = -1;
  for (const auto &value : cuts) {
    const auto cut = value.toMap();
    const double a = cut.value("start").toDouble(),
                 b = cut.value("end").toDouble();
    if (!std::isfinite(a) || !std::isfinite(b) || a < 0 ||
        b > m_duration + 0.05 || b - a < 0.099999 || a < previous)
      return false;
    previous = b;
  }
  return true;
}
void Video::setEditState(const QVariantMap &state) {
  if (m_busy || m_source.isEmpty() || !validEditState(state) ||
      m_editState == state)
    return;
  m_editState = state;
  m_draftDeleted = false;
  m_draftTimer.start();
}
void Video::recordSavedSignature(const QString &signature) {
  m_savedSignature = signature;
  saveDraftNow();
  emit changed();
}
bool Video::saveDraftNow() {
  m_draftTimer.stop();
  if (m_draftDeleted)
    return true;
  if (m_source.isEmpty() || !validEditState(m_editState))
    return false;
  const QString signature = m_editState.value("signature").toString();
  if (signature.isEmpty())
    return false;
  const bool edited =
      m_marks.hasCrop() || !m_marks.edits().isEmpty() ||
      m_editState.value("clipStart").toDouble() > 0.001 ||
      m_editState.value("clipEnd").toDouble() < m_duration - 0.001 ||
      m_editState.value("muted").toBool() ||
      !m_editState.value("cuts").toList().isEmpty() ||
      (!m_cameraSource.isEmpty() && m_cameraLayout.value("visible").toBool());
  m_draftId = idFor(m_source);
  if (!edited) {
    QFile::remove(directory() + "/" + m_draftId + ".json");
    m_draftSignature = signature;
    refreshDrafts();
    emit changed();
    return true;
  }
  const QFileInfo source(m_source.toLocalFile());
  if (!source.isFile() || source.size() != m_sourceSize ||
      source.lastModified().toMSecsSinceEpoch() != m_sourceModified ||
      !QDir().mkpath(directory())) {
    m_status =
        "Couldn't save the video draft. Keep the original video in place "
        "and check the application data folder.";
    emit changed();
    return false;
  }
  QFile::setPermissions(directory(),
                        QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
  QJsonArray edits;
  for (const auto &edit : m_marks.edits())
    edits.append(Frame::editToJson(edit));
  const QJsonObject document{
      {"version", 1},
      {"name", m_name},
      {"source", m_source.toString()},
      {"sourceSize", double(m_sourceSize)},
      {"sourceModified", double(m_sourceModified)},
      {"state", QJsonObject::fromVariantMap(m_editState)},
      {"edits", edits},
      {"cameraLayout", QJsonObject::fromVariantMap(m_cameraLayout)},
      {"savedPath", m_saved},
      {"savedSignature", m_savedSignature}};
  QSaveFile file(directory() + "/" + m_draftId + ".json");
  const auto bytes = QJsonDocument(document).toJson(QJsonDocument::Compact);
  if (!file.open(QIODevice::WriteOnly) ||
      !file.setPermissions(QFile::ReadOwner | QFile::WriteOwner) ||
      file.write(bytes) != bytes.size() || !file.commit()) {
    m_status =
        "Couldn't save the video draft. Check the application data folder.";
    emit changed();
    return false;
  }
  m_draftSignature = signature;
  refreshDrafts();
  emit changed();
  return true;
}
void Video::refreshDrafts() {
  QVariantList drafts;
  for (const auto &file :
       QDir(directory())
           .entryInfoList({"*.json"}, QDir::Files | QDir::NoSymLinks,
                          QDir::Time)) {
    const QString id = file.completeBaseName();
    if (!validId(id))
      continue;
    const auto doc = read(file.absoluteFilePath());
    if (doc.value("version").toInt() != 1)
      continue;
    drafts.append(QVariantMap{
        {"id", id},
        {"kind", "video"},
        {"name", doc.value("name").toString("Video")},
        {"when", file.lastModified().toString("MMM d, h:mm AP")},
        {"edits", doc.value("edits").toArray().size()},
        {"image", ""},
        {"exported", QFileInfo::exists(doc.value("savedPath").toString())}});
  }
  m_drafts = drafts;
  emit draftsChanged();
}
void Video::resumeDraft(const QString &id) {
  if (m_busy || !validId(id))
    return;
  const auto doc = read(directory() + "/" + id + ".json");
  const QUrl source(doc.value("source").toString());
  const QFileInfo info(source.toLocalFile());
  if (doc.value("version").toInt() != 1 || !source.isLocalFile() ||
      idFor(source) != id || !info.isFile() ||
      double(info.size()) != doc.value("sourceSize").toDouble() ||
      double(info.lastModified().toMSecsSinceEpoch()) !=
          doc.value("sourceModified").toDouble()) {
    m_status = "Couldn't reopen the draft. Its original video is missing or "
               "has changed. Keep original videos in place to resume edits.";
    emit changed();
    return;
  }
  m_pendingDraft = doc.toVariantMap();
  open(source);
}
void Video::restorePendingDraft() {
  if (m_pendingDraft.isEmpty())
    return;
  const auto doc =
      QJsonObject::fromVariantMap(std::exchange(m_pendingDraft, {}));
  const auto state = doc.value("state").toObject().toVariantMap();
  const auto encoded = doc.value("edits").toArray();
  QVector<Frame::Edit> edits;
  bool valid =
      validEditState(state) && encoded.size() <= MarkDocument::MaxEdits;
  for (const auto &value : encoded) {
    const auto edit = Frame::editFromJson(value.toObject());
    if (!edit || edit->start > m_duration || (edit->end > m_duration + 0.05)) {
      valid = false;
      break;
    }
    edits.append(*edit);
  }
  if (!valid) {
    m_status = "This video draft is damaged. The original video is open "
               "without edits.";
    return;
  }
  QImage frame(m_frameSize, QImage::Format_ARGB32_Premultiplied);
  frame.fill(Qt::transparent);
  m_marks.restore(frame, std::move(edits), -1);
  m_editState = state;
  setCameraLayout(doc.value("cameraLayout").toObject().toVariantMap());
  m_draftSignature = state.value("signature").toString();
  m_saved = doc.value("savedPath").toString();
  if (!QFileInfo::exists(m_saved))
    m_saved.clear();
  m_savedSignature =
      m_saved.isEmpty() ? QString() : doc.value("savedSignature").toString();
  m_status = "Video draft reopened. Your original video stays unchanged.";
  m_draftTimer.stop();
}
void Video::deleteDraft(const QString &id) {
  if (!validId(id) || m_busy)
    return;
  if (!QFile::remove(directory() + "/" + id + ".json")) {
    m_status = "Couldn't delete the video draft.";
    emit changed();
    return;
  }
  if (!m_source.isEmpty() && idFor(m_source) == id) {
    m_draftDeleted = true;
    m_draftSignature.clear();
    m_draftTimer.stop();
  }
  refreshDrafts();
}
void Video::setCameraLayout(const QVariantMap &layout) {
  if (m_busy)
    return;
  const double width = layout.value("width", 0.24).toDouble();
  const double x = layout.value("x", 0.74).toDouble(),
               y = layout.value("y", 0.72).toDouble();
  if (!std::isfinite(width) || !std::isfinite(x) || !std::isfinite(y))
    return;
  const double w = std::clamp(width, 0.12, 0.5);
  const double h =
      outputSize().isEmpty()
          ? w
          : w * outputSize().width() / outputSize().height() * 9.0 / 16;
  const QVariantMap next{{"visible", layout.value("visible", true).toBool()},
                         {"width", w},
                         {"x", std::clamp(x, 0., 1. - w)},
                         {"y", std::clamp(y, 0., std::max(0., 1. - h))}};
  if (next == m_cameraLayout)
    return;
  m_cameraLayout = next;
  emit changed();
}
QRectF Video::cameraBounds() const {
  const QSize size = outputSize();
  if (size.isEmpty())
    return {};
  const int width = std::max(
      2, int(std::min(
             {m_cameraLayout.value("width", 0.24).toDouble() * size.width(),
              double(size.width()), double(size.height()) * 16 / 9})) /
             2 * 2);
  const int height = std::max(2, int(width * 9.0 / 16) / 2 * 2);
  const int x =
      std::clamp(int(m_cameraLayout.value("x", 0.74).toDouble() * size.width()),
                 0, std::max(0, size.width() - width));
  const int y = std::clamp(
      int(m_cameraLayout.value("y", 0.72).toDouble() * size.height()), 0,
      std::max(0, size.height() - height));
  return {double(x) / size.width(), double(y) / size.height(),
          double(width) / size.width(), double(height) / size.height()};
}
void Video::readCameraTrack() {
  m_cameraSource = QUrl();
  m_cameraDuration = 0;
  m_cameraLayout = {
      {"visible", false}, {"width", 0.24}, {"x", 0.74}, {"y", 0.72}};
  const auto doc = read(m_source.toLocalFile() + ".camera.json");
  const QString name = doc.value("file").toString();
  // Sidecars can only refer to a sibling track, never an arbitrary path.
  if (doc.value("version").toInt() != 1 || name.isEmpty() ||
      name != QFileInfo(name).fileName())
    return;
  const QString path = QFileInfo(m_source.toLocalFile()).dir().filePath(name);
  if (!QFileInfo(path).isFile() || QFileInfo(path).isSymLink())
    return;
  const double duration = doc.value("duration").toDouble();
  if (!std::isfinite(duration) || duration <= 0 || duration > m_duration + 1)
    return;
  m_cameraSource = QUrl::fromLocalFile(path);
  m_cameraDuration = duration;
  m_cameraLayout["visible"] = true;
}
