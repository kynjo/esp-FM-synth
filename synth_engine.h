/**
 * Auricle Synth V14 Polyphony Fix V3 - 3-operator PM/FM DSP and voice architecture.
 */
#ifndef SYNTH_ENGINE_H
#define SYNTH_ENGINE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SYNTH_SAMPLE_RATE     (44100)
#define SYNTH_DMA_BUFFER_SIZE (64)
#define SYNTH_MAX_VOICES      (8)
#define SYNTH_MAX_NOTE_STACK  (16)
#define SINE_LUT_SIZE         (2048)

#define SYNTH_ALGORITHM_COUNT 8
#define SYNTH_ALGORITHM_FIRST_DIAGNOSTIC 8
#define SYNTH_ALGORITHM_LAST_DIAGNOSTIC 19

typedef enum {
    WAVE_SINE = 0, WAVE_TRIANGLE, WAVE_SAW, WAVE_SQUARE, WAVE_PULSE25,
    WAVE_HALF_SINE, WAVE_ABS_SINE, WAVE_DOUBLE_SINE, WAVE_FORMANT, WAVE_NOISE,
    WAVE_COUNT
} waveform_t;

typedef enum { ENV_IDLE = 0, ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE } env_stage_t;

typedef struct {
    float attack_ms, decay_ms, sustain_level, release_ms;
} adsr_params_t;

typedef struct {
    env_stage_t stage;
    float current_level, target_sustain, attack_step, decay_factor, release_factor;
} adsr_state_t;

typedef struct {
    waveform_t waveform;
    float ratio;
    float detune_cents;
    /* Carrier/audio level. Never used as PM depth. */
    float carrier_level;
    /* Explicit phase-modulation depth in cycles of phase. */
    float pm_index;
    /* Feedback amount, used only by algorithms 5..7. */
    float feedback;
    float velocity_sens;
    float key_scaling;
    adsr_params_t env;
} op_params_t;

typedef enum { FILTER_LOWPASS = 0, FILTER_BANDPASS, FILTER_HIGHPASS } filter_type_t;
typedef struct {
    filter_type_t type;
    float cutoff_hz, resonance, env_amount;
    adsr_params_t env;
} filter_params_t;

typedef enum { LFO_DEST_PITCH = 0, LFO_DEST_CUTOFF, LFO_DEST_FM_INDEX, LFO_DEST_AMP } lfo_dest_t;
typedef struct {
    waveform_t waveform;
    float rate_hz, depth;
    lfo_dest_t destination;
} lfo_params_t;

typedef enum { POLY_MODE_POLY = 0, POLY_MODE_MONO, POLY_MODE_UNISON, POLY_MODE_LEGATO } poly_mode_t;

typedef struct {
    char name[24];
    uint8_t algorithm;      /* 0..7 production; 8..18 diagnostics, preset-only. */
    float master_volume;
    float glide_ms;
    uint8_t polyphony;      /* POLY/MONO/LEGATO: voices or 1. UNISON: note slots. */
    uint8_t poly_mode;
    uint8_t unison_voices;  /* 1..SYNTH_MAX_VOICES; independent of polyphony. */
    float unison_spread_cents;
    op_params_t op[3];
    filter_params_t filter;
    lfo_params_t lfo1, lfo2;
} synth_patch_t;

typedef struct {
    bool active;
    /* True while the physical/MIDI key instance is still held. Release tails
     * remain active but are eligible for stealing and no longer match Note Off. */
    bool key_down;
    uint8_t midi_note;
    float velocity, target_freq, current_freq;
    uint32_t age;
    /* Unique MIDI trigger instance. Allows repeated same notes to coexist. */
    uint32_t trigger_id;
    uint32_t generation;
    float phase[3], fb[3];
    adsr_state_t env[3], filter_env;
    float svf_ic1, svf_ic2;
    /* Cached SVF coefficients. Recomputed at a control-rate cadence, not per sample. */
    float svf_g, svf_k, svf_a1;
    float svf_cutoff_cache;
    float svf_resonance_cache;
    uint8_t svf_type_cache;
    bool svf_coeff_valid;
    /* Short anti-click gain ramp for new/stolen voices. */
    float fade_gain;
    float fade_step;
} synth_voice_t;

typedef struct {
    synth_patch_t patch;
    synth_voice_t voices[SYNTH_MAX_VOICES];
    float lfo1_phase, lfo2_phase;
    float pitch_bend, mod_wheel;
    uint8_t active_voices;
    float scope_buffer[128];
} synth_engine_t;

extern synth_engine_t g_synth;

void synth_engine_init(void);
void synth_load_patch(const synth_patch_t *patch);
void synth_note_on(uint8_t note, uint8_t velocity);
void synth_note_off(uint8_t note);
void synth_all_notes_off(void);
void synth_set_pitch_bend(int16_t bend_raw);
void synth_set_mod_wheel(uint8_t val);
void synth_set_cc(uint8_t cc_num, uint8_t val);
void synth_patch_lock(void);
void synth_patch_unlock(void);
/* Must be called while synth_patch_lock() is held. */
void synth_scene_reset_locked(void);
void synth_audio_task(void *pvParameters);

#ifdef __cplusplus
}
#endif
#endif
