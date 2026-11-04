#include "synth_engine.h"
#include "pin_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/portmacro.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG = "SYNTH_DSP";
#define PI_FLOAT 3.14159265358979323846f
#define TWO_PI_FLOAT 6.28318530717958647692f
#define VOICE_POST_FILTER_PEAK 0.45f
#define POLY_MIX_HEADROOM 0.75f
#define FINAL_SAFETY_PEAK 0.98f
#define MAX_OPERATOR_INCREMENT 0.499f
#define NOTE_STACK_MAX SYNTH_MAX_NOTE_STACK
#define NOTE_EVENT_QUEUE_SIZE 64

typedef enum { NOTE_EVENT_ON = 0, NOTE_EVENT_OFF, NOTE_EVENT_ALL_OFF } note_event_type_t;
typedef struct { note_event_type_t type; uint8_t note; uint8_t velocity; } note_event_t;
static note_event_t s_note_events[NOTE_EVENT_QUEUE_SIZE];
static uint8_t s_note_event_head = 0;
static uint8_t s_note_event_tail = 0;
static uint32_t s_note_trigger_counter = 0;
/* Voice DSP is owned exclusively by the audio task. Other tasks only enqueue
 * note events or request a scene reset at the next audio block boundary. */
static bool s_scene_reset_pending = false;

synth_engine_t g_synth;
static float s_sine_lut[SINE_LUT_SIZE];
static float s_note_freq[128];
static i2s_chan_handle_t s_tx_chan = NULL;
static portMUX_TYPE s_voice_mux = portMUX_INITIALIZER_UNLOCKED;
static uint8_t s_held_notes[NOTE_STACK_MAX];
static uint8_t s_held_count = 0;
static uint32_t s_age_counter = 0;

static void init_sine_lut(void) {
    for (int i = 0; i < SINE_LUT_SIZE; ++i) s_sine_lut[i] = sinf((float)i * TWO_PI_FLOAT / (float)SINE_LUT_SIZE);
    for (int i = 0; i < 128; ++i) s_note_freq[i] = 440.0f * powf(2.0f, ((float)i - 69.0f) / 12.0f);
}

static inline float lookup_sine(float phase) {
    while (phase >= 1.0f) phase -= 1.0f;
    while (phase < 0.0f) phase += 1.0f;
    float x = phase * (float)SINE_LUT_SIZE;
    int i0 = ((int)x) & (SINE_LUT_SIZE - 1);
    int i1 = (i0 + 1) & (SINE_LUT_SIZE - 1);
    float f = x - (float)i0;
    return s_sine_lut[i0] + f * (s_sine_lut[i1] - s_sine_lut[i0]);
}

static inline float evaluate_waveform(waveform_t w, float phase) {
    while (phase >= 1.0f) phase -= 1.0f;
    while (phase < 0.0f) phase += 1.0f;
    switch (w) {
        case WAVE_SINE: return lookup_sine(phase);
        case WAVE_TRIANGLE:
            if (phase < 0.25f) return 4.0f * phase;
            if (phase < 0.75f) return 2.0f - 4.0f * phase;
            return 4.0f * phase - 4.0f;
        case WAVE_SAW: return 2.0f * phase - 1.0f;
        case WAVE_SQUARE: return phase < 0.5f ? 1.0f : -1.0f;
        case WAVE_PULSE25: return phase < 0.25f ? 1.0f : -1.0f;
        case WAVE_HALF_SINE: { float x = lookup_sine(phase); return x > 0.0f ? x : 0.0f; }
        case WAVE_ABS_SINE: return fabsf(lookup_sine(phase));
        case WAVE_DOUBLE_SINE: return phase < 0.5f ? lookup_sine(phase * 2.0f) : 0.0f;
        case WAVE_FORMANT: {
            float c = lookup_sine(phase * 3.0f);
            float win = 0.5f - 0.5f * lookup_sine(phase + 0.25f);
            return c * win * 2.0f;
        }
        case WAVE_NOISE: return ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f;
        default: return lookup_sine(phase);
    }
}

static void env_trigger(adsr_state_t *e, const adsr_params_t *p, float velocity) {
    float vel = fmaxf(0.0f, fminf(1.0f, velocity));
    e->stage = ENV_ATTACK; e->current_level = 0.0f;
    e->target_sustain = fmaxf(0.0f, fminf(1.0f, p->sustain_level)) * vel;
    float a = fmaxf(1.0f, p->attack_ms * SYNTH_SAMPLE_RATE / 1000.0f);
    float d = fmaxf(1.0f, p->decay_ms * SYNTH_SAMPLE_RATE / 1000.0f);
    e->attack_step = 1.0f / a;
    e->decay_factor = powf(0.001f, 1.0f / d);
    e->release_factor = 0.001f;
}

static void env_release(adsr_state_t *e, const adsr_params_t *p) {
    if (e->stage == ENV_IDLE) return;
    float r = fmaxf(1.0f, p->release_ms * SYNTH_SAMPLE_RATE / 1000.0f);
    e->stage = ENV_RELEASE;
    e->release_factor = powf(0.001f, 1.0f / r);
}

static inline float env_process(adsr_state_t *e) {
    switch (e->stage) {
        case ENV_IDLE: e->current_level = 0.0f; break;
        case ENV_ATTACK:
            e->current_level += e->attack_step;
            if (e->current_level >= 1.0f) { e->current_level = 1.0f; e->stage = ENV_DECAY; }
            break;
        case ENV_DECAY:
            e->current_level = e->target_sustain + (e->current_level - e->target_sustain) * e->decay_factor;
            if (fabsf(e->current_level - e->target_sustain) < 0.0005f) { e->current_level = e->target_sustain; e->stage = ENV_SUSTAIN; }
            break;
        case ENV_SUSTAIN: e->current_level = e->target_sustain; break;
        case ENV_RELEASE:
            e->current_level *= e->release_factor;
            if (e->current_level < 0.0005f) { e->current_level = 0.0f; e->stage = ENV_IDLE; }
            break;
    }
    return e->current_level;
}

