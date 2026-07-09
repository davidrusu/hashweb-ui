#include "SpanHighlighter.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QImage>
#include <QStandardPaths>
#include <QUrl>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlEngine>
#include <QSet>
#include <QTextDocument>

SpanHighlighter::SpanHighlighter(QObject *parent)
    : QSyntaxHighlighter(parent)
{
}

void SpanHighlighter::setQuickDocument(QQuickTextDocument *doc)
{
    if (doc == m_quickDoc)
        return;
    m_quickDoc = doc;
    setDocument(doc ? doc->textDocument() : nullptr);
    emit documentChanged();
}

void SpanHighlighter::setSpansJson(const QString &json)
{
    if (json == m_spansJson)
        return;
    m_spansJson = json;
    m_spans.clear();
    const QJsonArray arr = QJsonDocument::fromJson(json.toUtf8()).array();
    int off = 0;
    for (const auto &v : arr) {
        const QJsonObject o = v.toObject();
        Span s;
        s.start = off;
        s.end = off + o.value("len").toInt();
        off = s.end;
        for (const auto &kv : o.value("kinds").toArray()) {
            const QString k = kv.toString();
            if (k == "bold") s.bold = true;
            else if (k == "italic") s.italic = true;
            else if (k == "code") s.code = true;
            else if (k == "codeblock") s.codeBlock = true;
            else if (k == "math") s.math = true;
            else if (k == "eqblock") s.eqBlock = true;
            else if (k.startsWith("comment:")) s.comment = true;
        }
        s.concealW = o.value("concealW").toDouble();
        m_spans.append(s);
    }
    emit spansJsonChanged();
    refresh();
}

void SpanHighlighter::setCaret(int caret)
{
    if (caret == m_caret)
        return;
    // Only the conceal decision reads the caret; skip the rehighlight when
    // no span flips (i.e. on almost every caret move).
    QVector<bool> before;
    for (const auto &s : m_spans)
        before.append(concealed(s));
    m_caret = caret;
    emit caretChanged();
    for (int i = 0; i < m_spans.size(); i++) {
        if (before[i] != concealed(m_spans[i])) {
            refresh();
            return;
        }
    }
}

void SpanHighlighter::setCodeInk(const QColor &c)
{
    if (c == m_codeInk) return;
    m_codeInk = c;
    emit inksChanged();
    refresh();
}

void SpanHighlighter::setCodeBlockInk(const QColor &c)
{
    if (c == m_codeBlockInk) return;
    m_codeBlockInk = c;
    emit inksChanged();
    refresh();
}

void SpanHighlighter::setMathInk(const QColor &c)
{
    if (c == m_mathInk) return;
    m_mathInk = c;
    emit inksChanged();
    refresh();
}

void SpanHighlighter::setKwInk(const QColor &c)
{
    if (c == m_kwInk) return;
    m_kwInk = c;
    emit inksChanged();
    refresh();
}

void SpanHighlighter::setStrInk(const QColor &c)
{
    if (c == m_strInk) return;
    m_strInk = c;
    emit inksChanged();
    refresh();
}

void SpanHighlighter::setCmtInk(const QColor &c)
{
    if (c == m_cmtInk) return;
    m_cmtInk = c;
    emit inksChanged();
    refresh();
}

void SpanHighlighter::setNumInk(const QColor &c)
{
    if (c == m_numInk) return;
    m_numInk = c;
    emit inksChanged();
    refresh();
}

void SpanHighlighter::setLanguage(const QString &lang)
{
    if (lang == m_language) return;
    m_language = lang;
    emit languageChanged();
    refresh();
}

void SpanHighlighter::setEmbedsJson(const QString &json)
{
    if (json == m_embedsJson) return;
    m_embedsJson = json;
    m_embeds.clear();
    const QJsonArray arr = QJsonDocument::fromJson(json.toUtf8()).array();
    for (const auto &v : arr) {
        const QJsonObject o = v.toObject();
        m_embeds.append({o.value("pos").toInt(), o.value("w").toDouble()});
    }
    emit embedsJsonChanged();
    refresh();
}

void SpanHighlighter::refresh()
{
    if (document())
        rehighlight();
    m_revision++;
    emit revisionChanged();
}

