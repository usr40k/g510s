/*
 *  main.cpp - Qt6 entry point for the g510s QML frontend.
 *
 *  Mirrors the startup/teardown sequence of the original GTK main(): parse
 *  options, open libg15, load config + presets, spawn the three worker
 *  threads, then hand control to the Qt event loop.
 *
 *  g510s is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 3 of the License, or (at your
 *  option) any later version.
 *
 *  Copyright © 2025-2026 usr40k
 */

#include <QCommandLineParser>
#include <QDir>
#include <QApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QThreadPool>
#include <QRunnable>
#include <QAction>
#include <QDebug>
#include <QLibraryInfo>
#include <QDir>
#include <QFile>
#include <QSettings>

#include <libg15.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backend.h"
#include "dbusadaptor.h"

extern "C" {
#include "g510s.h"
#include "g510s-vars.h"



// Rewritten to the Nix store dir by the build, exactly like the GTK app.
#ifndef G510S_SHARE_DIR
#define G510S_SHARE_DIR "/usr/local/share/g510s/"
#endif
}

// True if the named Qt Quick Controls style can actually be resolved.
static bool isStyleAvailable(const QString &style) {
    if (style.isEmpty())
        return true;

    // Desktop styles live in the session's QML modules as "org.kde.kirigami".
    if (style.startsWith(QLatin1String("org."))) {
        QStringList roots = qEnvironmentVariable("QML2_IMPORT_PATH").split(QLatin1Char(':'),
                                                                          Qt::SkipEmptyParts);
        // Strip a trailing version suffix: org.kde.kirigami.2 -> org/kde/kirigami
        QString mod = style;
        const int lastDot = mod.lastIndexOf(QLatin1Char('.'));
        if (lastDot > 0)
            mod.truncate(lastDot);
        // Umbrella modules (org.kde.kirigami) have no top-level qmldir, so
        // accept either a qmldir or simply the module directory.
        for (const QString &r : roots) {
            const QString dir = r + QLatin1Char('/') + mod;
            if (QFile::exists(dir + QStringLiteral("/qmldir")) ||
                QDir(dir).exists())
                return true;
        }
        return false;
    }

    // Bundled styles ship with qtdeclarative.
    const QString controls = QLibraryInfo::path(QLibraryInfo::LibrariesPath)
                             + QStringLiteral("/qt-6/qml/QtQuick/Controls");
    return QFile::exists(controls + QLatin1Char('/') + style + QStringLiteral("/qmldir"));
}

static void printHelp() {
    printf("Usage: g510s <option> <value>\n\n");
    printf("Options:\n");
    printf("  --help|-h\tShow this help\n");
    printf("  --debug|-d <n>\tSet debug level (default 0)\n");
    printf("  --terminal <cmd>\tRun command and display output on LCD using small font\n");
}

// Runs a void(*)(void*) worker on the Qt global thread pool, preserving the
// original pthread_create() semantics.
class ThreadTask : public QRunnable {
public:
    using Fn = void *(*)(void *);
    ThreadTask(Fn fn, void *arg) : m_fn(fn), m_arg(arg) { setAutoDelete(true); }
    void run() override { m_fn(m_arg); }
private:
    Fn m_fn;
    void *m_arg;
};

