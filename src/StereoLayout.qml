import QtQuick

// The left channel grows up from the center line and the right grows down,
// so panning reads as the shape tipping one way.
CenterBars {
    id: layout
    centerGap: 2

    Connections {
        target: spectrum
        function onUpdated() {
            layout.upper = spectrum.levelsLeft
            layout.lower = spectrum.levelsRight
            layout.upperPeaks = spectrum.peaksLeft
            layout.lowerPeaks = spectrum.peaksRight
        }
    }
}
