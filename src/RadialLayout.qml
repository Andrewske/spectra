import QtQuick

// Bars around a ring, growing outward. Bass sits at the top and the bands
// run down both sides to the treble at the bottom, mirrored so the ring is
// symmetric. No peak caps: thin arcs at every radius turn into clutter.
Item {
    id: ring

    property var levels: []
    readonly property int count: levels.length

    readonly property real side: Math.min(width, height)
    readonly property real inner: side * 0.25
    // Room left between the ring and the edge, less a little so the glow
    // of a full bar isn't cut off.
    readonly property real reach: Math.max(0, side / 2 - inner - side * 0.03)
    // Each band appears twice, once per side, around the inner circle.
    readonly property real slot: count > 0 ? Math.PI * inner / count : 0
    readonly property real barWidth: Math.max(1, slot * (1 - settings.barGap))

    BarPalette {
        id: tones
        count: ring.count
    }

    Connections {
        target: spectrum
        function onUpdated() { ring.levels = spectrum.levels }
    }

    Repeater {
        model: ring.count * 2

        Item {
            id: bar
            required property int index
            readonly property int band: index % ring.count
            readonly property real level: ring.levels[band] ?? 0

            // Laid out pointing straight up from the center, then turned
            // into place around the center point.
            x: ring.width / 2 - width / 2
            y: ring.height / 2 - ring.inner - ring.reach
            width: ring.barWidth
            height: ring.reach
            transform: Rotation {
                origin.x: bar.width / 2
                origin.y: ring.reach + ring.inner
                angle: (bar.index < ring.count ? 1 : -1) * (bar.band + 0.5) / ring.count * 180
            }

            // Vertical mode reveals part of a gradient as long as the reach,
            // so color reads as loudness from the ring outward. The clip is
            // a rotated rectangle (a stencil, not antialiased), so it's wider
            // than the bar and only ever cuts the outer end.
            Item {
                anchors.bottom: parent.bottom
                anchors.horizontalCenter: parent.horizontalCenter
                width: parent.width + 4
                // A stub of 2 px keeps the ring visible through silence.
                height: Math.max(2, Math.round(bar.level * ring.reach))
                clip: tones.vertical

                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: bar.width
                    height: tones.vertical ? ring.reach : parent.height
                    antialiasing: true
                    color: tones.bandColors[bar.band] ?? "white"
                    gradient: tones.vertical ? tones.rising : null
                }
            }
        }
    }
}
