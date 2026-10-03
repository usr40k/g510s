import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
// The C++ types (Backend, LcdPreview) are registered by this module. The
// scoped import keeps it from re-exporting into the unqualified namespace of
// the sibling files, which would shadow their QtQuick.Controls types.
import G510s as G510sImpl

ApplicationWindow {
    id: window
    width: 880
    height: 660
    minimumWidth: 640
    minimumHeight: 480
    visible: true
    title: qsTr("g510s %1").arg(G510sImpl.Backend.version)

    // Closing hides to the tray; the daemon keeps running.
    onClosing: function (close) {
        if (!G510sImpl.Backend.guiHidden) {
            close.accepted = false
            G510sImpl.Backend.guiHidden = true
            window.hide()
        }
    }

    // Persist window geometry (debounced in the Timer below).
    Component.onCompleted: {
        var g = G510sImpl.Backend.savedWindowGeometry()
        if (g.width > 0 && g.height > 0) {
            window.width = g.width
            window.height = g.height
            window.x = g.x
            window.y = g.y
        }
    }
    onXChanged: geometrySaveTimer.restart()
    onYChanged: geometrySaveTimer.restart()
    onWidthChanged: geometrySaveTimer.restart()
    onHeightChanged: geometrySaveTimer.restart()

    Timer {
        id: geometrySaveTimer
        interval: 400
        onTriggered: G510sImpl.Backend.saveWindowGeometry(window.x, window.y,
                                                          window.width, window.height)
    }

    // Settings live in a drawer rather than a menu, so there is room to grow.
    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            spacing: 8
            ToolButton {
                text: "\u2630"
                font.pixelSize: 16
                onClicked: drawer.open()
            }
            Label {
                text: window.title
                font.bold: true
                Layout.fillWidth: true
            }
        }
    }

    Drawer {
        id: drawer
        width: Math.min(360, window.width - 60)
        height: window.height
        clip: true

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 10

            Label { text: qsTr("Settings"); font.pixelSize: 16; font.bold: true }

            GroupBox {
                Layout.fillWidth: true
                title: qsTr("Keyboard notifications")
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    CheckBox {
                        Layout.fillWidth: true
                        text: qsTr("Show toasts on the keyboard screen")
                        checked: G510sImpl.Backend.showLcdNotifications
                        onToggled: G510sImpl.Backend.showLcdNotifications = checked
                    }
                    CheckBox {
                        Layout.fillWidth: true
                        text: qsTr("Also send desktop notifications")
                        checked: G510sImpl.Backend.showSystemNotifications
                        onToggled: G510sImpl.Backend.showSystemNotifications = checked
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        enabled: G510sImpl.Backend.showLcdNotifications
                        Label { text: qsTr("Duration") }
                        Slider {
                            Layout.fillWidth: true
                            from: 500; to: 10000; stepSize: 250
                            value: G510sImpl.Backend.notificationDuration
                            onMoved: G510sImpl.Backend.notificationDuration = value
                        }
                        Label {
                            Layout.preferredWidth: 46
                            horizontalAlignment: Text.AlignRight
                            text: (Math.round(G510sImpl.Backend.notificationDuration / 100) / 10) + " s"
                        }
                    }
                }
            }

            GroupBox {
                Layout.fillWidth: true
                title: qsTr("Appearance")
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: qsTr("Qt style") }
                        ComboBox {
                            id: styleBox
                            Layout.fillWidth: true
                            property var styleValues: ["", "Basic", "Fusion", "Material",
                                                       "Universal", "Windows",
                                                       "org.kde.kirigami.2", "org.kde.kirigami"]
                            model: [qsTr("System (bundled)"), "Basic", "Fusion", "Material",
                                    "Universal", "Windows",
                                    qsTr("Kirigami (desktop)"), qsTr("Kirigami 2 (desktop)")]
                            // Reflect whatever is actually in effect.
                            Component.onCompleted: currentIndex = Math.max(0, styleValues.indexOf(
                                G510sImpl.Backend.styleOverride))
                            onActivated: {
                                G510sImpl.Backend.setStyleOverride(styleValues[currentIndex])
                                restartHint.open()
                            }
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: "#888"
                        font.pixelSize: 11
                        text: qsTr("System uses the styles g510s ships with.\n"
                                 + "Kirigami loads from your desktop session - see the\n"
                                 + "startup warning in the terminal. Restart to apply.")
                    }
                }
            }

            GroupBox {
                Layout.fillWidth: true
                title: qsTr("Startup and window")
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    CheckBox {
                        Layout.fillWidth: true
                        text: qsTr("Remember window position and size")
                        checked: G510sImpl.Backend.rememberGeometry
                        onToggled: G510sImpl.Backend.rememberGeometry = checked
                    }
                    CheckBox {
                        Layout.fillWidth: true
                        text: qsTr("Start hidden in the tray")
                        checked: G510sImpl.Backend.guiHidden
                        onToggled: G510sImpl.Backend.guiHidden = checked
                    }
                    CheckBox {
                        Layout.fillWidth: true
                        text: qsTr("Auto-save configuration on quit")
                        checked: G510sImpl.Backend.autoSaveOnQuit
                        onToggled: G510sImpl.Backend.autoSaveOnQuit = checked
                    }
                    CheckBox {
                        Layout.fillWidth: true
                        text: qsTr("Restore the active M-key bank on startup")
                        checked: G510sImpl.Backend.restoreActiveBank
                        onToggled: G510sImpl.Backend.restoreActiveBank = checked
                    }
                }
            }

            GroupBox {
                Layout.fillWidth: true
                title: qsTr("Lighting")
                ColumnLayout {
                    Layout.fillWidth: true
                    CheckBox {
                        Layout.fillWidth: true
                        text: qsTr("Fade between LED colours")
                        checked: G510sImpl.Backend.colorFade
                        onToggled: G510sImpl.Backend.colorFade = checked
                    }
                }
            }

            Item { Layout.fillHeight: true }

            RowLayout {
                Layout.fillWidth: true
                Button {
                    Layout.fillWidth: true
                    text: qsTr("Reload presets")
                    onClicked: G510sImpl.Backend.reloadPresets()
                }
                Button {
                    Layout.fillWidth: true
                    text: qsTr("Save now")
                    onClicked: G510sImpl.Backend.saveConfig()
                }
            }
        }
    }

    Dialog {
        id: restartHint
        title: qsTr("Style changed")
        modal: true
        anchors.centerIn: Overlay.overlay
        standardButtons: Dialog.Ok
        Label { text: qsTr("Restart g510s to apply the new Qt style.") }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 8

        // Device status banner
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 26
            radius: 4
            color: G510sImpl.Backend.deviceFound ? "#1f3d24" : "#4a1f1f"
            border.color: G510sImpl.Backend.deviceFound ? "#2ecc71" : "#e74c3c"
            Text {
                anchors.centerIn: parent
                color: "white"
                font.bold: true
                text: G510sImpl.Backend.deviceFound
                      ? qsTr("Connected to G510/G510s")
                      : qsTr("Not connected - waiting for keyboard")
            }
        }

        // LCD preview
        GroupBox {
            Layout.fillWidth: true
            title: qsTr("Display preview")

            ColumnLayout {
                anchors.fill: parent
                G510sImpl.LcdPreview {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 110
                }
            }
        }

        TabBar {
            id: tabBar
            Layout.fillWidth: true
            TabButton { text: qsTr("M1") }
            TabButton { text: qsTr("M2") }
            TabButton { text: qsTr("M3") }
            TabButton { text: qsTr("MR") }
            TabButton { text: qsTr("Display") }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabBar.currentIndex

            BankPage { bank: 1 }
            BankPage { bank: 2 }
            BankPage { bank: 3 }
            BankPage { bank: 4 }
            DisplayPage {}
        }
    }
}