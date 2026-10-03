/*
 *  backend.h - C++/Qt bridge between the QML frontend and the C g510s core.
 *
 *  g510s is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 3 of the License, or (at your
 *  option) any later version.
 *
 *  Copyright © 2025-2026 usr40k
 */

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QColor>
#include <QRect>
#include <QTimer>
#include <QQmlEngine>
#include <QQuickPaintedItem>
#include <QPainter>
#include <QMouseEvent>

extern "C" {
#include "g510s.h"
}

class LcdPreviewItem;

// Exposes g510s_data, the preset store and the LCD preview buffer to QML.
class Backend : public QObject {
    Q_OBJECT

    Q_PROPERTY(int mode READ mode WRITE setMode NOTIFY modeChanged)
    // Fired whenever any bank's colour changes, from the UI or from D-Bus,
    // so every colour control can re-sync.
    Q_PROPERTY(QString colorRevision READ colorRevision NOTIFY colorChanged)
    Q_PROPERTY(bool deviceFound READ deviceFound NOTIFY deviceFoundChanged)
    Q_PROPERTY(bool colorFade READ colorFade WRITE setColorFade NOTIFY colorFadeChanged)
    Q_PROPERTY(bool autoSaveOnQuit READ autoSaveOnQuit WRITE setAutoSaveOnQuit NOTIFY autoSaveOnQuitChanged)
    Q_PROPERTY(bool guiHidden READ guiHidden WRITE setGuiHidden NOTIFY guiHiddenChanged)
    Q_PROPERTY(QStringList presets READ presets NOTIFY presetsChanged)
    Q_PROPERTY(QString displayScript READ displayScript NOTIFY displayScriptChanged)
    Q_PROPERTY(QString displayScriptPath READ displayScriptPath CONSTANT)
    Q_PROPERTY(QString version READ version CONSTANT)

    // --- App settings (persisted to ~/.config/g510s/settings.conf) ---
    Q_PROPERTY(bool showLcdNotifications READ showLcdNotifications WRITE setShowLcdNotifications NOTIFY showLcdNotificationsChanged)
    Q_PROPERTY(bool showSystemNotifications READ showSystemNotifications WRITE setShowSystemNotifications NOTIFY showSystemNotificationsChanged)
    Q_PROPERTY(int  notificationDuration READ notificationDuration WRITE setNotificationDuration NOTIFY notificationDurationChanged)
    Q_PROPERTY(bool rememberGeometry READ rememberGeometry WRITE setRememberGeometry NOTIFY rememberGeometryChanged)
    Q_PROPERTY(bool restoreActiveBank READ restoreActiveBank WRITE setRestoreActiveBank NOTIFY restoreActiveBankChanged)
    Q_PROPERTY(QString styleOverride READ styleOverride WRITE setStyleOverride NOTIFY styleOverrideChanged)

public:
    explicit Backend(QObject *parent = nullptr);
    ~Backend() override;

    // Registered as a module singleton in main.cpp; also used from C++.
    static Backend *instance() { return s_instance; }

    // Colour + macro accessors are indexed by bank 1..4 (M1, M2, M3, MR).
    Q_INVOKABLE QColor color(int bank) const;
    // Bumped on every colour change; QML bindings use it as a dependency so
    // the spin boxes / swatch / gradient re-read `color()` reliably.
    Q_INVOKABLE QString colorRevision() const { return m_colorRevision; }
    // QColor::name() is not reachable from QML, so expose it directly.
    Q_INVOKABLE QString colorHex(int bank) const;
    // Reads one 0-255 channel (0=R, 1=G, 2=B).
    //
    // Deliberately NOT `QColor(bank).r` from QML: depending on how the colour
    // was built, QML hands those out normalised to 0..1, which made the spin
    // boxes show 1 for a colour whose hex was #ff0000.
    Q_INVOKABLE int colorComponent(int bank, int channel) const;
    // HSV <-> RGB helpers used by the gradient colour picker.
    Q_INVOKABLE QColor fromHsv(double h, double s, double v);
    Q_INVOKABLE QVariantMap toHsv(int bank) const;
    Q_INVOKABLE void   setColor(int bank, const QColor &color);

