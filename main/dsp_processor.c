/*
 * DSP Processor Implementation
 * FSD-DSP-001: DSP Presets + Loudness via BLE GATT
 *
 * Author: Robin Kluit
 * Date: 2026-01-20
 */

#include "dsp_processor.h"
#include <string.h>
#include <math.h>
#include "esp_log.h"

static const char *TAG = "DSP";

/* Mathematical constants */
#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

/* Conversion macros */
#define DB_TO_LINEAR(db) (powf(10.0f, (db) / 20.0f))
#define LINEAR_TO_DB(lin) (20.0f * log10f(lin))

/* Sample scaling */
#define INT16_TO_FLOAT(x) ((float)(x) / 32768.0f)
#define FLOAT_TO_INT16(x) ((int16_t)fmaxf(-32768.0f, fminf(32767.0f, (x) * 32768.0f)))

/*
 * Preset EQ Parameters (Section 8)
 * Each preset has 4 bands
 */
typedef struct {
    float freq;             /* Center/corner frequency (Hz) */
    float gain_db;          /* Gain (dB) */
    float q_or_slope;       /* Q for peaking, S for shelf */
    uint8_t type;           /* 0=low-shelf, 1=peaking, 2=high-shelf */
} eq_band_params_t;

#define EQ_TYPE_LOWSHELF    0
#define EQ_TYPE_PEAKING     1
#define EQ_TYPE_HIGHSHELF   2

/* Preset definitions from FSD Section 8 */
static const eq_band_params_t preset_params[DSP_PRESET_COUNT][DSP_NUM_EQ_BANDS] = {
    /* OFFICE (Section 8.2) */
    {
        { 160.0f,  +1.5f, 0.7f, EQ_TYPE_LOWSHELF },
        { 320.0f,  -1.0f, 1.0f, EQ_TYPE_PEAKING },
        { 2800.0f, -1.5f, 1.0f, EQ_TYPE_PEAKING },
        { 9000.0f, +0.5f, 0.7f, EQ_TYPE_HIGHSHELF }
    },
    /* FULL (Section 8.3) */
    {
        { 140.0f,  +4.0f, 0.8f, EQ_TYPE_LOWSHELF },
        { 420.0f,  -1.5f, 1.0f, EQ_TYPE_PEAKING },
        { 3200.0f, +0.7f, 1.0f, EQ_TYPE_PEAKING },
        { 9500.0f, +1.5f, 0.7f, EQ_TYPE_HIGHSHELF }
    },
    /* NIGHT (Section 8.4) */
    {
        { 160.0f,  +2.5f, 0.8f, EQ_TYPE_LOWSHELF },
        { 350.0f,  -1.0f, 1.0f, EQ_TYPE_PEAKING },
        { 2500.0f, +1.0f, 1.0f, EQ_TYPE_PEAKING },
        { 9000.0f, +1.0f, 0.7f, EQ_TYPE_HIGHSHELF }
    },
    /* SPEECH (Section 8.5) */
    {
        { 170.0f,  -2.0f, 0.8f, EQ_TYPE_LOWSHELF },
        { 300.0f,  -1.0f, 1.0f, EQ_TYPE_PEAKING },
        { 3200.0f, +3.0f, 1.0f, EQ_TYPE_PEAKING },
        { 7500.0f, -1.0f, 2.0f, EQ_TYPE_PEAKING }  /* Note: Last band is peaking in SPEECH */
    }
};

/* Loudness overlay parameters (Section 9.1) */
static const eq_band_params_t loudness_params[DSP_NUM_LOUDNESS_BANDS] = {
    { 140.0f,  +2.5f, 0.8f, EQ_TYPE_LOWSHELF },
    { 8500.0f, +1.0f, 0.7f, EQ_TYPE_HIGHSHELF }
};

/* Preset names */
static const char *preset_names[DSP_PRESET_COUNT] = {
    "OFFICE", "FULL", "NIGHT", "SPEECH"
};

/*
 * DSP State Structure
 * All state is statically allocated (FR-17)
 */
