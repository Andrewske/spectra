import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// The bars are the window: no toolbar, no menus, everything on the keyboard.
ApplicationWindow {
    id: window
    width: 960
    height: 400
    visible: true
    title: "Spectra"
    color: settings.background
    Material.theme: Material.Dark

    property bool helpShown: false
    readonly property string problems: [capture.error, settings.error]
        .filter(text => text.length > 0).join("\n")

    // Ticks once per rendered frame, so the bars move in step with vsync.
    FrameAnimation {
        running: true
        onTriggered: spectrum.tick(frameTime)
    }

    SpectrumBars {
        anchors.fill: parent
    }

    // A dark plate keeps the message readable over bright bars.
    Rectangle {
        anchors.centerIn: parent
        width: problemText.width + 24
        height: problemText.height + 16
        radius: 4
        color: "#e6000000"
        visible: window.problems.length > 0

        Text {
            id: problemText
            anchors.centerIn: parent
            width: Math.min(implicitWidth, window.width - 56)
            text: window.problems
            color: "#e0e0e0"
            font.pixelSize: 13
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
        }
    }

    HelpOverlay {
        anchors.fill: parent
        visible: window.helpShown
        onDismissed: window.helpShown = false
    }

    Shortcut {
        sequences: ["?", "F1"]
        context: Qt.ApplicationShortcut
        onActivated: window.helpShown = !window.helpShown
    }
    Shortcut {
        sequence: "Escape"
        context: Qt.ApplicationShortcut
        enabled: window.helpShown
        onActivated: window.helpShown = false
    }
    Shortcut {
        sequence: "Q"
        context: Qt.ApplicationShortcut
        onActivated: Qt.quit()
    }
    Shortcut {
        sequence: "Space"
        context: Qt.ApplicationShortcut
        onActivated: spectrum.frozen = !spectrum.frozen
    }
    Shortcut {
        sequence: "F"
        context: Qt.ApplicationShortcut
        onActivated: window.visibility = window.visibility === Window.FullScreen
                     ? Window.Windowed : Window.FullScreen
    }
    Shortcut {
        sequence: "P"
        context: Qt.ApplicationShortcut
        onActivated: settings.showPeaks = !settings.showPeaks
    }
    Shortcut {
        sequence: "C"
        context: Qt.ApplicationShortcut
        onActivated: settings.colorMode = settings.colorMode === "spectrum" ? "vertical" : "spectrum"
    }
}
