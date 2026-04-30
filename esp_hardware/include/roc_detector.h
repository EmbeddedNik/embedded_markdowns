/*
 * @file    roc_detector.h
 * @brief   Rate-of-Change (RoC) fault detector for ADC sensors.
 *
 *          Physical sensor readings are bounded by the rate at which the
 *          measured quantity can change.  A disconnected or shorted ADC input
 *          produces rapid, uncorrelated noise that far exceeds this bound.
 *
 *          The detector declares a fault when the absolute per-cycle change
 *          (|x[k] - x[k-1]|) exceeds a plausibility threshold for
 *          fault_confirm_n consecutive cycles.  A separate recover_confirm_n
 *          count (hysteresis) prevents chattering when the signal hovers near
 *          the threshold.
 *
 *          The algorithm is sensor-agnostic: one roc_detector_t instance per
 *          sensor, one roc_params_t per sensor type.  All arithmetic is
 *          integer; no heap allocation; O(1) time and space per update.
 *
 * @author  Claude Code (AI) / Niklas Grill
 * @date    2026-04-30
 */

#ifndef ROC_DETECTOR_H
#define ROC_DETECTOR_H

#include <stdbool.h>
#include <stdint.h>

/* ── Tunable parameters (constant per sensor type) ───────────────── */
typedef struct {
    int32_t delta_threshold;    /* max plausible |x[k] - x[k-1]| per cycle  */
    uint8_t fault_confirm_n;    /* consecutive over-threshold cycles → fault */
    uint8_t recover_confirm_n;  /* consecutive under-threshold cycles → OK   */
} roc_params_t;

/* ── Per-sensor state (zero-initialise at startup) ───────────────── */
typedef struct {
    int32_t last_value;         /* x[k-1]                                    */
    uint8_t fault_count;        /* consecutive over-threshold readings        */
    uint8_t recover_count;      /* consecutive under-threshold readings       */
    bool    fault_active;       /* current fault verdict                      */
    bool    initialized;        /* false until first reading is stored        */
} roc_detector_t;

/*
 * Feed one new sample into the detector.
 * Returns true while a fault is active, false when the signal is healthy.
 * The first call after initialisation or reset stores the value and returns
 * false so there is no spurious fault on startup.
 */
bool roc_update(roc_detector_t *d, const roc_params_t *p, int32_t new_value);

/* Reset detector to initial fault-free state (call after a driver error). */
void roc_reset(roc_detector_t *d);

#endif /* ROC_DETECTOR_H */