static inline void svf_update_coeffs(synth_voice_t *v, float cutoff, float resonance, filter_type_t type) {
    cutoff = fmaxf(20.0f, fminf(SYNTH_SAMPLE_RATE * 0.45f, cutoff));
    resonance = fmaxf(0.5f, fminf(10.0f, resonance));
    /* tanf() is deliberately outside the sample-rate inner loop. The filter
     * control signal changes slowly compared with the audio waveform, so an
     * 8-sample control-rate update is a good CPU/accuracy tradeoff on S3. */
    v->svf_g = tanf(PI_FLOAT * cutoff / SYNTH_SAMPLE_RATE);
    v->svf_k = 1.0f / resonance;
    v->svf_a1 = 1.0f / (1.0f + v->svf_g * (v->svf_g + v->svf_k));
    v->svf_cutoff_cache = cutoff;
    v->svf_resonance_cache = resonance;
    v->svf_type_cache = (uint8_t)type;
    v->svf_coeff_valid = true;
}

static inline float svf_process(synth_voice_t *v, float input) {
    float g = v->svf_g;
    float k = v->svf_k;
    float a1 = v->svf_a1;
    float v3 = input - v->svf_ic2;
    float v1 = (g * v3 + v->svf_ic1) * a1;
    float v2 = v->svf_ic2 + g * v1;
    v->svf_ic1 = 2.0f * v1 - v->svf_ic1;
    v->svf_ic2 = 2.0f * v2 - v->svf_ic2;
    float low = v2, band = v1, high = input - k * band - low;
    if (v->svf_type_cache == FILTER_HIGHPASS) return high;
    if (v->svf_type_cache == FILTER_BANDPASS) return band;
    return low;
}

static void voice_reset_dsp(synth_voice_t *v) {
    uint32_t generation = v->generation + 1U;
    memset(v, 0, sizeof(*v));
    v->generation = generation;
    v->env[0].stage = v->env[1].stage = v->env[2].stage = ENV_IDLE;
    v->filter_env.stage = ENV_IDLE;
}

static void hard_clear_voices_locked(void) {
    for (int i = 0; i < SYNTH_MAX_VOICES; ++i) voice_reset_dsp(&g_synth.voices[i]);
    g_synth.active_voices = 0;
    s_held_count = 0;
}

void synth_patch_lock(void) { portENTER_CRITICAL(&s_voice_mux); }
void synth_patch_unlock(void) { portEXIT_CRITICAL(&s_voice_mux); }
void synth_scene_reset_locked(void) {
    s_scene_reset_pending = true;
    s_note_event_head = s_note_event_tail = 0;
}

static void set_factory_patch(synth_patch_t *p) {
    memset(p, 0, sizeof(*p));
    strncpy(p->name, "DX E-Piano 80s", sizeof(p->name) - 1);
    p->algorithm = 1; p->master_volume = 0.35f; p->polyphony = 8; p->poly_mode = POLY_MODE_POLY;
    p->unison_voices = 2; p->unison_spread_cents = 8.0f;
    p->op[0].waveform = WAVE_SINE; p->op[0].ratio = 1.0f; p->op[0].carrier_level = 0.70f; p->op[0].pm_index = 0.0f;
    p->op[0].velocity_sens = 0.7f; p->op[0].env = (adsr_params_t){2,1800,0.15f,600};
    p->op[1].waveform = WAVE_SINE; p->op[1].ratio = 14.0f; p->op[1].detune_cents = 3; p->op[1].carrier_level = 0.0f; p->op[1].pm_index = 0.55f;
    p->op[1].velocity_sens = 0.85f; p->op[1].env = (adsr_params_t){2,350,0,250};
    p->op[2].waveform = WAVE_HALF_SINE; p->op[2].ratio = 1.0f; p->op[2].detune_cents = -4; p->op[2].carrier_level = 0.0f; p->op[2].pm_index = 0.35f;
    p->op[2].feedback = 0.0f; p->op[2].velocity_sens = 0.8f; p->op[2].env = (adsr_params_t){2,900,0.05f,450};
    p->filter = (filter_params_t){FILTER_LOWPASS,14000,1.2f,0.25f,{5,1500,0.4f,500}};
    p->lfo1 = (lfo_params_t){WAVE_TRIANGLE,4.5f,0.08f,LFO_DEST_PITCH};
    p->lfo2 = (lfo_params_t){WAVE_SINE,0.8f,0.15f,LFO_DEST_AMP};
}

void synth_engine_init(void) {
    memset(&g_synth, 0, sizeof(g_synth));
    init_sine_lut(); set_factory_patch(&g_synth.patch);
    for (int i = 0; i < SYNTH_MAX_VOICES; ++i) { memset(&g_synth.voices[i], 0, sizeof(g_synth.voices[i])); g_synth.voices[i].env[0].stage = g_synth.voices[i].env[1].stage = g_synth.voices[i].env[2].stage = ENV_IDLE; }
    s_held_count = 0; s_age_counter = 0; s_note_event_head = s_note_event_tail = 0;
    ESP_LOGI(TAG, "DSP Synth Engine V14 initialized");
}

