#include "syntaxhighlighter.h"

#include <QRegularExpression>
#include <QTextDocument>

namespace {

QTextCharFormat makeFormat(const QColor &fg, bool bold = false, bool italic = false) {
    QTextCharFormat f;
    f.setForeground(fg);
    if (bold) f.setFontWeight(QFont::Bold);
    if (italic) f.setFontItalic(true);
    return f;
}

} // namespace

ScriptHighlighter::ScriptHighlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent) {
    const QColor keywordCol(0xC5, 0x8A, 0x00);
    const QColor primitiveCol(0x00, 0x77, 0xCC);
    const QColor directiveCol(0x99, 0x00, 0x99);
    const QColor labelCol(0x00, 0x88, 0x44);
    const QColor commentCol(0x80, 0x90, 0x80);
    const QColor shellCol(0xAA, 0x44, 0x00);

    m_commentFormat = makeFormat(commentCol, false, true);
    m_shellFormat   = makeFormat(shellCol);
    m_numberFormat  = makeFormat(QColor(0xB0, 0x30, 0x60));

    auto add = [this](const QString &pattern, const QTextCharFormat &fmt) {
        m_rules.append({ QRegularExpression(pattern), fmt });
    };

    // Control flow
    const QTextCharFormat kw = makeFormat(keywordCol, true);
    add(QStringLiteral("\\b(LABEL:|GOTO:)\\b"), makeFormat(labelCol, true));
    add(QStringLiteral("\\b(IF|ELIF|ELSE|ENDIF|WHILE|ENDWHILE|BREAK|CONTINUE)\\b"), kw);

    // Drawing primitives (first token of a draw command)
    const QString prims = QStringLiteral(
        "GRAPH|RECT|LINE|PIXEL|TEXT|BAR|PIE|VBATCHBAR|CLR|STARTDELAY|ENDDELAY");
    add(QStringLiteral("^\\s*(") + prims + QStringLiteral(")\\b"),
        makeFormat(primitiveCol, true));

    // Directives
    add(QStringLiteral("^\\s*(!set|!led)\\b"), makeFormat(directiveCol, true));
    // Directive arguments worth picking out
    add(QStringLiteral("\\b(delay|backlight|contrast|mkeys|keyhandler)\\b"),
        makeFormat(directiveCol));
}

void ScriptHighlighter::highlightBlock(const QString &text) {
    // Embedded shell command: // ... //  (drawn after the draw arguments)
    const int shellStart = text.indexOf(QStringLiteral("//"));
    if (shellStart >= 0) {
        // Highlight the delimiters and everything between them as shell.
        setFormat(shellStart, 2, m_shellFormat);
        int end = text.indexOf(QStringLiteral("//"), shellStart + 2);
        if (end > shellStart)
            setFormat(shellStart + 2, end - (shellStart + 2), m_shellFormat);
    }

    for (const Rule &r : m_rules) {
        QRegularExpressionMatchIterator it = r.pattern.globalMatch(text);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            setFormat(m.capturedStart(), m.capturedLength(), r.format);
        }
    }

    // Numbers, skipping anything inside an embedded shell command.
    static const QRegularExpression num(QStringLiteral("\\b\\d+(\\.\\d+)?\\b"));
    QRegularExpressionMatchIterator it = num.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        if (shellStart >= 0 && m.capturedStart() >= shellStart)
            continue;
        setFormat(m.capturedStart(), m.capturedLength(), m_numberFormat);
    }

    // A whole-line comment wins over everything else.
    const int hash = text.indexOf(QLatin1Char('#'));
    if (hash >= 0 && (shellStart < 0 || hash < shellStart))
        setFormat(hash, text.length() - hash, m_commentFormat);
}
