# Auricle Synth V14 — DSP / Voice Architecture Rewrite

## Purpose
V14 resolves the architectural inconsistencies found in V13 rather than adding another limiter or detune workaround.

## Voice architecture
- Explicit runtime state: `active` + envelope stages, with `FREE` represented by `active=false`, `ACTIVE` by a live envelope, and `RELEASE` by release-stage envelopes.
- Voice generations prevent the audio task from writing a stale DMA-block snapshot over a voice changed by MIDI/UI while the block was rendered.
- Voice stealing prefers an already-releasing/quiet voice, then the oldest active voice.
- Stolen voices receive a full DSP reset: oscillator phases, feedback memories, filter integrators and envelope state.
- Preset loading performs a hard scene reset before installing the new patch.

## Polyphony modes
- `POLY`: `polyphony` is the maximum number of simultaneous voices.
- `MONO`: one voice with a last-note-priority held-note stack. Releasing the top note returns to the previous held note.
- `LEGATO`: one voice with a held-note stack. New notes change pitch without retriggering the envelopes; releasing the top note returns to the previous held note.
- `UNISON`: `unison_voices` is independent from `polyphony`; `polyphony` is the number of note slots and each note consumes `unison_voices`. `unison_spread_cents` distributes frequency detune around the center.

## FM / PM architecture
Production algorithms are strictly 0..7:

0. `3 -> 2 -> 1`
1. `(2 + 3) -> 1`
2. `3 -> (1 + 2)`
3. `(3 -> 2) + 1`
4. `1 + 2 + 3`
5. `(3*) -> 2 -> 1`
6. `3 -> (2*) -> 1`
7. `3 -> 2 -> (1*)`

Operator parameters are explicitly separated:
- `carrier_level`: audio gain when the operator is a carrier.
- `pm_index`: phase modulation depth, in cycles of phase.
- `feedback`: feedback amount; only algorithms 5..7 use it.
- `detune_cents`: frequency offset only.

Diagnostics are separate algorithm IDs 8..18 and are intended to be reached through the diagnostic preset bank, not the production algorithm selector.

## Mixer
Per-voice post-filter hard peak limit remains only as a safety boundary. The polyphonic mix uses `1/sqrt(N)` normalization instead of `1/N`, followed by master gain and a final hard safety clamp. There is no permanent `tanh()` clipper.

## Synchronization
The audio task copies one coherent patch/voice snapshot under the same critical section used by MIDI/UI control changes. At the end of the DMA block, each local voice is committed only if its generation still matches the generation captured at the beginning of the block.

## Diagnostic matrix
Preset bank contains:
- PURE SINE TEST
- SINE + FILTER
- AMP LFO
- PITCH LFO
- 2 SINE SUM
- 3 SINE SUM
- FM 2->1
- FM 3->1
- FM CASCADE
- FEEDBACK
- FM LFO

The first production check should be additive (`1+2+3`) in 1, 2, 4 and 8 voices. If that is clean, proceed to the FM topology tests.

## Build audit
Host-side syntax audit was run with:
`cc -std=c11 -Wall -Wextra -Werror -fsyntax-only`
against `synth_engine.c`, `preset_manager.c` and `ui_menu.c` using ESP API stubs.

A complete ESP-IDF 6.1 hardware build cannot be performed in this container because the user's ESP-IDF installation and target toolchain are local to their machine.


## V14.1 click/diagnostic gain fix
- Mono/legato retrigger no longer hard-resets an already sounding voice. Oscillator phase and filter state are preserved across note changes.
- Poly voice stealing retriggers an active stolen voice softly instead of resetting phase/filter state at the next DMA block boundary.
- Diagnostic 2/3-sine sum paths bypass the filter so the diagnostic measures oscillator summing rather than filter gain/resonance.
- DIAG 3 SINE SUM gain reduced to 0.60 per carrier and 0.28 master level.

## V14.2 — sample-boundary MIDI event fix

V14.1 still had an architectural source of clicks: MIDI Note On/Off modified voice DSP state from the MIDI/UI task while `synth_audio_task()` was rendering a 64-sample DMA block. The generation mechanism then correctly rejected the partially rendered voice state, but the next block resumed from the old snapshot. That can move oscillator phase/filter/envelope state backwards by up to one DMA block and creates a transient click.

V14.2 fixes this by adding a 64-event MIDI note queue. `synth_note_on()`, `synth_note_off()` and `synth_all_notes_off()` now only enqueue events. The audio task drains the queue at the start of a DMA block and applies all events before taking the voice snapshot. Voice DSP state is therefore changed only at an audio-buffer boundary.

This preserves the existing generation protection while removing the race between MIDI events and the DSP render loop. Event latency is bounded by one 64-sample buffer (~1.45 ms at 44.1 kHz).

## V15 — clean polyphonic voice engine

- Kept the V14.2 sample-boundary MIDI event queue that removed note clicks.
- Moved SVF `tanf()` coefficient calculation out of the audio sample inner loop; coefficients are refreshed at an 8-sample control cadence.
- Added per-voice cached SVF coefficients and validity state.
- Added 32-sample anti-click fade-in for new voices and live voice stealing.
- Unison voice stealing now retargets an existing unison group in-place instead of hard-clearing its DSP state.
- Mixer now normalizes from audible envelope energy, so silent/releasing voices do not consume polyphonic headroom.
- Voices whose three envelopes have reached `ENV_IDLE` are removed before contributing to the mixer/active count.
- Added diagnostic algorithm 19 and preset 27: `DIAG POLY 1-8 VOICES` — one sine carrier per voice, no filter/FM/LFO.
- Preset count is now 28.
