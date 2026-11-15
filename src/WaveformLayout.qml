import QtQuick
import QtQuick.Shapes

// The audio itself as a line across the window, colored by the gradient.
// Qt 6.12's ShapePath.strokeGradient strokes it directly: in spectrum mode
// the gradient runs left to right, in vertical mode it runs from the center
// line out to both edges, so color reads as amplitude.
Item {
    id: wave

    property var samples: []
    // A binding rather than an assignment, so a resize while frozen redraws too.
    readonly property var points: toPath(samples, width, height)
    property LinearGradient gradient: null

    // Builds the polyline once per frame from one copy of the samples.
    function toPath(values, w, h) {
        const n = values.length
        const dx = n > 1 ? w / (n - 1) : 0
        const middle = h / 2
        // Peaks land a little inside the edges, where the glow still fits.
        const scale = h * 0.42
        const out = new Array(n)
        for (let i = 0; i < n; ++i)
            out[i] = Qt.point(i * dx, middle - values[i] * scale)
        return out
    }

    // The stops are text so the gradient can be rebuilt as one object when
    // the colors change; its end points stay bound to the size.
    function makeGradient() {
        const vertical = settings.colorMode === "vertical"
        let source = "import QtQuick; import QtQuick.Shapes; LinearGradient {"
        for (const stop of gradientStops(vertical))
            source += "GradientStop { position: " + stop[0] + "; color: \"" + stop[1] + "\" }"
        const g = Qt.createQmlObject(source + "}", wave, "waveGradient")
        g.x2 = Qt.binding(() => vertical ? 0 : wave.width)
        g.y2 = Qt.binding(() => vertical ? wave.height : 0)
        return g
    }

    // [position, color] pairs in ascending order. Vertical mode mirrors the
    // colors around the center line, lowest in the middle.
    function gradientStops(vertical) {
        const colors = settings.colors
        const last = Math.max(1, colors.length - 1)
        const stops = []
        for (let i = 0; i < colors.length; ++i) {
            const t = i / last
            stops.push([vertical ? 0.5 - t / 2 : t, colors[i]])
            if (vertical && i > 0)
                stops.push([0.5 + t / 2, colors[i]])
        }
        return stops.sort((a, b) => a[0] - b[0])
    }

    function rebuildGradient() {
        const old = gradient
        gradient = makeGradient()
        if (old)
            old.destroy()
    }

    Connections {
        target: settings
        function onChanged() { wave.rebuildGradient() }
    }

    // A layout loads after the frame that switched to it, and a frozen or
    // sleeping spectrum sends no more frames, so take the current outputs now.
    Component.onCompleted: {
        rebuildGradient()
        feed.onUpdated()
    }

    Connections {
        id: feed
        target: spectrum
        function onUpdated() { wave.samples = spectrum.waveform }
    }

    Shape {
        anchors.fill: parent
        // The curve renderer antialiases on the GPU without multisampling.
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            strokeWidth: Math.max(2, Math.min(3, wave.height / 160))
            strokeGradient: wave.gradient
            fillColor: "transparent"
            joinStyle: ShapePath.RoundJoin
            capStyle: ShapePath.RoundCap

            PathPolyline { path: wave.points }
        }
    }
}
