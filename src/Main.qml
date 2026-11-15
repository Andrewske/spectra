import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Effects

// The visualizer is the window: one dim menu in the corner, everything else
// on the keyboard. With --wallpaper the same scene sits on a layer under
// every window, with no menu and no input.
ApplicationWindow {
    id: window
    width: 960
    height: 400
    // wallpaper::attach shows the window once it's on its layer.
    visible: !wallpaperMode
    title: "Spectra"
    color: wallpaperMode && settings.wallpaperTransparent ? "transparent" : settings.background
    Material.theme: Material.Dark

    property bool helpShown: false
    readonly property string problems: [capture.error, settings.error]
        .filter(text => text.length > 0).join("\n")

    // Ticks once per rendered frame, so the visualizer moves in step with vsync.
    // While capture sleeps the bars have already fallen to zero, so there's
    // nothing to animate and no reason to run an FFT on silence every frame.
    FrameAnimation {
        running: !capture.sleeping
        onTriggered: spectrum.tick(frameTime)
    }

    // As wallpaper the scene fills only the bottom of the screen and fades
    // a little, so it reads as background.
    Item {
        id: scene
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: wallpaperMode ? parent.height * settings.wallpaperHeight : parent.height
        opacity: wallpaperMode ? settings.wallpaperOpacity : 1

        // Glow is a blurred copy of the visualizer drawn underneath the sharp
        // one, so each mark gets a soft halo in its own color. MultiEffect
        // captures the visualizer itself; turning on its own layer instead
        // would squeeze it by the blur padding. Off, nothing is captured.
        MultiEffect {
            anchors.fill: visualizer
            source: visualizer
            visible: settings.glow
            opacity: 0.85
            blurEnabled: true
            blur: 1.0
            blurMax: 48
            blurMultiplier: 0.3
        }

        Visualizer {
            id: visualizer
            anchors.fill: parent
        }

        // Clicking the bars gives up on naming a preset, the way clicking
        // outside a field does anywhere else.
        TapHandler {
            onTapped: cornerMenu.stopNaming()
        }
    }

    CornerMenu {
        id: cornerMenu
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 8
        visible: !wallpaperMode
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
        visible: window.helpShown && !wallpaperMode
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
    // Ctrl+Q, not Q: a stray key in a window that's always tiled shouldn't quit it.
    Shortcut {
        sequence: "Ctrl+Q"
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
    Shortcut {
        sequence: "L"
        context: Qt.ApplicationShortcut
        onActivated: {
            const layouts = ["bars", "mirrored", "radial", "stereo", "waveform"]
            settings.layout = layouts[(layouts.indexOf(settings.layout) + 1) % layouts.length]
        }
    }
    // 1 to 9 apply the presets in the order the menu lists them.
    Instantiator {
        model: 9
        delegate: Shortcut {
            required property int index
            sequence: String(index + 1)
            context: Qt.ApplicationShortcut
            onActivated: presets.applyIndex(index)
        }
    }
}
