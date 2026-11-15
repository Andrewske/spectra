import QtQuick

// The bars. Levels and peaks are copied out of C++ once per frame, and each
// bar indexes into the copy, so the QList isn't converted to JS once per bar.
Item {
    id: bars

    property var levels: []
    property var peaks: []
    // An int only notifies when it changes, so the Repeater keeps its
    // delegates from frame to frame instead of rebuilding them.
    readonly property int count: levels.length

    readonly property real slot: count > 0 ? width / count : 0
    readonly property real barWidth: slot * (1 - settings.barGap)
    readonly property real capHeight: Math.max(2, Math.round(height * 0.008))

    BarPalette {
        id: tones
        count: bars.count
    }

    Connections {
        target: spectrum
        function onUpdated() {
            bars.levels = spectrum.levels
            bars.peaks = spectrum.peaks
        }
    }

    Repeater {
        model: bars.count

        Item {
            id: bar
            required property int index
            readonly property real level: bars.levels[index] ?? 0
            readonly property real peak: bars.peaks[index] ?? 0

            x: index * bars.slot + (bars.slot - bars.barWidth) / 2
            width: bars.barWidth
            height: bars.height

            // In vertical mode the gradient spans the whole window and the
            // bar only reveals its bottom part, so color reads as loudness.
            Item {
                anchors.bottom: parent.bottom
                width: parent.width
                height: Math.round(bar.level * bars.height)
                clip: tones.vertical

                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: tones.vertical ? bars.height : parent.height
                    color: tones.bandColors[bar.index] ?? "white"
                    gradient: tones.vertical ? tones.rising : null
                }
            }

            Rectangle {
                // Bound to the setting rather than sampled per frame, so P
                // still works while the bars are frozen.
                visible: settings.showPeaks && bar.peak > 0.002
                width: parent.width
                height: bars.capHeight
                y: Math.max(0, Math.round(bars.height - bar.peak * bars.height) - bars.capHeight)
                color: !visible ? "transparent"
                       : tones.vertical ? tones.levelColor(bar.peak) : tones.bandColors[bar.index]
            }
        }
    }
}
