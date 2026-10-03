/*
 *  backend.cpp - implementation of the QML <-> C bridge.
 *
 *  g510s is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 3 of the License, or (at your
 *  option) any later version.
 *
 *  Copyright © 2025-2026 usr40k
 */

#include "backend.h"
#include "g510s-vars.h"
#include "syntaxhighlighter.h"

#include <QDateTime>
#include <QMouseEvent>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QQuickItem>
#include <QSettings>
#include <QTextDocument>
#include <QStandardPaths>

extern "C" {
#include <libg15.h>

// Defined in the C core
void display_notification(const char *text, int duration_ms, int priority);
extern void (*ui_display_script_changed)(const char *);
}

// Thread-safe bridge: the C worker threads call this, and we hop to the GUI
// thread through a queued connection so QML is only ever touched on one thread.
Backend *Backend::s_instance = nullptr;
static bool g_attention = false;

extern "C" void ui_set_device_attention(int attention) {
    QMetaObject::invokeMethod(Backend::instance(), "setDeviceAttention",
                              Qt::QueuedConnection,
                              Q_ARG(int, attention));
}

extern "C" void ui_request_refresh(void) {
    QMetaObject::invokeMethod(Backend::instance(), "refresh", Qt::QueuedConnection);
}

extern "C" void update_preview(void) {
    // The QML preview repaints on a 200ms timer; nothing to do here.
}

// --- Backend ----------------------------------------------------------------

Backend::Backend(QObject *parent) : QObject(parent) {
    s_instance = this;

    // g510s-presets.c calls this whenever a preset is loaded out-of-band.
    ui_display_script_changed = +[](const char *script) {
        if (!Backend::instance()) return;
        QMetaObject::invokeMethod(Backend::instance(), "onDisplayScriptChanged",
                                  Qt::QueuedConnection,
                                  Q_ARG(const char *, script));
    };

    m_previewTimer = new QTimer(this);
    m_previewTimer->setInterval(200);
    connect(m_previewTimer, &QTimer::timeout, this, &Backend::onPreviewTimer);
    m_previewTimer->start();

    reloadSettings();
}

Backend::~Backend() {
    s_instance = nullptr;
}

void Backend::onDisplayScriptChanged(const char *script) {
    if (script)
        emit displayScriptChanged();
}

void Backend::onPreviewTimer() {
    // Only notify the preview; must not touch displayScriptChanged, which would
    // make the editor re-read the file and clobber in-progress typing.
    emit previewTick();
}

QColor Backend::color(int bank) const {
    const struct m_data_s *m = nullptr;
    switch (bank) {
        case 1: m = &g510s_data.m1; break;
        case 2: m = &g510s_data.m2; break;
        case 3: m = &g510s_data.m3; break;
        case 4: m = &g510s_data.mr; break;
        default: return QColor(Qt::white);
    }
    return QColor(m->red, m->green, m->blue);
}

QColor Backend::fromHsv(double h, double s, double v) {
    return QColor::fromHsvF(qBound(0.0, h, 1.0),
                            qBound(0.0, s, 1.0),
                            qBound(0.0, v, 1.0));
}

QVariantMap Backend::toHsv(int bank) const {
    const QColor c = color(bank);
    // Qt6's getHsvF() takes floats, not qreal.
    float h = 0, s = 0, v = 0;
    c.getHsvF(&h, &s, &v);
    return QVariantMap{{"h", h}, {"s", s}, {"v", v}};
}

int Backend::colorComponent(int bank, int channel) const {
    const QColor c = color(bank);
    switch (channel) {
        case 0: return c.red();
        case 1: return c.green();
        case 2: return c.blue();
        default: return 0;
    }
}

QString Backend::colorHex(int bank) const {
    return color(bank).name(QColor::HexRgb);
}

void Backend::setColorInternal(int bank, const QColor &color, bool notify) {
    struct m_data_s *m = nullptr;
    switch (bank) {
        case 1: m = &g510s_data.m1; break;
        case 2: m = &g510s_data.m2; break;
        case 3: m = &g510s_data.m3; break;
        case 4: m = &g510s_data.mr; break;
        default: return;
    }
    m->red   = qBound(0, color.red(),   255);
    m->green = qBound(0, color.green(), 255);
    m->blue  = qBound(0, color.blue(),  255);
    // Tell the update thread this bank's LED colour has changed.
    update = bank;
    g510s_note_action("colour change");
    if (notify) {
        m_colorRevision = QString::number(QDateTime::currentMSecsSinceEpoch());
        emit colorChanged(bank);
    }
}

