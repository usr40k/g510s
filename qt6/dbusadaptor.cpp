/*
 *  dbusadaptor.cpp
 *
 *  g510s is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 3 of the License, or (at your
 *  option) any later version.
 *
 *  Copyright © 2025-2026 usr40k
 */

#include "dbusadaptor.h"
#include "backend.h"

ControlAdaptor::ControlAdaptor(Backend *backend)
    : QDBusAbstractAdaptor(backend), m_backend(backend) {
    setAutoRelaySignals(false);

    connect(backend, &Backend::modeChanged,        this, &ControlAdaptor::emitChanged);
    connect(backend, &Backend::colorFadeChanged,   this, &ControlAdaptor::emitChanged);
    connect(backend, &Backend::autoSaveOnQuitChanged, this, &ControlAdaptor::emitChanged);
    connect(backend, &Backend::guiHiddenChanged,   this, &ControlAdaptor::emitChanged);
}

void ControlAdaptor::emitChanged() {
    QDBusMessage signal = QDBusMessage::createSignal(
        QStringLiteral("/org/g510s/control"),
        QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("PropertiesChanged"));

    QVariantMap changed;
    changed[QStringLiteral("Mode")]        = QVariant::fromValue(mode());
    changed[QStringLiteral("Color")]       = QVariant::fromValue(color());
    changed[QStringLiteral("ColorFade")]   = colorFade();
    changed[QStringLiteral("AutoSaveOnQuit")] = autoSaveOnQuit();
    changed[QStringLiteral("GuiHidden")]   = guiHidden();

    signal << QStringLiteral("org.g510s.control") << changed << QStringList();
    QDBusConnection::sessionBus().send(signal);
}

int ControlAdaptor::mode() const { return m_backend->mode(); }
void ControlAdaptor::setMode(int m) { m_backend->setMode(m); }

QList<int> ControlAdaptor::color() const {
    return {g510s_data.led_red, g510s_data.led_green, g510s_data.led_blue};
}

void ControlAdaptor::setColor(const QList<int> &rgb) {
    if (rgb.size() != 3) return;
    // Apply to whichever M-key bank is active, like the GTK build did.
    // setColor() emits colorChanged, so the UI re-syncs automatically.
    m_backend->setColor(m_backend->mode(), QColor(rgb.at(0), rgb.at(1), rgb.at(2)));
    setLedReport(rgb.at(0), rgb.at(1), rgb.at(2));
}

bool ControlAdaptor::colorFade() const { return m_backend->colorFade(); }
void ControlAdaptor::setColorFade(bool on) { m_backend->setColorFade(on); }

bool ControlAdaptor::autoSaveOnQuit() const { return m_backend->autoSaveOnQuit(); }
void ControlAdaptor::setAutoSaveOnQuit(bool on) { m_backend->setAutoSaveOnQuit(on); }

bool ControlAdaptor::guiHidden() const { return m_backend->guiHidden(); }
void ControlAdaptor::setGuiHidden(bool on) { m_backend->setGuiHidden(on); }

void ControlAdaptor::SetMode(int mode) {
    m_backend->setMode(mode);
    emitChanged();
}

// Report the LED colour on the bus.  Only the Color property is emitted here
// (mode/fade/autosave are unchanged by a colour set), which keeps the
// high-frequency `led_controller  ui` animation from flooding the UI with unrelated
// property-change storms.
void ControlAdaptor::setLedReport(int red, int green, int blue) {
    g510s_data.led_red   = red;
    g510s_data.led_green = green;
    g510s_data.led_blue  = blue;

    QDBusMessage signal = QDBusMessage::createSignal(
        QStringLiteral("/org/g510s/control"),
        QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("PropertiesChanged"));

    QVariantMap changed;
    changed[QStringLiteral("Color")] = QVariant::fromValue(QList<int>{red, green, blue});

    signal << QStringLiteral("org.g510s.control") << changed << QStringList();
    QDBusConnection::sessionBus().send(signal);
}

void ControlAdaptor::SetColor(int red, int green, int blue, bool save) {
    m_backend->setColor(m_backend->mode(), QColor(red, green, blue));
    setLedReport(red, green, blue);
    if (save)
        m_backend->saveConfig();
}

void ControlAdaptor::SetColorFade(bool fade)   { m_backend->setColorFade(fade); }
void ControlAdaptor::SetAutoSaveOnQuit(bool o) { m_backend->setAutoSaveOnQuit(o); }
void ControlAdaptor::SetGuiHidden(bool h)      { m_backend->setGuiHidden(h); }
void ControlAdaptor::SaveConfig()              { m_backend->saveConfig(); }

QString ControlAdaptor::GetMacro(int mode, int index) {
    return m_backend->macro(mode, index);
}

void ControlAdaptor::SetMacro(int mode, int index, const QString &macro) {
    m_backend->setMacro(mode, index, macro);
    m_backend->saveConfig();
    emitChanged();
}

void ControlAdaptor::RunMacro(int mode, int index) {
    m_backend->runMacro(mode, index);
}

void ControlAdaptor::SavePreset(const QString &name) { m_backend->savePreset(name); }

void ControlAdaptor::LoadPreset(const QString &name) {
    m_backend->loadPreset(name);
    m_backend->saveConfig();
}

QStringList ControlAdaptor::ListPresets() {
    m_backend->reloadPresets();
    return m_backend->presets();
}