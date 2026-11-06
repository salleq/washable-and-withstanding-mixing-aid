# Backlog

Ideas and requests that are not scheduled yet. Newest first.

## Meters closer to real time

The level meters (and the loudness numbers) feel slow next to a DAW's own meters.

Why they lag today:
- The analyser publishes its numbers once per 50 ms block (`Analyzer::subLen`), and the window redraws them from a 30 Hz timer (`startTimerHz (30)` in `PluginEditor.cpp`). Worst case that is about 80 ms behind, and the steps are visible.
- The M, S and I bars are loudness meters by definition: momentary is a 400 ms window, short-term 3 s. They will never jump like a peak meter, and should not.
- Only the TPL bar shows peaks, and it also comes through the 50 ms block.

What could be done:
- A separate fast path for the meters: the audio thread keeps the block peak and RMS per channel in atomics (max since last read), the window reads and resets them every frame.
- Redraw the meters with `juce::VBlankAttachment` (the screen's own rate, 60 Hz or more) instead of the 30 Hz timer, and only the meters' area, so the rest of the window costs nothing extra.
- Proper ballistics on top: instant attack, about 20 dB/s fall, a peak hold line for 1.5 s.
- Maybe a thin L/R peak strip next to the loudness bars, so both are visible: loudness for decisions, peaks for "is it moving".
- Keep the CPU use low while the window is closed (no fast path work then).
