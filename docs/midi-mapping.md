# MIDI mapping reference

Ground truth is `main/launchpad_modes.c` — this file documents it in one
place for people mapping pads in a DAW, or writing a test/host tool
against this firmware. If the two ever disagree, the code wins; update
this file to match.

## Transport

Raw 3-byte USB-MIDI messages, cable number 0, sent via
`orion_usb_midi_send()`:

| Byte 0 (status)       | Byte 1       | Byte 2        |
|------------------------|--------------|---------------|
| `0x90 \| channel` Note On  | note (0-127) | velocity (0-127) |
| `0x80 \| channel` Note Off | note (0-127) | 0 |
| `0xB0 \| channel` Control Change | CC number (0-127) | value (0-127) |

`channel` is 0-based in the packet and displayed as 1–16. The footer's
Channel button opens a 4x4 selector. Each mode recalls its own channel;
Drums initially uses channel 10 and the other modes initially use channel 1.
Changing mode/channel/layout releases held notes on their original channel.
All fingers must lift after these transitions before performance resumes.

## Note mode

Isomorphic layout: `note = 36 + row * cols + col`, where `(row, col)` is
the pad's position (row 0 = top) and `cols` is the active variant's column
count. Base note 36 = C2; this is a usable default, not a GM/DAW
convention. Velocity is fixed: **100** on press, Note Off (velocity 0) on
release — the firmware does not implement pressure or strike-velocity sensing.

| Variant | Shape (cols x rows) | Note range |
|---|---|---|
| 0 (default) | 8x4 | 36–67 |
| 1 | 8x2 | 36–51 |
| 2 | 4x4 | 36–51 |

(Range widths overlap across variants because the formula only depends on
`row*cols+col` — e.g. variant 1 and 2 both top out at 51 despite different
shapes, since both have `rows*cols=16`.)

## Drum mode

Fixed 4x4 grid, no variants — the mapping below is a real GM Percussion
Key Map lookup table (channel-10 convention), not formulaic, so it can't
generalize to other shapes without a different table. Velocity is fixed:
**110** on press. The initial channel is **10**, following the GM convention. A manually
selected channel is recalled for this mode until power-off. DAW drum racks
may use different channels and mappings; configure them explicitly.

