# Backlog

Ideas and requests that are not scheduled yet. Newest first.

## Sync references to the mix

Today a reference follows the DAW's song position with one offset (`cueSamples` in `ReferenceTracks::render`), set by the last click on its waveform. The offset is reset when another slot is chosen (`ReferenceTracks::updateAudioSide`) and is not saved with the project, so the same spot of the song does not land on the same spot of the reference twice.

Decided:
- **Earlier versions sync by themselves.** A slot marked EARLIER is the same song, so WWMA lines it up: cross-correlate the bounce against the BEFORE take (`BeforeTake`, recorded by song position) to find the offset, so it works even if the bounce starts at bar 2. With nothing recorded yet, 0:00 of the bounce = the start of the project.
- **Different songs: a setting "Sync references to mix".** While it is on, choosing a spot on the reference with the DAW at a spot pairs the two (DAW 1:12 → ref 0:58). A reference can have many pairs: your verse to its verse, your chorus to its chorus. Playing from DAW position X uses the nearest pair at or before X; when playback passes the next pair, the reference jumps to that pair's spot (with a short crossfade).
- Pairs are kept per slot and saved with the project, like the loop and the EARLIER mark. The big reference view lists them, with a way to remove one.
