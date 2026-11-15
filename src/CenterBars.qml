import QtQuick

// Bars that grow away from the horizontal center line: `upper` levels grow
// up, `lower` levels grow down, each over half the height. The mirrored and
// stereo layouts are this with different lists, copied in once per frame.
Item {
    id: bars

    property var upper: []
    property var lower: []
    property var upperPeaks: []
    property var lowerPeaks: []
    // Space between the two halves, so two different signals read apart.
    property real centerGap: 0

    readonly property int count: upper.length
    readonly property real half: Math.max(0, (height - centerGap) / 2)
    readonly property real slot: count > 0 ? width / count : 0
    readonly property real barWidth: slot * (1 - settings.barGap)
    readonly property real capHeight: Math.max(2, Math.round(height * 0.008))

    BarPalette {
        id: tones
        count: bars.count
    }

    Repeater {
        model: bars.count

        Item {
            id: bar
            required property int index
            readonly property real up: bars.upper[index] ?? 0
            readonly property real down: bars.lower[index] ?? 0
            readonly property color solid: tones.bandColors[index] ?? "white"

            x: index * bars.slot + (bars.slot - bars.barWidth) / 2
            width: bars.barWidth
            height: bars.height

            // As in SpectrumBars, vertical mode reveals part of a gradient
            // that spans the whole half, so color reads as loudness: low at
            // the center line, high at the edges.
            Item {
                y: bars.half - height
                width: parent.width
                height: Math.round(bar.up * bars.half)
                clip: tones.vertical

                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: tones.vertical ? bars.half : parent.height
                    color: bar.solid
                    gradient: tones.vertical ? tones.rising : null
                }
            }

            Item {
                y: bars.height - bars.half
                width: parent.width
                height: Math.round(bar.down * bars.half)
                clip: tones.vertical

                Rectangle {
                    width: parent.width
                    height: tones.vertical ? bars.half : parent.height
                    color: bar.solid
                    gradient: tones.vertical ? tones.falling : null
                }
            }

            // Caps are bound to the setting rather than sampled per frame,
            // so P still works while the bars are frozen.
            Rectangle {
                readonly property real peak: bars.upperPeaks[bar.index] ?? 0
                visible: settings.showPeaks && peak > 0.002
                width: parent.width
                height: bars.capHeight
                y: Math.max(0, Math.round(bars.half - peak * bars.half) - height)
                color: visible ? bars.capColor(bar.index, peak) : "transparent"
            }

            Rectangle {
                readonly property real peak: bars.lowerPeaks[bar.index] ?? 0
                visible: settings.showPeaks && peak > 0.002
                width: parent.width
                height: bars.capHeight
                y: Math.min(bars.height - height, Math.round(bars.height - bars.half + peak * bars.half))
                color: visible ? bars.capColor(bar.index, peak) : "transparent"
            }
        }
    }

    function capColor(index, peak) {
        return tones.vertical ? tones.levelColor(peak) : tones.bandColors[index] ?? "white"
    }
}