typedef struct {
    /* Configuration */
    uint32_t sample_rate;
    dsp_preset_t preset;
    bool loudness_enabled;

    /* Filter coefficients */
    biquad_coeffs_t hpf_coeffs;                             /* High-pass filter */
    biquad_coeffs_t eq_coeffs[DSP_NUM_EQ_BANDS];           /* Preset EQ */
    biquad_coeffs_t loudness_coeffs[DSP_NUM_LOUDNESS_BANDS]; /* Loudness overlay */

    /* Target coefficients for smooth transitions */
    biquad_coeffs_t eq_target[DSP_NUM_EQ_BANDS];
    biquad_coeffs_t loudness_target[DSP_NUM_LOUDNESS_BANDS];

    /* Filter state (stereo) */
    biquad_state_t hpf_state[2];                            /* L/R */
    biquad_state_t eq_state[DSP_NUM_EQ_BANDS][2];          /* L/R per band */
    biquad_state_t loudness_state[DSP_NUM_LOUDNESS_BANDS][2];

    /* Limiter state */
    limiter_state_t limiter;
    float limiter_threshold;
    float limiter_attack_coeff;
    float limiter_release_coeff;

    /* Gains */
    float pre_gain;
    float pre_gain_target;
    float loudness_gain;        /* 0.0 = off, 1.0 = on */
    float loudness_gain_target;
    float mute_gain;            /* 1.0 = unmuted, 0.0 = muted */
    float mute_gain_target;
    bool muted;                 /* Mute state */

    /* Audio Duck (FR-21) - panic button volume reduction */
    bool audio_duck_enabled;
    float audio_duck_gain;
    float audio_duck_gain_target;

    /* Volume Trim (FR-24) - device-side volume control */
    uint8_t volume_trim;            /* User-set volume (0-100) */
    float volume_gain;              /* Actual volume gain (linear) */
    float volume_gain_target;       /* Target volume gain (linear) */

    /* Normalizer/DRC (FR-22) - dynamic range compression */
    bool normalizer_enabled;
    float normalizer_envelope;
    float normalizer_gain;
    float normalizer_threshold;
    float normalizer_ratio;
    float normalizer_attack_coeff;
    float normalizer_release_coeff;
    float normalizer_makeup_gain;

    /* Smoothing coefficient */
    float smooth_coeff;

    /* Status */
    bool limiter_active;
    bool clipping_detected;

    /* Initialized flag */
    bool initialized;
} dsp_state_t;

/* Static DSP state - no dynamic allocation (FR-17) */
static dsp_state_t s_dsp;

/*
 * Calculate biquad coefficients for different filter types
 * Using Audio EQ Cookbook formulas
 */

static void calc_biquad_lowshelf(biquad_coeffs_t *c, float freq, float gain_db, float S, float fs)
{
    float A = DB_TO_LINEAR(gain_db / 2.0f);  /* sqrt of linear gain */
    float w0 = 2.0f * M_PI * freq / fs;
    float cos_w0 = cosf(w0);
    float sin_w0 = sinf(w0);
    float alpha = sin_w0 / 2.0f * sqrtf((A + 1.0f / A) * (1.0f / S - 1.0f) + 2.0f);

    float a0 = (A + 1.0f) + (A - 1.0f) * cos_w0 + 2.0f * sqrtf(A) * alpha;
    c->b0 = (A * ((A + 1.0f) - (A - 1.0f) * cos_w0 + 2.0f * sqrtf(A) * alpha)) / a0;
    c->b1 = (2.0f * A * ((A - 1.0f) - (A + 1.0f) * cos_w0)) / a0;
    c->b2 = (A * ((A + 1.0f) - (A - 1.0f) * cos_w0 - 2.0f * sqrtf(A) * alpha)) / a0;
    c->a1 = (-2.0f * ((A - 1.0f) + (A + 1.0f) * cos_w0)) / a0;
    c->a2 = ((A + 1.0f) + (A - 1.0f) * cos_w0 - 2.0f * sqrtf(A) * alpha) / a0;
}

static void calc_biquad_highshelf(biquad_coeffs_t *c, float freq, float gain_db, float S, float fs)
{
    float A = DB_TO_LINEAR(gain_db / 2.0f);
    float w0 = 2.0f * M_PI * freq / fs;
    float cos_w0 = cosf(w0);
    float sin_w0 = sinf(w0);
    float alpha = sin_w0 / 2.0f * sqrtf((A + 1.0f / A) * (1.0f / S - 1.0f) + 2.0f);

    float a0 = (A + 1.0f) - (A - 1.0f) * cos_w0 + 2.0f * sqrtf(A) * alpha;
    c->b0 = (A * ((A + 1.0f) + (A - 1.0f) * cos_w0 + 2.0f * sqrtf(A) * alpha)) / a0;
    c->b1 = (-2.0f * A * ((A - 1.0f) + (A + 1.0f) * cos_w0)) / a0;
    c->b2 = (A * ((A + 1.0f) + (A - 1.0f) * cos_w0 - 2.0f * sqrtf(A) * alpha)) / a0;
    c->a1 = (2.0f * ((A - 1.0f) - (A + 1.0f) * cos_w0)) / a0;
    c->a2 = ((A + 1.0f) - (A - 1.0f) * cos_w0 - 2.0f * sqrtf(A) * alpha) / a0;
}

