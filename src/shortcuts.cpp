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
#include <QUuid>
#include <QtConcurrent>

namespace {
constexpr int altMask = 8;
const char *startMarker = "-- omaframe:shortcuts:start";
const char *endMarker = "-- omaframe:shortcuts:end";
const char *legacyStart = "-- omaframe:recording-shortcut:start";
const char *legacyEnd = "-- omaframe:recording-shortcut:end";

QString description(Shortcuts::Action action) {
  if (action == Shortcuts::Action::Delay)
    return "Delayed screenshot with Omaframe";
  if (action == Shortcuts::Action::Pause)
    return "Pause recording with Omaframe";
  return action == Shortcuts::Action::Screenshot ? "Screenshot with Omaframe"
                                                 : "Record with Omaframe";
}
QString stockDescription(Shortcuts::Action action) {
  return (action == Shortcuts::Action::Screenshot ||
          action == Shortcuts::Action::Delay)
             ? "Screenshot"
             : "Screenrecording";
}
int defaultMask(Shortcuts::Action action) {
  return action == Shortcuts::Action::Delay        ? 1
         : action == Shortcuts::Action::Screenshot ? 0
         : action == Shortcuts::Action::Pause      ? altMask | 1
                                                   : altMask;
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
    p.waitForFinished(100);
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
           {Shortcuts::Action::Screenshot, Shortcuts::Action::Record,
            Shortcuts::Action::Pause, Shortcuts::Action::Delay})
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
  // Parse the supported capture invocation rather than assuming option order.
  // Split shell command segments, then use Qt's quote-aware argument splitter.
  if (action == Action::Delay) {
    const auto commands = bind.value("arg").toString().split(
        QRegularExpression("[;&|]+"), Qt::SkipEmptyParts);
    for (const auto &command : commands) {
      auto args = QProcess::splitCommand(command.trimmed());
      if (!args.isEmpty() && args.first() == "exec")
        args.removeFirst();
      if (!args.isEmpty() && args.first() == "env")
        args.removeFirst();
      while (!args.isEmpty() && QRegularExpression("^[A-Za-z_][A-Za-z0-9_]*=")
                                    .match(args.first())
                                    .hasMatch())
        args.removeFirst();
      if (args.isEmpty())
        continue;
      QString executable = args.takeFirst();
      if (executable.startsWith('\'') && executable.endsWith('\''))
        executable = executable.mid(1, executable.size() - 2);
      if (QFileInfo(executable).fileName() != "omaframe")
        continue;
      bool delayed = false, valid = true;
      for (int i = 0; i < args.size(); ++i) {
        const auto arg = args[i];
        if (arg == "--capture")
          continue;
        if (arg == "--delayed-capture") {
          if (delayed)
            valid = false;
          delayed = true;
          continue;
        }
        QString seconds;
        if (arg == "--delay" && i + 1 < args.size())
          seconds = args[++i];
        else if (arg.startsWith("--delay="))
          seconds = arg.mid(8);
        else {
          valid = false;
          continue;
        }
        bool ok;
        const int n = seconds.toInt(&ok);
        valid &= !delayed && ok && n >= 0 && n <= 30 &&
                 QRegularExpression("^[0-9]+$").match(seconds).hasMatch();
        delayed = true;
      }
      if (delayed && valid)
        return true;
    }
    return false;
  }
  static const QRegularExpression pause(
      R"((?:^|[\s/'"])omaframe['"]?\s+--(?:toggle-recording-pause|pause-recording|resume-recording)(?:['"\s;&|]|$))");
  return (action == Action::Record  ? record
          : action == Action::Pause ? pause
                                    : screenshot)
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
  return action == Action::Delay        ? "Shift+Print"
         : action == Action::Screenshot ? "Print"
         : action == Action::Pause      ? "Alt+Shift+Print"
                                        : "Alt+Print";
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
    stock &= action != Action::Pause &&
             bind.value("dispatcher").toString() == "__lua" &&
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
  return program + (action == Action::Delay        ? " --delayed-capture"
                    : action == Action::Screenshot ? " --capture"
                    : action == Action::Pause      ? " --toggle-recording-pause"
                                                   : " --record");
}