static void sanitize_patch(synth_patch_t *p) {
    if (p->algorithm > SYNTH_ALGORITHM_LAST_DIAGNOSTIC) p->algorithm = 0;
    p->master_volume = fmaxf(0.0f, fminf(1.0f, p->master_volume));
    p->polyphony = p->polyphony ? p->polyphony : 1;
    if (p->polyphony > SYNTH_MAX_VOICES) p->polyphony = SYNTH_MAX_VOICES;
    if (p->poly_mode > POLY_MODE_LEGATO) p->poly_mode = POLY_MODE_POLY;
    p->unison_voices = p->unison_voices ? p->unison_voices : 1;
    if (p->unison_voices > SYNTH_MAX_VOICES) p->unison_voices = SYNTH_MAX_VOICES;
    p->unison_spread_cents = fmaxf(0.0f, fminf(100.0f, p->unison_spread_cents));
    for (int i = 0; i < 3; ++i) {
        p->op[i].ratio = fmaxf(0.25f, fminf(32.0f, p->op[i].ratio));
        p->op[i].detune_cents = fmaxf(-50.0f, fminf(50.0f, p->op[i].detune_cents));
        p->op[i].carrier_level = fmaxf(0.0f, fminf(1.0f, p->op[i].carrier_level));
        p->op[i].pm_index = fmaxf(0.0f, fminf(2.0f, p->op[i].pm_index));
        p->op[i].feedback = fmaxf(0.0f, fminf(1.0f, p->op[i].feedback));
    }
}

void synth_load_patch(const synth_patch_t *patch) {
    if (!patch) return;
    portENTER_CRITICAL(&s_voice_mux);
    synth_scene_reset_locked();
    memcpy(&g_synth.patch, patch, sizeof(g_synth.patch));
    sanitize_patch(&g_synth.patch);
    portEXIT_CRITICAL(&s_voice_mux);
}

static void remove_held_note_locked(uint8_t note) {
    for (uint8_t i = 0; i < s_held_count; ++i) {
        if (s_held_notes[i] == note) {
            memmove(&s_held_notes[i], &s_held_notes[i + 1], (size_t)(s_held_count - i - 1));
            --s_held_count; return;
        }
    }
}


static void push_held_note_locked(uint8_t note) {
    remove_held_note_locked(note);
    if (s_held_count < NOTE_STACK_MAX) s_held_notes[s_held_count++] = note;
    else { memmove(s_held_notes, s_held_notes + 1, NOTE_STACK_MAX - 1); s_held_notes[NOTE_STACK_MAX - 1] = note; }
}

static inline float midi_to_freq(uint8_t note) { return s_note_freq[note & 0x7f]; }

static void env_retrigger(adsr_state_t *e, const adsr_params_t *p, float velocity, bool hard) {
    if (hard) { env_trigger(e, p, velocity); return; }
    float a = fmaxf(1.0f, p->attack_ms * SYNTH_SAMPLE_RATE / 1000.0f);
    e->stage = ENV_ATTACK;
    e->attack_step = fmaxf(0.000001f, (1.0f - e->current_level) / a);
    e->target_sustain = fmaxf(0.0f, fminf(1.0f, p->sustain_level)) * velocity;
    e->decay_factor = powf(0.001f, 1.0f / fmaxf(1.0f, p->decay_ms * SYNTH_SAMPLE_RATE / 1000.0f));
}

static void start_voice_locked(synth_voice_t *v, uint8_t note, uint8_t velocity, const synth_patch_t *p, float detune, bool hard, bool retrigger_env, uint32_t trigger_id) {
    uint32_t gen = v->generation + 1U;
    if (hard) {
        memset(v, 0, sizeof(*v));
        v->generation = gen;
        v->phase[0] = v->phase[1] = v->phase[2] = 0.0f;
        v->fb[0] = v->fb[1] = v->fb[2] = 0.0f;
        v->svf_ic1 = v->svf_ic2 = 0.0f;
        v->svf_coeff_valid = false;
    } else {
        v->generation = gen;
        /* Retargeting a live voice (voice steal/retrigger) keeps oscillator and
         * filter state but fades the replacement in over a short window. */
        v->fade_gain = 0.0f;
        v->fade_step = 1.0f / 32.0f;
    }
    v->active = true; v->key_down = true; v->midi_note = note; v->trigger_id = trigger_id ? trigger_id : ++s_note_trigger_counter; v->velocity = (float)velocity / 127.0f;
    v->target_freq = midi_to_freq(note) * exp2f(detune / 1200.0f * 1.0f);
    if (hard || p->glide_ms <= 0.0f) v->current_freq = v->target_freq;
    v->age = ++s_age_counter;
    if (hard) {
        v->fade_gain = 0.0f;
        v->fade_step = 1.0f / 32.0f;
    }
    for (int i = 0; i < 3; ++i) {
        float vel = 1.0f - fmaxf(0.0f, fminf(1.0f, p->op[i].velocity_sens)) * (1.0f - v->velocity);
        if (retrigger_env) env_retrigger(&v->env[i], &p->op[i].env, vel, hard);
    }
    if (retrigger_env) env_retrigger(&v->filter_env, &p->filter.env, 1.0f, hard);
}

static int oldest_voice_locked(uint8_t limit) {
    int idx = -1; uint32_t age = UINT32_MAX;
    for (int i = 0; i < limit; ++i) if (g_synth.voices[i].active && g_synth.voices[i].age < age) { idx = i; age = g_synth.voices[i].age; }
    return idx;
}

static int free_voice_locked(uint8_t limit) {
    for (int i = 0; i < limit; ++i) if (!g_synth.voices[i].active) return i;
    /* Prefer the quietest release voice, then oldest active. */
    int best = -1; float best_level = 2.0f;
    for (int i = 0; i < limit; ++i) {
        synth_voice_t *v = &g_synth.voices[i];
        bool rel = !v->key_down || v->env[0].stage == ENV_RELEASE || v->env[1].stage == ENV_RELEASE || v->env[2].stage == ENV_RELEASE;
        float level = v->env[0].current_level + v->env[1].current_level + v->env[2].current_level;
        if (rel && level < best_level) { best = i; best_level = level; }
    }
    return best >= 0 ? best : oldest_voice_locked(limit);
}

static void release_voice_locked(synth_voice_t *v) {
    v->key_down = false;
    for (int op = 0; op < 3; ++op) env_release(&v->env[op], &g_synth.patch.op[op].env);
    env_release(&v->filter_env, &g_synth.patch.filter.env);
}