void Backend::setColor(int bank, const QColor &color) {
    setColorInternal(bank, color, true);
}

QString Backend::macro(int bank, int index) const {
    const char *m = macro_by_index(bank, index);
    return m ? QString::fromUtf8(m) : QString();
}

void Backend::setMacro(int bank, int index, const QString &value) {
    char *m = macro_by_index(bank, index);
    if (!m) return;
    const QByteArray utf8 = value.toUtf8();
    memset(m, 0, GKEY_STRLEN);
    strncpy(m, utf8.constData(), GKEY_STRLEN - 1);
}

void Backend::runMacro(int bank, int index) {
    const char *m = macro_by_index(bank, index);
    if (m && *m)
        system(m);
}

QString Backend::bankPreset(int bank) const {
    const char *p = get_bank_preset(bank);
    return p ? QString::fromUtf8(p) : QString();
}

void Backend::setBankPreset(int bank, const QString &preset) {
    bind_preset_to_bank(bank, preset.toUtf8().constData());
    notify("Preset bound", 1500, 0);
}

void Backend::saveConfig() {
    save_config();
    notify("Config saved", 1500, 0);
}

void Backend::notify(const char *text, int fallbackMs, int priority) {
    if (!text) return;
    if (m_showLcdNotifications)
        display_notification(text, m_notificationDuration > 0 ? m_notificationDuration
                                                              : fallbackMs, priority);
    if (m_showSystemNotifications)
        emit desktopNotification(QString::fromUtf8(text));
}

void Backend::savePreset(const QString &name) {
    save_preset(name.toUtf8().constData());
    emitPresetsChanged();
    notify("Preset saved!", 2000, 0);
}

void Backend::loadPreset(const QString &name) {
    if (name.isEmpty() || name == "None") return;
    load_preset(name.toUtf8().constData());
    save_config();
    notify("Preset loaded!", 2000, 0);
}

void Backend::reloadPresets() {
    load_presets();
    emitPresetsChanged();
}

QStringList Backend::presets() const {
    QStringList list;
    const int n = preset_count();
    for (int i = 0; i < n; ++i)
        list << QString::fromUtf8(preset_name_at(i));
    return list;
}

QString Backend::displayScript() const {
    QFile f(displayScriptPath());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    return QString::fromUtf8(f.readAll());
}

void Backend::setDisplayScript(const QString &text) {
    const QString path = displayScriptPath();
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        qWarning() << "G510s: cannot write" << path;
        return;
    }
    f.write(text.toUtf8());
}

QString Backend::displayScriptPath() const {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::HomeLocation))
        .filePath(".config/g510s/display.txt");
}

void Backend::refresh() {
    emit modeChanged();
    emit colorFadeChanged();
    emit autoSaveOnQuitChanged();
    emit guiHiddenChanged();
    emitPresetsChanged();
    emit displayScriptChanged();
}

void Backend::emitPresetsChanged() {
    emit presetsChanged();
}

void Backend::setMode(int m) {
    if (m < 1 || m > 4) return;
    set_mkey_state(m);
    emit modeChanged();
}

void Backend::setColorFade(bool on) {
    g510s_data.color_fade = on ? 1 : 0;
    save_config();
    emit colorFadeChanged();
}

void Backend::setAutoSaveOnQuit(bool on) {
    g510s_data.auto_save_on_quit = on ? 1 : 0;
    emit autoSaveOnQuitChanged();
}

void Backend::setGuiHidden(bool on) {
    g510s_data.gui_hidden = on ? 1 : 0;
    emit guiHiddenChanged();
}

void Backend::setDeviceAttention(int attention) {
    g_attention = attention != 0;
    emit deviceFoundChanged();
}

// --- Colour picker ----------------------------------------------------------