    Q_INVOKABLE QString macro(int bank, int index) const;
    Q_INVOKABLE void    setMacro(int bank, int index, const QString &value);
    Q_INVOKABLE void    runMacro(int bank, int index);

    Q_INVOKABLE QString bankPreset(int bank) const;
    Q_INVOKABLE void    setBankPreset(int bank, const QString &preset);

    Q_INVOKABLE void saveConfig();
    Q_INVOKABLE void savePreset(const QString &name);
    Q_INVOKABLE void loadPreset(const QString &name);
    Q_INVOKABLE void reloadPresets();
    Q_INVOKABLE void setDisplayScript(const QString &text);
    Q_INVOKABLE void refresh();

    // Attaches the display-script syntax highlighter to a QML TextArea's
    // underlying QTextDocument.
    Q_INVOKABLE void attachHighlighter(QObject *textDocument);
    Q_INVOKABLE void saveSettings();
    Q_INVOKABLE void reloadSettings();

    // Window geometry persistence.
    Q_INVOKABLE QRect  savedWindowGeometry() const;
    Q_INVOKABLE void   saveWindowGeometry(int x, int y, int w, int h);
    Q_INVOKABLE bool   hasSavedGeometry() const;

    bool    showLcdNotifications() const { return m_showLcdNotifications; }
    bool    showSystemNotifications() const { return m_showSystemNotifications; }
    void    setShowSystemNotifications(bool on);
    void    setShowLcdNotifications(bool on);
    int     notificationDuration() const  { return m_notificationDuration; }
    void    setNotificationDuration(int ms);
    bool    rememberGeometry() const      { return m_rememberGeometry; }
    void    setRememberGeometry(bool on);
    bool    restoreActiveBank() const     { return m_restoreActiveBank; }
    void    setRestoreActiveBank(bool on);
    QString styleOverride() const         { return m_styleOverride; }
    void    setStyleOverride(const QString &s);

    int    mode() const            { return g510s_data.mkey_state; }
    void   setMode(int m);
    bool   deviceFound() const     { return device_found != 0; }
    bool   colorFade() const       { return g510s_data.color_fade != 0; }
    void   setColorFade(bool on);
    bool   autoSaveOnQuit() const  { return g510s_data.auto_save_on_quit != 0; }
    void   setAutoSaveOnQuit(bool on);
    bool   guiHidden() const       { return g510s_data.gui_hidden != 0; }
    void   setGuiHidden(bool on);
    QStringList presets() const;
    QString displayScript() const;
    QString displayScriptPath() const;
    QString version() const        { return QStringLiteral(G510S_VERSION); }

signals:
    void modeChanged();
    void colorChanged(int bank);
    void deviceFoundChanged();
    void colorFadeChanged();
    void autoSaveOnQuitChanged();
    void guiHiddenChanged();
    void presetsChanged();
    void displayScriptChanged();
    void showLcdNotificationsChanged();
    void showSystemNotificationsChanged();
    // Fired when a toast should also appear as a desktop notification.
    void desktopNotification(const QString &text);
    void notificationDurationChanged();
    void rememberGeometryChanged();
    void restoreActiveBankChanged();
    void styleOverrideChanged();
    // Fired every 200ms so the LCD preview repaints from preview_buffer.
    void previewTick();

public slots:
    // Invoked on the GUI thread whenever the C core reports a device change.
    void setDeviceAttention(int attention);
    // Invoked when a preset is loaded out-of-band (D-Bus).
    void onDisplayScriptChanged(const char *script);

private:
    // Shows a notification on the keyboard LCD, honouring the user's setting.
    void notify(const char *text, int fallbackMs, int priority);

    void onPreviewTimer();
    void emitPresetsChanged();

    void setColorInternal(int bank, const QColor &color, bool notify);

