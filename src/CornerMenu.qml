import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// The sparkle in the corner toggles glow on click. Hovering it opens a small
// panel below with the look toggles, the presets and a way to save one, so
// the window stays all bars until the pointer goes looking for controls.
Item {
    id: menu

    property bool open: false
    property bool naming: false
    // Typing a name keeps the panel up even after the pointer wanders off.
    readonly property bool wanted: hover.hovered || nameField.activeFocus
    readonly property var layouts: ["bars", "mirrored", "radial", "stereo", "waveform"]

    // The bounds grow with the panel, so the one HoverHandler covers the
    // sparkle, the gap below it and the panel.
    width: open ? Math.max(sparkle.width, panel.width) : sparkle.width
    height: open ? panel.y + panel.height : sparkle.height

    onWantedChanged: {
        if (wanted) {
            closeTimer.stop()
            open = true
        } else {
            closeTimer.restart()
        }
    }

    function capitalized(word) {
        return word.charAt(0).toUpperCase() + word.slice(1)
    }

    function startNaming() {
        nameField.text = ""
        nameError.text = ""
        naming = true
        nameField.forceActiveFocus()
    }

    function stopNaming() {
        naming = false
        nameField.focus = false
    }

    function saveName() {
        if (presets.saveCurrent(nameField.text))
            stopNaming()
        else
            nameError.text = qsTr("Use another name")
    }

    TextMetrics {
        id: longestMode
        font.pixelSize: 12
        text: "Spectrum"
    }
    TextMetrics {
        id: longestLayout
        font.pixelSize: 12
        text: "Waveform"
    }

    Timer {
        id: closeTimer
        interval: 300
        onTriggered: menu.open = false
    }

    HoverHandler {
        id: hover
    }

    GlowButton {
        id: sparkle
        anchors.top: parent.top
        anchors.right: parent.right
    }

    Rectangle {
        id: panel
        anchors.right: parent.right
        y: sparkle.height + 4
        width: content.implicitWidth + 20
        height: content.implicitHeight + 20
        radius: 6
        color: "#e6101010"
        opacity: menu.open ? 1 : 0
        visible: opacity > 0

        Behavior on opacity {
            NumberAnimation { duration: 120 }
        }

        Column {
            id: content
            x: 10
            y: 10
            spacing: 10

            Row {
                id: toggles
                spacing: 6

                MenuChip {
                    text: qsTr("Glow")
                    tip: qsTr("Glow (G)")
                    checked: settings.glow
                    onClicked: settings.glow = !settings.glow
                }
                MenuChip {
                    text: qsTr("Peaks")
                    tip: qsTr("Peak caps (P)")
                    checked: settings.showPeaks
                    onClicked: settings.showPeaks = !settings.showPeaks
                }
                MenuChip {
                    text: menu.capitalized(settings.colorMode)
                    tip: qsTr("Color mode (C)")
                    minimumWidth: longestMode.advanceWidth + 16
                    onClicked: settings.colorMode = settings.colorMode === "spectrum" ? "vertical" : "spectrum"
                }
                MenuChip {
                    text: menu.capitalized(settings.layout)
                    tip: qsTr("Layout (L)")
                    minimumWidth: longestLayout.advanceWidth + 16
                    onClicked: settings.layout = menu.layouts[(menu.layouts.indexOf(settings.layout) + 1) % menu.layouts.length]
                }
            }

            Flow {
                width: toggles.width
                spacing: 4

                Repeater {
                    model: presets.list
                    delegate: PresetSwatch {}
                }
            }

            Item {
                width: toggles.width
                height: 32

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: !menu.naming
                    text: qsTr("Save as preset…")
                    color: saveHover.hovered ? "#ffffff" : "#b0b0b0"
                    font.pixelSize: 12

                    HoverHandler {
                        id: saveHover
                        cursorShape: Qt.PointingHandCursor
                    }
                    TapHandler {
                        onTapped: menu.startNaming()
                    }
                }

                TextField {
                    id: nameField
                    anchors.fill: parent
                    visible: menu.naming
                    placeholderText: qsTr("Preset name")
                    font.pixelSize: 12
                    Material.containerStyle: Material.Outlined
                    Material.accent: "#e0e0e0"
                    topPadding: 6
                    bottomPadding: 6
                    Keys.onReturnPressed: menu.saveName()
                    Keys.onEnterPressed: menu.saveName()
                    Keys.onEscapePressed: menu.stopNaming()
                    onTextEdited: nameError.text = ""
                }
            }

            Text {
                id: nameError
                visible: text.length > 0
                color: "#ff8080"
                font.pixelSize: 11
            }
        }
    }
}