namespace {

// Tiny per-line lexers: keywords, strings, comments, numbers. Stateless
// across lines (multi-line strings degrade to per-line coloring) — plenty
// for a notes tool's code panels.
const QSet<QString> &keywordsFor(const QString &lang)
{
    static const QSet<QString> python = {
        "def", "return", "if", "elif", "else", "for", "while", "in", "not",
        "and", "or", "import", "from", "as", "class", "try", "except",
        "finally", "with", "lambda", "pass", "break", "continue", "yield",
        "raise", "None", "True", "False", "global", "nonlocal", "assert",
        "async", "await", "is", "del"};
    static const QSet<QString> javascript = {
        "function", "return", "if", "else", "for", "while", "in", "of",
        "var", "let", "const", "class", "extends", "new", "this", "typeof",
        "instanceof", "try", "catch", "finally", "throw", "switch", "case",
        "default", "break", "continue", "yield", "async", "await", "null",
        "undefined", "true", "false", "import", "from", "export", "delete"};
    static const QSet<QString> rust = {
        "fn", "return", "if", "else", "for", "while", "loop", "in", "let",
        "mut", "const", "static", "struct", "enum", "impl", "trait", "pub",
        "use", "mod", "crate", "self", "Self", "match", "move", "ref",
        "break", "continue", "async", "await", "dyn", "where", "unsafe",
        "true", "false", "as", "type"};
    static const QSet<QString> c = {
        "int", "char", "float", "double", "void", "long", "short",
        "unsigned", "signed", "return", "if", "else", "for", "while", "do",
        "switch", "case", "default", "break", "continue", "struct", "union",
        "enum", "typedef", "const", "static", "extern", "sizeof", "goto",
        "auto", "bool", "true", "false", "class", "public", "private",
        "namespace", "template", "typename", "virtual", "override", "new",
        "delete", "nullptr", "include"};
    static const QSet<QString> none;
    if (lang == QLatin1String("python")) return python;
    if (lang == QLatin1String("javascript") || lang == QLatin1String("js")) return javascript;
    if (lang == QLatin1String("rust")) return rust;
    if (lang == QLatin1String("c") || lang == QLatin1String("cpp") || lang == QLatin1String("c++")) return c;
    return none;
}

} // namespace

void SpanHighlighter::lexLine(const QString &text)
{
    const QSet<QString> &kw = keywordsFor(m_language);
    if (kw.isEmpty())
        return;
    const QString commentLead = m_language == QLatin1String("python")
                                    ? QStringLiteral("#")
                                    : QStringLiteral("//");
    QTextCharFormat kwFmt, strFmt, cmtFmt, numFmt;
    kwFmt.setForeground(m_kwInk);
    kwFmt.setFontWeight(QFont::Bold);
    strFmt.setForeground(m_strInk);
    cmtFmt.setForeground(m_cmtInk);
    cmtFmt.setFontItalic(true);
    numFmt.setForeground(m_numInk);

    const int n = text.length();
    int i = 0;
    while (i < n) {
        const QChar ch = text[i];
        if (QStringView(text).mid(i).startsWith(commentLead)) {
            setFormat(i, n - i, cmtFmt);
            return;
        }
        if (ch == QLatin1Char('"') || ch == QLatin1Char('\'')) {
            int j = i + 1;
            while (j < n && text[j] != ch) {
                if (text[j] == QLatin1Char('\\')) j++;
                j++;
            }
            setFormat(i, qMin(j + 1, n) - i, strFmt);
            i = j + 1;
            continue;
        }
        if (ch.isDigit()) {
            int j = i;
            while (j < n && (text[j].isLetterOrNumber() || text[j] == QLatin1Char('.')
                             || text[j] == QLatin1Char('_')))
                j++;
            setFormat(i, j - i, numFmt);
            i = j;
            continue;
        }
        if (ch.isLetter() || ch == QLatin1Char('_')) {
            int j = i;
            while (j < n && (text[j].isLetterOrNumber() || text[j] == QLatin1Char('_')))
                j++;
            if (kw.contains(text.mid(i, j - i)))
                setFormat(i, j - i, kwFmt);
            i = j;
            continue;
        }
        i++;
    }
}

