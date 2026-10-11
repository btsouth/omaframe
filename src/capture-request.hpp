#pragma once
#include <QCommandLineParser>
#include <QJsonObject>
#include <functional>
namespace CaptureRequest {
// -1 means no explicit delay; the optional shortcut resolves the preference
// before sending a normal request. CLI 0 is an explicitly immediate capture.
bool cliDelay(const QCommandLineParser &parser, int &seconds, QString &error);
// Whether one screenshot ends in the finish chooser. Default follows the
// Skip the finish chooser setting; --direct and --chooser override it.
enum class ChooserOverride { Default, Direct, Chooser };
bool skipsChooser(bool setting, ChooserOverride override);
// The request's name for an override: empty, "direct" or "chooser".
QString chooserName(ChooserOverride override);
bool parseChooser(const QString &name, ChooserOverride &override);
bool cliOverride(const QCommandLineParser &parser, ChooserOverride &override,
                 QString &error);
bool decode(const QJsonObject &request, QString &command, int &seconds,
            ChooserOverride &override);
enum class Handling { Proceed, Cancel, Busy };
Handling handle(const QString &command, bool delayed, bool busy);
// Recording controls always dispatch before screenshot cancellation/busy gates.
// The pause handler owns its asynchronous IPC reply.
bool dispatchControl(const QString &command, bool recordingActive,
                     std::function<void(const QString &)> pause,
                     std::function<void()> stop);
} // namespace CaptureRequest
