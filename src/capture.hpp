#pragma once
#include <QImage>
#include <QRect>
#include <QString>
#include <cstdint>
#include <memory>
struct MonitorInfo {
  QString name;
  QRect geometry;
  QSize pixelSize;
  qreal scale = 1;
  int workspaceId = 0;
};
class OutputCapture {
public:
  OutputCapture();
  ~OutputCapture();
  OutputCapture(const OutputCapture &) = delete;
  bool open(const QString &, QString &);
  bool grab(QImage &, QString &, int timeoutMs = 2000);
  bool isOpen() const;
  bool sessionStopped() const;
  QSize bufferSize() const;
  void close();

private:
  struct State;
  std::unique_ptr<State> state_;
};
bool captureOutputSurface(const MonitorInfo &, QImage &, QString &);
QImage normalizeWaylandCapture(const QImage &, std::uint32_t);
