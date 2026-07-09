#ifndef SPAN_HIGHLIGHTER_H
#define SPAN_HIGHLIGHTER_H

#include <QColor>
#include <QQuickTextDocument>
#include <QSyntaxHighlighter>
#include <QVector>

// Mark styling applied DIRECTLY to the block editor's QTextDocument, so the
// live editor is the one and only face of a block: character formats
// (weight, slant, ink, underline) never touch the text or its offsets, which
// keeps the CRDT diff/shadow machinery byte-identical while the user sees
// styled text in both view and edit.
//
// Inline math goes one step further: a span the caret is NOT inside is
// "concealed" — transparent ink plus per-character letter-spacing that
// squeezes the raw TeX to exactly the rendered image's width (the QML
// overlay draws the image over the span's rect). Text flows around the
// equation as if the image were in the document, with no ghost gap; moving
// the caret into the span lifts the conceal and the source expands in
// place. `revision` ticks after every rehighlight so geometry readers
// (positionToRectangle callers) know to re-query.
class SpanHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT
    Q_PROPERTY(QQuickTextDocument *document READ quickDocument WRITE setQuickDocument NOTIFY documentChanged)
    Q_PROPERTY(QString spansJson READ spansJson WRITE setSpansJson NOTIFY spansJsonChanged)
    Q_PROPERTY(int caret READ caret WRITE setCaret NOTIFY caretChanged)
    Q_PROPERTY(QColor codeInk READ codeInk WRITE setCodeInk NOTIFY inksChanged)
    Q_PROPERTY(QColor codeBlockInk READ codeBlockInk WRITE setCodeBlockInk NOTIFY inksChanged)
    Q_PROPERTY(QColor mathInk READ mathInk WRITE setMathInk NOTIFY inksChanged)
    // Code-panel syntax highlighting: set `language` (python/javascript/
    // rust/c) to run the little lexer; empty = off.
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    Q_PROPERTY(QColor kwInk READ kwInk WRITE setKwInk NOTIFY inksChanged)
    Q_PROPERTY(QColor strInk READ strInk WRITE setStrInk NOTIFY inksChanged)
    Q_PROPERTY(QColor cmtInk READ cmtInk WRITE setCmtInk NOTIFY inksChanged)
    Q_PROPERTY(QColor numInk READ numInk WRITE setNumInk NOTIFY inksChanged)
    // Inline embeds (U+FFFC atoms) concealed to a chip's width, same trick
    // as math: JSON [{pos,w}]. The QML overlay draws the chip.
    Q_PROPERTY(QString embedsJson READ embedsJson WRITE setEmbedsJson NOTIFY embedsJsonChanged)
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)

public:
    explicit SpanHighlighter(QObject *parent = nullptr);

    QQuickTextDocument *quickDocument() const { return m_quickDoc; }
    void setQuickDocument(QQuickTextDocument *doc);

    QString spansJson() const { return m_spansJson; }
    void setSpansJson(const QString &json);

    int caret() const { return m_caret; }
    void setCaret(int caret);

    QColor codeInk() const { return m_codeInk; }
    void setCodeInk(const QColor &c);
    QColor codeBlockInk() const { return m_codeBlockInk; }
    void setCodeBlockInk(const QColor &c);
    QColor mathInk() const { return m_mathInk; }
    void setMathInk(const QColor &c);
    QColor kwInk() const { return m_kwInk; }
    void setKwInk(const QColor &c);
    QColor strInk() const { return m_strInk; }
    void setStrInk(const QColor &c);
    QColor cmtInk() const { return m_cmtInk; }
    void setCmtInk(const QColor &c);
    QColor numInk() const { return m_numInk; }
    void setNumInk(const QColor &c);

    QString language() const { return m_language; }
    void setLanguage(const QString &lang);

    QString embedsJson() const { return m_embedsJson; }
    void setEmbedsJson(const QString &json);

    int revision() const { return m_revision; }

signals:
    void documentChanged();
    void spansJsonChanged();
    void caretChanged();
    void inksChanged();
    void languageChanged();
    void embedsJsonChanged();
    void revisionChanged();

protected:
    void highlightBlock(const QString &text) override;

private:
    struct Span {
        int start = 0;
        int end = 0;
        bool bold = false;
        bool italic = false;
        bool code = false;
        bool codeBlock = false;
        bool math = false;
        bool eqBlock = false;
        bool comment = false;
        qreal concealW = 0; // >0: target width when concealed (inline math)
    };

    bool concealed(const Span &s) const
    {
        return s.math && !s.eqBlock && s.concealW > 0
               && !(m_caret >= s.start && m_caret <= s.end);
    }
    void refresh();
    void lexLine(const QString &text);
    // Reserve embed atoms' chip widths in the layout via the inline
    // text-object handler (U+FFFC is object-itemized; fonts can't size it).
    void applyEmbedFormats();

    QQuickTextDocument *m_quickDoc = nullptr;
    QString m_spansJson;
    QVector<Span> m_spans;
    QString m_embedsJson;
    QVector<QPair<int, qreal>> m_embeds; // (global pos, chip width)
    QString m_language;
    int m_caret = -1;
    QColor m_codeInk;
    QColor m_codeBlockInk;
    QColor m_mathInk;
    QColor m_kwInk;
    QColor m_strInk;
    QColor m_cmtInk;
    QColor m_numInk;
    int m_revision = 0;
};

#endif // SPAN_HIGHLIGHTER_H