static void synth_note_on_locked(uint8_t note, uint8_t velocity) {
    poly_mode_t mode = (poly_mode_t)g_synth.patch.poly_mode;
    if (velocity == 0) { /* MIDI convention */
        remove_held_note_locked(note);
        if (g_synth.patch.poly_mode == POLY_MODE_MONO || g_synth.patch.poly_mode == POLY_MODE_LEGATO) {
            if (s_held_count) {
                uint8_t top = s_held_notes[s_held_count - 1];
                synth_voice_t *v = &g_synth.voices[0];
                if (mode == POLY_MODE_LEGATO) {
                    v->midi_note = top; v->target_freq = midi_to_freq(top); ++v->generation;
                } else {
                    start_voice_locked(v, top, (uint8_t)(v->velocity * 127.0f), &g_synth.patch, 0.0f, true, true, 0);
                }
            } else if (g_synth.voices[0].active) release_voice_locked(&g_synth.voices[0]);
        } else {
            int off_idx = -1;
    uint32_t oldest_id = UINT32_MAX;
    for (int i = 0; i < SYNTH_MAX_VOICES; ++i) {
        if (g_synth.voices[i].active && g_synth.voices[i].key_down &&
            g_synth.voices[i].midi_note == note &&
            g_synth.voices[i].trigger_id < oldest_id) {
            oldest_id = g_synth.voices[i].trigger_id;
            off_idx = i;
        }
    }
    if (off_idx >= 0) {
        uint32_t id = g_synth.voices[off_idx].trigger_id;
        /* In POLY this releases one voice. In UNISON all members of the note
         * group share an id, so the whole group releases atomically. */
        for (int i = 0; i < SYNTH_MAX_VOICES; ++i)
            if (g_synth.voices[i].active && g_synth.voices[i].key_down &&
                g_synth.voices[i].trigger_id == id)
                release_voice_locked(&g_synth.voices[i]);
    }
        }
        return;
    }
    if (mode == POLY_MODE_MONO || mode == POLY_MODE_LEGATO) {
        bool had_note = s_held_count != 0;
        push_held_note_locked(note);
        synth_voice_t *v = &g_synth.voices[0];
        /* Only an actually idle mono/legato voice gets a hard start.
         * Retriggering an already sounding voice must preserve oscillator/filter
         * state; resetting phase at a DMA-block boundary creates an audible click. */
        bool hard = !had_note || !v->active;
        start_voice_locked(v, note, velocity, &g_synth.patch, 0.0f, hard, true, 0);
        return;
    }
    if (mode == POLY_MODE_UNISON) {
        uint8_t count = g_synth.patch.unison_voices;
        if (count < 1) count = 1;
        if (count > SYNTH_MAX_VOICES) count = SYNTH_MAX_VOICES;
        uint8_t slots = g_synth.patch.polyphony;
        if (slots < 1) slots = 1;
        if (slots > SYNTH_MAX_VOICES / count) slots = SYNTH_MAX_VOICES / count;
        if (slots < 1) slots = 1;
        /* Repeated notes are valid independent note groups in UNISON too.
         * Group identity is trigger_id, never MIDI pitch. */

        /* UNISON semantics: polyphony is the number of note groups, not the
         * number of individual oscillators. Never allocate beyond slots*count. */
        int max_used_voices = slots * count;
        uint8_t used_groups = 0;
        for (int i = 0; i < SYNTH_MAX_VOICES; ++i) {
            if (g_synth.voices[i].active) {
                bool seen = false;
                for (int j = 0; j < i; ++j)
                    if (g_synth.voices[j].active &&
                        g_synth.voices[j].trigger_id == g_synth.voices[i].trigger_id) { seen = true; break; }
                if (!seen) ++used_groups;
            }
        }
        float center = ((float)count - 1.0f) * 0.5f;
        if (used_groups >= slots) {
            int steal = oldest_voice_locked((uint8_t)max_used_voices);
            if (steal < 0) return;
            uint32_t old_group = g_synth.voices[steal].trigger_id;
            uint32_t new_group = ++s_note_trigger_counter;
            /* Retarget the complete old group, identified by instance id rather
             * than pitch so repeated equal notes cannot merge groups. */
            uint8_t started = 0;
            for (int i = 0; i < SYNTH_MAX_VOICES && started < count; ++i) {
                if (g_synth.voices[i].active && g_synth.voices[i].trigger_id == old_group) {
                    float cents = ((float)started - center) * g_synth.patch.unison_spread_cents;
                    start_voice_locked(&g_synth.voices[i], note, velocity, &g_synth.patch, cents, false, true, new_group);
                    ++started;
                }
            }
            return;
        }

        /* New group: allocate exactly count free voices. */
        uint8_t started = 0;
        uint32_t new_group = ++s_note_trigger_counter;
        for (int i = 0; i < max_used_voices && started < count; ++i) {
            if (!g_synth.voices[i].active) {
                float cents = ((float)started - center) * g_synth.patch.unison_spread_cents;
                start_voice_locked(&g_synth.voices[i], note, velocity, &g_synth.patch, cents, true, true, new_group);
                ++started;
            }
        }
        return;
    }
    uint8_t limit = g_synth.patch.polyphony; if (limit < 1) limit = 1; if (limit > SYNTH_MAX_VOICES) limit = SYNTH_MAX_VOICES;
    /* POLY means one voice allocation per Note On. Do not search for an
     * already-playing voice with the same pitch: repeated keys are independent
     * note instances, just as in established polyphonic voice allocators. */
    int idx = free_voice_locked(limit);
    if (idx >= 0) {
        bool hard = !g_synth.voices[idx].active;
        start_voice_locked(&g_synth.voices[idx], note, velocity, &g_synth.patch,
                           0.0f, hard, true, 0);
    }
}

