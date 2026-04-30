/*
 * @file    roc_detector.c
 * @brief   Rate-of-Change fault detector implementation.
 * @author  Claude Code (AI) / Niklas Grill
 * @date    2026-04-30
 */

#include "roc_detector.h"

bool roc_update(roc_detector_t *d, const roc_params_t *p, int32_t new_value)
{
    if (!d->initialized) {
        d->last_value  = new_value;
        d->initialized = true;
        return false;
    }

    int32_t delta = new_value - d->last_value;
    if (delta < 0) delta = -delta;

    if (delta > p->delta_threshold) {
        d->recover_count = 0;
        if (d->fault_count < p->fault_confirm_n) {
            d->fault_count++;
        }
        if (d->fault_count >= p->fault_confirm_n) {
            d->fault_active = true;
        }
    } else {
        d->fault_count = 0;
        if (d->fault_active) {
            if (++d->recover_count >= p->recover_confirm_n) {
                d->fault_active  = false;
                d->recover_count = 0;
            }
        }
    }

    d->last_value = new_value;
    return d->fault_active;
}

void roc_reset(roc_detector_t *d)
{
    d->last_value    = 0;
    d->fault_count   = 0;
    d->recover_count = 0;
    d->fault_active  = false;
    d->initialized   = false;
}