    QString settingsPath() const;

    bool    m_showLcdNotifications = true;
    bool    m_showSystemNotifications = true;
    int     m_notificationDuration = 2000;
    bool    m_rememberGeometry = true;
    bool    m_restoreActiveBank = true;
    QString m_styleOverride;

    static Backend *s_instance;
    QTimer *m_previewTimer = nullptr;
    // Bumped whenever a colour changes so QML bindings re-evaluate.
    QString m_colorRevision;
};

// Colour picker: a hue/saturation square with a vertical brightness slider
// to its right.  Implemented in C++ so the gradients are drawn once and
// cached, rather than re-requested as images whenever the hue changes.
//
// Reads from and writes straight to the Backend, so RGB spin boxes, the hex
// field and D-Bus updates all stay in sync.
class ColorPickerItem : public QQuickPaintedItem {
    Q_OBJECT

    Q_PROPERTY(int bank READ bank WRITE setBank NOTIFY bankChanged)
    Q_PROPERTY(QColor color READ color NOTIFY colorPicked)

public:
    // Which control a drag is currently bound to.  Sticky for the whole drag
    // so moving outside a control still tracks instead of freezing.
    enum DragTarget { DragNone, DragSquare, DragHue };

    explicit ColorPickerItem(QQuickItem *parent = nullptr);

    int bank() const { return m_bank; }
    void setBank(int bank);

    QColor color() const { return m_color; }
    float hueF() const { return m_hue; }
    float satF() const { return m_sat; }
    float valF() const { return m_val; }

    void paint(QPainter *painter) override;

    // Input is driven from a QML MouseArea rather than these item handlers:
    // a bare QQuickItem does not take a mouse grab on press, so Qt stops
    // routing move events the moment the cursor leaves the item and the drag
    // dies a few pixels outside.  MouseArea grabs properly.
    //
    // beginDrag=true on press (picks the control under the cursor), false while
    // dragging (keeps tracking the original control, clamped to its edges).
    Q_INVOKABLE void pickAt(qreal x, qreal y, bool beginDrag);
    Q_INVOKABLE void endDrag();

    // Test helper: force an HSV state without going through mouse handlers.
    void setColorForTest(const QColor &c) {
        m_color = c;
        c.getHsvF(&m_hue, &m_sat, &m_val);
        update();
    }

signals:
    void bankChanged();
    void colorPicked();
    void picked();

private:
    // Layout: saturation/brightness square on the left, vertical hue bar on
    // the right.  Both are as tall as the item.
    QRectF squareRect() const;
    QRectF hueRect() const;

    void syncFromBackend();
    void pushToBackend();

    int m_bank = 1;
    QColor m_color = Qt::white;
    float m_hue = 0.0f;
    float m_sat = 0.0f;
    float m_val = 1.0f;
    // Guards against a feedback loop: pushing to the backend re-enters here.
    bool m_updating = false;
    // Which control the current drag belongs to; sticky for the whole drag.
    DragTarget m_drag = DragNone;

    // HSV has no defined hue when saturation is 0 (greys).  Remember the last
    // hue that had real saturation so the square and hue bar keep showing the
    // colour you were adjusting instead of snapping to pure red.
    float m_lastSaturatedHue = 0.0f;

    // Cached gradients, rebuilt only when the hue actually changes.
    QImage m_squareCache;
    float m_cachedHue = -1.0f;
    QImage m_hueCache;
};

// QQuickPaintedItem that renders preview_buffer (160x43 1bpp) scaled to fit.
class LcdPreviewItem : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(int displayWidth READ displayWidth CONSTANT)
    Q_PROPERTY(int displayHeight READ displayHeight CONSTANT)

public:
    explicit LcdPreviewItem(QQuickItem *parent = nullptr);

    int displayWidth() const  { return DISPLAY_WIDTH; }
    int displayHeight() const { return DISPLAY_HEIGHT; }

    void paint(QPainter *painter) override;

};