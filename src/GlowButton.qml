import QtQuick
import QtQuick.Controls
import QtQuick.Shapes

// A four-point sparkle that toggles glow. Off, it's a dim grey that stays
// out of the way of the bars; on, it fills with a slowly turning rainbow.
Item {
    id: button
    width: 28
    height: 28

    readonly property bool on: settings.glow

    HoverHandler {
        id: hover
        cursorShape: Qt.PointingHandCursor
    }
    TapHandler {
        onTapped: settings.glow = !settings.glow
    }

    Shape {
        anchors.centerIn: parent
        width: 20
        height: 20
        // The curve renderer antialiases on the GPU without multisampling.
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            strokeWidth: -1
            fillColor: button.on ? "transparent" : hover.hovered ? "#707070" : "#3c3c3c"
            fillGradient: button.on ? rainbow : null
            startX: 10; startY: 0
            PathQuad { x: 20; y: 10; controlX: 11.2; controlY: 8.8 }
            PathQuad { x: 10; y: 20; controlX: 11.2; controlY: 11.2 }
            PathQuad { x: 0; y: 10; controlX: 8.8; controlY: 11.2 }
            PathQuad { x: 10; y: 0; controlX: 8.8; controlY: 8.8 }
        }
    }

    ConicalGradient {
        id: rainbow
        centerX: 10; centerY: 10
        GradientStop { position: 0.0; color: "#ff0000" }
        GradientStop { position: 0.17; color: "#ffff00" }
        GradientStop { position: 0.33; color: "#00ff00" }
        GradientStop { position: 0.5; color: "#00ffff" }
        GradientStop { position: 0.67; color: "#0000ff" }
        GradientStop { position: 0.83; color: "#ff00ff" }
        GradientStop { position: 1.0; color: "#ff0000" }

        NumberAnimation on angle {
            running: button.on
            from: 0; to: 360
            duration: 6000
            loops: Animation.Infinite
        }
    }

    ToolTip.visible: hover.hovered
    ToolTip.delay: 600
    ToolTip.text: button.on ? qsTr("Glow on (G)") : qsTr("Glow off (G)")
}
