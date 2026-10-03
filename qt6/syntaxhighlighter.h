/*
 *  syntaxhighlighter.h - QSyntaxHighlighter for the g510s display script
 *  (~/.config/g510s/display.txt).
 *
 *  The script language is a small drawing/control language parsed by
 *  g510s-clock.c:
 *
 *      # a comment
 *      !set delay 300              directive
 *      led_controller  ui                     directive
 *      LABEL: / GOTO: / IF, ...    control flow
 *      RECT,0,25,160,1             drawing primitive
 *      80,2,C,0,0,// printf "hi" // draw + embedded shell command
 *
 *  g510s is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 3 of the License, or (at your
 *  option) any later version.
 *
 *  Copyright © 2025 usr_40476
 */

#pragma once

#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QVector>

class ScriptHighlighter : public QSyntaxHighlighter {
    Q_OBJECT

public:
    explicit ScriptHighlighter(QTextDocument *parent = nullptr);

protected:
    void highlightBlock(const QString &text) override;

private:
    struct Rule {
        QRegularExpression pattern;
        QTextCharFormat format;
    };

    QVector<Rule> m_rules;
    QTextCharFormat m_commentFormat;
    QTextCharFormat m_shellFormat;
    QTextCharFormat m_numberFormat;
};
