import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import G510s as G510sImpl

// One M-key bank page: LED colour picker, preset binding and the 18 G-keys.
Page {
    id: page

    // Bank number, 1..4 (M1, M2, M3, MR)
    property int bank: 1

    // Guarded colour accessors.
    //
    // These MUST read Backend.colorRevision, even though it is not used: it is
    // the property that changes on every colour update, so referencing it is
    // what makes QML treat the binding as dirty and re-evaluate.  Without it
    // the spin boxes / hex field / swatch only ever showed the startup value
    // and never followed the picker or a D-Bus SetColor.
    readonly property string _colorRev: G510sImpl.Backend.colorRevision

    function bankHex() {
        void _colorRev
        if (bank < 1)
            return "#ffffff"
        return G510sImpl.Backend.colorHex(bank)
    }

    // NOTE: do NOT write these as `bank >= 1 ? Backend.color(bank) : "#fff"`.
    // The branches have different types (QColor vs string), so QML coerces the
    // result to a string like "#e31e1e" and `.r`/`.g`/`.b` become undefined -
    // which made the spin boxes read 0 and made +/- feed NaN back.
    function bankColor() {
        void _colorRev
        if (bank < 1)
            return "#ffffff"
        return G510sImpl.Backend.color(bank)
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth

        ColumnLayout {
            width: page.width
            spacing: 8

            // --- LED colour picker ------------------------------------------
            GroupBox {
                Layout.fillWidth: true
                title: qsTr("LED colour for M%1").arg(page.bank)

                // Picker on the left, manual RGB/hex entry on the right.
                RowLayout {
                    anchors.fill: parent
                    spacing: 14

                    ColorPicker {
                        id: gradientPicker
                        Layout.preferredHeight: 200
                        Layout.preferredWidth: 234
                        Layout.alignment: Qt.AlignTop
                        bank: page.bank
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignTop
                        spacing: 10

                        GridLayout {
                            // Two columns = one label/value pair per row.
                            // (GridLayout fills row-major, so an odd column
                            // count would shift every second item into the
                            // wrong cell.)  No fillWidth: the grid hugs its
                            // contents rather than spreading across the row.
                            columns: 2
                            columnSpacing: 8
                            rowSpacing: 6

                            Label { text: "R"; Layout.alignment: Qt.AlignVCenter }
                            SpinBox {
                                Layout.preferredWidth: 132
                                Layout.minimumWidth: 132
                                from: 0; to: 255; editable: true
                                value: page.comp(0)
                                onValueModified: page.applyColor(value,
                                                                  page.comp(1),
                                                                  page.comp(2))
                            }
                            Label { text: "G"; Layout.alignment: Qt.AlignVCenter }
                            SpinBox {
                                Layout.preferredWidth: 132
                                Layout.minimumWidth: 132
                                from: 0; to: 255; editable: true
                                value: page.comp(1)
                                onValueModified: page.applyColor(page.comp(0),
                                                                  value,
                                                                  page.comp(2))
                            }
                            Label { text: "B"; Layout.alignment: Qt.AlignVCenter }
                            SpinBox {
                                Layout.preferredWidth: 132
                                Layout.minimumWidth: 132
                                from: 0; to: 255; editable: true
                                value: page.comp(2)
                                onValueModified: page.applyColor(page.comp(0),
                                                                  page.comp(1),
                                                                  value)
                            }

                            Label { text: qsTr("Hex"); Layout.alignment: Qt.AlignVCenter }
                            TextField {
                                Layout.preferredWidth: 120
                                text: page.bankHex()
                                onEditingFinished: page.applyHex(text)
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            // Swatch preview
                            Rectangle {
                                Layout.preferredWidth: 56
                                Layout.preferredHeight: 30
                                radius: 4
                                color: page.bankHex()
                                border.color: "gray"
                            }
                            Label {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                color: G510sImpl.Backend.mode === page.bank
                                       ? "#2ecc71" : "#888"
                                text: G510sImpl.Backend.mode === page.bank
                                      ? qsTr("active bank") : qsTr("inactive")
                            }
                        }
                    }
                }
            }

            // --- Preset bound to this bank -------------------------------
            GroupBox {
                Layout.fillWidth: true
                title: qsTr("Display preset bound to M%1").arg(page.bank)

                RowLayout {
                    anchors.fill: parent
                    Label { text: qsTr("Preset") }
                    ComboBox {
                        id: presetCombo
                        Layout.fillWidth: true
                        model: ["None"].concat(G510sImpl.Backend.presets)
                        onActivated: G510sImpl.Backend.setBankPreset(page.bank, currentText)
                    }
                    Button {
                        text: qsTr("Reload")
                        onClicked: G510sImpl.Backend.reloadPresets()
                    }
                }
            }

            // --- 18 G-key macro entries ----------------------------------
            GroupBox {
                Layout.fillWidth: true
                title: qsTr("G-key macros for M%1").arg(page.bank)

                GridLayout {
                    anchors.fill: parent
                    columns: 2
                    columnSpacing: 8
                    rowSpacing: 4

                    Repeater {
                        model: 18
                        delegate: RowLayout {
                            required property int index
                            Layout.fillWidth: true
                            Label {
                                text: "G" + (index + 1)
                                Layout.preferredWidth: 30
                            }
                            TextField {
                                Layout.fillWidth: true
                                text: G510sImpl.Backend.macro(page.bank, index + 1)
                                onEditingFinished: G510sImpl.Backend.setMacro(page.bank, index + 1, text)
                                placeholderText: qsTr("command to run")
                            }
                            Button {
                                text: qsTr("Run")
                                enabled: G510sImpl.Backend.macro(page.bank, index + 1).length > 0
                                onClicked: G510sImpl.Backend.runMacro(page.bank, index + 1)
                            }
                        }
                    }
                }
            }

            Item { Layout.fillHeight: true }
        }
    }

    // 0-255 channel accessor that does not rely on QML's QColor mapping.
    function comp(channel) {
        void _colorRev
        if (bank < 1)
            return 0
        return G510sImpl.Backend.colorComponent(bank, channel)
    }

    function applyColor(r, g, b) {
        G510sImpl.Backend.setColor(page.bank, Qt.rgba(r / 255, g / 255, b / 255, 1))
    }

    function applyHex(hex) {
        if (!/^#[0-9a-fA-F]{6}$/.test(hex))
            return
        G510sImpl.Backend.setColor(page.bank, hex)
    }
}