static void calc_biquad_peaking(biquad_coeffs_t *c, float freq, float gain_db, float Q, float fs)
{
    float A = DB_TO_LINEAR(gain_db / 2.0f);
    float w0 = 2.0f * M_PI * freq / fs;
    float cos_w0 = cosf(w0);
    float sin_w0 = sinf(w0);
    float alpha = sin_w0 / (2.0f * Q);

    float a0 = 1.0f + alpha / A;
    c->b0 = (1.0f + alpha * A) / a0;
    c->b1 = (-2.0f * cos_w0) / a0;
    c->b2 = (1.0f - alpha * A) / a0;
    c->a1 = (-2.0f * cos_w0) / a0;
    c->a2 = (1.0f - alpha / A) / a0;
}

static void calc_biquad_highpass(biquad_coeffs_t *c, float freq, float Q, float fs)
{
    float w0 = 2.0f * M_PI * freq / fs;
    float cos_w0 = cosf(w0);
    float sin_w0 = sinf(w0);
    float alpha = sin_w0 / (2.0f * Q);

    float a0 = 1.0f + alpha;
    c->b0 = ((1.0f + cos_w0) / 2.0f) / a0;
    c->b1 = (-(1.0f + cos_w0)) / a0;
    c->b2 = ((1.0f + cos_w0) / 2.0f) / a0;
    c->a1 = (-2.0f * cos_w0) / a0;
    c->a2 = (1.0f - alpha) / a0;
}

/* Calculate bypass (unity) filter coefficients */
static void calc_biquad_bypass(biquad_coeffs_t *c)
{
    c->b0 = 1.0f;
    c->b1 = 0.0f;
    c->b2 = 0.0f;
    c->a1 = 0.0f;
    c->a2 = 0.0f;
}

/* Calculate EQ band coefficients based on type */
static void calc_eq_band(biquad_coeffs_t *c, const eq_band_params_t *p, float fs)
{
    switch (p->type) {
    case EQ_TYPE_LOWSHELF:
        calc_biquad_lowshelf(c, p->freq, p->gain_db, p->q_or_slope, fs);
        break;
    case EQ_TYPE_HIGHSHELF:
        calc_biquad_highshelf(c, p->freq, p->gain_db, p->q_or_slope, fs);
        break;
    case EQ_TYPE_PEAKING:
    default:
        calc_biquad_peaking(c, p->freq, p->gain_db, p->q_or_slope, fs);
        break;
    }
}

/* Reset filter state */
static void reset_biquad_state(biquad_state_t *s)
{
    s->z1 = 0.0f;
    s->z2 = 0.0f;
}

/* Process single sample through biquad filter (Direct Form II Transposed) */
static inline float biquad_process(const biquad_coeffs_t *c, biquad_state_t *s, float in)
{
    float out = c->b0 * in + s->z1;
    s->z1 = c->b1 * in - c->a1 * out + s->z2;
    s->z2 = c->b2 * in - c->a2 * out;
    return out;
}

/* Interpolate between two coefficient sets */
static void interpolate_coeffs(biquad_coeffs_t *dst, const biquad_coeffs_t *current,
                               const biquad_coeffs_t *target, float alpha)
{
    dst->b0 = current->b0 + alpha * (target->b0 - current->b0);
    dst->b1 = current->b1 + alpha * (target->b1 - current->b1);
    dst->b2 = current->b2 + alpha * (target->b2 - current->b2);
    dst->a1 = current->a1 + alpha * (target->a1 - current->a1);
    dst->a2 = current->a2 + alpha * (target->a2 - current->a2);
}

/* Calculate smoothing coefficient for given time constant */
static float calc_smooth_coeff(float time_ms, float fs)
{
    /* First-order IIR smoothing: coeff = 1 - exp(-1 / (time * fs)) */
    float time_samples = (time_ms / 1000.0f) * fs;
    if (time_samples < 1.0f) time_samples = 1.0f;
    return 1.0f - expf(-1.0f / time_samples);
}

/*
 * Convert volume (0-100) to linear gain (FR-24)
 * Uses logarithmic mapping per FSD Section 10.5:
 *   100 → 0 dB
 *   80 → -6 dB
 *   60 → -12 dB
 *   40 → -20 dB
 *   20 → -35 dB
 *   0 → mute
 */
