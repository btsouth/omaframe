#include "shortcuts.hpp"
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThread>
#include <QtConcurrent>

namespace {
constexpr int altMask = 8;
const char *startMarker = "-- omaframe:shortcuts:start";
const char *endMarker = "-- omaframe:shortcuts:end";
const char *legacyStart = "-- omaframe:recording-shortcut:start";
const char *legacyEnd = "-- omaframe:recording-shortcut:end";

QString description(Shortcuts::Action action) {
  return action == Shortcuts::Action::Screenshot ? "Screenshot with Omaframe"
                                                 : "Record with Omaframe";
}
QString stockDescription(Shortcuts::Action action) {
  return action == Shortcuts::Action::Screenshot ? "Screenshot"
                                                 : "Screenrecording";
}
int defaultMask(Shortcuts::Action action) {
  return action == Shortcuts::Action::Screenshot ? 0 : altMask;
}
bool isDefaultKey(const QJsonObject &bind, Shortcuts::Action action) {
  return bind.value("key").toString().compare("Print", Qt::CaseInsensitive) ==
             0 &&
         bind.value("modmask").toInt() == defaultMask(action) &&
         bind.value("submap").toString().isEmpty();
}
QByteArray hyprctl(const QStringList &args, int timeout = 2500) {
  QProcess p;
  p.start("hyprctl", args);
  if (!p.waitForFinished(timeout)) {
    p.kill();
    p.waitForFinished();
    return {};
  }
  return p.exitCode() == 0 ? p.readAllStandardOutput() : QByteArray();
}
QString luaString(QString value) {
  return "\"" + value.replace("\\", "\\\\").replace("\"", "\\\"") + "\"";
}
/** Removes Omaframe's marked blocks, reporting which actions they bound.
 *  Returns false, changing nothing, when a block has no end marker: guessing
 *  where it ends could remove the user's own lines. */
bool withoutBlocks(QByteArray &text, QList<Shortcuts::Action> *found) {
  for (const auto &[start, end] :
       {std::pair{startMarker, endMarker}, std::pair{legacyStart, legacyEnd}})
    for (qsizetype from = text.indexOf(start); from >= 0;
         from = text.indexOf(start, from + 1))
      if (text.indexOf(end, from) < 0)
        return false;
  for (const auto &[start, end] :
       {std::pair{startMarker, endMarker}, std::pair{legacyStart, legacyEnd}}) {
    qsizetype from;
    while ((from = text.indexOf(start)) >= 0) {
      qsizetype to = text.indexOf(end, from) + qstrlen(end);
      if (to < text.size() && text.at(to) == '\n')
        ++to;
      const QByteArray block = text.mid(from, to - from);
      for (auto action :
           {Shortcuts::Action::Screenshot, Shortcuts::Action::Record})
        if (block.contains(description(action).toUtf8()) &&
            !found->contains(action))
          found->append(action);
      // Drop the blank line that introduced the block too.
      if (from > 0 && text.at(from - 1) == '\n' &&
          (from < 2 || text.at(from - 2) == '\n'))
        --from;
      text.remove(from, to - from);
    }
  }
  return true;
}
} // namespace

QString Shortcuts::keyLabel(const QJsonObject &bind) {
  const int mask = bind.value("modmask").toInt();
  QStringList parts;
  if (mask & 64)
    parts << "Super";
  if (mask & 4)
    parts << "Ctrl";
  if (mask & 8)
    parts << "Alt";
  if (mask & 1)
    parts << "Shift";
  QString key = bind.value("key").toString();
  if (key.size() == 1)
    key = key.toUpper();
  else if (!key.isEmpty())
    key = key.left(1).toUpper() + key.mid(1).toLower();
  parts << key;
  return parts.join('+');
}

bool Shortcuts::runsOmaframe(const QJsonObject &bind, Action action) {
  const QString dispatcher = bind.value("dispatcher").toString();
  // Omarchy's o.bind/o.rebind use Hyprland's Lua dispatcher. hyprctl shows an
  // opaque callback ID for those, but keeps the binding's description.
  if (dispatcher == "__lua")
    return bind.value("description")
               .toString()
               .compare(description(action), Qt::CaseInsensitive) == 0;
  if (dispatcher != "exec" && dispatcher != "execr")
    return false;
  static const QRegularExpression record(
      R"((?:^|[\s/'"])omaframe['"]?\s+--(?:record|stop-recording)(?:['"\s;&|]|$))");
  static const QRegularExpression screenshot(
      R"((?:^|[\s/'"])omaframe['"]?(?:\s+--(?:capture|screen|repeat))?\s*(?:['";&|]|$))");
  return (action == Action::Record ? record : screenshot)
      .match(bind.value("arg").toString())
      .hasMatch();
}