ColorPickerItem::ColorPickerItem(QQuickItem *parent) : QQuickPaintedItem(parent) {
    setFlag(ItemHasContents, true);
    setAcceptedMouseButtons(Qt::LeftButton);
    setImplicitSize(260, 190);

    // Keep in sync when the colour is changed from elsewhere (D-Bus, another
    // control, a config reload).
    if (Backend::instance()) {
        connect(Backend::instance(), &Backend::colorChanged, this,
                [this](int bank) {
                    if (bank == m_bank && !m_updating)
                        syncFromBackend();
                });
    }
    syncFromBackend();
}

void ColorPickerItem::setBank(int bank) {
    if (m_bank == bank)
        return;
    m_bank = bank;
    emit bankChanged();
    syncFromBackend();
    update();
}

void ColorPickerItem::syncFromBackend() {
    if (m_bank < 1 || !Backend::instance())
        return;
    m_updating = true;
    m_color = Backend::instance()->color(m_bank);
    m_color.getHsvF(&m_hue, &m_sat, &m_val);
    if (m_sat > 0.001f)
        m_lastSaturatedHue = m_hue;
    else
        m_hue = m_lastSaturatedHue;   // keep the square meaningful for greys
    m_updating = false;
    emit colorPicked();
    update();
}

void ColorPickerItem::pushToBackend() {
    if (m_bank < 1 || !Backend::instance())
        return;
    m_updating = true;
    if (m_sat > 0.001f)
        m_lastSaturatedHue = m_hue;
    m_color = QColor::fromHsvF(m_hue, m_sat, m_val);
    g510s_note_action("colour change");
    Backend::instance()->setColor(m_bank, m_color);
    m_updating = false;
    emit colorPicked();
    update();
}

QRectF ColorPickerItem::squareRect() const {
    // Square is as tall as the item; the hue bar sits to its right.
    const qreal side = height();
    return QRectF(0, 0, side, side);
}

QRectF ColorPickerItem::hueRect() const {
    const qreal side = height();
    return QRectF(side + 10, 0, 24, side);
}

void ColorPickerItem::paint(QPainter *painter) {
    const QRectF sq = squareRect();
    const QRectF hr = hueRect();
    painter->setRenderHint(QPainter::Antialiasing, false);

    // --- Saturation / brightness square ----------------------------------
    // x = saturation (left = white, right = full hue)
    // y = brightness  (top = full, bottom = black)
    // Rebuilt only when the hue changes.
    if (m_cachedHue != m_hue) {
        const int N = 160;
        m_squareCache = QImage(N, N, QImage::Format_RGB32);
        const QColor hueC = QColor::fromHsvF(m_hue, 1.0f, 1.0f);
        const int hr_ = hueC.red(), hg = hueC.green(), hb = hueC.blue();
        for (int y = 0; y < N; ++y) {
            QRgb *line = reinterpret_cast<QRgb *>(m_squareCache.scanLine(y));
            const qreal v = 1.0 - y / (qreal)(N - 1);      // brightness
            for (int x = 0; x < N; ++x) {
                const qreal s = x / (qreal)(N - 1);        // saturation
                // white -> hue by saturation, then scale by brightness
                const int r = qRound((255 + (hr_ - 255) * s) * v);
                const int g = qRound((255 + (hg - 255) * s) * v);
                const int b = qRound((255 + (hb - 255) * s) * v);
                line[x] = qRgb(qBound(0, r, 255), qBound(0, g, 255), qBound(0, b, 255));
            }
        }
        m_cachedHue = m_hue;
    }
    painter->drawImage(sq, m_squareCache);

    // Crosshair: x = saturation, y = brightness.
    const qreal cx = sq.left() + m_sat * sq.width();
    const qreal cy = sq.top()  + (1.0f - m_val) * sq.height();
    painter->setBrush(Qt::NoBrush);
    painter->setPen(QPen(QColor(0, 0, 0, 190), 3.5));
    painter->drawEllipse(QRectF(cx - 6, cy - 6, 12, 12));
    painter->setPen(QPen(QColor(255, 255, 255, 235), 1.5));
    painter->drawEllipse(QRectF(cx - 6, cy - 6, 12, 12));

    // --- Vertical hue bar (red at the top) -------------------------------
    if (m_hueCache.isNull()) {
        const int N = 256;
        m_hueCache = QImage(1, N, QImage::Format_RGB32);
        for (int y = 0; y < N; ++y)
            m_hueCache.setPixel(0, y, QColor::fromHsvF(y / (qreal)(N - 1), 1.0f, 1.0f).rgb());
    }
    painter->drawImage(hr, m_hueCache, QRectF(0, 0, 1, 255));

    // Hue handle.
    const qreal hy = hr.top() + m_hue * hr.height();
    painter->setPen(QPen(QColor(255, 255, 255), 2.5));
    painter->drawLine(QPointF(hr.left() - 5, hy), QPointF(hr.right() + 5, hy));
    painter->setPen(QPen(QColor(0, 0, 0, 150), 1));
    painter->drawLine(QPointF(hr.left() - 5, hy + 1.5), QPointF(hr.right() + 5, hy + 1.5));

    painter->setBrush(Qt::NoBrush);
    painter->setPen(QPen(QColor(90, 90, 90), 1));
    painter->drawRect(sq);
    painter->drawRect(hr);
}

