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

    readonly property bool vertical: settings.colorMode === "vertical"
    readonly property real slot: count > 0 ? width / count : 0
    readonly property real barWidth: slot * (1 - settings.barGap)
    readonly property real capHeight: Math.max(2, Math.round(height * 0.008))

    // One solid color per bar for spectrum mode, worked out when the colors
    // or the band count change rather than every frame.
    readonly property var barColors: colorsAcross(count, settings.colors)
    // The same gradient sampled at 32 heights, so a peak cap in vertical
    // mode can take the color at its height without calling into C++.
    readonly property var heightColors: colorsAcross(32, settings.colors)
    property Gradient verticalGradient: null

    function colorsAcross(n, stops) {
        const out = []
        for (let i = 0; i < n; ++i)
            out.push(settings.colorAt(n > 1 ? i / (n - 1) : 0))
        return out
    }

    // Gradient stops can't be bound to a list, so the gradient is rebuilt as
    // one object when the colors change, and every bar shares it.
    function rebuildGradient() {
        const stops = settings.colors
        let source = "import QtQuick; Gradient {"
        for (let i = 0; i < stops.length; ++i) {
            const position = stops.length > 1 ? 1 - i / (stops.length - 1) : 0
            source += "GradientStop { position: " + position + "; color: \"" + stops[i] + "\" }"
        }
        const old = verticalGradient
        verticalGradient = Qt.createQmlObject(source + "}", bars, "verticalGradient")
        if (old)
            old.destroy()
    }

    function peakColor(index) {
        if (!vertical)
            return barColors[index]
        return heightColors[Math.min(31, Math.round(peaks[index] * 31))]
    }

    Component.onCompleted: rebuildGradient()

    Connections {
        target: settings
        function onChanged() { bars.rebuildGradient() }
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
                clip: bars.vertical

                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: bars.vertical ? bars.height : parent.height
                    color: bars.barColors[bar.index] ?? "white"
                    gradient: bars.vertical ? bars.verticalGradient : null
                }
            }

            Rectangle {
                // Bound to the setting rather than sampled per frame, so P
                // still works while the bars are frozen.
                visible: settings.showPeaks && bar.peak > 0.002
                width: parent.width
                height: bars.capHeight
                y: Math.max(0, Math.round(bars.height - bar.peak * bars.height) - bars.capHeight)
                color: visible ? bars.peakColor(bar.index) : "transparent"
            }
        }
    }
}