QString Shortcuts::omaframeKey(const QJsonArray &binds, Action action) {
  QString other;
  for (const auto &value : binds) {
    const auto bind = value.toObject();
    if (!bind.value("submap").toString().isEmpty() ||
        !runsOmaframe(bind, action))
      continue;
    if (isDefaultKey(bind, action))
      return keyLabel(bind);
    if (other.isEmpty())
      other = keyLabel(bind);
  }
  return other;
}

QString Shortcuts::defaultKey(Action action) {
  return action == Action::Screenshot ? "Print" : "Alt+Print";
}

QString Shortcuts::defaultKeyState(const QJsonArray &binds, Action action) {
  bool any = false, stock = true;
  for (const auto &value : binds) {
    const auto bind = value.toObject();
    if (!isDefaultKey(bind, action))
      continue;
    any = true;
    if (runsOmaframe(bind, action))
      return "omaframe";
    stock &= bind.value("dispatcher").toString() == "__lua" &&
             bind.value("description").toString() == stockDescription(action);
  }
  return !any ? "none" : stock ? "stock" : "custom";
}

QString Shortcuts::command(Action action, const QString &executable) {
  const QString onPath = QStandardPaths::findExecutable("omaframe");
  QString program;
  if (!onPath.isEmpty() && QFileInfo(onPath).canonicalFilePath() ==
                               QFileInfo(executable).canonicalFilePath())
    program = "omaframe";
  else
    program = "'" + QString(executable).replace("'", "'\\''") + "'";
  return program + (action == Action::Screenshot ? " --capture" : " --record");
}

QString Shortcuts::luaLine(Action action, const QString &executable) {
  return QString("o.rebind(%1, %2, %3)")
      .arg(luaString(action == Action::Screenshot ? "PRINT" : "ALT + PRINT"),
           luaString(description(action)),
           luaString(command(action, executable)));
}

bool Shortcuts::install(const QString &configDir, const QString &executable,
                        const QList<Action> &actions, QString *error,
                        QString *backup) {
  auto fail = [&](const QString &reason) {
    if (error)
      *error = reason;
    return false;
  };
  QFile startup(configDir + "/hyprland.lua");
  if (!startup.open(QIODevice::ReadOnly) ||
      !QRegularExpression(R"(require\s*\(?\s*["']hypr\.bindings["'])")
           .match(QString::fromUtf8(startup.readAll()))
           .hasMatch())
    return fail("This Hyprland setup does not load hypr/bindings.lua, so "
                "Omaframe cannot add its shortcuts. Bind them yourself.");
  const QString path = configDir + "/bindings.lua";
  QFile source(path);
  if (!source.open(QIODevice::ReadOnly))
    return fail("Cannot open hypr/bindings.lua.");
  const QByteArray original = source.readAll();
  source.close();
  QList<Action> wanted;
  QByteArray kept = original;
  if (!withoutBlocks(kept, &wanted))
    return fail("Omaframe's block in hypr/bindings.lua is missing its end "
                "line, so it was left alone. Delete the block and try again.");
  for (Action action : actions) {
    const QRegularExpression custom(
        action == Action::Screenshot
            ? R"((?:o\.(?:re)?bind|hl\.bind)\s*\(\s*["']\s*PRINT\s*["'])"
            : R"((?:o\.(?:re)?bind|hl\.bind)\s*\(\s*["']\s*ALT\s*\+\s*PRINT\s*["'])",
        QRegularExpression::CaseInsensitiveOption);
    if (custom.match(QString::fromUtf8(kept)).hasMatch())
      return fail(defaultKey(action) +
                  " already has a custom binding. Omaframe left it alone.");
    if (!wanted.contains(action))
      wanted.append(action);
  }
  if (!QFileInfo(executable).isExecutable())
    return fail("The Omaframe executable is unavailable for the shortcut.");
  while (kept.endsWith("\n\n"))
    kept.chop(1);
  if (!kept.isEmpty() && !kept.endsWith('\n'))
    kept += '\n';
  QByteArray block = QByteArray("\n") + startMarker +
                     "\n-- Added by Omaframe. Delete this block to restore "
                     "Omarchy's defaults.\n";
  for (Action action : {Action::Screenshot, Action::Record})
    if (wanted.contains(action))
      block += luaLine(action, executable).toUtf8() + '\n';
  block += QByteArray(endMarker) + '\n';
  const QByteArray updated = kept + block;
  if (updated == original)
    return true;
  const QString copy = path + ".bak.omaframe-" +
                       QDateTime::currentDateTimeUtc().toString(
                           "yyyyMMdd-hhmmsszzz");
  if (!QFile::copy(path, copy))
    return fail("Could not back up hypr/bindings.lua.");
  if (backup)
    *backup = copy;
  QSaveFile output(path);
  if (!output.open(QIODevice::WriteOnly) ||
      output.write(updated) != updated.size() || !output.commit())
    return fail("Could not save the Omaframe shortcuts.");
  return true;
}

