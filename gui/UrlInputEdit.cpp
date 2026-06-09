#include "UrlInputEdit.hpp"

#include "StreamProvider.hpp"

#include <QMimeData>
#include <QRegularExpression>
#include <QUrl>

namespace {

QString extractUrlFromMimeData(const QMimeData *mime) {
    if (mime == nullptr) {
        return {};
    }

    QString merged = mime->text();

    if (mime->hasHtml()) {
        static const QRegularExpression hrefRe(
            QStringLiteral(R"(href\s*=\s*["'](https?://[^"']+)["'])"),
            QRegularExpression::CaseInsensitiveOption);
        QRegularExpressionMatchIterator hit = hrefRe.globalMatch(mime->html());
        while (hit.hasNext()) {
            merged += QLatin1Char(' ');
            merged += hit.next().captured(1);
        }
    }

    if (mime->hasUrls()) {
        for (const QUrl &u : mime->urls()) {
            if (u.isValid() && u.scheme().startsWith(QStringLiteral("http"), Qt::CaseInsensitive)) {
                merged += QLatin1Char(' ');
                merged += u.toString(QUrl::FullyEncoded);
            }
        }
    }

    return streamProviderExtractUrl(merged.trimmed());
}

} // namespace

UrlInputEdit::UrlInputEdit(QWidget *parent) : QTextEdit(parent) {
    setAcceptRichText(false);
}

void UrlInputEdit::insertFromMimeData(const QMimeData *source) {
    const QString url = extractUrlFromMimeData(source);
    if (!url.isEmpty() &&
        (url.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive) ||
         url.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive))) {
        setPlainText(url);
        return;
    }

    QTextEdit::insertFromMimeData(source);
}
