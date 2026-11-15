import QtQuick

// Picks the layout named by settings.layout. Each layout fills this item and
// reads spectrum.* itself.
Item {
    id: visualizer

    readonly property var sources: ({
        bars: "SpectrumBars.qml",
        mirrored: "MirroredLayout.qml",
        radial: "RadialLayout.qml",
        stereo: "StereoLayout.qml",
        waveform: "WaveformLayout.qml"
    })

    Loader {
        anchors.fill: parent
        source: visualizer.sources[settings.layout] ?? "SpectrumBars.qml"
    }
}
