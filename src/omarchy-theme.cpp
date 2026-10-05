#include "omarchy-theme.hpp"

#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextStream>

#include <cmath>

namespace {

/** Flat "section.key" -> raw value from a TOML subset: sections, `key = value`,
 *  quoted strings and bare numbers. Everything Omarchy writes fits this. */
QHash<QString, QString> readToml(const QString &path) {
  QHash<QString, QString> values;
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    return values;
  QString section;
  QTextStream in(&file);
  static const QRegularExpression sectionLine(QStringLiteral(R"re(^\s*\[([A-Za-z0-9_.-]+)\]\s*(#.*)?$)re"));
  static const QRegularExpression valueLine(
      QStringLiteral(R"re(^\s*([A-Za-z0-9_-]+)\s*=\s*(?:"([^"]*)"|'([^']*)'|([^#\s][^#]*?))\s*(#.*)?$)re"));
  while (!in.atEnd()) {
    const QString line = in.readLine();
    if (const auto match = sectionLine.match(line); match.hasMatch()) {
      section = match.captured(1);
      continue;
    }
    const auto match = valueLine.match(line);
    if (!match.hasMatch())
      continue;
    QString value = match.captured(2);
    if (value.isEmpty())
      value = match.captured(3);
    if (value.isEmpty())
      value = match.captured(4).trimmed();
    values.insert(section.isEmpty() ? match.captured(1) : section + '.' + match.captured(1), value);
  }
  return values;
}

qreal luminance(const QColor &color) {
  auto channel = [](qreal c) { return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); };
  return 0.2126 * channel(color.redF()) + 0.7152 * channel(color.greenF()) + 0.0722 * channel(color.blueF());
}

} // namespace

OmarchyTheme::OmarchyTheme(QObject *parent, QString stateDir, QString configDir, bool queryHyprland)
    : QObject(parent), stateDir_(std::move(stateDir)), configDir_(std::move(configDir)),
      queryHyprland_(queryHyprland) {
  if (stateDir_.isEmpty())
    stateDir_ = QDir::homePath() + QStringLiteral("/.local/state/omarchy");
  if (configDir_.isEmpty())
    configDir_ = QDir::homePath() + QStringLiteral("/.config/omarchy");
  // A theme switch removes and replaces several files in quick succession.
  debounce_.setSingleShot(true);
  debounce_.setInterval(150);
  connect(&debounce_, &QTimer::timeout, this, &OmarchyTheme::reload);
  connect(&watcher_, &QFileSystemWatcher::fileChanged, &debounce_, qOverload<>(&QTimer::start));
  connect(&watcher_, &QFileSystemWatcher::directoryChanged, &debounce_, qOverload<>(&QTimer::start));
  reload();
}

QColor OmarchyTheme::parseColor(const QString &raw) {
  QString value = raw.trimmed();
  // Hyprland gradients: "rgba(33ccffee) rgba(00ff99ee) 45deg" -> first stop.
  static const QRegularExpression angle(QStringLiteral(R"re(^-?\d+(\.\d+)?deg$)re"));
  for (const QString &part : value.split(QRegularExpression(QStringLiteral(R"re(\s+(?![^(]*\)))re")), Qt::SkipEmptyParts)) {
    if (!angle.match(part).hasMatch()) {
      value = part;
      break;
    }
  }
  static const QRegularExpression hex(QStringLiteral(R"re(^#?([0-9A-Fa-f]{6})([0-9A-Fa-f]{2})?$)re"));
  static const QRegularExpression wrapped(QStringLiteral(R"re(^rgba?\(\s*([0-9A-Fa-f]{6})([0-9A-Fa-f]{2})?\s*\)$)re"));
  static const QRegularExpression channels(
      QStringLiteral(R"re(^rgba?\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*(?:,\s*([0-9.]+)\s*)?\)$)re"));
  auto fromHex = [](const QString &rgb, const QString &a) {
    QColor color(QLatin1Char('#') + rgb);
    if (!a.isEmpty())
      color.setAlpha(a.toInt(nullptr, 16));
    return color;
  };
  if (!value.startsWith(QLatin1String("rgb"))) {
    if (const auto m = hex.match(value); m.hasMatch())
      return fromHex(m.captured(1), m.captured(2));
    return {};
  }
  if (const auto m = wrapped.match(value); m.hasMatch())
    return fromHex(m.captured(1), m.captured(2));
  if (const auto m = channels.match(value); m.hasMatch()) {
    QColor color(m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt());
    if (!m.captured(4).isEmpty()) {
      const qreal a = m.captured(4).toDouble();
      color.setAlphaF(std::clamp(a > 1 ? a / 255.0 : a, 0.0, 1.0));
    }
    return color;
  }
  return {};
}

qreal OmarchyTheme::contrast(const QColor &a, const QColor &b) {
  const qreal la = luminance(a), lb = luminance(b);
  return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

QColor OmarchyTheme::alpha(const QColor &color, qreal value) const {
  QColor result = color;
  result.setAlphaF(std::clamp(value, 0.0, 1.0));
  return result;
}

QColor OmarchyTheme::mix(const QColor &from, const QColor &to, qreal t) const {
  t = std::clamp(t, 0.0, 1.0);
  return QColor::fromRgbF(from.redF() + (to.redF() - from.redF()) * t,
                          from.greenF() + (to.greenF() - from.greenF()) * t,
                          from.blueF() + (to.blueF() - from.blueF()) * t,
                          from.alphaF() + (to.alphaF() - from.alphaF()) * t);
}

/** A shell.toml value: a palette role, a reference to another shell key
 *  (e.g. "hyprland.active-border"), or a literal color. */
QColor OmarchyTheme::role(const QString &value, const QColor &fallback, int depth) const {
  const QString token = value.trimmed().toLower();
  if (token.isEmpty() || depth > 8)
    return fallback;
  if (token == QLatin1String("foreground") || token == QLatin1String("text"))
    return palette_.value(QStringLiteral("foreground"));
  for (const char *name : {"accent", "urgent", "muted", "background"})
    if (token == QLatin1String(name))
      return palette_.value(QString::fromLatin1(name));
  if (token == QLatin1String("transparent"))
    return QColor(0, 0, 0, 0);
  if (shell_.contains(token))
    return role(shell_.value(token), fallback, depth + 1);
  const QColor color = parseColor(value);
  return color.isValid() ? color : fallback;
}

/** The furthest blend of `text` toward `background` (up to `limit`) that keeps
 *  at least `target` contrast. Light themes lose contrast faster than dark. */
QColor OmarchyTheme::fade(const QColor &text, const QColor &background, qreal target, qreal limit) const {
  qreal low = 0, high = limit;
  for (int i = 0; i < 12; ++i) {
    const qreal t = (low + high) / 2;
    (contrast(mix(text, background, t), background) >= target ? low : high) = t;
  }
  return mix(text, background, low);
}

qreal OmarchyTheme::number(const QString &key, qreal fallback) const {
  bool ok = false;
  const qreal value = shell_.value(key).toDouble(&ok);
  return ok && std::isfinite(value) ? std::clamp(value, 0.0, 1.0) : fallback;
}

/** A surface color with its `-alpha` companion, as Omarchy's Color.composed. */
QColor OmarchyTheme::surface(const QString &section, const QString &key, const QColor &fallback,
                             qreal fallbackAlpha) const {
  const QString base = section + '.' + key;
  return alpha(role(shell_.value(base), fallback), number(base + QStringLiteral("-alpha"), fallbackAlpha));
}

QColor OmarchyTheme::readableOn(const QColor &fill) const {
  const QColor base = alpha(fill, 1.0), bg = alpha(background_, 1.0), fg = alpha(text_, 1.0);
  const QColor best = contrast(base, bg) >= contrast(base, fg) ? bg : fg;
  if (contrast(base, best) >= 3)
    return best;
  return luminance(base) > 0.35 ? QColor(Qt::black) : QColor(Qt::white);
}

void OmarchyTheme::reload() {
  const auto snapshot = [this] {
    return std::tuple(name_, dark_, background_, well_, text_, muted_, faint_,
                      accent_, onAccent_, urgent_, recording_, frame_, popupFrame_,
                      scrim_, selectedText_, separator_, controlFill_, hoverFill_,
                      pressedFill_, selectedFill_, controlBorder_, hoverBorder_,
                      focusBorder_, radius_);
  };
  const auto old = snapshot();
  const QString themeDir = stateDir_ + QStringLiteral("/current/theme");
  const auto colors = readToml(themeDir + QStringLiteral("/colors.toml"));
  shell_ = readToml(themeDir + QStringLiteral("/shell.toml"));
  // Machine-level overrides win over the theme, as in the shell.
  const auto overrides = readToml(configDir_ + QStringLiteral("/shell.toml"));
  for (auto it = overrides.begin(); it != overrides.end(); ++it)
    shell_.insert(it.key(), it.value());

  auto pick = [&](std::initializer_list<const char *> keys, const QColor &fallback) {
    for (const char *key : keys)
      if (const QColor color = parseColor(colors.value(QString::fromLatin1(key))); color.isValid())
        return color;
    return fallback;
  };
  // Defaults are the shell's own, for a desktop without an Omarchy theme.
  const QColor foreground = pick({"foreground", "color7"}, QColor(0xca, 0xcc, 0xcc));
  const QColor bg = pick({"background", "color0"}, QColor(0x10, 0x13, 0x15));
  palette_ = {{QStringLiteral("foreground"), foreground},
              {QStringLiteral("background"), bg},
              {QStringLiteral("accent"), pick({"accent", "color4"}, foreground)},
              {QStringLiteral("urgent"), pick({"red", "color1"}, QColor(0xa5, 0x55, 0x55))},
              {QStringLiteral("muted"), pick({"muted", "color8"}, foreground)}};

  QFile nameFile(stateDir_ + QStringLiteral("/current/theme.name"));
  name_ = nameFile.open(QIODevice::ReadOnly) ? QString::fromUtf8(nameFile.readAll()).trimmed() : QString();
  const QString mode = colors.value(QStringLiteral("mode")).toLower();
  dark_ = mode == QLatin1String("light") ? false : mode == QLatin1String("dark") ? true : luminance(bg) < 0.5;

  background_ = surface(QStringLiteral("menu"), QStringLiteral("background"), bg, 1.0);
  const QColor opaque = alpha(background_, 1.0);
  text_ = role(shell_.value(QStringLiteral("menu.text")), foreground);
  accent_ = palette_.value(QStringLiteral("accent"));
  urgent_ = palette_.value(QStringLiteral("urgent"));
  recording_ = role(shell_.value(QStringLiteral("bar.active")), urgent_);
  frame_ = surface(QStringLiteral("menu"), QStringLiteral("border"), foreground, 1.0);
  popupFrame_ = surface(QStringLiteral("popups"), QStringLiteral("border"), accent_, 1.0);
  scrim_ = surface(QStringLiteral("menu"), QStringLiteral("scrim"), bg, 0.5);
  selectedText_ = role(shell_.value(QStringLiteral("menu.selected-text")), accent_);
  // Theme `muted` is often a border tone too dark to read (Matte Black puts
  // #333333 on #121212), so secondary text is a blend of the text color.
  const qreal textContrast = contrast(text_, opaque);
  muted_ = fade(text_, opaque, std::min(4.5, textContrast * 0.6), 0.4);
  faint_ = fade(text_, opaque, std::min(3.0, textContrast * 0.45), 0.6);
  separator_ = alpha(text_, 0.14);
  well_ = pick({"dark_background"}, mix(opaque, dark_ ? Qt::black : Qt::white, 0.25));
  onAccent_ = readableOn(accent_);

  const QColor normal = role(shell_.value(QStringLiteral("controls.normal-color")), text_);
  const QColor hover = role(shell_.value(QStringLiteral("controls.hover-cursor-color")), text_);
  const QColor selected = role(shell_.value(QStringLiteral("controls.selected-color")), text_);
  controlFill_ = alpha(normal, number(QStringLiteral("controls.normal-fill-alpha"), 0.04));
  hoverFill_ = alpha(hover, number(QStringLiteral("controls.hover-cursor-fill-alpha"), 0.08));
  pressedFill_ = alpha(hover, number(QStringLiteral("controls.pressed-fill-alpha"), 0.22));
  selectedFill_ = alpha(selected, number(QStringLiteral("controls.selected-fill-alpha"), 0.18));
  controlBorder_ = alpha(role(shell_.value(QStringLiteral("controls.normal-border")), normal),
                         number(QStringLiteral("controls.normal-border-alpha"), 0.4));
  hoverBorder_ = alpha(role(shell_.value(QStringLiteral("controls.hover-cursor-border")), hover),
                       number(QStringLiteral("controls.hover-cursor-border-alpha"), 0.25));
  // Omarchy's focus state mirrors hover (25% alpha), which is too quiet for
  // a keyboard-first capture flow; the accent keeps focus unmistakable.
  focusBorder_ = accent_;

  rearm();
  if (queryHyprland_)
    readRounding();
  if (snapshot() != old)
    emit changed();
}

void OmarchyTheme::rearm() {
  // Replaced files and directories drop their watches; add whatever exists.
  const QStringList paths = {stateDir_,
                             stateDir_ + QStringLiteral("/current"),
                             stateDir_ + QStringLiteral("/current/theme"),
                             stateDir_ + QStringLiteral("/current/theme/colors.toml"),
                             stateDir_ + QStringLiteral("/current/theme/shell.toml"),
                             configDir_,
                             configDir_ + QStringLiteral("/shell.toml")};
  QStringList missing;
  for (const QString &path : paths)
    if (QFileInfo::exists(path) && !watcher_.files().contains(path) && !watcher_.directories().contains(path))
      missing << path;
  if (!missing.isEmpty())
    watcher_.addPaths(missing);
}

void OmarchyTheme::readRounding() {
  // Omarchy corners follow Hyprland's decoration:rounding (0 by default).
  if (QStandardPaths::findExecutable(QStringLiteral("hyprctl")).isEmpty())
    return;
  auto *process = new QProcess(this);
  connect(process, &QProcess::finished, this, [this, process] {
    process->deleteLater();
    const auto json = QJsonDocument::fromJson(process->readAllStandardOutput()).object();
    if (!json.contains(QStringLiteral("int")))
      return;
    const int value = std::clamp(json.value(QStringLiteral("int")).toInt(), 0, 24);
    if (value != radius_) {
      radius_ = value;
      emit changed();
    }
  });
  connect(process, &QProcess::errorOccurred, process, &QObject::deleteLater);
  process->start(QStringLiteral("hyprctl"), {QStringLiteral("getoption"), QStringLiteral("decoration:rounding"),
                                             QStringLiteral("-j")});
}
