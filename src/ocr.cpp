#include "ocr.hpp"
#include <QBuffer>
#include <QElapsedTimer>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QThread>
#include <memory>
#include <sys/resource.h>

namespace Ocr {
bool available() {
  return !QStandardPaths::findExecutable("tesseract").isEmpty();
}

std::optional<QVector<Word>> read(const QImage &image,
                                  const std::atomic_bool *cancel) {
  auto cancelled = [cancel] { return cancel && cancel->load(); };
  if (image.isNull() || cancelled())
    return std::nullopt;
  // Screen text at 1x is too small for tesseract: digits run together and
  // dots go missing. Doubling fixes that; large captures are usually from
  // scaled displays whose text is already big enough.
  const double scale =
      qint64(image.width()) * image.height() <= 4200000 ? 2 : 1;
  QImage gray = image.convertToFormat(QImage::Format_Grayscale8);
  if (scale != 1)
    gray = gray.scaled(gray.size() * scale, Qt::IgnoreAspectRatio,
                       Qt::SmoothTransformation);
  // A screen full of text takes one tesseract several seconds. Bands read
  // side by side, one thread each, take about half as long. Each band
  // reaches past its edges so a line cut by one is whole in the next, and
  // keeps only the words whose middle is its own.
  const int height = gray.height();
  const int bands =
      std::clamp(std::min(QThread::idealThreadCount() / 2, height / 500), 1, 4);
  const int overlap = 200;
  struct Band {
    int top = 0, from = 0, to = 0;
    std::unique_ptr<QProcess> process;
  };
  std::vector<Band> parts(bands);
  QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
  environment.insert("OMP_THREAD_LIMIT", "1");
  const QString languages =
      qEnvironmentVariable("OMARCHY_OCR_LANGS", "eng").trimmed();
  auto stop = [&parts] {
    for (Band &band : parts)
      if (band.process && band.process->state() != QProcess::NotRunning) {
        band.process->kill();
        band.process->waitForFinished();
      }
  };
  for (int i = 0; i < bands; ++i) {
    Band &band = parts[i];
    band.from = i * height / bands;
    band.to = (i + 1) * height / bands;
    band.top = std::max(0, band.from - overlap);
    const int bottom = std::min(height, band.to + overlap);
    QByteArray input;
    {
      QBuffer buffer(&input);
      buffer.open(QIODevice::WriteOnly);
      if (!gray.copy(0, band.top, gray.width(), bottom - band.top)
               .save(&buffer, "PGM")) {
        stop();
        return std::nullopt;
      }
    }
    band.process = std::make_unique<QProcess>();
    band.process->setProcessEnvironment(environment);
    // Like a video export, reading stays out of the way of the desktop.
    band.process->setChildProcessModifier(
        [] { setpriority(PRIO_PROCESS, 0, 10); });
    // One block of lines, like omarchy-capture-text. Automatic layout
    // reads aligned terminal output column by column and drops lines of
    // dashes, such as a private key's header.
    band.process->start("tesseract",
                        {"stdin", "stdout", "-l",
                         languages.isEmpty() ? "eng" : languages, "--psm", "6",
                         "tsv"});
    if (!band.process->waitForStarted(3000) || cancelled()) {
      stop();
      return std::nullopt;
    }
    // With no event loop, written data waits in QProcess until something
    // waits on it. Hand it all over now so the bands really run together.
    band.process->write(input);
    while (band.process->bytesToWrite() > 0)
      if (!band.process->waitForBytesWritten(3000) || cancelled()) {
        stop();
        return std::nullopt;
      }
    band.process->closeWriteChannel();
  }
  QElapsedTimer timer;
  timer.start();
  for (Band &band : parts)
    while (band.process->state() != QProcess::NotRunning) {
      band.process->waitForFinished(100);
      if (cancelled() || timer.elapsed() > 60000) {
        stop();
        return std::nullopt;
      }
    }
  QVector<Word> words;
  for (int i = 0; i < bands; ++i) {
    const Band &band = parts[i];
    if (band.process->exitStatus() != QProcess::NormalExit ||
        band.process->exitCode() != 0)
      return std::nullopt;
    for (Word word : parseTsv(band.process->readAllStandardOutput())) {
      word.box.translate(0, band.top);
      const int middle = word.box.center().y();
      if (middle < band.from || (middle >= band.to && i < bands - 1))
        continue;
      word.block += i * 100000;
      const QRectF box(word.box);
      word.box = QRectF(box.topLeft() / scale, box.size() / scale)
                     .toAlignedRect();
      words << word;
    }
  }
  return words;
}

QVector<Word> parseTsv(const QByteArray &tsv, double scale) {
  QVector<Word> words;
  for (const QByteArray &row : tsv.split('\n')) {
    const QList<QByteArray> cells = row.split('\t');
    // level page block paragraph line word left top width height conf text
    if (cells.size() < 12 || cells[0] != "5")
      continue;
    const QString text = QString::fromUtf8(cells[11]).trimmed();
    if (text.isEmpty() || cells[10].toDouble() < 0)
      continue;
    Word word;
    word.text = text;
    word.block = cells[2].toInt();
    word.paragraph = cells[3].toInt();
    word.line = cells[4].toInt();
    const QRectF box(cells[6].toDouble(), cells[7].toDouble(),
                     cells[8].toDouble(), cells[9].toDouble());
    word.box = QRectF(box.topLeft() / scale, box.size() / scale)
                   .toAlignedRect();
    words << word;
  }
  return words;
}

// Words on one line, in reading order.
using Line = QVector<const Word *>;
static QVector<Line> lines(const QVector<Word> &words) {
  QVector<Line> result;
  const Word *previous = nullptr;
  for (const Word &word : words) {
    if (!previous || previous->block != word.block ||
        previous->paragraph != word.paragraph || previous->line != word.line)
      result.append(Line{});
    result.last() << &word;
    previous = &word;
  }
  return result;
}

QString text(const QVector<Word> &words) {
  QStringList out;
  const Word *previous = nullptr;
  for (const Line &line : lines(words)) {
    const Word *first = line.first();
    if (previous && (previous->block != first->block ||
                     previous->paragraph != first->paragraph))
      out << QString();
    QStringList parts;
    for (const Word *word : line)
      parts << word->text;
    out << parts.join(' ');
    previous = first;
  }
  return out.join('\n');
}

namespace {
// Words joined into one string, remembering which word each character
// came from so a match can be turned back into a rectangle.
struct Joined {
  QString text;
  QVector<const Word *> word;
  QVector<int> offset;
};
Joined join(const Line &line, int first, int last, const QString &separator) {
  Joined joined;
  for (int i = first; i <= last; ++i) {
    if (i > first)
      for (QChar c : separator) {
        joined.text += c;
        joined.word << nullptr;
        joined.offset << -1;
      }
    const Word *word = line[i];
    for (int c = 0; c < word->text.size(); ++c) {
      joined.text += word->text[c];
      joined.word << word;
      joined.offset << c;
    }
  }
  return joined;
}
// The part of each word a match covers. Characters are assumed to be
// equally wide, so a cut inside a word gets one extra character of room.
QRect cover(const Joined &joined, qsizetype start, qsizetype end) {
  QHash<const Word *, QPair<int, int>> spans;
  for (qsizetype i = start; i < end; ++i) {
    const Word *word = joined.word[i];
    if (!word)
      continue;
    auto it = spans.find(word);
    if (it == spans.end())
      spans.insert(word, {joined.offset[i], joined.offset[i]});
    else
      it->second = joined.offset[i];
  }
  QRect result;
  for (auto it = spans.cbegin(); it != spans.cend(); ++it) {
    const Word *word = it.key();
    const int length = std::max<int>(1, word->text.size());
    const double charWidth = double(word->box.width()) / length;
    double left = word->box.left() + charWidth * it.value().first;
    double right = word->box.left() + charWidth * (it.value().second + 1);
    if (it.value().first > 0)
      left -= charWidth;
    if (it.value().second < length - 1)
      right += charWidth;
    left = std::max<double>(left, word->box.left());
    right = std::min<double>(right, word->box.left() + word->box.width());
    result |= QRect(QPoint(qFloor(left), word->box.top()),
                    QPoint(qCeil(right) - 1, word->box.bottom()));
  }
  return result;
}
QRect bounds(const Line &line) {
  QRect result;
  for (const Word *word : line)
    result |= word->box;
  return result;
}

// Known prefixes, allowing for the letters OCR confuses most.
QString fuzzy(const QString &prefix) {
  QString pattern;
  for (QChar c : prefix) {
    if (QStringLiteral("oO0").contains(c))
      pattern += "[oO0]";
    else if (QStringLiteral("iIl1").contains(c))
      pattern += "[iIl1|]";
    else if (c == 'g' || c == '9')
      pattern += "[g9]";
    else if (c == 's' || c == 'S' || c == '5')
      pattern += "[sS5]";
    else
      pattern += QRegularExpression::escape(QString(c));
  }
  return pattern;
}
QString anyOf(const QStringList &prefixes) {
  QStringList parts;
  for (const QString &prefix : prefixes)
    parts << fuzzy(prefix);
  return "(?:" + parts.join('|') + ")";
}
// What can follow a token prefix: anything but spaces and the punctuation
// that usually surrounds a value.
const QString tail = R"([^\s"'`,;()<>\[\]{}])";
const QString start = R"((?<![A-Za-z0-9]))";

struct Rule {
  QRegularExpression pattern;
  std::function<bool(const QString &)> accept;
  /** Also try it on neighboring words joined together. An address would
   *  swallow the word before it, and OCR rarely splits one. */
  bool joined = true;
};
const QVector<Rule> &tokenRules() {
  static const QVector<Rule> rules = [] {
    QVector<Rule> r;
    r << Rule{QRegularExpression(start +
                                 anyOf({"ghp_", "gho_", "ghs_", "ghu_", "ghr_",
                                        "github_pat_"}) +
                                 tail + "{20,}"),
              {}};
    r << Rule{QRegularExpression(R"((?<![A-Z0-9])(?:AKIA|ASIA|AK[Il1|]A|AS[Il1|]A)[A-Z0-9]{16})"),
              {}};
    r << Rule{QRegularExpression(start + "e[yv]J" + tail + "{27,}"),
              [](const QString &m) { return m.contains('.'); }};
    r << Rule{QRegularExpression(start + anyOf({"xoxb-", "xoxp-", "xoxa-",
                                                "xoxr-", "xoxe-"}) +
                                 tail + "{10,}"),
              {}};
    r << Rule{QRegularExpression(start + anyOf({"sk_live_", "rk_live_"}) +
                                 tail + "{16,}"),
              {}};
    // Anthropic (sk-ant-) and OpenAI (sk-, sk-proj-) keys.
    r << Rule{QRegularExpression(start + fuzzy("sk-") + tail + "{20,}"), {}};
    r << Rule{QRegularExpression(
                  R"([A-Za-z0-9._%+-]+@[A-Za-z0-9-]+(?:\.[A-Za-z0-9-]+)*\.([A-Za-z]{2,}))"),
              [](const QString &m) {
                // git@github.com:owner/repo and name@2x.png are not
                // addresses anyone needs hidden.
                static const QStringList files{"png", "jpg",  "jpeg", "gif",
                                               "svg", "webp", "js",   "ts",
                                               "css", "json", "md",   "txt"};
                const QString domain = m.section('@', 1);
                return !m.startsWith("git@") &&
                       !files.contains(domain.section('.', -1).toLower());
              },
              false};
    return r;
  }();
  return rules;
}

bool luhn(const QString &digits) {
  int sum = 0;
  bool twice = false;
  for (qsizetype i = digits.size() - 1; i >= 0; --i) {
    int d = digits[i].digitValue();
    if (twice && (d *= 2) > 9)
      d -= 9;
    sum += d;
    twice = !twice;
  }
  return sum % 10 == 0;
}
bool iban(QString value) {
  value.remove(' ');
  if (value.size() < 15 || value.size() > 34)
    return false;
  const QString moved = value.mid(4) + value.left(4);
  int remainder = 0;
  for (QChar c : moved) {
    const int n = c.isDigit() ? c.digitValue() : c.unicode() - 'A' + 10;
    remainder = (remainder * (n > 9 ? 100 : 10) + n) % 97;
  }
  return remainder == 1;
}
bool publicAddress(const QRegularExpressionMatch &m) {
  int octet[4];
  for (int i = 0; i < 4; ++i)
    if ((octet[i] = m.captured(i + 1).toInt()) > 255)
      return false;
  // All single digits reads as a version number, like 1.2.3.4.
  if (octet[0] < 10 && octet[1] < 10 && octet[2] < 10 && octet[3] < 10)
    return false;
  const int a = octet[0], b = octet[1];
  return !(a == 0 || a == 10 || a == 127 || a >= 224 ||
           (a == 169 && b == 254) || (a == 172 && b >= 16 && b <= 31) ||
           (a == 192 && b == 168) || (a == 100 && b >= 64 && b <= 127));
}
bool base64Line(const Line &line) {
  if (line.size() > 3)
    return false;
  QString joined;
  for (const Word *word : line)
    joined += word->text;
  if (joined.size() < 56)
    return false;
  static const QRegularExpression hex("^[0-9a-fA-F]+$");
  if (hex.match(joined).hasMatch())
    return false;
  int base64 = 0;
  for (QChar c : joined)
    if (c.isLetterOrNumber() || c == '+' || c == '/' || c == '=')
      ++base64;
  return base64 >= joined.size() * 0.95;
}
} // namespace

QVector<QRect> findSecrets(const QVector<Word> &words) {
  QVector<QRect> found;
  const QVector<Line> all = lines(words);
  static const QRegularExpression card(
      R"((?<![\d.,])(?:\d{3,8}(?:[ -]\d{3,8}){1,5}|\d{13,19})(?![\d]|[.,]\d))");
  static const QRegularExpression ibanPattern(
      R"((?<![A-Za-z0-9])[A-Z]{2}\d{2}(?: ?[A-Z0-9]{4}){2,7}(?: ?[A-Z0-9]{1,3})?(?![A-Za-z0-9]))");
  static const QRegularExpression address(
      R"((?<![\w.])(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})(?!\d|\.\d))");
  static const QRegularExpression keyStart(
      R"(BEG[Il1|]N\b.*\bPR[Il1|]VATE\s+KEY)");
  static const QRegularExpression keyEnd(R"(\bEND\b.*\bPR[Il1|]VATE\s+KEY)");
  for (qsizetype l = 0; l < all.size(); ++l) {
    const Line &line = all[l];
    // Tokens, also across words OCR split apart.
    for (int first = 0; first < line.size(); ++first)
      for (int last = first; last < std::min<int>(line.size(), first + 3);
           ++last) {
        if (last > first) {
          const QRect a = line[last - 1]->box, b = line[last]->box;
          if (b.left() - a.right() > std::max(a.height(), b.height()))
            break;
        }
        const Joined joined = join(line, first, last, {});
        for (const Rule &rule : tokenRules()) {
          if (last > first && !rule.joined)
            continue;
          auto it = rule.pattern.globalMatch(joined.text);
          while (it.hasNext()) {
            const auto m = it.next();
            if (!rule.accept || rule.accept(m.captured()))
              found << cover(joined, m.capturedStart(), m.capturedEnd());
          }
        }
      }
    const Joined spaced = join(line, 0, line.size() - 1, " ");
    auto cards = card.globalMatch(spaced.text);
    while (cards.hasNext()) {
      const auto m = cards.next();
      // Try every run of whole groups, so a card next to other numbers is
      // still found.
      static const QRegularExpression group(R"(\d+)");
      QVector<QRegularExpressionMatch> groups;
      auto g = group.globalMatch(m.captured());
      while (g.hasNext())
        groups << g.next();
      qsizetype bestStart = -1, bestEnd = -1;
      for (int i = 0; i < groups.size(); ++i) {
        QString digits;
        for (int j = i; j < groups.size(); ++j) {
          digits += groups[j].captured();
          if (digits.size() > 19)
            break;
          if (digits.size() >= 13 && luhn(digits) &&
              groups[j].capturedEnd() - groups[i].capturedStart() >
                  bestEnd - bestStart) {
            bestStart = groups[i].capturedStart();
            bestEnd = groups[j].capturedEnd();
          }
        }
      }
      if (bestStart >= 0)
        found << cover(spaced, m.capturedStart() + bestStart,
                       m.capturedStart() + bestEnd);
    }
    auto ibans = ibanPattern.globalMatch(spaced.text);
    while (ibans.hasNext()) {
      const auto m = ibans.next();
      QString value = m.captured();
      qsizetype end = m.capturedEnd();
      while (value.size() >= 15 && !iban(value)) {
        const qsizetype cut = value.lastIndexOf(' ');
        if (cut < 0)
          break;
        end -= value.size() - cut;
        value.truncate(cut);
      }
      if (iban(value))
        found << cover(spaced, m.capturedStart(), end);
    }
    auto addresses = address.globalMatch(spaced.text);
    while (addresses.hasNext()) {
      const auto m = addresses.next();
      if (publicAddress(m))
        found << cover(spaced, m.capturedStart(), m.capturedEnd());
    }
    // A private key: the header and everything down to its end line.
    if (keyStart.match(spaced.text).hasMatch()) {
      QRect key = bounds(line);
      for (qsizetype next = l + 1; next < all.size() && next <= l + 100;
           ++next) {
        key |= bounds(all[next]);
        if (keyEnd.match(join(all[next], 0, all[next].size() - 1, " ").text)
                .hasMatch())
          break;
      }
      found << key;
    }
    // A key body whose header OCR dropped or that is out of the picture:
    // two or more lines in a row that are one long run of base64.
    if (base64Line(line)) {
      qsizetype next = l + 1;
      QRect body = bounds(line);
      while (next < all.size() && base64Line(all[next])) {
        const QRect below = bounds(all[next]);
        if (below.top() - body.bottom() > 2 * below.height())
          break;
        body |= below;
        ++next;
      }
      if (next - l >= 2)
        found << body;
    }
  }
  // Overlapping finds are one secret.
  for (bool merged = true; merged;) {
    merged = false;
    for (qsizetype i = 0; i < found.size() && !merged; ++i)
      for (qsizetype j = i + 1; j < found.size(); ++j)
        if (found[i].adjusted(-1, -1, 1, 1).intersects(found[j])) {
          found[i] |= found[j];
          found.removeAt(j);
          merged = true;
          break;
        }
  }
  std::sort(found.begin(), found.end(), [](const QRect &a, const QRect &b) {
    return a.top() != b.top() ? a.top() < b.top() : a.left() < b.left();
  });
  return found;
}
} // namespace Ocr
