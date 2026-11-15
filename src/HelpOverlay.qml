import QtQuick

// Every shortcut, shown over the bars when the user presses ?.
Rectangle {
    id: overlay
    color: "#d0000000"

    signal dismissed()

    readonly property var shortcuts: [
        ["?", "Show or hide this help"],
        ["Space", "Freeze or unfreeze the bars"],
        ["F", "Full screen"],
        ["P", "Peak caps on or off"],
        ["C", "Switch color mode (spectrum or vertical)"],
        ["L", "Next layout (bars, mirrored, radial, stereo, waveform)"],
        ["1-9", "Apply a preset (hover the sparkle for the menu)"],
        ["G", "Glow on or off (or click the sparkle)"],
        ["Ctrl+Q", "Quit"]
    ]

    MouseArea {
        anchors.fill: parent
        onClicked: overlay.dismissed()
    }

    Column {
        anchors.centerIn: parent
        spacing: 8

        Grid {
            columns: 2
            columnSpacing: 20
            rowSpacing: 8

            Repeater {
                model: overlay.shortcuts.length * 2

                Text {
                    required property int index
                    text: overlay.shortcuts[Math.floor(index / 2)][index % 2]
                    color: index % 2 === 0 ? "white" : "#b0b0b0"
                    font.pixelSize: 15
                    font.bold: index % 2 === 0
                }
            }
        }

        Text {
            text: "Colors and tuning: " + settings.path
            color: "#808080"
            font.pixelSize: 12
            topPadding: 12
        }
    }
}
