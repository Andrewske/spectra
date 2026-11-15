import QtQuick

// The left channel grows up from the center line and the right grows down,
// so panning reads as the shape tipping one way.
CenterBars {
    id: layout
    centerGap: 2

    // A layout loads after the frame that switched to it, and a frozen or
    // sleeping spectrum sends no more frames, so take the current outputs now.
    Component.onCompleted: feed.onUpdated()

    Connections {
        id: feed
        target: spectrum
        function onUpdated() {
            layout.upper = spectrum.levelsLeft
            layout.lower = spectrum.levelsRight
            layout.upperPeaks = spectrum.peaksLeft
            layout.lowerPeaks = spectrum.peaksRight
        }
    }
}
