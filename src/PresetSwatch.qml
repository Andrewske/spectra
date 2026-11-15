import QtQuick
import QtQuick.Controls

// One preset in the corner menu: a chip painted with its colors, the number
// key that applies it underneath, and a small x on user presets to delete.
Item {
    id: swatch

    required property var modelData
    required property int index

    readonly property bool isCurrent: presets.current === modelData.name
    // A preset without colors keeps the current ones, so it gets a neutral chip.
    readonly property var colorList: modelData.colors.length > 0 ? modelData.colors : ["#505050"]

    width: 36
    height: 34

    // The outline sits outside the chip so it never covers the colors.
    Rectangle {
        anchors.fill: chip
        anchors.margins: -3
        radius: 5
        color: "transparent"
        border.width: 1.5
        border.color: "#ffffff"
        visible: swatch.isCurrent
    }

    Rectangle {
        id: chip
        x: 3
        y: 3
        width: 30
        height: 16
        radius: 3
        gradient: Gradient {
            id: fill
            orientation: Gradient.Horizontal
        }
    }

    Text {
        anchors.horizontalCenter: chip.horizontalCenter
        anchors.top: chip.bottom
        anchors.topMargin: 3
        text: swatch.index < 9 ? String(swatch.index + 1) : ""
        color: swatch.isCurrent || hover.hovered ? "#e0e0e0" : "#808080"
        font.pixelSize: 10
    }

    HoverHandler {
        id: hover
        cursorShape: Qt.PointingHandCursor
    }
    TapHandler {
        onTapped: presets.apply(swatch.modelData.name)
    }

    // A MouseArea rather than a TapHandler, because it accepts the click and
    // so the chip underneath doesn't also apply the preset being deleted.
    Rectangle {
        width: 14
        height: 14
        radius: 7
        x: chip.x + chip.width - 9
        y: chip.y - 5
        color: removeArea.containsMouse ? "#d04040" : "#303030"
        border.width: 1
        border.color: "#80ffffff"
        visible: hover.hovered && !swatch.modelData.builtIn

        Text {
            anchors.centerIn: parent
            text: "×"
            color: "#ffffff"
            font.pixelSize: 11
        }
        MouseArea {
            id: removeArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: presets.remove(swatch.modelData.name)
        }
    }

    Component {
        id: stopMaker
        GradientStop {}
    }

    // Gradient stops can't come from a model, so they're made once here; the
    // Repeater rebuilds the swatch whenever the preset list changes.
    Component.onCompleted: {
        const last = Math.max(1, colorList.length - 1)
        fill.stops = colorList.map((color, i) => stopMaker.createObject(fill, { position: i / last, color: color }))
    }

    ToolTip.visible: hover.hovered
    ToolTip.delay: 400
    ToolTip.text: modelData.name
}