static void synth_note_off_locked(uint8_t note) {
    remove_held_note_locked(note);
    poly_mode_t mode = (poly_mode_t)g_synth.patch.poly_mode;
    if (mode == POLY_MODE_MONO || mode == POLY_MODE_LEGATO) {
        synth_voice_t *v = &g_synth.voices[0];
        if (s_held_count) {
            uint8_t top = s_held_notes[s_held_count - 1];
            if (mode == POLY_MODE_LEGATO) {
                v->midi_note = top; v->target_freq = midi_to_freq(top); ++v->generation;
            } else {
                start_voice_locked(v, top, (uint8_t)(v->velocity * 127.0f), &g_synth.patch, 0.0f, true, true, 0);
            }
        } else if (v->active) release_voice_locked(v);
        return;
    }
    int off_idx = -1;
    uint32_t oldest_id = UINT32_MAX;
    for (int i = 0; i < SYNTH_MAX_VOICES; ++i) {
        if (g_synth.voices[i].active && g_synth.voices[i].key_down &&
            g_synth.voices[i].midi_note == note &&
            g_synth.voices[i].trigger_id < oldest_id) {
            oldest_id = g_synth.voices[i].trigger_id;
            off_idx = i;
        }
    }
    if (off_idx >= 0) {
        uint32_t id = g_synth.voices[off_idx].trigger_id;
        /* In POLY this releases one voice. In UNISON all members of the note
         * group share an id, so the whole group releases atomically. */
        for (int i = 0; i < SYNTH_MAX_VOICES; ++i)
            if (g_synth.voices[i].active && g_synth.voices[i].key_down &&
                g_synth.voices[i].trigger_id == id)
                release_voice_locked(&g_synth.voices[i]);
    }
}

static bool queue_note_event_locked(note_event_type_t type, uint8_t note, uint8_t velocity) {
    uint8_t next = (uint8_t)((s_note_event_head + 1U) % NOTE_EVENT_QUEUE_SIZE);
    if (next == s_note_event_tail) {
        /* MIDI bursts can be large, but losing a note-off is worse than
         * dropping an old queued event. Keep the newest event and report it. */
        if (type == NOTE_EVENT_OFF || type == NOTE_EVENT_ALL_OFF) {
            s_note_event_tail = (uint8_t)((s_note_event_tail + 1U) % NOTE_EVENT_QUEUE_SIZE);
        } else {
            return false;
        }
    }
    s_note_events[s_note_event_head] = (note_event_t){ type, note, velocity };
    s_note_event_head = next;
    return true;
}

static void process_note_events_locked(void) {
    while (s_note_event_tail != s_note_event_head) {
        note_event_t ev = s_note_events[s_note_event_tail];
        s_note_event_tail = (uint8_t)((s_note_event_tail + 1U) % NOTE_EVENT_QUEUE_SIZE);
        switch (ev.type) {
            case NOTE_EVENT_ON: synth_note_on_locked(ev.note, ev.velocity); break;
            case NOTE_EVENT_OFF: synth_note_off_locked(ev.note); break;
            case NOTE_EVENT_ALL_OFF: hard_clear_voices_locked(); break;
        }
    }
}

void synth_note_on(uint8_t note, uint8_t velocity) {
    portENTER_CRITICAL(&s_voice_mux);
    (void)queue_note_event_locked(NOTE_EVENT_ON, note, velocity);
    portEXIT_CRITICAL(&s_voice_mux);
}
void synth_note_off(uint8_t note) {
    portENTER_CRITICAL(&s_voice_mux);
    (void)queue_note_event_locked(NOTE_EVENT_OFF, note, 0);
    portEXIT_CRITICAL(&s_voice_mux);
}
void synth_all_notes_off(void) {
    portENTER_CRITICAL(&s_voice_mux);
    (void)queue_note_event_locked(NOTE_EVENT_ALL_OFF, 0, 0);
    portEXIT_CRITICAL(&s_voice_mux);
}
void synth_set_pitch_bend(int16_t raw) { portENTER_CRITICAL(&s_voice_mux); int32_t d = (int32_t)raw - 8192; g_synth.pitch_bend = ((float)d / 8192.0f) * 2.0f; portEXIT_CRITICAL(&s_voice_mux); }
void synth_set_mod_wheel(uint8_t val) { portENTER_CRITICAL(&s_voice_mux); g_synth.mod_wheel = (float)val / 127.0f; portEXIT_CRITICAL(&s_voice_mux); }
void synth_set_cc(uint8_t cc, uint8_t val) {
    portENTER_CRITICAL(&s_voice_mux); float n = (float)val / 127.0f;
    switch (cc) { case 1: g_synth.mod_wheel = n; break; case 7: g_synth.patch.master_volume = n; break; case 74: g_synth.patch.filter.cutoff_hz = 20.0f + n*n*18000.0f; break; case 71: g_synth.patch.filter.resonance = 0.5f + n*8.0f; break; case 123: (void)queue_note_event_locked(NOTE_EVENT_ALL_OFF, 0, 0); break; default: break; }
    portEXIT_CRITICAL(&s_voice_mux);
}

static void init_i2s_pcm5102a(void) {
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    cc.dma_desc_num = 6; cc.dma_frame_num = SYNTH_DMA_BUFFER_SIZE;
    ESP_ERROR_CHECK(i2s_new_channel(&cc, &s_tx_chan, NULL));
    i2s_std_config_t cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SYNTH_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {.mclk = I2S_GPIO_UNUSED, .bclk = I2S_BCK_IO, .ws = I2S_WS_IO, .dout = I2S_DATA_OUT_IO, .din = I2S_DATA_IN_IO,
                     .invert_flags = {.mclk_inv=false,.bclk_inv=false,.ws_inv=false}}
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_tx_chan, &cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(s_tx_chan));
    ESP_LOGI(TAG, "PCM5102A I2S BCK=%d WS=%d DATA=%d", I2S_BCK_IO, I2S_WS_IO, I2S_DATA_OUT_IO);
}