static float volume_to_gain(uint8_t volume)
{
    if (volume == 0) {
        return 0.0f;  /* Mute */
    }
    if (volume >= 100) {
        return 1.0f;  /* 0 dB */
    }

    /* Use piecewise linear interpolation in dB domain for smooth curve */
    float db;
    float v = (float)volume;

    if (volume >= 80) {
        /* 80-100 → -6 to 0 dB */
        db = -6.0f + (v - 80.0f) * (6.0f / 20.0f);
    } else if (volume >= 60) {
        /* 60-80 → -12 to -6 dB */
        db = -12.0f + (v - 60.0f) * (6.0f / 20.0f);
    } else if (volume >= 40) {
        /* 40-60 → -20 to -12 dB */
        db = -20.0f + (v - 40.0f) * (8.0f / 20.0f);
    } else if (volume >= 20) {
        /* 20-40 → -35 to -20 dB */
        db = -35.0f + (v - 20.0f) * (15.0f / 20.0f);
    } else {
        /* 0-20 → -60 to -35 dB (approaching mute) */
        db = -60.0f + v * (25.0f / 20.0f);
    }

    return DB_TO_LINEAR(db);
}

/* Calculate limiter coefficients */
static void calc_limiter_coeffs(dsp_state_t *dsp)
{
    dsp->limiter_threshold = DB_TO_LINEAR(DSP_LIMITER_THRESHOLD_DB);
    dsp->limiter_attack_coeff = calc_smooth_coeff(DSP_LIMITER_ATTACK_MS, (float)dsp->sample_rate);
    dsp->limiter_release_coeff = calc_smooth_coeff(DSP_LIMITER_RELEASE_MS, (float)dsp->sample_rate);
}

/* Update all filter coefficients for current settings */
static void update_filters(dsp_state_t *dsp)
{
    float fs = (float)dsp->sample_rate;

    /* High-pass filter (always active) */
    calc_biquad_highpass(&dsp->hpf_coeffs, DSP_HPF_FREQ_HZ, DSP_HPF_Q, fs);

    /* Preset EQ */
    for (int i = 0; i < DSP_NUM_EQ_BANDS; i++) {
        calc_eq_band(&dsp->eq_target[i], &preset_params[dsp->preset][i], fs);
    }

    /* Loudness overlay (calculate coefficients, will be blended based on loudness_gain) */
    for (int i = 0; i < DSP_NUM_LOUDNESS_BANDS; i++) {
        calc_eq_band(&dsp->loudness_target[i], &loudness_params[i], fs);
    }

    /* Limiter */
    calc_limiter_coeffs(dsp);

    /* Normalizer/DRC coefficients (FR-22) */
    dsp->normalizer_attack_coeff = calc_smooth_coeff(DSP_NORMALIZER_ATTACK_MS, fs);
    dsp->normalizer_release_coeff = calc_smooth_coeff(DSP_NORMALIZER_RELEASE_MS, fs);

    /* Smoothing coefficient */
    dsp->smooth_coeff = calc_smooth_coeff(DSP_SMOOTHING_MS, fs);
}

/*
 * Public API Implementation
 */

esp_err_t dsp_init(uint32_t sample_rate)
{
    ESP_LOGI(TAG, "Initializing DSP at %lu Hz", sample_rate);

    /* Clear state */
    memset(&s_dsp, 0, sizeof(s_dsp));

    /* Set initial configuration */
    s_dsp.sample_rate = sample_rate;
    s_dsp.preset = DSP_PRESET_OFFICE;   /* Default preset */
    s_dsp.loudness_enabled = false;

    /* Set gains */
    s_dsp.pre_gain = DB_TO_LINEAR(DSP_PRE_GAIN_DB);
    s_dsp.pre_gain_target = s_dsp.pre_gain;
    s_dsp.loudness_gain = 0.0f;
    s_dsp.loudness_gain_target = 0.0f;
    s_dsp.mute_gain = 1.0f;     /* Start unmuted */
    s_dsp.mute_gain_target = 1.0f;
    s_dsp.muted = false;

    /* Initialize Audio Duck (FR-21) */
    s_dsp.audio_duck_enabled = false;
    s_dsp.audio_duck_gain = 1.0f;
    s_dsp.audio_duck_gain_target = 1.0f;

    /* Initialize Volume Trim (FR-24) */
    s_dsp.volume_trim = DSP_VOLUME_TRIM_DEFAULT;
    s_dsp.volume_gain = 1.0f;
    s_dsp.volume_gain_target = 1.0f;

    /* Initialize Normalizer/DRC (FR-22) */
    s_dsp.normalizer_enabled = false;
    s_dsp.normalizer_envelope = 0.0f;
    s_dsp.normalizer_gain = 1.0f;
    s_dsp.normalizer_threshold = DB_TO_LINEAR(DSP_NORMALIZER_THRESHOLD_DB);
    s_dsp.normalizer_ratio = DSP_NORMALIZER_RATIO;
    s_dsp.normalizer_makeup_gain = DB_TO_LINEAR(DSP_NORMALIZER_MAKEUP_DB);

    /* Initialize limiter */
    s_dsp.limiter.envelope = 0.0f;
    s_dsp.limiter.gain = 1.0f;

    /* Calculate filter coefficients */
    update_filters(&s_dsp);

    /* Copy target coefficients to current (no smoothing on init) */
    memcpy(s_dsp.eq_coeffs, s_dsp.eq_target, sizeof(s_dsp.eq_coeffs));
    for (int i = 0; i < DSP_NUM_LOUDNESS_BANDS; i++) {
        calc_biquad_bypass(&s_dsp.loudness_coeffs[i]);
    }

    /* Reset all filter states */
    for (int ch = 0; ch < 2; ch++) {
        reset_biquad_state(&s_dsp.hpf_state[ch]);
        for (int i = 0; i < DSP_NUM_EQ_BANDS; i++) {
            reset_biquad_state(&s_dsp.eq_state[i][ch]);
        }
        for (int i = 0; i < DSP_NUM_LOUDNESS_BANDS; i++) {
            reset_biquad_state(&s_dsp.loudness_state[i][ch]);
        }
    }

    s_dsp.initialized = true;
    ESP_LOGI(TAG, "DSP initialized: preset=%s, loudness=%s, pre-gain=%.1f dB",
             preset_names[s_dsp.preset],
             s_dsp.loudness_enabled ? "ON" : "OFF",
             DSP_PRE_GAIN_DB);

    return ESP_OK;
}

