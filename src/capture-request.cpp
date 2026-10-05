#include "capture-request.hpp"
#include <QRegularExpression>
#include <cmath>
bool CaptureRequest::cliDelay(const QCommandLineParser &parser, int &seconds,
                              QString &error) {
  seconds = -1;
  if (!parser.isSet("delay")) return true;
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
  for (const auto *mode : {"studio", "screen", "repeat", "scroll", "record",
                          "stop-recording", "pause-recording", "resume-recording",
                          "toggle-recording-pause"})
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
bool CaptureRequest::decode(const QJsonObject &request, QString &command,
                            int &seconds) {
  command = request.value("command").toString();
  if (!QStringList{"capture", "screen", "repeat", "scroll", "record", "studio",
                   "open", "stop-recording", "pause-recording", "resume-recording",
                   "toggle-recording-pause"}.contains(command))
    return false;
  seconds = -1;
  if (!request.contains("delaySeconds")) return true;
  const auto delay = request.value("delaySeconds");
  const double n = delay.toDouble(-2);
  if (!delay.isDouble() || n < -1 || n > 30 || n != std::floor(n) ||
      (command != "capture" && n != -1)) return false;
  seconds = int(n);
  return true;
}
CaptureRequest::Handling CaptureRequest::handle(const QString &command,
                                               bool delayed, bool busy) {
  if (delayed)
    return command == "capture" || command == "screen" || command == "repeat"
               ? Handling::Cancel : Handling::Busy;
  return busy ? Handling::Busy : Handling::Proceed;
}