static inline float clamp_audio(float x) { if (!isfinite(x)) return 0.0f; return fmaxf(-VOICE_POST_FILTER_PEAK, fminf(VOICE_POST_FILTER_PEAK, x)); }

static float render_production(uint8_t alg, synth_voice_t *v, const synth_patch_t *p, float e1, float e2, float e3, const float ks[3], float lfo_fm) {
    float o1, o2, o3, pm2 = fmaxf(0.0f, fminf(2.0f, p->op[1].pm_index * (1.0f + lfo_fm)));
    float pm3 = fmaxf(0.0f, fminf(2.0f, p->op[2].pm_index * (1.0f + lfo_fm)));
    float c1 = p->op[0].carrier_level * ks[0] * e1;
    float c2 = p->op[1].carrier_level * ks[1] * e2;
    float c3 = p->op[2].carrier_level * ks[2] * e3;
    switch (alg) {
        case 0: /* 3 -> 2 -> 1 */
            o3 = evaluate_waveform(p->op[2].waveform, v->phase[2]) * e3 * ks[2];
            /* Plain cascade: feedback is reserved for algorithms 5..7. */
            o2 = evaluate_waveform(p->op[1].waveform, v->phase[1] + o3 * pm3) * e2 * ks[1];
            o1 = evaluate_waveform(p->op[0].waveform, v->phase[0] + o2 * pm2) * c1;
            return o1;
        case 1: /* (2 + 3) -> 1 */
            o2 = evaluate_waveform(p->op[1].waveform, v->phase[1]) * e2 * ks[1];
            o3 = evaluate_waveform(p->op[2].waveform, v->phase[2]) * e3 * ks[2];
            o1 = evaluate_waveform(p->op[0].waveform, v->phase[0] + o2*pm2 + o3*pm3) * c1;
            return o1;
        case 2: /* 3 -> (1 + 2) */
            o3 = evaluate_waveform(p->op[2].waveform, v->phase[2]) * e3 * ks[2];
            o1 = evaluate_waveform(p->op[0].waveform, v->phase[0] + o3*pm3) * c1;
            o2 = evaluate_waveform(p->op[1].waveform, v->phase[1] + o3*pm3) * c2;
            return 0.5f * (o1 + o2);
        case 3: /* (3 -> 2) + 1 */
            o3 = evaluate_waveform(p->op[2].waveform, v->phase[2]) * e3 * ks[2];
            o2 = evaluate_waveform(p->op[1].waveform, v->phase[1] + o3*pm3) * c2;
            o1 = evaluate_waveform(p->op[0].waveform, v->phase[0]) * c1;
            return 0.5f * (o1 + o2);
        case 4: /* 1 + 2 + 3 */
            o1 = evaluate_waveform(p->op[0].waveform, v->phase[0]) * c1;
            o2 = evaluate_waveform(p->op[1].waveform, v->phase[1]) * c2;
            o3 = evaluate_waveform(p->op[2].waveform, v->phase[2]) * c3;
            return (o1 + o2 + o3) * 0.3333333f;
        case 5: /* (3*) -> 2 -> 1 */
            o3 = evaluate_waveform(p->op[2].waveform, v->phase[2] + v->fb[2] * p->op[2].feedback * 0.25f) * e3 * ks[2];
            v->fb[2] = o3;
            o2 = evaluate_waveform(p->op[1].waveform, v->phase[1] + o3*pm3) * e2 * ks[1];
            o1 = evaluate_waveform(p->op[0].waveform, v->phase[0] + o2*pm2) * c1;
            return o1;
        case 6: /* 3 -> (2*) -> 1 */
            o3 = evaluate_waveform(p->op[2].waveform, v->phase[2]) * e3 * ks[2];
            o2 = evaluate_waveform(p->op[1].waveform, v->phase[1] + o3*pm3 + v->fb[1]*p->op[1].feedback*0.25f) * e2 * ks[1];
            v->fb[1] = o2;
            o1 = evaluate_waveform(p->op[0].waveform, v->phase[0] + o2*pm2) * c1;
            return o1;
        case 7: /* 3 -> 2 -> (1*) */
        default:
            o3 = evaluate_waveform(p->op[2].waveform, v->phase[2]) * e3 * ks[2];
            o2 = evaluate_waveform(p->op[1].waveform, v->phase[1] + o3*pm3) * e2 * ks[1];
            o1 = evaluate_waveform(p->op[0].waveform, v->phase[0] + o2*pm2 + v->fb[0]*p->op[0].feedback*0.25f) * c1;
            v->fb[0] = o1;
            return o1;
    }
}

