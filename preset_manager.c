#include "preset_manager.h"
#include "synth_engine.h"
#include <string.h>
#include <math.h>

static synth_patch_t s_presets[PRESET_COUNT];
static uint8_t s_current = 0;

static void set_name(synth_patch_t *p, const char *name)
{
    memset(p->name, 0, sizeof(p->name));
    strncpy(p->name, name, sizeof(p->name) - 1);
}

void preset_manager_init(void)
{
    /* Start from the same factory patch used by the synth engine. */
    for (int i = 0; i < PRESET_COUNT; ++i) {
        s_presets[i] = g_synth.patch;
    }

    set_name(&s_presets[0], "DX E-Piano 80s");

    set_name(&s_presets[1], "Soft Bell");
    s_presets[1].op[1].ratio = 7.0f;
    s_presets[1].op[1].env.decay_ms = 900.0f;
    s_presets[1].op[2].ratio = 2.0f;

    set_name(&s_presets[2], "Bright Bell");
    s_presets[2].op[1].ratio = 14.0f;
    s_presets[2].op[1].carrier_level = 0.9f;
    s_presets[2].op[1].env.decay_ms = 500.0f;
    s_presets[2].filter.cutoff_hz = 11000.0f;

    set_name(&s_presets[3], "FM Bass");
    s_presets[3].algorithm = 0;
    s_presets[3].op[0].ratio = 1.0f;
    s_presets[3].op[0].carrier_level = 1.0f;
    s_presets[3].op[1].ratio = 2.0f;
    s_presets[3].op[1].carrier_level = 0.55f;
    s_presets[3].op[2].ratio = 1.0f;
    s_presets[3].filter.cutoff_hz = 3500.0f;
    s_presets[3].filter.env_amount = 0.65f;

    set_name(&s_presets[4], "FM Pluck");
    s_presets[4].algorithm = 0;
    for (int i = 0; i < 3; ++i) {
        s_presets[4].op[i].env.attack_ms = 1.0f;
        s_presets[4].op[i].env.sustain_level = 0.0f;
    }
    s_presets[4].op[0].env.decay_ms = 420.0f;
    s_presets[4].op[1].env.decay_ms = 220.0f;
    s_presets[4].op[2].env.decay_ms = 160.0f;
    s_presets[4].op[1].ratio = 3.0f;

    set_name(&s_presets[5], "Organ");
    s_presets[5].algorithm = 4;
    s_presets[5].op[0].ratio = 1.0f;
    s_presets[5].op[1].ratio = 2.0f;
    s_presets[5].op[2].ratio = 3.0f;
    s_presets[5].op[0].carrier_level = 0.8f;
    s_presets[5].op[1].carrier_level = 0.45f;
    s_presets[5].op[2].carrier_level = 0.25f;
    s_presets[5].op[0].env.sustain_level = 1.0f;
    s_presets[5].op[1].env.sustain_level = 0.7f;
    s_presets[5].op[2].env.sustain_level = 0.5f;
    s_presets[5].op[0].env.decay_ms = 80.0f;
    s_presets[5].op[1].env.decay_ms = 100.0f;
    s_presets[5].op[2].env.decay_ms = 120.0f;

    set_name(&s_presets[6], "Warm Pad");
    s_presets[6].op[0].env.attack_ms = 500.0f;
    s_presets[6].op[0].env.release_ms = 1800.0f;
    s_presets[6].op[1].env.attack_ms = 700.0f;
    s_presets[6].op[1].env.release_ms = 2000.0f;
    s_presets[6].op[2].env.attack_ms = 900.0f;
    s_presets[6].op[2].env.release_ms = 2200.0f;
    s_presets[6].lfo1.rate_hz = 0.35f;
    s_presets[6].lfo1.depth = 0.025f;
    s_presets[6].lfo1.destination = LFO_DEST_PITCH;

    set_name(&s_presets[7], "Brass");
    s_presets[7].algorithm = 0;
    s_presets[7].op[1].ratio = 1.0f;
    s_presets[7].op[1].carrier_level = 0.45f;
    s_presets[7].op[2].ratio = 2.0f;
    s_presets[7].op[2].carrier_level = 0.35f;
    s_presets[7].op[0].env.attack_ms = 80.0f;
    s_presets[7].op[1].env.attack_ms = 120.0f;
    s_presets[7].op[2].env.attack_ms = 160.0f;
    s_presets[7].filter.cutoff_hz = 6500.0f;

    set_name(&s_presets[8], "Sync Lead");
    s_presets[8].algorithm = 5;
    s_presets[8].op[1].feedback = 0.0f;
    s_presets[8].op[2].feedback = 0.55f;
    s_presets[8].op[1].ratio = 2.0f;
    s_presets[8].glide_ms = 80.0f;
    s_presets[8].lfo1.rate_hz = 5.5f;
    s_presets[8].lfo1.depth = 0.025f;

    set_name(&s_presets[9], "Hollow FM");
    s_presets[9].algorithm = 2;
    s_presets[9].op[2].ratio = 0.5f;
    s_presets[9].op[2].carrier_level = 0.8f;
    s_presets[9].filter.cutoff_hz = 5000.0f;

    set_name(&s_presets[10], "Metallic");
    s_presets[10].algorithm = 6;
    s_presets[10].op[1].feedback = 0.7f;
    s_presets[10].op[1].ratio = 7.0f;
    s_presets[10].op[2].ratio = 5.0f;
    s_presets[10].filter.cutoff_hz = 9000.0f;

    set_name(&s_presets[11], "Pulse Lead");
    s_presets[11].op[0].waveform = WAVE_PULSE25;
    s_presets[11].op[0].env.attack_ms = 5.0f;
    s_presets[11].op[0].env.sustain_level = 0.8f;
    s_presets[11].glide_ms = 50.0f;

    set_name(&s_presets[12], "Noise Perc");
    s_presets[12].algorithm = 4;
    s_presets[12].op[0].waveform = WAVE_NOISE;
    s_presets[12].op[0].env.attack_ms = 1.0f;
    s_presets[12].op[0].env.decay_ms = 120.0f;
    s_presets[12].op[0].env.sustain_level = 0.0f;
    s_presets[12].op[1].carrier_level = 0.0f;
    s_presets[12].op[2].carrier_level = 0.0f;
    s_presets[12].filter.cutoff_hz = 7000.0f;

    set_name(&s_presets[13], "Filter Sweep");
    s_presets[13].filter.cutoff_hz = 1200.0f;
    s_presets[13].filter.env_amount = 1.0f;
    s_presets[13].filter.env.attack_ms = 20.0f;
    s_presets[13].filter.env.decay_ms = 1800.0f;
    s_presets[13].filter.env.sustain_level = 0.2f;

    set_name(&s_presets[14], "Tremolo Pad");
    s_presets[14].op[0].env.attack_ms = 250.0f;
    s_presets[14].op[0].env.release_ms = 1200.0f;
    s_presets[14].lfo2.waveform = WAVE_SINE;
    s_presets[14].lfo2.rate_hz = 5.0f;
    s_presets[14].lfo2.depth = 0.45f;
    s_presets[14].lfo2.destination = LFO_DEST_AMP;

    set_name(&s_presets[15], "Vibrato Lead");
    s_presets[15].glide_ms = 45.0f;
    s_presets[15].lfo1.rate_hz = 5.8f;
    s_presets[15].lfo1.depth = 0.08f;
    s_presets[15].lfo1.destination = LFO_DEST_PITCH;

    // Diagnostic preset: one pure sine oscillator, no operator summing/PM.
    set_name(&s_presets[16], "PURE SINE TEST");
    s_presets[16].algorithm = 8;
    s_presets[16].master_volume = 0.35f;
    s_presets[16].polyphony = 1;
    s_presets[16].glide_ms = 0.0f;
    for (int i = 0; i < 3; ++i) {
        s_presets[16].op[i].waveform = WAVE_SINE;
        s_presets[16].op[i].ratio = 1.0f;
        s_presets[16].op[i].detune_cents = 0.0f;
        s_presets[16].op[i].carrier_level = 0.0f;
        s_presets[16].op[i].feedback = 0.0f;
        s_presets[16].op[i].velocity_sens = 0.0f;
        s_presets[16].op[i].key_scaling = 0.0f;
        s_presets[16].op[i].env.attack_ms = 5.0f;
        s_presets[16].op[i].env.decay_ms = 50.0f;
        s_presets[16].op[i].env.sustain_level = 1.0f;
        s_presets[16].op[i].env.release_ms = 100.0f;
    }
    s_presets[16].op[0].carrier_level = 0.65f;
    s_presets[16].filter.type = FILTER_LOWPASS;
    s_presets[16].filter.cutoff_hz = 20000.0f;
    s_presets[16].filter.resonance = 0.5f;
    s_presets[16].filter.env_amount = 0.0f;
    s_presets[16].filter.env.attack_ms = 1.0f;
    s_presets[16].filter.env.decay_ms = 1.0f;
    s_presets[16].filter.env.sustain_level = 0.0f;
    s_presets[16].filter.env.release_ms = 1.0f;
    s_presets[16].lfo1.waveform = WAVE_SINE;
    s_presets[16].lfo1.rate_hz = 1.0f;
    s_presets[16].lfo1.depth = 0.0f;
    s_presets[16].lfo1.destination = LFO_DEST_PITCH;
    s_presets[16].lfo2.waveform = WAVE_SINE;
    s_presets[16].lfo2.rate_hz = 1.0f;
    s_presets[16].lfo2.depth = 0.0f;
    s_presets[16].lfo2.destination = LFO_DEST_AMP;


    /* Diagnostic matrix: isolate one DSP subsystem at a time. */
    set_name(&s_presets[17], "DIAG SINE+FILTER");
    s_presets[17].algorithm = 9;
    s_presets[17].master_volume = 0.35f;
    s_presets[17].polyphony = 1;
    s_presets[17].glide_ms = 0.0f;
    for (int i = 0; i < 3; ++i) {
        s_presets[17].op[i].waveform = WAVE_SINE;
        s_presets[17].op[i].ratio = 1.0f;
        s_presets[17].op[i].carrier_level = 0.0f;
        s_presets[17].op[i].feedback = 0.0f;
        s_presets[17].op[i].velocity_sens = 0.0f;
        s_presets[17].op[i].key_scaling = 0.0f;
    }
    s_presets[17].op[0].carrier_level = 0.65f;
    s_presets[17].filter.type = FILTER_LOWPASS;
    s_presets[17].filter.cutoff_hz = 3000.0f;
    s_presets[17].filter.resonance = 0.0f;
    s_presets[17].filter.env_amount = 0.0f;
    s_presets[17].lfo1.depth = 0.0f;
    s_presets[17].lfo2.depth = 0.0f;

    set_name(&s_presets[18], "DIAG AMP LFO");
    s_presets[18].algorithm = 10;
    s_presets[18].master_volume = 0.35f;
    s_presets[18].polyphony = 1;
    s_presets[18].op[0].carrier_level = 0.65f;
    s_presets[18].lfo1.waveform = WAVE_SINE;
    s_presets[18].lfo1.rate_hz = 2.0f;
    s_presets[18].lfo1.depth = 0.5f;
    s_presets[18].lfo1.destination = LFO_DEST_AMP;
    s_presets[18].lfo2.depth = 0.0f;

    set_name(&s_presets[19], "DIAG PITCH LFO");
    s_presets[19].algorithm = 11;
    s_presets[19].master_volume = 0.35f;
    s_presets[19].polyphony = 1;
    s_presets[19].op[0].carrier_level = 0.65f;
    s_presets[19].lfo1.waveform = WAVE_SINE;
    s_presets[19].lfo1.rate_hz = 5.0f;
    s_presets[19].lfo1.depth = 0.5f;
    s_presets[19].lfo1.destination = LFO_DEST_PITCH;
    s_presets[19].lfo2.depth = 0.0f;

    set_name(&s_presets[20], "DIAG 2 SINE SUM");
    s_presets[20].algorithm = 12;
    s_presets[20].master_volume = 0.35f;
    s_presets[20].polyphony = 1;
    s_presets[20].op[0].waveform = WAVE_SINE;
    s_presets[20].op[1].waveform = WAVE_SINE;
    s_presets[20].op[2].waveform = WAVE_SINE;
    s_presets[20].op[0].ratio = 1.0f;
    s_presets[20].op[1].ratio = 2.0f;
    s_presets[20].op[2].ratio = 1.0f;
    s_presets[20].op[0].carrier_level = 0.8f;
    s_presets[20].op[1].carrier_level = 0.8f;
    s_presets[20].op[2].carrier_level = 0.0f;
    s_presets[20].lfo1.depth = 0.0f;
    s_presets[20].lfo2.depth = 0.0f;

    set_name(&s_presets[21], "DIAG 3 SINE SUM");
    s_presets[21].algorithm = 13;
    s_presets[21].master_volume = 0.28f;
    s_presets[21].polyphony = 1;
    for (int i = 0; i < 3; ++i) {
        s_presets[21].op[i].waveform = WAVE_SINE;
        s_presets[21].op[i].carrier_level = 0.60f;
        s_presets[21].op[i].feedback = 0.0f;
    }
    s_presets[21].op[0].ratio = 1.0f;
    s_presets[21].op[1].ratio = 2.0f;
    s_presets[21].op[2].ratio = 3.0f;
    s_presets[21].lfo1.depth = 0.0f;
    s_presets[21].lfo2.depth = 0.0f;

    set_name(&s_presets[22], "DIAG FM 2->1");
    s_presets[22].algorithm = 14;
    s_presets[22].master_volume = 0.35f;
    s_presets[22].polyphony = 1;
    s_presets[22].op[0].waveform = WAVE_SINE;
    s_presets[22].op[1].waveform = WAVE_SINE;
    s_presets[22].op[2].waveform = WAVE_SINE;
    s_presets[22].op[0].carrier_level = 0.8f;
    s_presets[22].op[1].carrier_level = 0.20f;
    s_presets[22].op[2].carrier_level = 0.0f;
    s_presets[22].op[0].ratio = 1.0f;
    s_presets[22].op[1].ratio = 2.0f;
    s_presets[22].lfo1.depth = 0.0f;
    s_presets[22].lfo2.depth = 0.0f;

    set_name(&s_presets[23], "DIAG FM 3->1");
    s_presets[23].algorithm = 15;
    s_presets[23].master_volume = 0.35f;
    s_presets[23].polyphony = 1;
    for (int i = 0; i < 3; ++i) {
        s_presets[23].op[i].waveform = WAVE_SINE;
        s_presets[23].op[i].feedback = 0.0f;
    }
    s_presets[23].op[0].carrier_level = 0.8f;
    s_presets[23].op[1].carrier_level = 0.12f;
    s_presets[23].op[2].carrier_level = 0.12f;
    s_presets[23].op[0].ratio = 1.0f;
    s_presets[23].op[1].ratio = 2.0f;
    s_presets[23].op[2].ratio = 3.0f;
    s_presets[23].lfo1.depth = 0.0f;
    s_presets[23].lfo2.depth = 0.0f;

    set_name(&s_presets[24], "DIAG FM CASCADE");
    s_presets[24].algorithm = 16;
    s_presets[24].master_volume = 0.35f;
    s_presets[24].polyphony = 1;
    for (int i = 0; i < 3; ++i) {
        s_presets[24].op[i].waveform = WAVE_SINE;
        s_presets[24].op[i].feedback = 0.0f;
    }
    s_presets[24].op[0].carrier_level = 0.8f;
    s_presets[24].op[1].carrier_level = 0.15f;
    s_presets[24].op[2].carrier_level = 0.10f;
    s_presets[24].op[0].ratio = 1.0f;
    s_presets[24].op[1].ratio = 2.0f;
    s_presets[24].op[2].ratio = 3.0f;
    s_presets[24].lfo1.depth = 0.0f;
    s_presets[24].lfo2.depth = 0.0f;

    set_name(&s_presets[25], "DIAG FEEDBACK");
    s_presets[25].algorithm = 17;
    s_presets[25].master_volume = 0.30f;
    s_presets[25].polyphony = 1;
    s_presets[25].op[0].waveform = WAVE_SINE;
    s_presets[25].op[1].waveform = WAVE_SINE;
    s_presets[25].op[2].waveform = WAVE_SINE;
    s_presets[25].op[0].carrier_level = 0.8f;
    s_presets[25].op[1].carrier_level = 0.12f;
    s_presets[25].op[2].carrier_level = 0.0f;
    s_presets[25].op[1].feedback = 0.15f;
    s_presets[25].op[0].ratio = 1.0f;
    s_presets[25].op[1].ratio = 2.0f;
    s_presets[25].lfo1.depth = 0.0f;
    s_presets[25].lfo2.depth = 0.0f;

    set_name(&s_presets[26], "DIAG FM LFO");
    s_presets[26].algorithm = 18;
    s_presets[26].master_volume = 0.35f;
    s_presets[26].polyphony = 1;
    s_presets[26].op[0].waveform = WAVE_SINE;
    s_presets[26].op[1].waveform = WAVE_SINE;
    s_presets[26].op[2].waveform = WAVE_SINE;
    s_presets[26].op[0].carrier_level = 0.8f;
    s_presets[26].op[1].carrier_level = 0.15f;
    s_presets[26].op[2].carrier_level = 0.0f;
    s_presets[26].op[0].ratio = 1.0f;
    s_presets[26].op[1].ratio = 2.0f;
    s_presets[26].lfo1.waveform = WAVE_SINE;
    s_presets[26].lfo1.rate_hz = 1.0f;
    s_presets[26].lfo1.depth = 0.25f;
    s_presets[26].lfo1.destination = LFO_DEST_FM_INDEX;
    s_presets[26].lfo2.depth = 0.0f;

    /* Dedicated polyphony reference: one sine per voice, no filter/FM/LFO.
     * Play 1, 2, 4 and 8 simultaneous notes to isolate allocator + mixer. */
    set_name(&s_presets[27], "DIAG POLY 1-8 VOICES");
    s_presets[27].algorithm = 19;
    s_presets[27].master_volume = 0.30f;
    s_presets[27].polyphony = SYNTH_MAX_VOICES;
    s_presets[27].poly_mode = POLY_MODE_POLY;
    s_presets[27].glide_ms = 0.0f;
    s_presets[27].unison_voices = 1;
    for (int i = 0; i < 3; ++i) {
        s_presets[27].op[i].waveform = WAVE_SINE;
        s_presets[27].op[i].ratio = 1.0f;
        s_presets[27].op[i].detune_cents = 0.0f;
        s_presets[27].op[i].carrier_level = 0.0f;
        s_presets[27].op[i].pm_index = 0.0f;
        s_presets[27].op[i].feedback = 0.0f;
        s_presets[27].op[i].velocity_sens = 0.0f;
        s_presets[27].op[i].key_scaling = 0.0f;
        s_presets[27].op[i].env.attack_ms = 3.0f;
        s_presets[27].op[i].env.decay_ms = 30.0f;
        s_presets[27].op[i].env.sustain_level = 1.0f;
        s_presets[27].op[i].env.release_ms = 80.0f;
    }
    s_presets[27].op[0].carrier_level = 0.65f;
    s_presets[27].filter.cutoff_hz = 20000.0f;
    s_presets[27].filter.resonance = 0.5f;
    s_presets[27].filter.env_amount = 0.0f;
    s_presets[27].lfo1.depth = 0.0f;
    s_presets[27].lfo2.depth = 0.0f;

    /* V14: explicit PM depth is separate from carrier/audio level.  Production
     * presets retain their old timbre approximately, but modulators no longer
     * borrow the carrier level implicitly. */
    for (int i = 0; i < PRESET_COUNT; ++i) {
        s_presets[i].unison_voices = 2;
        s_presets[i].unison_spread_cents = 8.0f;
        s_presets[i].op[1].pm_index = fminf(1.25f, s_presets[i].op[1].carrier_level * 1.10f);
        s_presets[i].op[2].pm_index = fminf(1.25f, s_presets[i].op[2].carrier_level * 1.10f);
    }
    /* Diagnostic PM/feedback depths are intentionally small and deterministic. */
    s_presets[16].op[1].pm_index = s_presets[16].op[2].pm_index = 0.0f;
    s_presets[17].op[1].pm_index = s_presets[17].op[2].pm_index = 0.0f;
    s_presets[18].op[1].pm_index = s_presets[18].op[2].pm_index = 0.0f;
    s_presets[19].op[1].pm_index = s_presets[19].op[2].pm_index = 0.0f;
    s_presets[20].op[1].pm_index = s_presets[20].op[2].pm_index = 0.0f;
    s_presets[21].op[1].pm_index = s_presets[21].op[2].pm_index = 0.0f;
    s_presets[22].op[1].pm_index = 0.35f;
    s_presets[23].op[2].pm_index = 0.35f;
    s_presets[24].op[1].pm_index = 0.35f;
    s_presets[25].op[1].pm_index = 0.35f;
    s_presets[26].op[1].pm_index = 0.35f;
    s_presets[27].op[1].pm_index = s_presets[27].op[2].pm_index = 0.0f;

    /* Global factory gain trim: keep every preset well below full-scale while
     * we continue tuning the polyphonic mixer and FM gain staging. */
    for (int i = 0; i < PRESET_COUNT; ++i) {
        s_presets[i].master_volume = fminf(s_presets[i].master_volume, 0.35f);
    }

    s_current = 0;
}

uint8_t preset_manager_current(void)
{
    return s_current;
}

uint8_t preset_manager_select(int delta)
{
    int next = (int)s_current + delta;
    while (next < 0) next += PRESET_COUNT;
    while (next >= PRESET_COUNT) next -= PRESET_COUNT;
    s_current = (uint8_t)next;
    return s_current;
}

void preset_manager_load(uint8_t index)
{
    if (index >= PRESET_COUNT) return;
    s_current = index;
    /* Loading a preset is a hard scene change; the synth engine clears all
       voices before installing the new topology. */
    synth_load_patch(&s_presets[index]);
}

const char *preset_manager_name(uint8_t index)
{
    if (index >= PRESET_COUNT) return "---";
    return s_presets[index].name;
}
