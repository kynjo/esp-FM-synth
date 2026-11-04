# Auricle Synth V14

ESP32-S3 3-operator PM/FM synthesizer for ESP-IDF 6.1.

Hardware configuration is unchanged:
- PCM5102A: BCK GPIO4, WS/LCK GPIO5, DATA GPIO6
- SH1106 I2C: SDA GPIO8, SCL GPIO9, address 0x3C
- EC11: CLK GPIO1, DT GPIO2, SW GPIO3
- PREV GPIO10, NEXT GPIO11
- Native USB MIDI GPIO19/20
- Status LED GPIO7

V14 is a DSP/voice architecture rewrite focused on polyphony correctness, FM topology correctness, patch synchronization, and diagnostic isolation.

See `PATCH_NOTES.md` for the complete architecture and test plan.


### V14.1 hardware test
Expected: no obvious click on ordinary mono/poly note-on transitions; DIAG 3 SINE SUM should be clean and lower in level.

### V14.2 click fix

MIDI note events are now queued and applied only at the beginning of an audio DMA block. This prevents voice state from being changed while a block is being rendered and eliminates the phase/state rollback that caused note clicks in V14/V14.1.

## V15 polyphony diagnostic

Preset 27, `DIAG POLY 1-8 VOICES`, isolates the polyphonic allocator and mixer. Use it to test 1, 2, 4 and 8 simultaneous MIDI notes before evaluating the production FM algorithms.

V15 also keeps the V14.2 sample-boundary MIDI queue and adds cached SVF coefficients plus short voice fade-in on allocation/stealing.