void ColorPickerItem::pickAt(qreal x, qreal y, bool beginDrag) {
    const QPointF p(x, y);

    if (beginDrag) {
        // Remember which control the drag belongs to; it stays active for the
        // whole gesture so sliding onto the other control doesn't jump.
        const QRectF sq = squareRect();
        const QRectF hr = hueRect();
        if (sq.contains(p))
            m_drag = DragSquare;
        else if (hr.contains(p))
            m_drag = DragHue;
        else
            m_drag = DragNone;
    }

    if (m_drag == DragSquare) {
        const QRectF sq = squareRect();
        if (sq.width() <= 0 || sq.height() <= 0)
            return;
        // Clamp, don't reject: dragging past an edge keeps working.
        m_sat = qBound(0.0, (x - sq.left()) / sq.width(), 1.0);
        m_val = qBound(0.0, 1.0 - (y - sq.top()) / sq.height(), 1.0);
        pushToBackend();
    } else if (m_drag == DragHue) {
        const QRectF hr = hueRect();
        if (hr.height() <= 0)
            return;
        m_hue = qBound(0.0, (y - hr.top()) / hr.height(), 1.0);
        pushToBackend();
    }
}

void ColorPickerItem::endDrag() {
    m_drag = DragNone;
}

// --- Syntax highlighting ---------------------------------------------------

void Backend::attachHighlighter(QObject *textDocument) {
    if (!textDocument)
        return;
    if (auto *doc = qobject_cast<QTextDocument *>(textDocument)) {
        // Owned by the document, so it dies with the editor.
        new ScriptHighlighter(doc);
    }
}

// --- App settings -----------------------------------------------------------

QString Backend::settingsPath() const {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::HomeLocation))
        .filePath(".config/g510s/settings.conf");
}

void Backend::reloadSettings() {
    QSettings s(settingsPath(), QSettings::IniFormat);
    m_showLcdNotifications = s.value("notifications/enabled", true).toBool();
    m_showSystemNotifications = s.value("notifications/systemEnabled", true).toBool();
    m_notificationDuration = qBound(500, s.value("notifications/durationMs", 2000).toInt(), 30000);
    m_rememberGeometry = s.value("window/rememberGeometry", true).toBool();
    m_restoreActiveBank = s.value("startup/restoreActiveBank", true).toBool();
    m_styleOverride = s.value("ui/styleOverride", QString()).toString();

    emit showLcdNotificationsChanged();
    emit showSystemNotificationsChanged();
    emit notificationDurationChanged();
    emit rememberGeometryChanged();
    emit restoreActiveBankChanged();
    emit styleOverrideChanged();
}

void Backend::saveSettings() {
    QSettings s(settingsPath(), QSettings::IniFormat);
    s.setValue("notifications/enabled", m_showLcdNotifications);
    s.setValue("notifications/systemEnabled", m_showSystemNotifications);
    s.setValue("notifications/durationMs", m_notificationDuration);
    s.setValue("window/rememberGeometry", m_rememberGeometry);
    s.setValue("startup/restoreActiveBank", m_restoreActiveBank);
    s.setValue("ui/styleOverride", m_styleOverride);
    s.sync();
}

