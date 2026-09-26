/** @fileoverview The current Omarchy theme as QML-ready roles, reloaded live. */
#pragma once

#include <QColor>
#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QTimer>

/** Reads the same sources as the Omarchy shell, so Omaframe's chrome matches
 *  the bar, menus and popups: the foundational palette from
 *  ~/.local/state/omarchy/current/theme/colors.toml, surface roles and control
 *  state alphas from the theme's shell.toml (with ~/.config/omarchy/shell.toml
 *  layered on top), corner rounding from Hyprland, and the `monospace`
 *  fontconfig alias. `omarchy-theme-set` replaces the theme directory, so the
 *  parent is watched and every change re-reads and re-arms the watches. Content
 *  colors (finishes, rendered output) never come from here. */
class OmarchyTheme : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString name READ name NOTIFY changed)
  Q_PROPERTY(bool dark READ dark NOTIFY changed)
  Q_PROPERTY(QColor background READ background NOTIFY changed)
  Q_PROPERTY(QColor well READ well NOTIFY changed)
  Q_PROPERTY(QColor text READ text NOTIFY changed)
  Q_PROPERTY(QColor muted READ muted NOTIFY changed)
  Q_PROPERTY(QColor faint READ faint NOTIFY changed)
  Q_PROPERTY(QColor accent READ accent NOTIFY changed)
  Q_PROPERTY(QColor onAccent READ onAccent NOTIFY changed)
  Q_PROPERTY(QColor urgent READ urgent NOTIFY changed)
  Q_PROPERTY(QColor recording READ recording NOTIFY changed)
  Q_PROPERTY(QColor frame READ frame NOTIFY changed)
  Q_PROPERTY(QColor popupFrame READ popupFrame NOTIFY changed)
  Q_PROPERTY(QColor scrim READ scrim NOTIFY changed)
  Q_PROPERTY(QColor selectedText READ selectedText NOTIFY changed)
  Q_PROPERTY(QColor separator READ separator NOTIFY changed)
  Q_PROPERTY(QColor controlFill READ controlFill NOTIFY changed)
  Q_PROPERTY(QColor hoverFill READ hoverFill NOTIFY changed)
  Q_PROPERTY(QColor pressedFill READ pressedFill NOTIFY changed)
  Q_PROPERTY(QColor selectedFill READ selectedFill NOTIFY changed)
  Q_PROPERTY(QColor controlBorder READ controlBorder NOTIFY changed)
  Q_PROPERTY(QColor hoverBorder READ hoverBorder NOTIFY changed)
  Q_PROPERTY(QColor focusBorder READ focusBorder NOTIFY changed)
  Q_PROPERTY(int radius READ radius NOTIFY changed)
  Q_PROPERTY(QString fontFamily READ fontFamily NOTIFY changed)

public:
  /** `stateDir` and `configDir` default to ~/.local/state/omarchy and
   *  ~/.config/omarchy; tests pass temporary directories. */
  explicit OmarchyTheme(QObject *parent = nullptr, QString stateDir = {},
                        QString configDir = {}, bool queryHyprland = true);

  QString name() const { return name_; }
  bool dark() const { return dark_; }
  QColor background() const { return background_; }
  QColor well() const { return well_; }
  QColor text() const { return text_; }
  QColor muted() const { return muted_; }
  QColor faint() const { return faint_; }
  QColor accent() const { return accent_; }
  QColor onAccent() const { return onAccent_; }
  QColor urgent() const { return urgent_; }
  QColor recording() const { return recording_; }
  QColor frame() const { return frame_; }
  QColor popupFrame() const { return popupFrame_; }
  QColor scrim() const { return scrim_; }
  QColor selectedText() const { return selectedText_; }
  QColor separator() const { return separator_; }
  QColor controlFill() const { return controlFill_; }
  QColor hoverFill() const { return hoverFill_; }
  QColor pressedFill() const { return pressedFill_; }
  QColor selectedFill() const { return selectedFill_; }
  QColor controlBorder() const { return controlBorder_; }
  QColor hoverBorder() const { return hoverBorder_; }
  QColor focusBorder() const { return focusBorder_; }
  int radius() const { return radius_; }
  QString fontFamily() const { return QStringLiteral("monospace"); }

  /** `color` with its alpha replaced. */
  Q_INVOKABLE QColor alpha(const QColor &color, qreal alpha) const;
  /** Linear blend from `from` (t = 0) to `to` (t = 1). */
  Q_INVOKABLE QColor mix(const QColor &from, const QColor &to, qreal t) const;

  /** Text for a filled surface: whichever theme color reads better on it,
   *  falling back to black or white when neither reaches 3:1. */
  Q_INVOKABLE QColor readableOn(const QColor &fill) const;

  /** Re-reads every source now. Normally driven by the file watcher. */
  Q_INVOKABLE void reload();

  /** A `#rrggbb`, `#rrggbbaa`, `rgb(rrggbb)`, `rgba(rrggbbaa)` or
   *  `rgba(r, g, b, a)` color; for a Hyprland gradient, its first stop. */
  static QColor parseColor(const QString &value);
  /** WCAG contrast ratio between two opaque colors. */
  static qreal contrast(const QColor &a, const QColor &b);

signals:
  void changed();

private:
  void rearm();
  void readRounding();
  QColor role(const QString &value, const QColor &fallback, int depth = 0) const;
  QColor surface(const QString &section, const QString &key,
                 const QColor &fallback, qreal fallbackAlpha) const;
  qreal number(const QString &key, qreal fallback) const;
  QColor fade(const QColor &text, const QColor &background, qreal target, qreal limit) const;

  QString stateDir_;
  QString configDir_;
  bool queryHyprland_;
  QFileSystemWatcher watcher_;
  QTimer debounce_;
  QHash<QString, QColor> palette_;
  QHash<QString, QString> shell_;

  QString name_;
  bool dark_ = true;
  QColor background_, well_, text_, muted_, faint_, accent_, onAccent_,
      urgent_, recording_, frame_, popupFrame_, scrim_, selectedText_,
      separator_, controlFill_, hoverFill_, pressedFill_, selectedFill_,
      controlBorder_, hoverBorder_, focusBorder_;
  int radius_ = 0;
};