esp_err_t dsp_set_sample_rate(uint32_t sample_rate)
{
    if (!s_dsp.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (sample_rate == s_dsp.sample_rate) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Reconfiguring DSP for %lu Hz", sample_rate);
    s_dsp.sample_rate = sample_rate;

    /* Recalculate all filter coefficients */
    update_filters(&s_dsp);

    /* Reset filter states to prevent artifacts */
    for (int ch = 0; ch < 2; ch++) {
        reset_biquad_state(&s_dsp.hpf_state[ch]);
        for (int i = 0; i < DSP_NUM_EQ_BANDS; i++) {
            reset_biquad_state(&s_dsp.eq_state[i][ch]);
        }
        for (int i = 0; i < DSP_NUM_LOUDNESS_BANDS; i++) {
            reset_biquad_state(&s_dsp.loudness_state[i][ch]);
        }
    }

    return ESP_OK;
}

esp_err_t dsp_set_preset(dsp_preset_t preset)
{
    if (preset >= DSP_PRESET_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_dsp.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (preset == s_dsp.preset) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Setting preset: %s", preset_names[preset]);
    s_dsp.preset = preset;

    /* Calculate new target coefficients (smoothing happens during processing) */
    float fs = (float)s_dsp.sample_rate;
    for (int i = 0; i < DSP_NUM_EQ_BANDS; i++) {
        calc_eq_band(&s_dsp.eq_target[i], &preset_params[preset][i], fs);
    }

    /* Update volume gain target (preset may affect volume cap - FR-24) */
    uint8_t effective = dsp_get_effective_volume();
    s_dsp.volume_gain_target = volume_to_gain(effective);

    return ESP_OK;
}

dsp_preset_t dsp_get_preset(void)
{
    return s_dsp.preset;
}

esp_err_t dsp_set_loudness(bool enabled)
{
    if (!s_dsp.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (enabled == s_dsp.loudness_enabled) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Setting loudness: %s", enabled ? "ON" : "OFF");
    s_dsp.loudness_enabled = enabled;
    s_dsp.loudness_gain_target = enabled ? 1.0f : 0.0f;

    return ESP_OK;
}

bool dsp_get_loudness(void)
{
    return s_dsp.loudness_enabled;
}

esp_err_t dsp_set_mute(bool muted)
{
    if (!s_dsp.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (muted == s_dsp.muted) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Setting mute: %s", muted ? "ON" : "OFF");
    s_dsp.muted = muted;
    s_dsp.mute_gain_target = muted ? 0.0f : 1.0f;

    return ESP_OK;
}

bool dsp_get_mute(void)
{
    return s_dsp.muted;
}

esp_err_t dsp_set_audio_duck(bool enabled)
{
    if (!s_dsp.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (enabled == s_dsp.audio_duck_enabled) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Setting audio duck: %s", enabled ? "ON" : "OFF");
    s_dsp.audio_duck_enabled = enabled;
    /* ~25% volume = -12 dB reduction when enabled */
    s_dsp.audio_duck_gain_target = enabled ? DB_TO_LINEAR(DSP_AUDIO_DUCK_GAIN_DB) : 1.0f;

    return ESP_OK;
}

bool dsp_get_audio_duck(void)
{
    return s_dsp.audio_duck_enabled;
}

esp_err_t dsp_set_normalizer(bool enabled)
{
    if (!s_dsp.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (enabled == s_dsp.normalizer_enabled) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Setting normalizer: %s", enabled ? "ON" : "OFF");
    s_dsp.normalizer_enabled = enabled;

    /* Reset normalizer state when toggling */
    if (enabled) {
        s_dsp.normalizer_envelope = 0.0f;
        s_dsp.normalizer_gain = 1.0f;
    }

    /* Update volume gain target (normalizer affects volume cap - FR-24) */
    uint8_t effective = dsp_get_effective_volume();
    s_dsp.volume_gain_target = volume_to_gain(effective);

    return ESP_OK;
}

bool dsp_get_normalizer(void)
{
    return s_dsp.normalizer_enabled;
}

uint8_t dsp_get_volume_cap(void)
{
    uint8_t cap = 100;

    /* NIGHT preset has a volume cap (FSD 10.4) */
    if (s_dsp.preset == DSP_PRESET_NIGHT) {
        cap = DSP_VOLUME_CAP_NIGHT;
    }

    /* Normalizer reduces headroom, so lower the cap (FSD 10.4) */
    if (s_dsp.normalizer_enabled && cap > DSP_VOLUME_CAP_NORMALIZER_REDUCTION) {
        cap -= DSP_VOLUME_CAP_NORMALIZER_REDUCTION;
    }

    return cap;
}

uint8_t dsp_get_effective_volume(void)
{
    uint8_t cap = dsp_get_volume_cap();
    uint8_t effective = s_dsp.volume_trim;

    if (effective > cap) {
        effective = cap;
    }

    return effective;
}

esp_err_t dsp_set_volume_trim(uint8_t value)
{
    if (!s_dsp.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Clamp to valid range */
    if (value > 100) {
        value = 100;
    }

    if (value == s_dsp.volume_trim) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Setting volume trim: %d%% (cap: %d%%)", value, dsp_get_volume_cap());
    s_dsp.volume_trim = value;

    /* Calculate effective volume with cap applied */
    uint8_t effective = dsp_get_effective_volume();
    s_dsp.volume_gain_target = volume_to_gain(effective);

    return ESP_OK;
}

uint8_t dsp_get_volume_trim(void)
{
    return s_dsp.volume_trim;
}

void dsp_get_status(dsp_status_t *status)
{
    if (status == NULL) {
        return;
    }

    status->preset = (uint8_t)s_dsp.preset;
    status->loudness = s_dsp.loudness_enabled ? 1 : 0;
    status->flags = DSP_FLAG_LIMITER_ACTIVE;  /* Limiter is always active */

    if (s_dsp.muted) {
        status->flags |= DSP_FLAG_MUTED;
    }

    if (s_dsp.audio_duck_enabled) {
        status->flags |= DSP_FLAG_AUDIO_DUCK;
    }

    if (s_dsp.normalizer_enabled) {
        status->flags |= DSP_FLAG_NORMALIZER;
    }

    if (s_dsp.clipping_detected) {
        status->flags |= DSP_FLAG_CLIPPING;
        s_dsp.clipping_detected = false;  /* Clear after read */
    }
}

const char *dsp_preset_name(dsp_preset_t preset)
{
    if (preset >= DSP_PRESET_COUNT) {
        return "UNKNOWN";
    }
    return preset_names[preset];
}

/*
 * Real-time audio processing
 */
void dsp_process(int16_t *samples, uint32_t num_samples)
{
    if (!s_dsp.initialized || samples == NULL || num_samples == 0) {
        return;
    }

    /* Process stereo interleaved samples */
    uint32_t num_frames = num_samples / 2;

    for (uint32_t i = 0; i < num_frames; i++) {
        /* Convert to float */
        float left = INT16_TO_FLOAT(samples[i * 2]);
        float right = INT16_TO_FLOAT(samples[i * 2 + 1]);

        /* Apply pre-gain (FR-7) */
        s_dsp.pre_gain += s_dsp.smooth_coeff * (s_dsp.pre_gain_target - s_dsp.pre_gain);
        left *= s_dsp.pre_gain;
        right *= s_dsp.pre_gain;

        /* High-pass filter (protection) */
        left = biquad_process(&s_dsp.hpf_coeffs, &s_dsp.hpf_state[0], left);
        right = biquad_process(&s_dsp.hpf_coeffs, &s_dsp.hpf_state[1], right);

        /* Preset EQ (with coefficient smoothing) */
        for (int b = 0; b < DSP_NUM_EQ_BANDS; b++) {
            /* Smooth coefficients toward target */
            interpolate_coeffs(&s_dsp.eq_coeffs[b], &s_dsp.eq_coeffs[b],
                             &s_dsp.eq_target[b], s_dsp.smooth_coeff);

            left = biquad_process(&s_dsp.eq_coeffs[b], &s_dsp.eq_state[b][0], left);
            right = biquad_process(&s_dsp.eq_coeffs[b], &s_dsp.eq_state[b][1], right);
        }

        /* Loudness overlay (FR-9) - crossfade based on loudness_gain */
        s_dsp.loudness_gain += s_dsp.smooth_coeff * (s_dsp.loudness_gain_target - s_dsp.loudness_gain);

        if (s_dsp.loudness_gain > 0.001f) {
            /* Apply loudness filters and blend */
            float loud_left = left;
            float loud_right = right;

            for (int b = 0; b < DSP_NUM_LOUDNESS_BANDS; b++) {
                /* Update loudness coefficients toward target */
                interpolate_coeffs(&s_dsp.loudness_coeffs[b], &s_dsp.loudness_coeffs[b],
                                 &s_dsp.loudness_target[b], s_dsp.smooth_coeff);

                loud_left = biquad_process(&s_dsp.loudness_coeffs[b],
                                          &s_dsp.loudness_state[b][0], loud_left);
                loud_right = biquad_process(&s_dsp.loudness_coeffs[b],
                                           &s_dsp.loudness_state[b][1], loud_right);
            }

            /* Crossfade between dry and loudness-processed */
            left = left * (1.0f - s_dsp.loudness_gain) + loud_left * s_dsp.loudness_gain;
            right = right * (1.0f - s_dsp.loudness_gain) + loud_right * s_dsp.loudness_gain;
        }

        /* Normalizer/DRC (FR-22) - dynamic range compression */
        if (s_dsp.normalizer_enabled) {
            float norm_peak = fmaxf(fabsf(left), fabsf(right));

            /* Update envelope with attack/release */
            if (norm_peak > s_dsp.normalizer_envelope) {
                s_dsp.normalizer_envelope += s_dsp.normalizer_attack_coeff *
                                            (norm_peak - s_dsp.normalizer_envelope);
            } else {
                s_dsp.normalizer_envelope += s_dsp.normalizer_release_coeff *
                                            (norm_peak - s_dsp.normalizer_envelope);
            }

            /* Calculate compression gain */
            if (s_dsp.normalizer_envelope > s_dsp.normalizer_threshold) {
                /* Compression: gain = threshold / envelope^(1 - 1/ratio) */
                /* For ratio 4:1: if input is 4dB above threshold, output is 1dB above */
                float over_threshold = s_dsp.normalizer_envelope / s_dsp.normalizer_threshold;
                float compressed = powf(over_threshold, 1.0f - 1.0f / s_dsp.normalizer_ratio);
                s_dsp.normalizer_gain = 1.0f / compressed;
            } else {
                s_dsp.normalizer_gain = 1.0f;
            }

            /* Apply compression gain and makeup gain */
            left *= s_dsp.normalizer_gain * s_dsp.normalizer_makeup_gain;
            right *= s_dsp.normalizer_gain * s_dsp.normalizer_makeup_gain;
        }

        /* Limiter (FR-11) - soft-knee peak limiter */
        float peak = fmaxf(fabsf(left), fabsf(right));

        /* Update envelope (attack/release) */
        if (peak > s_dsp.limiter.envelope) {
            s_dsp.limiter.envelope += s_dsp.limiter_attack_coeff *
                                     (peak - s_dsp.limiter.envelope);
        } else {
            s_dsp.limiter.envelope += s_dsp.limiter_release_coeff *
                                     (peak - s_dsp.limiter.envelope);
        }

        /* Calculate gain reduction */
        if (s_dsp.limiter.envelope > s_dsp.limiter_threshold) {
            s_dsp.limiter.gain = s_dsp.limiter_threshold / s_dsp.limiter.envelope;
            s_dsp.limiter_active = true;
        } else {
            s_dsp.limiter.gain = 1.0f;
            s_dsp.limiter_active = false;
        }

        /* Apply gain reduction */
        left *= s_dsp.limiter.gain;
        right *= s_dsp.limiter.gain;

        /* Hard clip protection (should rarely trigger with limiter) */
        if (fabsf(left) > 1.0f || fabsf(right) > 1.0f) {
            s_dsp.clipping_detected = true;
            left = fmaxf(-1.0f, fminf(1.0f, left));
            right = fmaxf(-1.0f, fminf(1.0f, right));
        }

        /* Volume Trim (FR-24) - device-side volume control */
        s_dsp.volume_gain += s_dsp.smooth_coeff * (s_dsp.volume_gain_target - s_dsp.volume_gain);
        left *= s_dsp.volume_gain;
        right *= s_dsp.volume_gain;

        /* Audio Duck (FR-21) - smooth volume reduction for panic button */
        s_dsp.audio_duck_gain += s_dsp.smooth_coeff * (s_dsp.audio_duck_gain_target - s_dsp.audio_duck_gain);
        left *= s_dsp.audio_duck_gain;
        right *= s_dsp.audio_duck_gain;

        /* Apply mute with smooth fade */
        s_dsp.mute_gain += s_dsp.smooth_coeff * (s_dsp.mute_gain_target - s_dsp.mute_gain);
        left *= s_dsp.mute_gain;
        right *= s_dsp.mute_gain;

        /* Convert back to int16 */
        samples[i * 2] = FLOAT_TO_INT16(left);
        samples[i * 2 + 1] = FLOAT_TO_INT16(right);
    }
}

void dsp_process_float(float *left, float *right, uint32_t num_frames)
{
    if (!s_dsp.initialized || left == NULL || right == NULL || num_frames == 0) {
        return;
    }

    for (uint32_t i = 0; i < num_frames; i++) {
        float l = left[i];
        float r = right[i];

        /* Apply pre-gain */
        l *= s_dsp.pre_gain;
        r *= s_dsp.pre_gain;

        /* HPF */
        l = biquad_process(&s_dsp.hpf_coeffs, &s_dsp.hpf_state[0], l);
        r = biquad_process(&s_dsp.hpf_coeffs, &s_dsp.hpf_state[1], r);

        /* Preset EQ */
        for (int b = 0; b < DSP_NUM_EQ_BANDS; b++) {
            l = biquad_process(&s_dsp.eq_coeffs[b], &s_dsp.eq_state[b][0], l);
            r = biquad_process(&s_dsp.eq_coeffs[b], &s_dsp.eq_state[b][1], r);
        }

        /* Loudness */
        if (s_dsp.loudness_gain > 0.001f) {
            for (int b = 0; b < DSP_NUM_LOUDNESS_BANDS; b++) {
                l = biquad_process(&s_dsp.loudness_coeffs[b], &s_dsp.loudness_state[b][0], l);
                r = biquad_process(&s_dsp.loudness_coeffs[b], &s_dsp.loudness_state[b][1], r);
            }
        }

        /* Normalizer/DRC (FR-22) */
        if (s_dsp.normalizer_enabled) {
            float norm_peak = fmaxf(fabsf(l), fabsf(r));
            if (norm_peak > s_dsp.normalizer_envelope) {
                s_dsp.normalizer_envelope += s_dsp.normalizer_attack_coeff *
                                            (norm_peak - s_dsp.normalizer_envelope);
            } else {
                s_dsp.normalizer_envelope += s_dsp.normalizer_release_coeff *
                                            (norm_peak - s_dsp.normalizer_envelope);
            }

            if (s_dsp.normalizer_envelope > s_dsp.normalizer_threshold) {
                float over_threshold = s_dsp.normalizer_envelope / s_dsp.normalizer_threshold;
                float compressed = powf(over_threshold, 1.0f - 1.0f / s_dsp.normalizer_ratio);
                s_dsp.normalizer_gain = 1.0f / compressed;
            } else {
                s_dsp.normalizer_gain = 1.0f;
            }

            l *= s_dsp.normalizer_gain * s_dsp.normalizer_makeup_gain;
            r *= s_dsp.normalizer_gain * s_dsp.normalizer_makeup_gain;
        }

        /* Limiter */
        float peak = fmaxf(fabsf(l), fabsf(r));
        if (peak > s_dsp.limiter.envelope) {
            s_dsp.limiter.envelope += s_dsp.limiter_attack_coeff * (peak - s_dsp.limiter.envelope);
        } else {
            s_dsp.limiter.envelope += s_dsp.limiter_release_coeff * (peak - s_dsp.limiter.envelope);
        }

        if (s_dsp.limiter.envelope > s_dsp.limiter_threshold) {
            s_dsp.limiter.gain = s_dsp.limiter_threshold / s_dsp.limiter.envelope;
        } else {
            s_dsp.limiter.gain = 1.0f;
        }

        l *= s_dsp.limiter.gain;
        r *= s_dsp.limiter.gain;

        /* Volume Trim (FR-24) */
        l *= s_dsp.volume_gain;
        r *= s_dsp.volume_gain;

        /* Audio Duck (FR-21) */
        l *= s_dsp.audio_duck_gain;
        r *= s_dsp.audio_duck_gain;

        /* Apply mute */
        l *= s_dsp.mute_gain;
        r *= s_dsp.mute_gain;

        left[i] = fmaxf(-1.0f, fminf(1.0f, l));
        right[i] = fmaxf(-1.0f, fminf(1.0f, r));
    }
}
