import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Effects

// The bars are the window: one dim icon in the corner, everything else on
// the keyboard.
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

    // Glow is a blurred copy of the bars drawn underneath the sharp ones, so
    // each bar gets a soft halo in its own color. MultiEffect captures the
    // bars itself; turning on the bars' own layer instead would squeeze them
    // by the blur padding. Off, nothing is captured, so glow costs nothing.
    MultiEffect {
        anchors.fill: bars
        source: bars
        visible: settings.glow
        opacity: 0.85
        blurEnabled: true
        blur: 1.0
        blurMax: 48
        blurMultiplier: 0.3
    }

    SpectrumBars {
        id: bars
        anchors.fill: parent
    }

    // Corner controls. Presets will join the glow button here.
    Row {
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 8
        spacing: 4

        GlowButton {}
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
        sequence: "G"
        context: Qt.ApplicationShortcut
        onActivated: settings.glow = !settings.glow
    }
    Shortcut {
        sequence: "C"
        context: Qt.ApplicationShortcut
        onActivated: settings.colorMode = settings.colorMode === "spectrum" ? "vertical" : "spectrum"
    }
}
