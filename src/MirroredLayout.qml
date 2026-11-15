import QtQuick

// Bars grow up and down from the center line, the same level both ways.
CenterBars {
    id: layout

    // A layout loads after the frame that switched to it, and a frozen or
    // sleeping spectrum sends no more frames, so take the current outputs now.
    Component.onCompleted: feed.onUpdated()

    Connections {
        id: feed
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