QString Shortcuts::luaLine(Action action, const QString &executable) {
  return QString("hl.unbind(%1)\no.bind(%1, %2, %3)")
      .arg(luaString(action == Action::Delay        ? "SHIFT + PRINT"
                     : action == Action::Screenshot ? "PRINT"
                     : action == Action::Pause      ? "ALT + SHIFT + PRINT"
                                                    : "ALT + PRINT"),
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
  for (Action action : actions)
    if (!wanted.contains(action))
      wanted.append(action);
  // Recovered actions will also be regenerated after the user's lines.
  // Drop any they have overridden since the previous setup.
  for (Action action : QList<Action>(wanted)) {
    const QRegularExpression custom(
        action == Action::Delay
            ? R"((?:o\.(?:re)?bind|hl\.bind)\s*\(\s*["']\s*SHIFT\s*\+\s*PRINT\s*["'])"
        : action == Action::Screenshot
            ? R"((?:o\.(?:re)?bind|hl\.bind)\s*\(\s*["']\s*PRINT\s*["'])"
        : action == Action::Pause
            ? R"((?:o\.(?:re)?bind|hl\.bind)\s*\(\s*["']\s*(?:ALT\s*\+\s*SHIFT|SHIFT\s*\+\s*ALT)\s*\+\s*PRINT\s*["'])"
            : R"((?:o\.(?:re)?bind|hl\.bind)\s*\(\s*["']\s*ALT\s*\+\s*PRINT\s*["'])",
        QRegularExpression::CaseInsensitiveOption);
    if (custom.match(QString::fromUtf8(kept)).hasMatch()) {
      if (actions.contains(action))
        return fail(defaultKey(action) +
                    " already has a custom binding. Omaframe left it alone.");
      wanted.removeAll(action);
    }
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
  for (Action action :
       {Action::Screenshot, Action::Record, Action::Pause, Action::Delay})
    if (wanted.contains(action))
      block += luaLine(action, executable).toUtf8() + '\n';
  block += QByteArray(endMarker) + '\n';
  const QByteArray updated = kept + block;
  if (updated == original)
    return true;
  const QString copy = path + ".bak.omaframe-" +
                       QDateTime::currentDateTimeUtc().toString(
                           "yyyyMMdd-hhmmsszzz") + "-" +
                       QUuid::createUuid().toString(QUuid::Id128);
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
          (m_recordKey.isEmpty() && free(m_recordState)) ||
          (m_pauseKey.isEmpty() && free(m_pauseState)));
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
  if (m_pauseKey.isEmpty() && free(m_pauseState))
    actions << Shortcuts::Action::Pause;
  if (!actions.isEmpty())
    run(actions);
}

void ShortcutSetup::setUpDelay() {
  if (m_available && !m_checking && m_delayKey.isEmpty() &&
      (m_delayState == "stock" || m_delayState == "none"))
    run({Shortcuts::Action::Delay});
}

void ShortcutSetup::setUpRecording() {
  QList<Shortcuts::Action> actions;
  if (m_recordKey.isEmpty() &&
      (m_recordState == "stock" || m_recordState == "none"))
    actions << Shortcuts::Action::Record;
  if (m_pauseKey.isEmpty() && m_pauseState == "none")
    actions << Shortcuts::Action::Pause;
  if (!actions.isEmpty())
    run(actions);
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
            m_delayKey = Shortcuts::omaframeKey(r.binds, Action::Delay);
            m_delayState =
                r.available ? Shortcuts::defaultKeyState(r.binds, Action::Delay)
                            : "unknown";
            m_screenshotKey = Shortcuts::omaframeKey(r.binds, Action::Screenshot);
            m_recordKey = Shortcuts::omaframeKey(r.binds, Action::Record);
            m_pauseKey = Shortcuts::omaframeKey(r.binds, Action::Pause);
            m_screenshotState =
                r.available ? Shortcuts::defaultKeyState(r.binds, Action::Screenshot)
                            : "unknown";
            m_recordState = r.available
                                ? Shortcuts::defaultKeyState(r.binds, Action::Record)
                                : "unknown";
            m_pauseState =
                r.available ? Shortcuts::defaultKeyState(r.binds, Action::Pause)
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
    // Check the running compositor before touching the user's config. Older
    // Omarchy has o.bind and hl.unbind, but no o.rebind helper.
    const auto capabilities = hyprctl({"eval",
        "assert(type(hl.unbind) == 'function' and type(o.bind) == 'function', "
        "'Omaframe shortcut API unavailable')"});
    if (capabilities.trimmed() != "ok") {
      r.message = "This Hyprland setup does not support automatic shortcut "
                  "setup. Your bindings were left unchanged.";
      return r;
    }
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
        r.message = "Capture shortcuts are ready." +
                    (backup.isEmpty() ? QString()
                                      : " Previous bindings were backed up.");
        return r;
      }
      QThread::msleep(100);
    }
    // Keep the old bindings if the new ones could not be verified live. An
    // empty backup means install found nothing to change.
    const bool unchanged = backup.isEmpty();
    bool restored = unchanged;
    if (!unchanged) {
      QSaveFile restore(path);
      restored = restore.open(QIODevice::WriteOnly) &&
                 restore.write(original) == original.size() && restore.commit();
    }
    const bool reloaded =
        restored && hyprctl({"reload"}, 5000).trimmed() == "ok";
    r.binds = binds().array();
    r.message = unchanged ? "Hyprland did not accept the shortcuts. Your bindings "
                            "file was left unchanged."
                : restored ? "Hyprland did not accept the new shortcuts. Your "
                             "bindings file was restored."
                           : "Hyprland did not accept the new shortcuts, and "
                             "Omaframe could not restore your bindings file.";
    if (restored && !reloaded)
      r.message += unchanged ? " Hyprland could not reload your bindings."
                             : " Hyprland could not reload the restored bindings.";
    if (!unchanged)
      r.message += " Previous bindings: " + backup;
    return r;
  }));
}
