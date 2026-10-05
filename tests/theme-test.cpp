#include "omarchy-theme.hpp"
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace {

const QString themesRoot = QStringLiteral("/usr/share/omarchy/themes");

void write(const QString &path, const QByteArray &contents) {
  QDir().mkpath(QFileInfo(path).path());
  QFile file(path);
  QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  file.write(contents);
}

/** Stage a theme the way omarchy-theme-set does: build next-theme, remove the
 *  current theme directory, then move the new one into place. */
void applyTheme(const QString &state, const QString &name, const QByteArray &colors,
                const QByteArray &shell = {}) {
  const QString next = state + QStringLiteral("/current/next-theme");
  QDir(next).removeRecursively();
  write(next + QStringLiteral("/colors.toml"), colors);
  if (!shell.isEmpty())
    write(next + QStringLiteral("/shell.toml"), shell);
  QDir(state + QStringLiteral("/current/theme")).removeRecursively();
  QVERIFY(QDir().rename(next, state + QStringLiteral("/current/theme")));
  write(state + QStringLiteral("/current/theme.name"), name.toUtf8());
}

QByteArray stockColors(const QString &theme) {
  QFile file(themesRoot + '/' + theme + QStringLiteral("/colors.toml"));
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QColor tomlColor(const QByteArray &toml, const char *key) {
  for (const QByteArray &line : toml.split('\n')) {
    const QByteArray trimmed = line.trimmed();
    if (trimmed.startsWith(QByteArray(key) + ' ') || trimmed.startsWith(QByteArray(key) + '='))
      return OmarchyTheme::parseColor(QString::fromUtf8(trimmed.mid(trimmed.indexOf('=') + 1)).remove('"'));
  }
  return {};
}

} // namespace

class ThemeTest : public QObject {
  Q_OBJECT
private slots:
  void parsesOmarchyColorForms() {
    QCOMPARE(OmarchyTheme::parseColor("#509475"), QColor("#509475"));
    QCOMPARE(OmarchyTheme::parseColor("rgba(33ccffee)").alpha(), 0xee);
    QCOMPARE(OmarchyTheme::parseColor("rgba(33ccffee) rgba(00ff99ee) 45deg").rgb(), QColor("#33ccff").rgb());
    QCOMPARE(OmarchyTheme::parseColor("45deg rgb(112233)").rgb(), QColor("#112233").rgb());
    QVERIFY(qAbs(OmarchyTheme::parseColor("rgba(10, 20, 30, 0.5)").alphaF() - 0.5) < 0.01);
    QVERIFY(!OmarchyTheme::parseColor("dark").isValid());
    QVERIFY(!OmarchyTheme::parseColor("").isValid());
  }

  void everyStockThemeIsFaithfulAndReadable_data() {
    QTest::addColumn<QString>("theme");
    const auto themes = QDir(themesRoot).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    if (themes.isEmpty())
      QSKIP("Omarchy themes are not installed");
    for (const QString &theme : themes)
      if (QFile::exists(themesRoot + '/' + theme + QStringLiteral("/colors.toml")))
        QTest::newRow(qPrintable(theme)) << theme;
  }
  void everyStockThemeIsFaithfulAndReadable() {
    QFETCH(QString, theme);
    QTemporaryDir state, config;
    const QByteArray colors = stockColors(theme);
    applyTheme(state.path(), theme, colors);
    OmarchyTheme omarchy(nullptr, state.path(), config.path(), false);
    QCOMPARE(omarchy.name(), theme);
    QCOMPARE(omarchy.background().rgb(), tomlColor(colors, "background").rgb());
    QCOMPARE(omarchy.text().rgb(), tomlColor(colors, "foreground").rgb());
    QCOMPARE(omarchy.accent().rgb(), tomlColor(colors, "accent").rgb());
    QCOMPARE(omarchy.dark(), !colors.contains("mode = \"light\""));
    const QColor bg = omarchy.background();
    // Secondary text stays readable in every theme, including ones whose own
    // `muted` is a border tone. Themes whose foreground itself is low
    // contrast are held to proportionally.
    const qreal textContrast = OmarchyTheme::contrast(omarchy.text(), bg);
    QVERIFY2(OmarchyTheme::contrast(omarchy.muted(), bg) >= std::min(4.5, textContrast * 0.6) - 0.05,
             qPrintable(QString("muted %1 on %2").arg(omarchy.muted().name(), bg.name())));
    QVERIFY(OmarchyTheme::contrast(omarchy.faint(), bg) >= std::min(3.0, textContrast * 0.45) - 0.05);
    QVERIFY(omarchy.muted() != omarchy.text());
    QVERIFY2(OmarchyTheme::contrast(omarchy.onAccent(), omarchy.accent()) >= 3,
             qPrintable(QString("%1 on %2").arg(omarchy.onAccent().name(), omarchy.accent().name())));
    QVERIFY(omarchy.controlBorder().alphaF() > 0.3 && omarchy.controlBorder().alphaF() < 0.5);
  }

  void notifiesForIndividualProperties_data() {
    QTest::addColumn<QByteArray>("property");
    QTest::newRow("name") << QByteArray("name");
    QTest::newRow("dark") << QByteArray("dark");
    QTest::newRow("pressedFill") << QByteArray("pressedFill");
  }
  void notifiesForIndividualProperties() {
    QFETCH(QByteArray, property);
    QTemporaryDir state, config;
    const QByteArray colors = "mode = \"dark\"\nbackground = \"#101010\"\n"
                              "foreground = \"#eeeeee\"\ndark_background = \"#080808\"\n";
    applyTheme(state.path(), "One", colors);
    OmarchyTheme omarchy(nullptr, state.path(), config.path(), false);
    QSignalSpy changed(&omarchy, &OmarchyTheme::changed);
    const QVariant before = omarchy.property(property.constData());
    if (property == "name")
      write(state.path() + "/current/theme.name", "Two");
    else if (property == "dark")
      write(state.path() + "/current/theme/colors.toml",
            QByteArray(colors).replace("dark\"", "light\""));
    else
      write(config.path() + "/shell.toml", "[controls]\npressed-fill-alpha = 0.6\n");
    omarchy.reload();
    QVERIFY(omarchy.property(property.constData()) != before);
    QCOMPARE(changed.count(), 1);
    omarchy.reload();
    QCOMPARE(changed.count(), 1);
  }

  void followsThemeSwitchesAndEdits() {
    QTemporaryDir state, config;
    applyTheme(state.path(), "One", "background = \"#101010\"\nforeground = \"#eeeeee\"\naccent = \"#3366ff\"\n");
    OmarchyTheme omarchy(nullptr, state.path(), config.path(), false);
    QSignalSpy changed(&omarchy, &OmarchyTheme::changed);
    QCOMPARE(omarchy.accent(), QColor("#3366ff"));

    applyTheme(state.path(), "Two", "mode = \"light\"\nbackground = \"#fafafa\"\nforeground = \"#202020\"\naccent = \"#aa3300\"\n");
    QTRY_VERIFY_WITH_TIMEOUT(changed.count() >= 1, 3000);
    QCOMPARE(omarchy.name(), QString("Two"));
    QCOMPARE(omarchy.accent(), QColor("#aa3300"));
    QVERIFY(!omarchy.dark());

    // Editing colors.toml in place, as a theme editor would.
    changed.clear();
    QTest::qWait(200);
    write(state.path() + "/current/theme/colors.toml",
          "mode = \"light\"\nbackground = \"#fafafa\"\nforeground = \"#202020\"\naccent = \"#008844\"\n");
    QTRY_VERIFY_WITH_TIMEOUT(changed.count() >= 1, 3000);
    QCOMPARE(omarchy.accent(), QColor("#008844"));

    // A second switch after the first still arrives: watches were re-armed.
    changed.clear();
    applyTheme(state.path(), "Three", "background = \"#000000\"\nforeground = \"#ffffff\"\naccent = \"#ffcc00\"\n");
    QTRY_VERIFY_WITH_TIMEOUT(changed.count() >= 1, 3000);
    QCOMPARE(omarchy.name(), QString("Three"));
    QVERIFY(omarchy.dark());
  }

  void shellRolesReferencesAndUserOverrides() {
    QTemporaryDir state, config;
    applyTheme(state.path(), "Roles", "background = \"#111111\"\nforeground = \"#dddddd\"\naccent = \"#44aa88\"\nred = \"#ff5555\"\n",
               "[hyprland]\nactive-border = \"rgba(33ccffee) rgba(00ff99ee) 45deg\"\n"
               "[popups]\nborder = \"hyprland.active-border\"\n"
               "[bar]\nactive = \"#ff00aa\"\n"
               "[menu]\nscrim = \"background\" # dim\nscrim-alpha = 0.7\nselected-text = \"accent\"\n"
               "[controls]\nnormal-fill-alpha = 0.1\nnormal-border-alpha = 0.6\n");
    write(config.path() + "/shell.toml", "[menu]\nbackground = \"#222233\"\n");
    OmarchyTheme omarchy(nullptr, state.path(), config.path(), false);
    QCOMPARE(omarchy.popupFrame().rgb(), QColor("#33ccff").rgb());
    QCOMPARE(omarchy.recording(), QColor("#ff00aa"));
    QCOMPARE(omarchy.scrim().rgb(), QColor("#111111").rgb());
    QVERIFY(qAbs(omarchy.scrim().alphaF() - 0.7) < 0.01);
    QCOMPARE(omarchy.selectedText(), QColor("#44aa88"));
    QVERIFY(qAbs(omarchy.controlFill().alphaF() - 0.1) < 0.01);
    QVERIFY(qAbs(omarchy.controlBorder().alphaF() - 0.6) < 0.01);
    QCOMPARE(omarchy.background(), QColor("#222233"));
    QCOMPARE(omarchy.urgent(), QColor("#ff5555"));
  }

  void withoutOmarchyUsesShellDefaults() {
    QTemporaryDir state, config;
    OmarchyTheme omarchy(nullptr, state.path() + "/missing", config.path() + "/missing", false);
    QCOMPARE(omarchy.background(), QColor("#101315"));
    QCOMPARE(omarchy.text(), QColor("#cacccc"));
    QVERIFY(omarchy.dark());
    QCOMPARE(omarchy.radius(), 0);
    QCOMPARE(omarchy.fontFamily(), QString("monospace"));
  }
};

QTEST_GUILESS_MAIN(ThemeTest)
#include "theme-test.moc"
