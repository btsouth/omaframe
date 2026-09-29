#include "ocr.hpp"
#include <QFontDatabase>
#include <QGuiApplication>
#include <QPainter>
#include <QtTest>

// One line of words, each ten pixels a character wide with a space between.
static QVector<Ocr::Word> words(const QStringList &texts, int line = 1,
                                int y = 0) {
  QVector<Ocr::Word> result;
  int x = 0;
  for (const QString &text : texts) {
    result << Ocr::Word{text, QRect(x, y, 10 * text.size(), 20), 1, 1, line};
    x += 10 * text.size() + 10;
  }
  return result;
}
static QVector<QRect> secretsIn(const QStringList &texts) {
  return Ocr::findSecrets(words(texts));
}
// The part of `rect` inside any of `cover`, from 0 to 1.
static double covered(const QRect &rect, const QVector<QRect> &cover) {
  const QRegion region = std::accumulate(
      cover.cbegin(), cover.cend(), QRegion(),
      [](QRegion r, const QRect &c) { return r.united(c); });
  qint64 area = 0;
  for (const QRect &part : region.intersected(rect))
    area += qint64(part.width()) * part.height();
  return double(area) / (qint64(rect.width()) * rect.height());
}

class OcrTest final : public QObject {
  Q_OBJECT
private slots:
  void findsEachKind_data() {
    QTest::addColumn<QStringList>("line");
    QTest::addColumn<QString>("secret");
    QTest::newRow("email") << QStringList{"mail", "jane.doe@example.com", "now"}
                           << "jane.doe@example.com";
    QTest::newRow("jwt")
        << QStringList{"Bearer",
                       "eyJhbGciOiJIUzI1NiJ9.eyJzdWIiOiIxMjM0In0.dozjgNryP4J3jVmNHl0w5N"}
        << "eyJhbGciOiJIUzI1NiJ9.eyJzdWIiOiIxMjM0In0.dozjgNryP4J3jVmNHl0w5N";
    QTest::newRow("aws") << QStringList{"key", "AKIAIOSFODNN7EXAMPLE"}
                         << "AKIAIOSFODNN7EXAMPLE";
    QTest::newRow("aws session") << QStringList{"ASIAY34FZKBOKMUTVV7A"}
                                 << "ASIAY34FZKBOKMUTVV7A";
    QTest::newRow("github")
        << QStringList{"ghp_R8x2KqLm4Vn7Pz9Wt3Ys6Bd1Fh5Jc0Ae2Gk"}
        << "ghp_R8x2KqLm4Vn7Pz9Wt3Ys6Bd1Fh5Jc0Ae2Gk";
    QTest::newRow("github fine-grained")
        << QStringList{"github_pat_11ABCDEFG0123456789_abcdefghijklmnop"}
        << "github_pat_11ABCDEFG0123456789_abcdefghijklmnop";
    QTest::newRow("github misread") << QStringList{"9hp_R8x2KqLm4Vn7Pz9Wt3Ys6Bd1"}
                                    << "9hp_R8x2KqLm4Vn7Pz9Wt3Ys6Bd1";
    QTest::newRow("slack")
        << QStringList{"xoxb-1234567890-0987654321-AbCdEfGh"}
        << "xoxb-1234567890-0987654321-AbCdEfGh";
    QTest::newRow("stripe") << QStringList{"rk_live_51H8xYzAbCdEfGhIjKlMn"}
                            << "rk_live_51H8xYzAbCdEfGhIjKlMn";
    QTest::newRow("anthropic")
        << QStringList{"sk-ant-api03-Xk2mP9qR4tV7wY1zB3cD5fG8hJ0kL"}
        << "sk-ant-api03-Xk2mP9qR4tV7wY1zB3cD5fG8hJ0kL";
    QTest::newRow("openai") << QStringList{"sk-proj-Ab12Cd34Ef56Gh78Ij90Kl"}
                            << "sk-proj-Ab12Cd34Ef56Gh78Ij90Kl";
    QTest::newRow("card") << QStringList{"card", "4111", "1111", "1111", "1111"}
                          << "4111 1111 1111 1111";
    QTest::newRow("card run together") << QStringList{"4111111111111111"}
                                       << "4111111111111111";
    QTest::newRow("amex") << QStringList{"3782", "822463", "10005"}
                          << "3782 822463 10005";
    QTest::newRow("iban")
        << QStringList{"IBAN", "GB82", "WEST", "1234", "5698", "7654", "32"}
        << "GB82 WEST 1234 5698 7654 32";
    QTest::newRow("public address") << QStringList{"server", "203.0.113.45:8080"}
                                    << "203.0.113.45";
  }
  void findsEachKind() {
    QFETCH(QStringList, line);
    QFETCH(QString, secret);
    const auto found = secretsIn(line);
    QCOMPARE(found.size(), 1);
    // The secret's own characters, at ten pixels each.
    const QString spaced = line.join(' ');
    const int at = spaced.indexOf(secret);
    QVERIFY(at >= 0);
    const QRect expected(10 * at, 0, 10 * secret.size(), 20);
    QCOMPARE(covered(expected, found), 1.0);
    // Words around it are left alone, give or take a character.
    QVERIFY(found.first().left() >= expected.left() - 20);
    QVERIFY(found.first().right() <= expected.right() + 20);
  }
  void ignoresNearMisses_data() {
    QTest::addColumn<QStringList>("line");
    QTest::newRow("fails luhn") << QStringList{"4111", "1111", "1111", "1112"};
    QTest::newRow("commit")
        << QStringList{"commit", "9fceb02d0ae598e95dc970b74767f19372d61af8"};
    QTest::newRow("version") << QStringList{"version", "1.2.3.4"};
    QTest::newRow("tagged version") << QStringList{"v10.20.30.40"};
    QTest::newRow("loopback") << QStringList{"127.0.0.1"};
    QTest::newRow("private") << QStringList{"192.168.1.10", "10.0.0.7",
                                            "172.20.3.4", "169.254.1.1"};
    QTest::newRow("subnet mask") << QStringList{"255.255.255.0"};
    QTest::newRow("git remote") << QStringList{"git@github.com:owner/repo.git"};
    QTest::newRow("image name") << QStringList{"icon@2x.png"};
    QTest::newRow("phone") << QStringList{"+1", "555", "123", "4567"};
    QTest::newRow("date") << QStringList{"2026-09-28", "17:00:07"};
    QTest::newRow("short sk") << QStringList{"sk-learn"};
    QTest::newRow("words ending in sk") << QStringList{"task-1234567890abcdefghijklmn"};
    QTest::newRow("bad iban") << QStringList{"GB82", "WEST", "1234", "5698", "7654", "33"};
    QTest::newRow("jwt without dots")
        << QStringList{"eyJhbGciOiJIUzI1NiJ9eyJzdWIiOiIxMjM0In0"};
  }
  void ignoresNearMisses() {
    QFETCH(QStringList, line);
    QCOMPARE(secretsIn(line), QVector<QRect>());
  }
  void joinsTokensOcrSplit() {
    const auto found = secretsIn({"TOKEN=ghp_R8x2KqLm", "4Vn7Pz9Wt3Ys6Bd1Fh5J"});
    QCOMPARE(found.size(), 1);
    // From ghp_ to the end of the second word.
    QVERIFY(found.first().left() <= 60 && found.first().left() >= 40);
    QCOMPARE(found.first().right(), 10 * 38 + 10 - 1);
  }
  void hidesAWholePrivateKey() {
    QVector<Ocr::Word> all;
    const QStringList lines{"$ cat key", "-----BEGIN OPENSSH PRIVATE KEY-----",
                            "b3BlbnNzaC1rZXktdjEAAAAABG5vbmUAAAAEbm9uZQAAAAAAAAABAAAAMwAAAAtz",
                            "QyNTUxOQAAACDSQ7XbQ9d7ZgB0nWb9oK7oE1t8x3v1yH6s4k2p0rT6RwAAAJgT",
                            "-----END OPENSSH PRIVATE KEY-----", "$ ls"};
    for (int i = 0; i < lines.size(); ++i)
      all += words(lines[i].split(' '), i + 1, i * 24);
    const auto found = Ocr::findSecrets(all);
    QCOMPARE(found.size(), 1);
    QCOMPARE(found.first().top(), 24);
    QCOMPARE(found.first().bottom(), 4 * 24 + 19);
  }
  void findsAKeyBodyWithoutItsHeader() {
    QVector<Ocr::Word> all;
    all += words({"b3BlbnNzaC1rZXktdjEAAAAABG5vbmUAAAAEbm9uZQAAAAAAAAABAAAAMwAAAAtz"}, 1, 0);
    all += words({"QyNTUxOQAAACDSQ7XbQ9d7ZgB0nWb9oK7oE1t8x3v1yH6s4k2p0rT6RwAAAJgT"}, 2, 24);
    QCOMPARE(Ocr::findSecrets(all).size(), 1);
    // One long line alone, or lines of hex like checksums, are not a key.
    QCOMPARE(Ocr::findSecrets(all.mid(0, 1)).size(), 0);
    QVector<Ocr::Word> sums;
    sums += words({"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"}, 1, 0);
    sums += words({"d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592"}, 2, 24);
    QCOMPARE(Ocr::findSecrets(sums).size(), 0);
  }
  void readsTesseractTsv() {
    const QByteArray tsv =
        "level\tpage_num\tblock_num\tpar_num\tline_num\tword_num\tleft\ttop\twidth\theight\tconf\ttext\n"
        "1\t1\t0\t0\t0\t0\t0\t0\t400\t200\t-1\t\n"
        "5\t1\t1\t1\t1\t1\t20\t40\t60\t20\t95.1\tHello\n"
        "5\t1\t1\t1\t1\t2\t90\t40\t70\t20\t94.0\tthere\n"
        "5\t1\t1\t1\t2\t1\t20\t70\t40\t20\t93.0\tnext\n"
        "5\t1\t2\t1\t1\t1\t20\t140\t80\t20\t92.0\tapart\n"
        "5\t1\t2\t1\t1\t2\t110\t140\t10\t20\t-1\t \n";
    const auto read = Ocr::parseTsv(tsv, 2);
    QCOMPARE(read.size(), 4);
    QCOMPARE(read[0].box, QRect(10, 20, 30, 10));
    QCOMPARE(Ocr::text(read), QString("Hello there\nnext\n\napart"));
  }
  void findsSecretsRenderedOnScreen_data() {
    QTest::addColumn<bool>("dark");
    QTest::addColumn<bool>("mono");
    QTest::addColumn<int>("pixels");
    for (bool dark : {true, false})
      for (bool mono : {true, false})
        for (int pixels : {14, 18}) {
          const QString name = QString("%1 %2 %3px")
                                   .arg(dark ? "dark" : "light",
                                        mono ? "mono" : "sans")
                                   .arg(pixels);
          QTest::newRow(qPrintable(name)) << dark << mono << pixels;
        }
  }
  void findsSecretsRenderedOnScreen() {
    if (!Ocr::available())
      QSKIP("tesseract is not installed");
    QFETCH(bool, dark);
    QFETCH(bool, mono);
    QFETCH(int, pixels);
    QFont font = mono ? QFontDatabase::systemFont(QFontDatabase::FixedFont)
                      : QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    font.setPixelSize(pixels);
    const QList<QPair<QString, QString>> lines{
        {"$ export GITHUB_TOKEN=", "ghp_R8x2KqLm4Vn7Pz9Wt3Ys6Bd1Fh5Jc0Ae2Gk"},
        {"AWS_ACCESS_KEY_ID=", "AKIAIOSFODNN7EXAMPLE"},
        {"Signed in as ", "jane.doe@example.com"},
        {"ANTHROPIC_API_KEY=", "sk-ant-api03-Xk2mP9qR4tV7wY1zB3cD5fG8hJ0kL"},
        {"Card on file: ", "4111 1111 1111 1111"},
        {"Connected to ", "203.0.113.45"},
    };
    QImage image(1200, 60 + lines.size() * pixels * 2, QImage::Format_RGB32);
    image.fill(dark ? QColor("#1e1e2e") : QColor("#fafafa"));
    QPainter painter(&image);
    painter.setFont(font);
    painter.setPen(dark ? QColor("#cdd6f4") : QColor("#1e1e1e"));
    const QFontMetrics metrics(font);
    QVector<QRect> drawn;
    for (int i = 0; i < lines.size(); ++i) {
      const QPoint at(24, 30 + i * pixels * 2 + metrics.ascent());
      painter.drawText(at, lines[i].first + lines[i].second);
      const int x = at.x() + metrics.horizontalAdvance(lines[i].first);
      drawn << QRect(x, at.y() - metrics.ascent() + 2,
                     metrics.horizontalAdvance(lines[i].second),
                     metrics.ascent() - 2);
    }
    painter.end();
    const auto read = Ocr::read(image);
    QVERIFY(read.has_value());
    const auto found = Ocr::findSecrets(*read);
    for (int i = 0; i < drawn.size(); ++i) {
      // Grown the way hiding grows them.
      QVector<QRect> grown;
      for (const QRect &rect : found)
        grown << rect.adjusted(-4, -4, 4, 4);
      if (covered(drawn[i], grown) < 0.97) {
        qWarning() << "Missed" << lines[i].second << "in"
                   << Ocr::text(*read);
        QFAIL("A rendered secret was not covered");
      }
    }
    QVERIFY(found.size() <= drawn.size());
  }
  void tallImagesAreReadInBandsWithoutLosingOrRepeatingLines() {
    if (!Ocr::available())
      QSKIP("tesseract is not installed");
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPixelSize(16);
    QImage image(900, 1500, QImage::Format_RGB32);
    image.fill(QColor("#1e1e2e"));
    QPainter painter(&image);
    painter.setFont(font);
    painter.setPen(QColor("#cdd6f4"));
    // Every 28 pixels, so lines sit on and near each band's edges.
    const int rows = 1500 / 28 - 1;
    for (int i = 0; i < rows; ++i)
      painter.drawText(24, 30 + i * 28,
                       QString("row %1 status ok").arg(i + 100));
    // Right where two bands meet on a machine with four or more cores.
    painter.drawText(460, 30 + 13 * 28,
                     "ghp_R8x2KqLm4Vn7Pz9Wt3Ys6Bd1Fh5Jc0Ae2Gk");
    painter.end();
    const auto read = Ocr::read(image);
    QVERIFY(read.has_value());
    const QString text = Ocr::text(*read);
    for (int i = 0; i < rows; ++i)
      QVERIFY2(text.count(QString("row %1").arg(i + 100)) == 1,
               qPrintable(QString("row %1 in:\n%2").arg(i + 100).arg(text)));
    QCOMPARE(Ocr::findSecrets(*read).size(), 1);
  }
  void readingCanBeCancelled() {
    if (!Ocr::available())
      QSKIP("tesseract is not installed");
    QImage image(400, 100, QImage::Format_RGB32);
    image.fill(Qt::white);
    std::atomic_bool cancel = true;
    QVERIFY(!Ocr::read(image, &cancel).has_value());
  }
};
QTEST_MAIN(OcrTest)
#include "ocr-test.moc"
