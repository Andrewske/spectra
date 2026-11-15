#pragma once

// TODO: owned by the capture task. A home for watching PipeWire streams
// (what's playing, which app, output latency) so capture.cpp stays about
// recording. Free functions for parsing pactl/pw-dump output, so they can be
// tested on canned JSON.
