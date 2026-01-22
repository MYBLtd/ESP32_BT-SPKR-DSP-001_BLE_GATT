/*
 * DSP Processor Module
 * FSD-DSP-001: DSP Presets + Loudness via BLE GATT
 *
 * Implements:
 * - FR-7: Global DSP headroom (Pre-gain: -6 dB)
 * - FR-8: DSP presets (OFFICE, FULL, NIGHT, SPEECH)
 * - FR-9: Loudness toggle
 * - FR-11: Safety limiter
 * - FR-13: Live parameter updates with smoothing
 * - FR-16: CPU budget (efficient IIR biquads)
 * - FR-17: No dynamic allocations in audio callback
 *
 * Author: Robin Kluit
 * Date: 2026-01-20
 */

#ifndef DSP_PROCESSOR_H
#define DSP_PROCESSOR_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * DSP Presets (FR-8)
 */
typedef enum {
    DSP_PRESET_OFFICE = 0,  /* Office/background - mild EQ */
    DSP_PRESET_FULL   = 1,  /* Full/rich - enhanced bass and treble */
    DSP_PRESET_NIGHT  = 2,  /* Night/evening - balanced for low volume */
    DSP_PRESET_SPEECH = 3,  /* Speech/podcast - voice clarity */
    DSP_PRESET_COUNT  = 4
} dsp_preset_t;

/*
 * DSP Status flags (for BLE status notifications)
 */
typedef struct {
    uint8_t preset;         /* Current preset (0-3) */
    uint8_t loudness;       /* Loudness enabled (0/1) */
    uint8_t flags;          /* Status flags bitfield */
} dsp_status_t;

/* Status flag bits */
#define DSP_FLAG_LIMITER_ACTIVE   (1 << 0)
#define DSP_FLAG_CLIPPING         (1 << 1)
#define DSP_FLAG_THERMAL_WARN     (1 << 2)
#define DSP_FLAG_MUTED            (1 << 3)
#define DSP_FLAG_AUDIO_DUCK       (1 << 4)  /* FR-21: Audio duck (panic) active */
#define DSP_FLAG_NORMALIZER       (1 << 5)  /* FR-22: Normalizer/DRC active */

/*
 * Biquad filter coefficients
 * Transfer function: H(z) = (b0 + b1*z^-1 + b2*z^-2) / (1 + a1*z^-1 + a2*z^-2)
 * Note: a0 is normalized to 1.0
 */
typedef struct {
    float b0, b1, b2;       /* Numerator coefficients */
    float a1, a2;           /* Denominator coefficients (a0 = 1.0) */
} biquad_coeffs_t;

/*
 * Biquad filter state (per channel)
 */
typedef struct {
    float z1, z2;           /* Delay line (Direct Form II Transposed) */
} biquad_state_t;

/*
 * Limiter state
 */
typedef struct {
    float envelope;         /* Current envelope level */
    float gain;             /* Current gain reduction */
} limiter_state_t;

/*
 * Parameter smoothing state
 */
typedef struct {
    float current;          /* Current smoothed value */
    float target;           /* Target value */
    float coeff;            /* Smoothing coefficient */
} smooth_param_t;

/*
 * DSP Configuration (compile-time constants)
 */
#define DSP_SAMPLE_RATE_44100   44100
#define DSP_SAMPLE_RATE_48000   48000
#define DSP_NUM_EQ_BANDS        4       /* Preset EQ bands */
#define DSP_NUM_LOUDNESS_BANDS  2       /* Loudness overlay bands */
#define DSP_SMOOTHING_MS        30      /* Parameter smoothing time (FR-13) */

/* Global defaults (Section 8.1) */
#define DSP_PRE_GAIN_DB         (-6.0f) /* FR-7: Headroom for EQ boosts */
#define DSP_HPF_FREQ_HZ         95.0f   /* High-pass filter cutoff */
#define DSP_HPF_Q               0.707f  /* Butterworth Q */

/* Limiter settings (Section 4.3) */
#define DSP_LIMITER_THRESHOLD_DB (-1.0f)
#define DSP_LIMITER_ATTACK_MS   3.0f
#define DSP_LIMITER_RELEASE_MS  120.0f

