import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import G510s as G510sImpl

// Display tab: edit ~/.config/g510s/display.txt and manage presets.
Page {
    id: page

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        // --- Preset management ------------------------------------------
        GroupBox {
            Layout.fillWidth: true
            title: qsTr("Presets")

            GridLayout {
                anchors.fill: parent
                columns: 3
                columnSpacing: 6
                rowSpacing: 4

                Label { text: qsTr("Preset") }
                ComboBox {
                    id: presetCombo
                    Layout.fillWidth: true
                    model: ["None"].concat(G510sImpl.Backend.presets)
                }
                Button {
                    text: qsTr("Load")
                    enabled: presetCombo.currentIndex > 0
                    onClicked: G510sImpl.Backend.loadPreset(presetCombo.currentText)
                }

                Label { text: qsTr("Save as") }
                TextField {
                    id: newPresetName
                    Layout.fillWidth: true
                    placeholderText: qsTr("preset name")
                }
                Button {
                    text: qsTr("Save")
                    enabled: newPresetName.text.length > 0
                    onClicked: {
                        G510sImpl.Backend.savePreset(newPresetName.text)
                        presetCombo.currentIndex = presetCombo.count - 1
                    }
                }

                Label { text: "" }
                Button {
                    text: qsTr("Reload preset list")
                    onClicked: G510sImpl.Backend.reloadPresets()
                }
                Label { text: "" }
            }
        }

        // --- Display script editor --------------------------------------
        GroupBox {
            Layout.fillWidth: true
            Layout.fillHeight: true
            title: qsTr("display.txt")

            ScrollView {
                anchors.fill: parent
                TextArea {
                    id: scriptEditor
                    wrapMode: TextArea.NoWrap
                    font.family: "monospace"
                    selectByMouse: true
                    // Load once on creation; subsequent saves push to disk.
                    text: G510sImpl.Backend.displayScript
                    onTextChanged: saveTimer.restart()

                    // Syntax highlighting for the display-script language.
                    Component.onCompleted: G510sImpl.Backend.attachHighlighter(textDocument)
                }
            }
        }

        Label {
            Layout.fillWidth: true
            elide: Text.ElideMiddle
            color: "gray"
            text: G510sImpl.Backend.displayScriptPath
        }
    }

    // Debounce writes so we do not hit the disk on every keystroke
    Timer {
        id: saveTimer
        interval: 500
        onTriggered: G510sImpl.Backend.setDisplayScript(scriptEditor.text)
    }
}