static float render_diagnostic(uint8_t alg, synth_voice_t *v, const synth_patch_t *p, float e1, float e2, float e3, const float ks[3], float fm_mod) {
    float o1, o2, o3;
    switch (alg) {
        case 8: return evaluate_waveform(WAVE_SINE, v->phase[0]) * p->op[0].carrier_level * ks[0] * e1;
        case 9: return evaluate_waveform(WAVE_SINE, v->phase[0]) * p->op[0].carrier_level * ks[0] * e1;
        case 10: /* AMP-LFO is applied once in the global mixer. */
            return evaluate_waveform(WAVE_SINE, v->phase[0]) * p->op[0].carrier_level * ks[0] * e1;
        case 11: return evaluate_waveform(WAVE_SINE, v->phase[0] + fm_mod*0.05f) * p->op[0].carrier_level * ks[0] * e1;
        case 12:
            o1=evaluate_waveform(WAVE_SINE,v->phase[0])*p->op[0].carrier_level*ks[0]*e1;
            o2=evaluate_waveform(WAVE_SINE,v->phase[1])*p->op[1].carrier_level*ks[1]*e2; return 0.5f*(o1+o2);
        case 13:
            o1=evaluate_waveform(WAVE_SINE,v->phase[0])*p->op[0].carrier_level*ks[0]*e1;
            o2=evaluate_waveform(WAVE_SINE,v->phase[1])*p->op[1].carrier_level*ks[1]*e2;
            o3=evaluate_waveform(WAVE_SINE,v->phase[2])*p->op[2].carrier_level*ks[2]*e3; return (o1+o2+o3)/3.0f;
        case 14:
            o2=evaluate_waveform(WAVE_SINE,v->phase[1])*e2*ks[1]; return evaluate_waveform(WAVE_SINE,v->phase[0]+o2*p->op[1].pm_index)*p->op[0].carrier_level*ks[0]*e1;
        case 15:
            o3=evaluate_waveform(WAVE_SINE,v->phase[2])*e3*ks[2]; return evaluate_waveform(WAVE_SINE,v->phase[0]+o3*p->op[2].pm_index)*p->op[0].carrier_level*ks[0]*e1;
        case 16:
            o3=evaluate_waveform(WAVE_SINE,v->phase[2])*e3*ks[2]; o2=evaluate_waveform(WAVE_SINE,v->phase[1]+o3*p->op[2].pm_index)*e2*ks[1]; return evaluate_waveform(WAVE_SINE,v->phase[0]+o2*p->op[1].pm_index)*p->op[0].carrier_level*ks[0]*e1;
        case 17:
            o3=evaluate_waveform(WAVE_SINE,v->phase[2])*e3*ks[2]; o2=evaluate_waveform(WAVE_SINE,v->phase[1]+o3*p->op[2].pm_index+v->fb[1]*p->op[1].feedback*0.25f)*e2*ks[1]; v->fb[1]=o2; return evaluate_waveform(WAVE_SINE,v->phase[0]+o2*p->op[1].pm_index)*p->op[0].carrier_level*ks[0]*e1;
        case 18: {
            float pm = fmaxf(0.0f, fminf(2.0f, p->op[1].pm_index * (1.0f + fm_mod)));
            o2=evaluate_waveform(WAVE_SINE,v->phase[1])*e2*ks[1];
            o1=evaluate_waveform(WAVE_SINE,v->phase[0]+o2*pm)*p->op[0].carrier_level*ks[0]*e1;
            return o1;
        }
        case 19:
            /* Polyphony reference: exactly one sine carrier per voice. */
            return evaluate_waveform(WAVE_SINE, v->phase[0]) * p->op[0].carrier_level * e1;
        default: return 0.0f;
    }
}

