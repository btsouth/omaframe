#include "capture-request.hpp"
#include <QRegularExpression>
#include <cmath>
bool CaptureRequest::cliDelay(const QCommandLineParser &parser, int &seconds,
                              QString &error) {
  seconds = -1;
  if (!parser.isSet("delay"))
    return true;
  const QString value = parser.value("delay");
  if (!QRegularExpression("^[0-9]+$").match(value).hasMatch()) {
    error = "--delay needs integer seconds from 0 to 30.";
    return false;
  }
  bool ok;
  seconds = value.toInt(&ok);
  if (!ok || seconds > 30) {
    error = "--delay needs integer seconds from 0 to 30.";
    return false;
  }
  for (const auto *mode :
       {"studio", "history", "screen", "repeat", "scroll", "record", "stop-recording",
        "pause-recording", "resume-recording", "toggle-recording-pause"})
    if (parser.isSet(mode)) {
      error = "--delay is available with default capture or --capture only.";
      return false;
    }
  if (!parser.positionalArguments().isEmpty()) {
    error = "--delay cannot open a file.";
    return false;
  }
  return true;
}
bool CaptureRequest::skipsChooser(bool setting, ChooserOverride override) {
  return override == ChooserOverride::Direct ||
         (override == ChooserOverride::Default && setting);
}
QString CaptureRequest::chooserName(ChooserOverride override) {
  return override == ChooserOverride::Direct    ? "direct"
         : override == ChooserOverride::Chooser ? "chooser"
                                                : QString();
}
bool CaptureRequest::parseChooser(const QString &name,
                                  ChooserOverride &override) {
  if (name.isEmpty())
    override = ChooserOverride::Default;
  else if (name == "direct")
    override = ChooserOverride::Direct;
  else if (name == "chooser")
    override = ChooserOverride::Chooser;
  else
    return false;
  return true;
}
bool CaptureRequest::cliOverride(const QCommandLineParser &parser,
                                 ChooserOverride &override, QString &error) {
  override = ChooserOverride::Default;
  const bool direct = parser.isSet("direct"), chooser = parser.isSet("chooser");
  if (!direct && !chooser)
    return true;
  const QString flag = direct ? "--direct" : "--chooser";
  if (direct && chooser) {
    error = "--direct and --chooser cannot be used together.";
    return false;
  }
  for (const auto *mode :
       {"studio", "history", "scroll", "record", "stop-recording",
        "pause-recording", "resume-recording", "toggle-recording-pause"})
    if (parser.isSet(mode)) {
      error = flag + " is available with default capture, --capture, "
                     "--screen or --repeat only.";
      return false;
    }
  if (!parser.positionalArguments().isEmpty()) {
    error = flag + " cannot open a file.";
    return false;
  }
  override = direct ? ChooserOverride::Direct : ChooserOverride::Chooser;
  return true;
}
bool CaptureRequest::decode(const QJsonObject &request, QString &command,
                            int &seconds, ChooserOverride &override) {
  command = request.value("command").toString();
  if (!QStringList{"capture", "screen", "repeat", "scroll", "record", "studio", "history",
                   "open", "stop-recording", "pause-recording",
                   "resume-recording", "toggle-recording-pause"}
           .contains(command))
    return false;
  override = ChooserOverride::Default;
  if (request.contains("chooser")) {
    const auto chooser = request.value("chooser");
    if (!chooser.isString() || chooser.toString().isEmpty() ||
        !parseChooser(chooser.toString(), override) ||
        (command != "capture" && command != "screen" && command != "repeat"))
      return false;
  }
  seconds = -1;
  if (!request.contains("delaySeconds"))
    return true;
  const auto delay = request.value("delaySeconds");
  const double n = delay.toDouble(-2);
  if (!delay.isDouble() || n < -1 || n > 30 || n != std::floor(n) ||
      (command != "capture" && n != -1))
    return false;
  seconds = int(n);
  return true;
}
CaptureRequest::Handling CaptureRequest::handle(const QString &command,
                                                bool delayed, bool busy) {
  if (delayed)
    return command == "capture" || command == "screen" || command == "repeat"
               ? Handling::Cancel
               : Handling::Busy;
  return busy ? Handling::Busy : Handling::Proceed;
}

bool CaptureRequest::dispatchControl(const QString &command,
                                     bool recordingActive,
                                     std::function<void(const QString &)> pause,
                                     std::function<void()> stop) {
  if (command == "pause-recording" || command == "resume-recording" ||
      command == "toggle-recording-pause") {
    pause(command);
    return true;
  }
  if (command == "stop-recording" || (command == "record" && recordingActive)) {
    stop();
    return true;
  }
  return false;
}
