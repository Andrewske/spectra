import QtQuick

// Bars grow up and down from the center line, the same level both ways.
CenterBars {
    id: layout

    Connections {
        target: spectrum
        function onUpdated() {
            // One conversion per list per frame, shared by both halves.
            const levels = spectrum.levels
            const peaks = spectrum.peaks
            layout.upper = levels
            layout.lower = levels
            layout.upperPeaks = peaks
            layout.lowerPeaks = peaks
        }
    }
}
