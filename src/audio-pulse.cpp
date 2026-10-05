#include "audio-levels.hpp"
#include <QThread>
#include <pulse/pulseaudio.h>

namespace {
class PulseWorker final : public QObject {
  Q_OBJECT
signals:
  void samples(int channel, quint64 generation, const QList<float> &peaks);
  void unavailable(int channel, quint64 generation);
public:
  struct Channel {
    PulseWorker *owner;
    int index;
    QString name;
    pa_stream *stream = nullptr;
    pa_operation *operation = nullptr;
  } channels[2]{{this, 0, {}}, {this, 1, {}}};
  pa_threaded_mainloop *loop = nullptr;
  pa_context *context = nullptr;
  quint64 generation = 0;
  QTimer timeout;
  PulseWorker() {
    timeout.setSingleShot(true);
    connect(&timeout, &QTimer::timeout, this, [this] {
      for (auto &c : channels) if (!c.name.isEmpty() && !c.stream) report(c);
      // A context which never became ready gets one bounded attempt.
      if (context) {
        pa_threaded_mainloop_lock(loop);
        const bool ready = pa_context_get_state(context) == PA_CONTEXT_READY;
        pa_threaded_mainloop_unlock(loop);
        if (!ready) shutdown();
      }
    });
  }
  void report(const Channel &c) { emit unavailable(c.index, generation); }
  void shutdown() {
    timeout.stop();
    if (!loop) return;
    pa_threaded_mainloop_lock(loop);
    for (auto &c : channels) {
      if (c.operation) { pa_operation_cancel(c.operation); pa_operation_unref(c.operation); c.operation = nullptr; }
      if (c.stream) {
        pa_stream_set_read_callback(c.stream, nullptr, nullptr);
        pa_stream_set_state_callback(c.stream, nullptr, nullptr);
        pa_stream_disconnect(c.stream);
        pa_stream_unref(c.stream);
        c.stream = nullptr;
      }
    }
    if (context) {
      pa_context_set_state_callback(context, nullptr, nullptr);
      pa_context_disconnect(context);
      pa_context_unref(context);
      context = nullptr;
    }
    pa_threaded_mainloop_unlock(loop);
    // Stop joins Pulse callbacks before their userdata or mainloop is freed.
    pa_threaded_mainloop_stop(loop);
    pa_threaded_mainloop_free(loop);
    loop = nullptr;
  }
  void open(const AudioSnapshot &snapshot, quint64 nextGeneration) {
    shutdown();
    generation = nextGeneration;
    channels[0].name = snapshot.mic; channels[1].name = snapshot.sound;
    loop = pa_threaded_mainloop_new();
    if (!loop) { for (const auto &c : channels) if (!c.name.isEmpty()) report(c); return; }
    context = pa_context_new(pa_threaded_mainloop_get_api(loop), "Omaframe selected input meters");
    if (!context || pa_threaded_mainloop_start(loop) < 0) {
      for (const auto &c : channels) if (!c.name.isEmpty()) report(c);
      if (context) pa_context_unref(context);
      context = nullptr;
      pa_threaded_mainloop_free(loop); loop = nullptr;
      return;
    }
    pa_threaded_mainloop_lock(loop);
    pa_context_set_state_callback(context, contextState, this);
    const int result = pa_context_connect(context, nullptr, PA_CONTEXT_NOAUTOSPAWN, nullptr);
    pa_threaded_mainloop_unlock(loop);
    if (result < 0) {
      for (const auto &c : channels) if (!c.name.isEmpty()) report(c);
      shutdown();
    } else timeout.start(2000);
  }
  static void contextState(pa_context *context, void *userdata) {
    auto *self = static_cast<PulseWorker *>(userdata);
    const auto state = pa_context_get_state(context);
    if (state == PA_CONTEXT_READY) {
      for (auto &c : self->channels)
        if (!c.name.isEmpty()) {
          const auto name = c.name.toUtf8();
          c.operation = pa_context_get_source_info_by_name(context, name.constData(), sourceInfo, &c);
          if (!c.operation) self->report(c);
        }
    } else if (state == PA_CONTEXT_FAILED || state == PA_CONTEXT_TERMINATED) {
      for (const auto &c : self->channels) if (!c.name.isEmpty()) self->report(c);
      const auto generation = self->generation;
      QMetaObject::invokeMethod(self, [self, generation] {
        if (self->generation == generation) self->shutdown();
      }, Qt::QueuedConnection);
    }
  }
  static void sourceInfo(pa_context *, const pa_source_info *info, int end, void *userdata) {
    auto &c = *static_cast<Channel *>(userdata);
    auto *self = c.owner;
    if (end) {
      if (c.operation) { pa_operation_unref(c.operation); c.operation = nullptr; }
      if (!c.stream) self->report(c);
      return;
    }
    if (!info || QString::fromUtf8(info->name) != c.name) { self->report(c); return; }
    const pa_sample_spec spec{PA_SAMPLE_FLOAT32NE, 25, info->channel_map.channels};
    if (!pa_sample_spec_valid(&spec)) { self->report(c); return; }
    c.stream = pa_stream_new(self->context, c.index == 0 ? "Microphone level" : "Computer sound level",
                             &spec, &info->channel_map);
    if (!c.stream) { self->report(c); return; }
    pa_stream_set_state_callback(c.stream, streamState, &c);
    pa_stream_set_read_callback(c.stream, read, &c);
    const uint32_t frame = pa_frame_size(&spec);
    const pa_buffer_attr attr{frame * 4, UINT32_MAX, UINT32_MAX, UINT32_MAX, frame};
    const auto flags = pa_stream_flags_t(PA_STREAM_PEAK_DETECT | PA_STREAM_DONT_MOVE);
    const auto name = c.name.toUtf8();
    if (pa_stream_connect_record(c.stream, name.constData(), &attr, flags) < 0) self->report(c);
  }
  static void streamState(pa_stream *stream, void *userdata) {
    auto &c = *static_cast<Channel *>(userdata);
    const auto state = pa_stream_get_state(stream);
    if (state == PA_STREAM_FAILED || state == PA_STREAM_TERMINATED) c.owner->report(c);
  }
  static void read(pa_stream *stream, size_t, void *userdata) {
    auto &c = *static_cast<Channel *>(userdata);
    QList<float> peaks;
    while (pa_stream_readable_size(stream) > 0) {
      const void *data = nullptr;
      size_t bytes = 0;
      if (pa_stream_peek(stream, &data, &bytes) < 0) { c.owner->report(c); return; }
      if (!bytes) break;
      // Holes are missing data, not a measured zero.
      if (data) {
        const auto *samples = static_cast<const float *>(data);
        for (size_t i = 0; i < bytes / sizeof(float); ++i) peaks.append(samples[i]);
      }
      if (pa_stream_drop(stream) < 0) { c.owner->report(c); return; }
    }
    if (!peaks.isEmpty()) emit c.owner->samples(c.index, c.owner->generation, peaks);
  }
};
class PulseBackend final : public AudioLevelBackend {
  QThread *thread = new QThread;
  PulseWorker *worker = new PulseWorker;
public:
  PulseBackend() {
    connect(worker, &PulseWorker::samples, this, &AudioLevelBackend::samples, Qt::QueuedConnection);
    connect(worker, &PulseWorker::unavailable, this, &AudioLevelBackend::unavailable, Qt::QueuedConnection);
    worker->timeout.setParent(worker);
    worker->moveToThread(thread);
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
  }
  ~PulseBackend() override {
    // Detach delivery first; deletion never waits for the Pulse lock on the UI.
    disconnect(this, nullptr, nullptr, nullptr);
    QMetaObject::invokeMethod(worker, [w = worker, t = thread] {
      w->shutdown(); t->quit();
    }, Qt::QueuedConnection);
  }
  void open(const AudioSnapshot &s, quint64 g) override {
    QMetaObject::invokeMethod(worker, [w = worker, s, g] { w->open(s, g); }, Qt::QueuedConnection);
  }
  void close() override {
    QMetaObject::invokeMethod(worker, [w = worker] { w->shutdown(); }, Qt::QueuedConnection);
  }
};
} // namespace
std::unique_ptr<AudioLevelBackend> pulseAudioBackend() { return std::make_unique<PulseBackend>(); }

#include "audio-pulse.moc"