Row 0 is the top of the screen, row 3 the bottom (kick/snare under the
thumb, matching a real hardware drum pad's layout convention):

| Row | Col 0 | Col 1 | Col 2 | Col 3 |
|---|---|---|---|---|
| 0 (top) | 49 Crash Cymbal 1 | 51 Ride Cymbal 1 | 57 Crash Cymbal 2 | 59 Ride Cymbal 2 |
| 1 | 45 Low Tom | 47 Low-Mid Tom | 48 Hi-Mid Tom | 50 High Tom |
| 2 | 39 Hand Clap | 42 Closed Hi-Hat | 44 Pedal Hi-Hat | 46 Open Hi-Hat |
| 3 (bottom) | 35 Acoustic Bass Drum | 36 Bass Drum 1 | 38 Acoustic Snare | 40 Electric Snare |

## Mixer-CC mode

Each strip uses **relative vertical dragging**, with raw absolute 7-bit CC
messages on the wire. Touch-down does not change or send the value. Moving
up increases it, moving down decreases it; only changed values are sent.
The full throw is `max(100, grid_area.h - 143)` logical pixels. On release,
the value remains parked. Per-channel values are kept in RAM until reboot.
This does not implement host feedback or DAW parameter takeover.

CC number = `20 + col` (the "undefined, general purpose" range of the
MIDI 1.0 CC table — safe against colliding with a conventional CC's usual
meaning):

| Variant | Columns | CC numbers used |
|---|---|---|
| 0 (default) | 4 | CC20–23 |
| 1 | 8 | CC20–27 |
| 2 | 2 | CC20–21 |

## XY-macro mode

No discrete pads — the entire grid area is one continuous touch surface.
The first touch owns the surface. On acquisition sends both, then sends
only axes whose quantized values change:

- **CC1** (Mod Wheel) ← X position, normalized 0–127 across the surface's width
- **CC74** (Filter Cutoff / "Brightness") ← Y position, normalized 0–127,
  inverted (top of surface = 127)

Assignments must be checked in the target synth or DAW; they are not
universal mappings. Releasing holds the last values. No variants.

## Macros (internal Knob mode)

A grid of rotary CC knobs, one per cell — a genuine discrete grid (unlike
Mixer-CC), but **not** press/release pad events: pressing a knob grabs it,
and dragging **vertically** (not around an arc) changes its value,
relative to wherever the drag started — press, don't move, and nothing
changes; drag up to increase, down to decrease. This is deliberate, not a
simplification: a flat touchscreen has no physical detents, so mapping an
absolute touch angle to a value both forces pixel-precise touch-down
accuracy and is numerically unstable near a knob's small center — the same
relative-drag convention TouchOSC/Lemur/Ableton's own touch-knob widgets
use. Releasing leaves the knob parked at its last value — nothing is sent
on release, same policy as Mixer-CC.

CC number = `102 + knob_index` (row-major index within the variant's grid;
MIDI 1.0's CC102-119 undefined/general-purpose block — distinct from
Mixer-CC's CC20-27 and XY's CC1/CC74, so all three modes' CC traffic never
collides even if a host is listening for all of them at once):

| Variant | Shape (cols x rows) | Knobs | CC numbers used |
|---|---|---|---|
| 0 (default) | 4x2 | 8 | CC102–109 |
| 1 | 2x2 | 4 | CC102–105 |
| 2 | 4x4 | 16 | CC102–117 |

Each macro initializes locally to 0 at boot, then recalls its value per
channel until power-off. These are not queried host values. The on-screen pointer
sweeps -135°..+135° (0° = straight up = the mid-value), matching a real
potentiometer's physical travel range rather than a full circle.

## Panic and delivery status

Panic releases tracked notes, then sends CC64=0, CC123=0 and CC120=0 on
every channel used by the controller since boot, plus the current channel.
This intentionally affects other voices sharing those channels. `Sent`
means USB accepted the writes, not that the destination applied them.
A failed transport write latches a visible warning until the *next*
successful send (not only on a Panic retry). There is no reliable
retransmission queue yet.

## Looper

Not a new message type of its own — it records and replays whatever the
*other* modes actually send, **plus** whatever a connected host/DAW sends
back to the device while a track is armed. 4 independent tracks (T1-T4);
tap a track button to select which one Record/Play/Clear target — the
other three keep doing whatever they were already doing (playing,
recording, paused) regardless of which one is selected, so tracks can
layer: record track 1, let it loop, select track 2, record over the top
of it, and so on. Tracks are not forced to share a length — each has its
own independently-timed loop.

Recording/playback state is global (not tied to which tab is currently
showing): tap Record to start capturing on the selected track, switch to
Notes, play a part, switch back to Loop and tap Record again to close the
loop at that length — it starts looping immediately. Switch to any other
mode and keep playing; the armed track keeps capturing, and every track
already looping keeps playing underneath. Tap Record again while playing
to overdub that track (adds new events into its existing loop without
erasing it); tap again to stop adding. Play/Stop pauses and resumes that
track's playback (resumes from where it paused, not from the start).
Clear wipes the selected track only — the other three are unaffected.

A BPM readout with +/- buttons (default 120, range 20-300) and a pulsing
beat indicator give a visual tempo reference. This is a feel aid only —
it does not quantize or snap recording/playback to a grid; each track's
actual timing is exactly what you played.

Played-back events are re-sent exactly as originally captured (same
channel, same note/CC numbers and values they had when recorded) —
changing the channel selector afterward does not retroactively change
already-recorded events, only what gets recorded *next*. Up to 2048
events per track; recording auto-stops on that track (closing its loop at
whatever was captured so far) if that fills up. Not saved across
power-off, same as every other mode's state.
