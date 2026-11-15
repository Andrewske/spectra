import QtQuick
import QtQuick.Controls

// A small labelled pill in the corner menu. Checked chips are lit; the
// tooltip names the key that does the same thing.
Rectangle {
    id: chip

    property string text
    property string tip
    property bool checked: false
    // Chips whose label changes keep the width of their longest label, so the
    // panel doesn't jump as they cycle.
    property real minimumWidth: 0
    signal clicked()

    implicitWidth: Math.max(minimumWidth, label.implicitWidth + 16)
    implicitHeight: 24
    radius: 4
    color: chip.checked ? "#33ffffff" : hover.hovered ? "#1affffff" : "transparent"
    border.width: 1
    border.color: chip.checked ? "#99ffffff" : "#33ffffff"

    Text {
        id: label
        anchors.centerIn: parent
        text: chip.text
        color: chip.checked || hover.hovered ? "#ffffff" : "#b0b0b0"
        font.pixelSize: 12
    }

    HoverHandler {
        id: hover
        cursorShape: Qt.PointingHandCursor
    }
    TapHandler {
        onTapped: chip.clicked()
    }

    ToolTip.visible: hover.hovered && chip.tip.length > 0
    ToolTip.delay: 600
    ToolTip.text: chip.tip
}
