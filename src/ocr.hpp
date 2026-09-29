#pragma once
#include <QImage>
#include <QRect>
#include <QString>
#include <QVector>
#include <atomic>
#include <optional>

/** Reading the text in a screenshot with tesseract, and spotting what looks
 *  like a secret in it. Nothing here touches the disk. */
namespace Ocr {
struct Word {
  QString text;
  /** In pixels of the image that was read. */
  QRect box;
  int block = 0, paragraph = 0, line = 0;
};
/** Whether tesseract is installed. Without it the feature stays hidden. */
bool available();
/** Reads the words in `image`. Blocks, so call it from a worker thread.
 *  Returns nothing if tesseract is missing, fails, or `cancel` is set. */
std::optional<QVector<Word>> read(const QImage &image,
                                  const std::atomic_bool *cancel = nullptr);
/** Parses tesseract's TSV output, scaling boxes down by `scale`. */
QVector<Word> parseTsv(const QByteArray &tsv, double scale = 1);
/** The words as plain text: one line per line, a blank line between
 *  paragraphs. */
QString text(const QVector<Word> &words);
/** Rectangles, in the same pixels as the word boxes, over everything that
 *  looks like a secret: emails, tokens and keys, card numbers, IBANs,
 *  public IPv4 addresses and private keys. */
QVector<QRect> findSecrets(const QVector<Word> &words);
} // namespace Ocr
