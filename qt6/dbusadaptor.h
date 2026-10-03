/*
 *  dbusadaptor.h - Qt D-Bus adaptor exposing the org.g510s.control interface.
 *
 *  Keeps the exact same bus name, object path, method signatures and property
 *  names as the previous GLib GDBus implementation, so existing callers (and
 *  tests/dbus-color-test.sh) work unchanged.
 *
 *  g510s is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 3 of the License, or (at your
 *  option) any later version.
 *
 *  Copyright © 2025-2026 usr40k
 */

#pragma once

#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

class Backend;

class ControlAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.g510s.control")
    Q_PROPERTY(int  Mode        READ mode        WRITE setMode)
    Q_PROPERTY(QList<int> Color READ color       WRITE setColor)
    Q_PROPERTY(bool ColorFade   READ colorFade   WRITE setColorFade)
    Q_PROPERTY(bool AutoSaveOnQuit READ autoSaveOnQuit WRITE setAutoSaveOnQuit)
    Q_PROPERTY(bool GuiHidden   READ guiHidden   WRITE setGuiHidden)

public:
    explicit ControlAdaptor(Backend *backend);

    int  mode() const;
    void setMode(int mode);

    QList<int> color() const;
    void       setColor(const QList<int> &rgb);

    bool colorFade() const;
    void setColorFade(bool fade);

    bool autoSaveOnQuit() const;
    void setAutoSaveOnQuit(bool on);

    bool guiHidden() const;
    void setGuiHidden(bool hidden);

public slots:
    void SetMode(int mode);
    void SetColor(int red, int green, int blue, bool save);
    void SetColorFade(bool fade);
    void SetAutoSaveOnQuit(bool autosave);
    void SetGuiHidden(bool hidden);
    void SaveConfig();

    QString     GetMacro(int mode, int index);
    void        SetMacro(int mode, int index, const QString &macro);
    void        RunMacro(int mode, int index);

    void        SavePreset(const QString &name);
    void        LoadPreset(const QString &name);
    QStringList ListPresets();

private:
    void emitChanged();
    void setLedReport(int red, int green, int blue);

    Backend *m_backend;
};