ShortcutSetup::ShortcutSetup(QObject *parent) : QObject(parent) {}

bool ShortcutSetup::canSetUp() const {
  auto free = [](const QString &state) {
    return state == "stock" || state == "none";
  };
  return m_available && !m_checking &&
         ((m_screenshotKey.isEmpty() && free(m_screenshotState)) ||
          (m_recordKey.isEmpty() && free(m_recordState)));
}

void ShortcutSetup::refresh() { run({}); }

void ShortcutSetup::setUp() {
  QList<Shortcuts::Action> actions;
  auto free = [](const QString &state) {
    return state == "stock" || state == "none";
  };
  if (m_screenshotKey.isEmpty() && free(m_screenshotState))
    actions << Shortcuts::Action::Screenshot;
  if (m_recordKey.isEmpty() && free(m_recordState))
    actions << Shortcuts::Action::Record;
  if (!actions.isEmpty())
    run(actions);
}

void ShortcutSetup::setUpRecording() {
  if (m_recordKey.isEmpty() &&
      (m_recordState == "stock" || m_recordState == "none"))
    run({Shortcuts::Action::Record});
}

void ShortcutSetup::run(const QList<Shortcuts::Action> &actions) {
  const int generation = ++m_generation;
  m_checking = true;
  if (!actions.isEmpty())
    m_message = "Setting up shortcuts…";
  emit changed();
  struct Result {
    QJsonArray binds;
    bool available = false;
    QString message;
  };
  auto *watcher = new QFutureWatcher<Result>(this);
  connect(watcher, &QFutureWatcher<Result>::finished, this,
          [this, watcher, generation, installing = !actions.isEmpty()] {
            const Result r = watcher->result();
            watcher->deleteLater();
            if (generation != m_generation)
              return;
            m_checking = false;
            m_available = r.available;
            using Shortcuts::Action;
            m_screenshotKey = Shortcuts::omaframeKey(r.binds, Action::Screenshot);
            m_recordKey = Shortcuts::omaframeKey(r.binds, Action::Record);
            m_screenshotState =
                r.available ? Shortcuts::defaultKeyState(r.binds, Action::Screenshot)
                            : "unknown";
            m_recordState = r.available
                                ? Shortcuts::defaultKeyState(r.binds, Action::Record)
                                : "unknown";
            if (installing || !r.message.isEmpty())
              m_message = r.message;
            emit changed();
          });
  watcher->setFuture(QtConcurrent::run([actions] {
    Result r;
    auto binds = [] { return QJsonDocument::fromJson(hyprctl({"-j", "binds"})); };
    auto current = binds();
    r.available = current.isArray();
    r.binds = current.array();
    if (actions.isEmpty() || !r.available)
      return r;
    const QString configDir =
        QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) +
        "/hypr";
    const QString path = configDir + "/bindings.lua";
    QFile before(path);
    const QByteArray original =
        before.open(QIODevice::ReadOnly) ? before.readAll() : QByteArray();
    before.close();
    const QString executable = QCoreApplication::applicationFilePath();
    QString error, backup;
    if (!Shortcuts::install(configDir, executable, actions, &error, &backup)) {
      r.message = error;
      return r;
    }
    // `eval` activates Lua bindings now. `reload` can keep a cached
    // `require("hypr.bindings")` module in the running compositor.
    QStringList lines;
    for (auto action : actions)
      lines << Shortcuts::luaLine(action, executable);
    hyprctl({"eval", lines.join('\n')}, 5000);
    for (int attempt = 0; attempt < 10; ++attempt) {
      r.binds = binds().array();
      bool live = true;
      for (auto action : actions)
        live &= !Shortcuts::omaframeKey(r.binds, action).isEmpty();
      if (live) {
        QStringList keys;
        for (auto action : actions)
          keys << Shortcuts::defaultKey(action);
        r.message = keys.join(" and ") +
                    (keys.size() == 1 ? " now opens" : " now open") +
                    " Omaframe." +
                    (backup.isEmpty()
                         ? QString()
                         : " Your previous bindings.lua is saved as " +
                               QFileInfo(backup).fileName() + ".");
        return r;
      }
      QThread::msleep(100);
    }
    // Keep the old bindings if the new ones could not be verified live.
    QFile now(path);
    if (!backup.isEmpty() && now.open(QIODevice::ReadOnly)) {
      now.close();
      QSaveFile restore(path);
      if (restore.open(QIODevice::WriteOnly) &&
          restore.write(original) == original.size() && restore.commit())
        hyprctl({"reload"}, 5000);
    }
    r.binds = binds().array();
    r.message = "Hyprland did not accept the new shortcuts, so your bindings "
                "were left as they were.";
    return r;
  }));
}