void SpanHighlighter::highlightBlock(const QString &text)
{
    // Syntax first: the mark/conceal passes below override token colors
    // where they overlap (marks carry intent; tokens are decoration).
    if (!m_language.isEmpty())
        lexLine(text);
    if (m_spans.isEmpty() && m_embeds.isEmpty())
        return;
    // QTextDocument counts the separator between text blocks as one
    // character, matching "\n" in the plain-text model — global span
    // offsets translate by the block's start position.
    const int base = currentBlock().position();
    const int blockLen = text.length();

    for (const auto &s : m_spans) {
        // A markless span has nothing to say — and an empty QTextCharFormat
        // would ERASE the lexer's token colors under it (setFormat replaces).
        if (!(s.bold || s.italic || s.code || s.codeBlock || s.math
              || s.eqBlock || s.comment))
            continue;
        const int from = qMax(s.start - base, 0);
        const int to = qMin(s.end - base, blockLen);
        if (from >= to)
            continue;

        QTextCharFormat fmt;
        if (s.bold)
            fmt.setFontWeight(QFont::Bold);
        if (s.italic)
            fmt.setFontItalic(true);
        if (s.code && m_codeInk.isValid()) {
            fmt.setForeground(m_codeInk);
            fmt.setFontFamilies({QStringLiteral("Menlo")});
        }
        if (s.codeBlock && m_codeBlockInk.isValid())
            fmt.setForeground(m_codeBlockInk);
        if ((s.math || s.eqBlock) && m_mathInk.isValid()) {
            fmt.setForeground(m_mathInk);
            fmt.setFontItalic(true);
            fmt.setFontFamilies({QStringLiteral("Menlo")});
        }
        if (s.comment)
            fmt.setFontUnderline(true);

        if (concealed(s)) {
            // Squeeze the span to the rendered image's width: transparent
            // ink, and letter-spacing chosen so n characters advance
            // exactly concealW pixels in total. Keep a sliver of advance
            // per character so caret hit-testing stays monotonic. Fresh
            // format: the visible branch's family/slant would skew the
            // width the metrics below assume.
            fmt = QTextCharFormat();
            const int n = s.end - s.start;
            const qreal natural =
                QFontMetricsF(document()->defaultFont())
                    .horizontalAdvance(text.mid(from, to - from))
                * qreal(n) / qreal(qMax(to - from, 1));
            qreal delta = (s.concealW - natural) / qreal(qMax(n, 1));
            const qreal perChar = natural / qreal(qMax(n, 1));
            if (perChar + delta < 0.5)
                delta = 0.5 - perChar;
            fmt.setForeground(Qt::transparent);
            fmt.setFontLetterSpacingType(QFont::AbsoluteSpacing);
            fmt.setFontLetterSpacing(delta);
        }

        setFormat(from, to - from, fmt);
    }

    // Embed atoms (U+FFFC) squeeze to their chip's width — same conceal
    // trick as math, but always on (an atom has no source form to edit).
    for (const auto &e : m_embeds) {
        const int local = e.first - base;
        if (local < 0 || local >= blockLen)
            continue;
        const qreal adv = QFontMetricsF(document()->defaultFont())
                              .horizontalAdvance(text.at(local));
        QTextCharFormat fmt;
        fmt.setForeground(Qt::transparent);
        fmt.setFontLetterSpacingType(QFont::AbsoluteSpacing);
        fmt.setFontLetterSpacing(qMax(e.second - adv, 0.5 - adv));
        setFormat(local, 1, fmt);
    }
}

// Basecamp's plugin sandbox only lets QML load file:// URLs under the
// plugin's OWN directories (its engine also rejects image:// providers and
// data: URLs — everything non-allowlisted). So rendered math is saved as a
// PNG inside the plugin tree when it's writable (the installed-plugin case),
// falling back to the user cache dir when the QML lives in the read-only
// nix store (the standalone runner, whose engine has no interceptor).
class KbMath : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    // `image` is an ItemGrabResult.image (QImage in a QVariant); `qmlDir`
    // is Qt.resolvedUrl(".") from the view. Returns a loadable URL.
    Q_INVOKABLE QString saveImage(const QString &key, const QVariant &image,
                                  const QUrl &qmlDir) const
    {
        const QImage img = image.value<QImage>();
        if (img.isNull() || key.isEmpty())
            return QString();
        QString cache = qmlDir.toLocalFile() + QStringLiteral("/.mathcache");
        if (!QDir().mkpath(cache) || !QFileInfo(cache).isWritable())
            cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                    + QStringLiteral("/hashweb-math");
        if (!QDir().mkpath(cache))
            return QString();
        const QString path = cache + QLatin1Char('/') + key
                             + QStringLiteral(".png");
        if (!QFile::exists(path))
            img.save(path, "PNG");
        return QUrl::fromLocalFile(path).toString();
    }
};

// QML has no clipboard API; the view process has a QGuiApplication. Exposed
// as the KbClipboard singleton so paste interception can read the text.
class KbClipboard : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    Q_INVOKABLE QString text() const
    {
        const QClipboard *cb = QGuiApplication::clipboard();
        return cb ? cb->text() : QString();
    }
};

// Registered the same way the replica factory registers the backend type:
// process-global, at load time of this dylib in the VIEW process (the QML
// scene's process — the module plugin itself runs in ui-host, no QML there).
namespace {
void registerSpanHighlighter()
{
    qmlRegisterType<SpanHighlighter>("Logos.HashwebKb", 1, 0, "SpanHighlighter");
    qmlRegisterSingletonType<KbClipboard>(
        "Logos.HashwebKb", 1, 0, "KbClipboard",
        [](QQmlEngine *, QJSEngine *) -> QObject * { return new KbClipboard; });
    qmlRegisterSingletonType<KbMath>(
        "Logos.HashwebKb", 1, 0, "KbMath",
        [](QQmlEngine *, QJSEngine *) -> QObject * { return new KbMath; });
}
} // namespace

Q_COREAPP_STARTUP_FUNCTION(registerSpanHighlighter)

#include "SpanHighlighter.moc"
