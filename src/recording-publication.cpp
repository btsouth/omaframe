#include "recording.hpp"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTemporaryFile>
#include <QUuid>
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace {
bool renameFile(const QString &from, const QString &to) {
  return ::syscall(SYS_renameat2, AT_FDCWD, QFile::encodeName(from).constData(),
                   AT_FDCWD, QFile::encodeName(to).constData(),
                   RENAME_NOREPLACE) == 0;
}
bool openByRecorder(const QString &path) {
  struct stat expected{};
  if (::stat(QFile::encodeName(path).constData(), &expected) != 0)
    return true;
  const auto processes =
      QDir("/proc").entryList(QDir::Dirs | QDir::NoDotAndDotDot);
  for (const auto &pid : processes) {
    bool numeric = false;
    pid.toLongLong(&numeric);
    if (!numeric)
      continue;
    QFile cmd("/proc/" + pid + "/cmdline");
    QByteArray commandLine;
    if (cmd.open(QIODevice::ReadOnly))
      commandLine = cmd.readAll();
    QFile comm("/proc/" + pid + "/comm");
    QByteArray processName;
    if (comm.open(QIODevice::ReadOnly))
      processName = comm.readAll().trimmed();
    const auto args = commandLine.split('\0');
    if (!processName.startsWith("gpu-screen-reco") &&
        (args.isEmpty() || !QFileInfo(QString::fromUtf8(args.first()))
                                .fileName()
                                .contains("gpu-screen-recorder")))
      continue;
    const QDir fds("/proc/" + pid + "/fd");
    if (!fds.isReadable())
      return true;
    for (const auto &fd :
         fds.entryList(QDir::AllEntries | QDir::NoDotAndDotDot)) {
      struct stat s{};
      if (::stat(QFile::encodeName(fds.filePath(fd)).constData(), &s) == 0 &&
          s.st_dev == expected.st_dev && s.st_ino == expected.st_ino)
        return true;
    }
    // An -o argument also protects the startup gap before the output is opened.
    if (args.contains(path.toUtf8()))
      return true;
  }
  return false;
}
} // namespace
QString Recording::partPath(const QString &final) {
  const QFileInfo file(final);
  return file.dir().filePath('.' + file.completeBaseName() + ".part.mp4");
}
QString Recording::publishPart(const QString &part, const QString &final,
                               bool incomplete) {
  if (!QFileInfo(part).isFile() || QFileInfo(part).isSymLink())
    return {};
  const QString destination =
      incomplete ? final.left(final.size() - 4) + "-incomplete.mp4" : final;
  if (QFileInfo::exists(destination))
    return {};
  const QString camera = part + "-webcam.mp4", sidecar = part + ".camera.json";
  const QString newCamera = destination + "-webcam.mp4",
                newSidecar = destination + ".camera.json";
  if (incomplete) {
    // Recovery publishes the screen even when an auxiliary is damaged.
    if (!renameFile(part, destination))
      return {};
    auto preserve = [](const QString &from, const QString &to) {
      if (renameFile(from, to))
        return to;
      const auto separate =
          to + ".damaged-" + QUuid::createUuid().toString(QUuid::Id128);
      return renameFile(from, separate) ? separate : QString();
    };
    QString keptCamera;
    if (QFileInfo::exists(camera))
      keptCamera = preserve(camera, newCamera);
    if (QFileInfo::exists(sidecar)) {
      QFile input(sidecar);
      QJsonDocument metadata;
      if (!QFileInfo(sidecar).isSymLink() && input.open(QIODevice::ReadOnly) &&
          input.size() <= 64 * 1024)
        metadata = QJsonDocument::fromJson(input.readAll());
      input.close();
      if (!metadata.isObject() || metadata.object().value("version") != 1) {
        preserve(sidecar, newSidecar + ".damaged");
      } else {
        auto doc = metadata.object();
        if (!keptCamera.isEmpty())
          doc.insert("file", QFileInfo(keptCamera).fileName());
        // Keep the original until the rewritten sidecar is safely published.
        QTemporaryFile output(
            QFileInfo(final).dir().filePath(".omaframe-camera-XXXXXX"));
        const auto bytes = QJsonDocument(doc).toJson(QJsonDocument::Compact);
        if (output.open() &&
            output.setPermissions(QFile::ReadOwner | QFile::WriteOwner) &&
            output.write(bytes) == bytes.size() && output.flush() &&
            renameFile(output.fileName(), newSidecar))
          QFile::remove(sidecar);
        else
          preserve(sidecar, newSidecar + ".damaged");
      }
    }
    QFile::remove(part + ".ts");
    return destination;
  }
  bool movedCamera = false, movedSidecar = false;
  auto rollback = [&] {
    if (movedCamera)
      renameFile(newCamera, camera);
    if (movedSidecar)
      QFile::remove(newSidecar);
  };
  if (QFileInfo::exists(camera)) {
    if (QFileInfo(camera).isSymLink() || !renameFile(camera, newCamera))
      return {};
    movedCamera = true;
  }
  if (QFileInfo::exists(sidecar)) {
    QFile input(sidecar);
    if (QFileInfo(sidecar).isSymLink() || !input.open(QIODevice::ReadOnly) ||
        input.size() > 64 * 1024) {
      rollback();
      return {};
    }
    auto doc = QJsonDocument::fromJson(input.readAll()).object();
    input.close();
    if (doc.value("version").toInt() != 1) {
      if (!incomplete) {
        rollback();
        return {};
      }
      doc = QJsonObject{{"version", 1}, {"missing", true}};
    }
    if (movedCamera)
      doc.insert("file", QFileInfo(newCamera).fileName());
    QTemporaryFile output(
        QFileInfo(final).dir().filePath(".omaframe-camera-XXXXXX"));
    const auto bytes = QJsonDocument(doc).toJson(QJsonDocument::Compact);
    if (!output.open() ||
        !output.setPermissions(QFile::ReadOwner | QFile::WriteOwner) ||
        output.write(bytes) != bytes.size() || !output.flush()) {
      rollback();
      return {};
    }
    const auto temporary = output.fileName();
    output.close();
    if (!renameFile(temporary, newSidecar)) {
      rollback();
      return {};
    }
    movedSidecar = true;
  }
  if (!renameFile(part, destination)) {
    rollback();
    return {};
  }
  if (movedSidecar)
    QFile::remove(sidecar);
  QFile::remove(part + ".ts");
  return destination;
}
void Recording::recoverParts(const QString &folder, qint64 minimumAgeSeconds) {
  const QFileInfo directory(folder);
  if (directory.canonicalFilePath() !=
      QDir::cleanPath(directory.absoluteFilePath()))
    return;
  static const QRegularExpression pattern(
      "^\\.(Recording-[0-9]{4}-[0-9]{2}-[0-9]{2}_[0-9]{2}-[0-9]{2}-[0-9]{2}-[0-"
      "9a-f]{6})\\.part\\.mp4$");
  for (const auto &file : QDir(folder).entryInfoList(
           QDir::Files | QDir::Hidden | QDir::NoSymLinks)) {
    const auto match = pattern.match(file.fileName());
    if (!match.hasMatch() ||
        file.lastModified().secsTo(QDateTime::currentDateTime()) <
            minimumAgeSeconds ||
        openByRecorder(file.absoluteFilePath()))
      continue;
    auto final = file.dir().filePath(match.captured(1) + ".mp4");
    if (QFileInfo::exists(final.left(final.size() - 4) + "-incomplete.mp4"))
      final = file.dir().filePath(
          match.captured(1) + "-" +
          QUuid::createUuid().toString(QUuid::Id128).left(6) + ".mp4");
    publishPart(file.absoluteFilePath(), final, true);
  }
}
