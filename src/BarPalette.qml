import QtQuick

// The colors every layout draws with, worked out when the colors or the band
// count change rather than every frame. Spectrum mode gives each band one
// solid color; vertical mode colors by level, through `levelColor` for small
// marks and the shared gradients for bars.
QtObject {
    id: palette

    property int count: 0
    readonly property bool vertical: settings.colorMode === "vertical"
    // One solid color per band, low to high across the gradient.
    readonly property var bandColors: colorsAcross(count, settings.colors)
    // The gradient sampled at 32 levels, so a peak cap can take the color at
    // its height without calling into C++.
    readonly property var levelColors: colorsAcross(32, settings.colors)
    // Vertical-mode gradients: `rising` puts the lowest color at the bottom,
    // `falling` at the top, for bars that grow downward.
    property Gradient rising: null
    property Gradient falling: null

    function colorsAcross(n, stops) {
        const out = []
        for (let i = 0; i < n; ++i)
            out.push(settings.colorAt(n > 1 ? i / (n - 1) : 0))
        return out
    }

    function levelColor(level) {
        return levelColors[Math.max(0, Math.min(31, Math.round(level * 31)))]
    }

    // Gradient stops can't be bound to a list, so each gradient is rebuilt
    // as one object when the colors change, and every bar shares it.
    function makeGradient(lowAtBottom) {
        const stops = settings.colors
        let source = "import QtQuick; Gradient {"
        for (let i = 0; i < stops.length; ++i) {
            const t = stops.length > 1 ? i / (stops.length - 1) : 0
            const position = lowAtBottom ? 1 - t : t
            source += "GradientStop { position: " + position + "; color: \"" + stops[i] + "\" }"
        }
        return Qt.createQmlObject(source + "}", palette, "barGradient")
    }

    function rebuild() {
        const old = [rising, falling]
        rising = makeGradient(true)
        falling = makeGradient(false)
        old.forEach(gradient => { if (gradient) gradient.destroy() })
    }

    Component.onCompleted: rebuild()

    property Connections watcher: Connections {
        target: settings
        function onChanged() { palette.rebuild() }
    }
}