void Backend::setShowLcdNotifications(bool on) {
    if (m_showLcdNotifications == on) return;
    m_showLcdNotifications = on;
    saveSettings();
    emit showLcdNotificationsChanged();
}

void Backend::setShowSystemNotifications(bool on) {
    if (m_showSystemNotifications == on) return;
    m_showSystemNotifications = on;
    saveSettings();
    emit showSystemNotificationsChanged();
}

void Backend::setNotificationDuration(int ms) {
    ms = qBound(500, ms, 30000);
    if (m_notificationDuration == ms) return;
    m_notificationDuration = ms;
    saveSettings();
    emit notificationDurationChanged();
}

void Backend::setRememberGeometry(bool on) {
    if (m_rememberGeometry == on) return;
    m_rememberGeometry = on;
    saveSettings();
    emit rememberGeometryChanged();
}

void Backend::setRestoreActiveBank(bool on) {
    if (m_restoreActiveBank == on) return;
    m_restoreActiveBank = on;
    saveSettings();
    emit restoreActiveBankChanged();
}

void Backend::setStyleOverride(const QString &style) {
    if (m_styleOverride == style) return;
    m_styleOverride = style;
    saveSettings();
    emit styleOverrideChanged();
}

// --- Window geometry --------------------------------------------------------

bool Backend::hasSavedGeometry() const {
    if (!m_rememberGeometry) return false;
    QSettings s(settingsPath(), QSettings::IniFormat);
    return s.contains("window/width") && s.contains("window/height");
}

QRect Backend::savedWindowGeometry() const {
    if (!hasSavedGeometry())
        return QRect();
    QSettings s(settingsPath(), QSettings::IniFormat);
    return QRect(s.value("window/x").toInt(), s.value("window/y").toInt(),
                 s.value("window/width").toInt(), s.value("window/height").toInt());
}

void Backend::saveWindowGeometry(int x, int y, int w, int h) {
    if (!m_rememberGeometry || w <= 0 || h <= 0)
        return;
    QSettings s(settingsPath(), QSettings::IniFormat);
    s.setValue("window/x", x);
    s.setValue("window/y", y);
    s.setValue("window/width", w);
    s.setValue("window/height", h);
    s.sync();
}

// --- LCD preview ------------------------------------------------------------

LcdPreviewItem::LcdPreviewItem(QQuickItem *parent) : QQuickPaintedItem(parent) {
    setFlag(ItemHasContents, true);
    connect(Backend::instance(), &Backend::previewTick, this, [this] { update(); });
}

void LcdPreviewItem::paint(QPainter *painter) {
    const int w = DISPLAY_WIDTH;
    const int h = DISPLAY_HEIGHT;
    const int pitch = w / 8;

    painter->fillRect(QRectF(0, 0, width(), height()), QColor(26, 26, 26));

    // The LCD is a fixed-pixel grid, so only ever scale by whole integers.
    // Fractional scaling merged adjacent pixels together and made the preview
    // drift out of sync with the real display.
    int scale = static_cast<int>(qMin(width() / w, height() / h));
    if (scale < 1)
        scale = 1;

    const qreal dw = w * scale;
    const qreal dh = h * scale;
    const qreal offX = (width()  - dw) / 2;
    const qreal offY = (height() - dh) / 2;

    // Render the whole panel as a 1x1 image first, then scale it with
    // nearest-neighbour so every source pixel becomes an exact scale x scale
    // block - no interpolation, no seams.
    QImage panel(w, h, QImage::Format_RGB32);
    const QColor on(43, 224, 76);
    const QColor off(13, 13, 13);
    for (int y = 0; y < h; ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(panel.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const int byteIdx = y * pitch + x / 8;
            const int bitIdx  = 0x80 >> (x % 8);
            const bool pixel = byteIdx < G510S_PREVIEW_BUFFER_LEN &&
                               (preview_buffer[byteIdx] & bitIdx);
            line[x] = (pixel ? on : off).rgb();
        }
    }

    painter->setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter->drawImage(QRectF(offX, offY, dw, dh),
                       panel, QRectF(0, 0, w, h));

    // Border
    painter->setPen(QColor(77, 77, 77));
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(QRectF(offX - 1, offY - 1, dw + 2, dh + 2));
}