void synth_audio_task(void *pvParameters) {
    (void)pvParameters; init_i2s_pcm5102a();
    int16_t dma[SYNTH_DMA_BUFFER_SIZE*2]; size_t written=0; const float sr=(float)SYNTH_SAMPLE_RATE;
    while (1) {
        /* Apply MIDI events only at an audio-buffer boundary. External tasks
         * never mutate voice DSP state while this task is rendering. This is
         * essential: otherwise the generation check discards a partially
         * rendered voice and the next DMA block jumps back in oscillator phase,
         * producing a click. */
        synth_patch_t p; float pitch, mod, l1, l2;
        portENTER_CRITICAL(&s_voice_mux);
        if (s_scene_reset_pending) {
            hard_clear_voices_locked();
            s_scene_reset_pending = false;
        }
        process_note_events_locked();
        memcpy(&p, &g_synth.patch, sizeof(p));
        pitch = g_synth.pitch_bend; mod = g_synth.mod_wheel;
        l1 = g_synth.lfo1_phase; l2 = g_synth.lfo2_phase;
        portEXIT_CRITICAL(&s_voice_mux);
        sanitize_patch(&p);
        float dm[3]; for(int i=0;i<3;++i) dm[i]=powf(2.0f,p.op[i].detune_cents/1200.0f);
        float l1i=fmaxf(0.0f,p.lfo1.rate_hz)/sr, l2i=fmaxf(0.0f,p.lfo2.rate_hz)/sr;
        float bend=powf(2.0f,pitch/12.0f);
        uint8_t maxvoices;
        if (p.poly_mode == POLY_MODE_UNISON) {
            uint8_t uv = p.unison_voices ? p.unison_voices : 1;
            uint8_t slots = p.polyphony ? p.polyphony : 1;
            uint16_t total = (uint16_t)uv * (uint16_t)slots;
            maxvoices = (uint8_t)(total > SYNTH_MAX_VOICES ? SYNTH_MAX_VOICES : total);
        } else {
            maxvoices = p.polyphony > SYNTH_MAX_VOICES ? SYNTH_MAX_VOICES : p.polyphony;
        }
        if(maxvoices<1)maxvoices=1;
        for(int s=0;s<SYNTH_DMA_BUFFER_SIZE;++s){
            float a1=evaluate_waveform(p.lfo1.waveform,l1),a2=evaluate_waveform(p.lfo2.waveform,l2);
            l1+=l1i;l2+=l2i;if(l1>=1)l1-=1.0f;if(l2>=1)l2-=1.0f;
            float pitch_mod=bend, cutoff_mod=0, fm_mod=0, amp_mod=0;
            float d1=fmaxf(0.0f,fminf(1.0f,fmaxf(p.lfo1.depth,mod*0.8f))),d2=fmaxf(0.0f,fminf(1.0f,p.lfo2.depth));
            if(p.lfo1.destination==LFO_DEST_PITCH && d1>0.000001f)pitch_mod*=powf(2,a1*d1*1.5f/12.0f); else if(p.lfo1.destination==LFO_DEST_CUTOFF)cutoff_mod+=a1*d1; else if(p.lfo1.destination==LFO_DEST_FM_INDEX)fm_mod+=a1*d1; else amp_mod+=a1*d1*0.5f;
            if(p.lfo2.destination==LFO_DEST_PITCH && d2>0.000001f)pitch_mod*=powf(2,a2*d2*1.5f/12.0f); else if(p.lfo2.destination==LFO_DEST_CUTOFF)cutoff_mod+=a2*d2; else if(p.lfo2.destination==LFO_DEST_FM_INDEX)fm_mod+=a2*d2; else amp_mod+=a2*d2*0.5f;
            float mix=0.0f;
            for(int vi=0;vi<maxvoices;++vi){
                synth_voice_t *v=&g_synth.voices[vi]; if(!v->active)continue;
                if(p.glide_ms>0 && v->current_freq!=v->target_freq){float step=4.0f/fmaxf(1.0f,p.glide_ms*sr/1000.0f);v->current_freq+=(v->target_freq-v->current_freq)*fminf(1.0f,step);}else v->current_freq=v->target_freq;
                float e[3]={env_process(&v->env[0]),env_process(&v->env[1]),env_process(&v->env[2])}; float fe=env_process(&v->filter_env);
                if(v->env[0].stage==ENV_IDLE&&v->env[1].stage==ENV_IDLE&&v->env[2].stage==ENV_IDLE){
                    v->active=false;
                    v->fade_gain=0.0f;
                    continue;
                }
                float base=v->current_freq*pitch_mod; float inc[3];
                for(int op=0;op<3;++op){float hz=base*p.op[op].ratio*dm[op];inc[op]=fmaxf(0,fminf(MAX_OPERATOR_INCREMENT,hz/sr));}
                float ks[3]; float kp=((float)v->midi_note-60.0f)/60.0f; for(int op=0;op<3;++op)ks[op]=fmaxf(0.25f,fminf(1.75f,1.0f+p.op[op].key_scaling*kp));
                float out=(p.algorithm<8)?render_production(p.algorithm,v,&p,e[0],e[1],e[2],ks,fm_mod):render_diagnostic(p.algorithm,v,&p,e[0],e[1],e[2],ks,fm_mod);
                v->phase[0]+=inc[0];v->phase[1]+=inc[1];v->phase[2]+=inc[2];for(int op=0;op<3;++op)if(v->phase[op]>=1)v->phase[op]-=1.0f;
                /* Compare the same clamped value that svf_update_coeffs()
                 * caches. Otherwise a legal 20 kHz cutoff is compared against
                 * the 0.45*Fs clamp (19.845 kHz) and tanf() runs every sample.
                 * Refreshing at an 8-sample control cadence bounds the cost
                 * when the filter envelope/LFO is moving quickly. */
                float cutoff=p.filter.cutoff_hz*(1.0f+p.filter.env_amount*fe+cutoff_mod);
                cutoff=fmaxf(20.0f,fminf(SYNTH_SAMPLE_RATE*0.45f,cutoff));
                /* Pure/additive diagnostics are intended to measure oscillator
                 * and voice summing, not filter gain. Keep the filter out of
                 * these reference paths. */
                bool bypass_filter = (p.algorithm == 8 || p.algorithm == 12 || p.algorithm == 13 || p.algorithm == 19);
                if (!bypass_filter && (s == 0 || (s & 7) == 0) && (!v->svf_coeff_valid ||
                    fabsf(cutoff - v->svf_cutoff_cache) > 1.0f ||
                    fabsf(p.filter.resonance - v->svf_resonance_cache) > 0.001f ||
                    v->svf_type_cache != (uint8_t)p.filter.type)) {
                    svf_update_coeffs(v, cutoff, p.filter.resonance, p.filter.type);
                }
                float filtered=bypass_filter ? out : svf_process(v,out);
                if (v->fade_gain < 1.0f) {
                    v->fade_gain = fminf(1.0f, v->fade_gain + v->fade_step);
                }
                float voice_sample = clamp_audio(filtered) * v->fade_gain;
                /* Do not normalize by instantaneous envelope energy. That
                 * would divide out the ADSR attack/release and can turn a
                 * nominally zero-amplitude note-on into an immediate full-level
                 * step (audible click). Count only voices that actually produce
                 * a non-negligible signal and use a bounded 1/sqrt(N) headroom. */
                mix += voice_sample;
                if(!isfinite(v->phase[0])||!isfinite(v->phase[1])||!isfinite(v->phase[2])||!isfinite(v->fb[0])||!isfinite(v->fb[1])||!isfinite(v->fb[2])){v->phase[0]=v->phase[1]=v->phase[2]=0;v->fb[0]=v->fb[1]=v->fb[2]=0;}
            }
            /* Fixed gain based on the configured maximum simultaneous voices.
             * Never change the gain of already sounding voices when another
             * voice crosses an envelope threshold. */
            uint8_t mix_voices = maxvoices;
            if (mix_voices < 1) mix_voices = 1;
            mix *= POLY_MIX_HEADROOM / sqrtf((float)mix_voices);
            mix*=fmaxf(0,1+amp_mod)*p.master_volume;
            float sample=fmaxf(-FINAL_SAFETY_PEAK,fminf(FINAL_SAFETY_PEAK,mix));
            int16_t s16=(int16_t)(sample*32767.0f); dma[s*2]=s16;dma[s*2+1]=s16;
            g_synth.scope_buffer[s%128]=sample;
        }
        portENTER_CRITICAL(&s_voice_mux);
        g_synth.lfo1_phase=l1; g_synth.lfo2_phase=l2;
        g_synth.active_voices=0;
        for(int i=0;i<SYNTH_MAX_VOICES;++i) if(g_synth.voices[i].active) ++g_synth.active_voices;
        portEXIT_CRITICAL(&s_voice_mux);
        ESP_ERROR_CHECK(i2s_channel_write(s_tx_chan,dma,sizeof(dma),&written,portMAX_DELAY));
    }
}

#ifdef SYNTH_HOST_TEST
void synth_test_process_events(void) {
    portENTER_CRITICAL(&s_voice_mux);
    if (s_scene_reset_pending) {
        hard_clear_voices_locked();
        s_scene_reset_pending = false;
    }
    process_note_events_locked();
    portEXIT_CRITICAL(&s_voice_mux);
}
#endif
