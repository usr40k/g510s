import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import G510s as G510sImpl

// Hue/saturation+brightness square with a vertical hue bar beside it.
// All drawing and the hit-testing/clamping maths live in C++ (ColorPicker);
// this wrapper only hosts the MouseArea that feeds it pointer positions.
Item {
    id: wrapper

    property int bank: 1
    readonly property color currentColor: picker.color

    implicitHeight: 200

    G510sImpl.ColorPicker {
        id: picker
        anchors.fill: parent
        bank: wrapper.bank
    }

    // A plain QQuickItem never grabs the mouse on press, so Qt stops sending
    // move events as soon as the cursor leaves it and the drag dies.  A
    // MouseArea does grab, so it keeps receiving positions right across the
    // desktop; pickAt() clamps them to the active control's edges.
    MouseArea {
        anchors.fill: parent
        preventStealing: true
        acceptedButtons: Qt.LeftButton
        cursorShape: Qt.CrossCursor

        onPressed: function (mouse) {
            picker.pickAt(mouse.x, mouse.y, true)
        }
        onPositionChanged: function (mouse) {
            if (pressed)
                picker.pickAt(mouse.x, mouse.y, false)
        }
        onReleased: picker.endDrag()
        onCanceled: picker.endDrag()
    }
}
