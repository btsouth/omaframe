#pragma once
#include <QQueue>
#include <QQuickWindow>
#include <functional>

namespace CaptureDismissal {
inline const QString scope = QStringLiteral("omaframe-capture-countdown");
int frameWait(QScreen *screen);

// All compositor commands share this asynchronous, deadline-bound queue.
// Tests inject the process transport, but run the real dismissal sequence.
class Boundary final : public QObject {
public:
  struct Reply {
    bool success = false;
    QByteArray output;
  };
  using Done = std::function<void(bool)>;
  using Stop = std::function<void()>;
  using Transport =
      std::function<Stop(const QStringList &, std::function<void(Reply)>)>;
  explicit Boundary(QObject *parent = nullptr, Transport transport = {});
  void recover(Done done);
  void begin(quint64 generation, QScreen *screen, std::function<void()> hide,
             Done done);
  void placeBadge(quint64 generation, std::function<void(QScreen *)> done);
  void clear(quint64 generation, QQuickWindow *badge, Done done);
  void cancel(Done done);

private:
  struct Command {
    QStringList args;
    int deadline;
    std::function<void(Reply)> done;
    std::function<bool()> current;
  };
  void command(Command command);
  void next();
  void prepare(quint64 epoch, Done done);
  void restore(Done done);
  void desktopGone(quint64 epoch, int frames, Done done);
  void finish(quint64 epoch, bool success, Done done);
  void afterFrames(quint64 epoch, int frames, std::function<void()> done);
  bool current(quint64 epoch) const { return epoch == m_epoch; }
  QString rule(const QString &suffix) const;
  Transport m_transport;
  QQueue<Command> m_commands;
  Stop m_abort;
  QString m_ruleId;
  quint64 m_epoch = 0, m_generation = 0;
  bool m_running = false;
};
} // namespace CaptureDismissal