/* Audio Duck settings (FR-21) */
#define DSP_AUDIO_DUCK_GAIN_DB  (-12.0f) /* ~25% volume reduction */

/* Normalizer/DRC settings (FR-22) */
#define DSP_NORMALIZER_THRESHOLD_DB (-20.0f)
#define DSP_NORMALIZER_RATIO        4.0f    /* 4:1 compression ratio */
#define DSP_NORMALIZER_ATTACK_MS    7.0f    /* 5-10 ms attack */
#define DSP_NORMALIZER_RELEASE_MS   150.0f  /* 100-200 ms release */
#define DSP_NORMALIZER_MAKEUP_DB    6.0f    /* Makeup gain to compensate */

/*
 * Initialize DSP processor
 * Must be called before processing audio
 *
 * @param sample_rate Initial sample rate (44100 or 48000)
 * @return ESP_OK on success
 */
esp_err_t dsp_init(uint32_t sample_rate);

/*
 * Reconfigure DSP for new sample rate
 * Called when A2DP sample rate changes
 *
 * @param sample_rate New sample rate
 * @return ESP_OK on success
 */
esp_err_t dsp_set_sample_rate(uint32_t sample_rate);

/*
 * Set DSP preset (FR-8)
 * Applies preset EQ with parameter smoothing (FR-13)
 *
 * @param preset Preset to activate
 * @return ESP_OK on success
 */
esp_err_t dsp_set_preset(dsp_preset_t preset);

/*
 * Get current preset
 *
 * @return Current active preset
 */
dsp_preset_t dsp_get_preset(void);

/*
 * Set loudness state (FR-9)
 * Applies loudness overlay EQ with smoothing
 *
 * @param enabled true to enable loudness
 * @return ESP_OK on success
 */
esp_err_t dsp_set_loudness(bool enabled);

/*
 * Get loudness state
 *
 * @return true if loudness is enabled
 */
bool dsp_get_loudness(void);

/*
 * Set mute state
 * Silences audio output when enabled
 *
 * @param muted true to mute, false to unmute
 * @return ESP_OK on success
 */
esp_err_t dsp_set_mute(bool muted);

/*
 * Get mute state
 *
 * @return true if muted
 */
bool dsp_get_mute(void);

/*
 * Set audio duck state (FR-21)
 * Reduces volume to ~25-30% when enabled (panic button)
 *
 * @param enabled true to enable audio duck
 * @return ESP_OK on success
 */
esp_err_t dsp_set_audio_duck(bool enabled);

/*
 * Get audio duck state
 *
 * @return true if audio duck is enabled
 */
bool dsp_get_audio_duck(void);

/*
 * Set normalizer state (FR-22)
 * Enables dynamic range compression when active
 * Makes quiet sounds louder and loud sounds quieter
 *
 * @param enabled true to enable normalizer
 * @return ESP_OK on success
 */
esp_err_t dsp_set_normalizer(bool enabled);

/*
 * Get normalizer state
 *
 * @return true if normalizer is enabled
 */
bool dsp_get_normalizer(void);

/*
 * Get DSP status for BLE notification
 *
 * @param status Pointer to status structure to fill
 */
void dsp_get_status(dsp_status_t *status);

/*
 * Process audio samples through DSP chain
 * This is the main real-time processing function.
 * Called from the audio callback - must be fast and allocation-free (FR-17)
 *
 * Signal chain (Section 4.1):
 * Input -> Pre-gain -> HPF -> Preset EQ -> Loudness EQ -> Limiter -> Output
 *
 * @param samples Interleaved stereo samples (int16_t L,R,L,R,...)
 * @param num_samples Total number of samples (not frames)
 */
void dsp_process(int16_t *samples, uint32_t num_samples);

/*
 * Process audio samples (32-bit float version for internal use)
 *
 * @param left Left channel samples
 * @param right Right channel samples
 * @param num_frames Number of stereo frames
 */
void dsp_process_float(float *left, float *right, uint32_t num_frames);

/*
 * Get preset name string
 *
 * @param preset Preset ID
 * @return Preset name string
 */
const char *dsp_preset_name(dsp_preset_t preset);

#ifdef __cplusplus
}
#endif

#endif /* DSP_PROCESSOR_H */