int main(int argc, char *argv[]) {
    // Confine QML/plugin lookup to the Qt we were linked against, *unless* the
    // user explicitly asked for a desktop style (Kirigami/Breeze), which lives
    // in the session's QML path.
    //
    // A desktop session exports QML2_IMPORT_PATH / QT_PLUGIN_PATH pointing at
    // *its* Qt modules (on KDE that pulls in Plasma's Kirigami, which then
    // fails to resolve its own components and spams
    // "qmlRegisterType requires absolute URLs" dozens of times).  Restricting
    // the search path keeps us on our own consistent Qt instead of mixing two.
    {
        // Reads the saved override straight from the settings file, because the
        // Backend does not exist yet at this point.
        QSettings pre(QStringLiteral("%1/.config/g510s/settings.conf")
                          .arg(QString::fromLocal8Bit(qgetenv("HOME"))),
                      QSettings::IniFormat);
        const QString want = pre.value("ui/styleOverride").toString();
        const bool external = want.contains("kirigami", Qt::CaseInsensitive) ||
                              want.contains("breeze", Qt::CaseInsensitive) ||
                              want.startsWith("org.");
        if (want.contains("kirigami", Qt::CaseInsensitive)) {
            fprintf(stderr,
                    "G510s: note - using the desktop '%s' style. It is loaded from\n"
                    "      your session's QML modules and may log\n"
                    "      \"qmlRegisterType requires absolute URLs\" warnings,\n"
                    "      and may render differently because it targets a\n"
                    "      different Qt than the one g510s is linked against.\n",
                    qPrintable(want));
        }
        if (!external) {
            const QString ownQml = QLibraryInfo::path(QLibraryInfo::LibrariesPath)
                                   + QStringLiteral("/qt-6/qml");
            const QString ownPlugins = QLibraryInfo::path(QLibraryInfo::PluginsPath);
            qputenv("QML2_IMPORT_PATH", ownQml.toUtf8());
            qputenv("QT_PLUGIN_PATH", ownPlugins.toUtf8());
        }
    }

    int debug = 0;
    bool opt_invalid = false;

    // Parse options before QGuiApplication so --help/--terminal behave the same
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            printHelp();
            return 0;
        } else if (!strcmp(argv[i], "--debug") || !strcmp(argv[i], "-d")) {
            if (argv[i + 1] && is_number(argv[i + 1])) {
                i++;
                debug = atoi(argv[i]);
            } else {
                opt_invalid = true;
                break;
            }
        } else if (!strcmp(argv[i], "--dump-display-buffer")) {
            dump_display_buffer = 1;
        } else if (!strcmp(argv[i], "--terminal")) {
            terminal_mode = 1;
            if (argv[i + 1]) {
                i++;
                strncpy(terminal_cmd, argv[i], sizeof(terminal_cmd) - 1);
                terminal_cmd[sizeof(terminal_cmd) - 1] = '\0';
                i++;
                while (i < argc) {
                    strncat(terminal_cmd, " ", sizeof(terminal_cmd) - strlen(terminal_cmd) - 1);
                    strncat(terminal_cmd, argv[i], sizeof(terminal_cmd) - strlen(terminal_cmd) - 1);
                    i++;
                }
            } else {
                opt_invalid = true;
                break;
            }
        } else {
            opt_invalid = true;
            break;
        }
    }

    if (opt_invalid) {
        printf("G510s: invalid option specified!\n\n");
        printHelp();
        return 0;
    }

    if (debug != 0) {
        libg15Debug(debug);
        printf("G510s: debugging enabled, level %i\n", debug);
    }

    // Initialise the keyboard
    if (setupLibG15(0x46d, 0xc22d, 0) == G15_NO_ERROR) {
        printf("G510s: connected to 046d:c22d\n");
        device_found = 1;
        usb_id = "046d:c22d";
    } else if (setupLibG15(0x46d, 0xc22e, 0) == G15_NO_ERROR) {
        printf("G510s: connected to 046d:c22e\n");
        device_found = 1;
        usb_id = "046d:c22e";
    } else {
        printf("G510s: no keyboard found, waiting for one\n");
        device_found = 0;
    }

    if (device_found) {
        if (init_uinput() != 0)
            printf("G510s: failed to initialize uinput, media keys not available\n");
    }

    init_data();
    g510s_pvars_init();
    lcdlist_t *lcdlist = lcdlist_init();
    check_dir();
    load_config();
    load_presets();

    // Start the worker threads
    QThreadPool::globalInstance()->start(new ThreadTask(server_function, lcdlist));
    QThreadPool::globalInstance()->start(new ThreadTask(update_function, lcdlist));
    QThreadPool::globalInstance()->start(new ThreadTask(key_function,   lcdlist));

    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("g510s");
    QCoreApplication::setApplicationName("g510s");
    QCoreApplication::setApplicationVersion(G510S_VERSION);

    auto *backend = new Backend(&app);
    new ControlAdaptor(backend);

    // Register Backend as a module singleton explicitly.  Using QML_SINGLETON
    // on the class made Qt construct a *second* Backend, so QML read colour and
    // macro state that nothing ever updated.
    qmlRegisterSingletonInstance("G510s", 1, 0, "Backend", backend);

    // Qt style.  Must be set before any QML is loaded.
    const QString styleOverride = backend->styleOverride();
    QQuickStyle::setStyle(styleOverride.isEmpty() ? QStringLiteral("Basic")
                                                  : styleOverride);
    qmlRegisterType<LcdPreviewItem>("G510s", 1, 0, "LcdPreview");
    qmlRegisterType<ColorPickerItem>("G510s", 1, 0, "ColorPicker");

    // Register org.g510s.control on the session bus
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        qWarning("G510s: no session bus, D-Bus control interface disabled");
    } else if (!bus.registerObject(QStringLiteral("/org/g510s/control"), backend)) {
        qWarning("G510s: failed to register D-Bus object");
    } else if (!bus.registerService(QStringLiteral("org.g510s.control"))) {
        qWarning("G510s: failed to own D-Bus name org.g510s.control");
    }

    // System tray icon (replaces AppIndicator3)
    auto *tray = new QSystemTrayIcon(&app);
    tray->setIcon(QIcon(QStringLiteral(G510S_SHARE_DIR "g510s.svg")));
    if (!device_found)
        tray->setIcon(QIcon(QStringLiteral(G510S_SHARE_DIR "g510s-alert.svg")));
    tray->setToolTip(QStringLiteral("g510s"));

    auto *trayMenu = new QMenu;
    QAction *showAction = trayMenu->addAction("Show");
    trayMenu->addSeparator();
    QAction *quitAction = trayMenu->addAction("Quit");
    tray->setContextMenu(trayMenu);

    // Sanity-check the requested style *before* the engine loads anything:
    // QQuickStyle::setStyle() may only be called once, so there is no
    // recovering from a bad choice after the fact.
    QString style = styleOverride.isEmpty() ? QStringLiteral("Basic") : styleOverride;
    if (!isStyleAvailable(style)) {
        fprintf(stderr,
                "G510s: Qt style '%s' is not installed - using the bundled\n"
                "      Basic style instead. Pick another one in Settings.\n",
                qPrintable(style));
        style = QStringLiteral("Basic");
    }
    QQuickStyle::setStyle(style);

    QQmlApplicationEngine engine;
    engine.loadFromModule("G510s", "Main");

    if (engine.rootObjects().isEmpty()) {
        qCritical("G510s: failed to load QML");
        return 1;
    }

    auto *root = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    if (!root) {
        qCritical("G510s: QML root is not a window");
        return 1;
    }

    QObject::connect(showAction, &QAction::triggered, root, &QQuickWindow::show);
    QObject::connect(quitAction, &QAction::triggered, &app, &QCoreApplication::quit);
    // Left click on the tray icon shows the window, matching the GTK behaviour.
    QObject::connect(tray, &QSystemTrayIcon::activated, root,
                     [root](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger ||
            reason == QSystemTrayIcon::DoubleClick)
            root->show();
    });
    tray->show();

    // Desktop notifications for toasts (mirrors the LCD toast in the UI).
    QObject::connect(backend, &Backend::desktopNotification, tray,
                     [tray, backend](const QString &text) {
        if (!tray->isVisible())
            return;   // no tray support: stay quiet rather than spam
        tray->showMessage(QObject::tr("g510s"), text,
                          QSystemTrayIcon::Information,
                          backend->notificationDuration());
    });

    // Debug helper: grab the window a moment after startup and exit.  Used to
    // verify the real rendered layout during development.
    if (qEnvironmentVariableIsSet("G510S_SCREENSHOT")) {
        const QString path = qEnvironmentVariable("G510S_SCREENSHOT");
        QTimer::singleShot(2500, root, [root, path] {
            if (root->grabWindow().save(path)) {
                fprintf(stderr, "G510s: wrote %s\n", qPrintable(path));
            } else {
                fprintf(stderr, "G510s: screenshot failed\n");
            }
            QCoreApplication::quit();
        });
    }

    // Light up the M-key LED for the active bank and push the current LED
    // colour to the keyboard.  The GTK build did this on startup; without it
    // the hardware stays dark until you press an M-key.
    if (device_found) {
        set_mkey_state(g510s_data.mkey_state);
        const struct m_data_s *m =
            g510s_data.mkey_state == 1 ? &g510s_data.m1 :
            g510s_data.mkey_state == 2 ? &g510s_data.m2 :
            g510s_data.mkey_state == 3 ? &g510s_data.m3 : &g510s_data.mr;
        setG510LEDColor(m->red, m->green, m->blue);
        g510s_data.led_red   = m->red;
        g510s_data.led_green = m->green;
        g510s_data.led_blue  = m->blue;
    }

    // Reflect the configured initial visibility
    if (g510s_data.gui_hidden)
        root->hide();

    app.exec();

    // Tear down the C core
    leaving = 1;
    if (g510s_data.auto_save_on_quit)
        save_config();

    QThreadPool::globalInstance()->waitForDone(2000);

    if (device_found) {
        exit_uinput();
        exitLibG15();
        if (usb_id) {
            char cmd[128];
            snprintf(cmd, sizeof(cmd), "usbreset %s", usb_id);
            system(cmd);
        }
    }

    lcdlist_destroy(&lcdlist);
    return 0;
}