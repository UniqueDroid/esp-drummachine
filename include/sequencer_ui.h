#pragma once

// Display + touch: 16x8 step grid, touch-to-toggle, playhead highlight.
void sequencerUiInit();
void sequencerUiPollTouch();
void sequencerUiUpdatePlayhead();

// Touch and SD share the same VSPI peripheral at different pin mappings.
// Call this once, after all boot-time SD access (mount + sample loading)
// is done and before the main loop starts, to repoint the shared bus back
// to the touch controller's pins.
void sequencerUiRestoreTouchSpi();

// Syncs the slider/label widgets and the step grid to the current
// trackMatrix/bpm/volume globals - call after something outside the UI
// (e.g. loading a saved pattern at boot) changed them directly.
void sequencerUiRefreshFromState();
