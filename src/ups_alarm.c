/**
 * @file ups_alarm.c
 * @brief UPS alarm tables and alarm bridge registration.
 *
 * Owns: the alarm tables (what to monitor), per-unit runtime state, and
 * the wiring to alarm_bridge (ups_alarm_register_all).
 *
 * Does NOT own alarm behaviour – every event is forwarded verbatim to
 * ups_alarm_manager_handle_event(), which decides what to do.
 *
 * Table columns
 * ─────────────
 *  device_address  FC03 register address (must exist in ups_map.c).
 *  condition       ALARM_COND_BITMASK / ALARM_COND_RANGE / ALARM_COND_CHANGE.
 *  lo_limit        Lower bound (RANGE) or ignored (BITMASK / CHANGE).
 *  hi_limit        Upper bound (RANGE) or bit mask (BITMASK) or ignored (CHANGE).
 *  error_code      Forwarded in the event; meaning defined in ups_alarm.h.
 *  description     Human-readable label; forwarded in the event only.
 *
 * Condition quick reference
 * ─────────────────────────
 *  BITMASK  fires when (value & hi_limit) == hi_limit.
 *           Same register can appear N times for N independent bits.
 *
 *  RANGE    fires when value < lo_limit || value > hi_limit.
 *           Upper bound only: lo_limit = 0.
 *           Lower bound only: hi_limit = 0xFFFF.
 *
 *  CHANGE   fires when value value != prev_value.
 *           No sticky suppression – every change is an independent event.
 */

#include "ups_alarm.h"
#include "ups_alarm_manager.h"
#include "alarm_engine.h"
#include "alarm_bridge.h"
#include "device_register_map.h"
#include "config_loader.h"
#include "ups/ups_map.h"
#include "log.h"

#define ARRAY_SIZE(a)  (sizeof(a) / sizeof((a)[0]))

/* ═══════════════════════════════════════════════════════════════════════
 * Shared alarm table — same hardware model for all UPS units.
 * Per-unit runtime state is kept separate so sticky flags and prev_value
 * are not shared across units.
 *
 *  device_addr  condition            lo      hi      error_code               description
 * ═══════════════════════════════════════════════════════════════════════ */
static const alarm_entry_t ups_alarm_table[] = {
    { 0x0000, ALARM_COND_BITMASK,  0x0000, 0x0100, UPS_ERR_WARNING_1,        "warning reg1 bit 0x0100"      },
    { 0x0000, ALARM_COND_BITMASK,  0x0000, 0x0010, UPS_ERR_WARNING_2,        "warning reg1 bit 0x0010"      },
    { 0x00BF, ALARM_COND_RANGE,    0x0014, 0xFFFF, UPS_ERR_BATT_LOW,         "battery capacity < 20%"       },
    { 0x00CB, ALARM_COND_RANGE,    0x0000, 0x0258, UPS_ERR_BATT_TEMP_HIGH,   "battery temperature high"     },
    { 0x00D0, ALARM_COND_RANGE,    0x00B4, 0x00F0, UPS_ERR_OUTPUT_VOLT_HIGH, "output voltage out of range"  },
    { 0x0001, ALARM_COND_CHANGE,   0x0000, 0x0000, UPS_ERR_FAULT_CHANGE,     "fault register changed"       },
};

/* Per-unit mutable runtime state – one slot set per UPS unit. */
static alarm_state_t ups1_alarm_states[ARRAY_SIZE(ups_alarm_table)];
/* ── UID → alarm state registry ──────────────────────────────────────── */

typedef struct {
    uint8_t       uid;
    alarm_state_t *states;
    size_t         count;
} ups_alarm_registry_t;

static const ups_alarm_registry_t alarm_registry[] = {
    { 1u, ups1_alarm_states, ARRAY_SIZE(ups_alarm_table) },
};

/* Stable ctx storage – lifetime must outlast alarm_bridge_stop(). */
static alarm_engine_ctx_t ups_alarm_ctxs[ARRAY_SIZE(alarm_registry)];
static size_t             ups_alarm_ctx_count = 0u;

/* ── alarm_read_fn ────────────────────────────────────────────────────── */

/**
 * @brief Read one register from the internal pool by device_address.
 * userdata is a const device_map_profile_t *.
 */
static bool alarm_read_register(uint16_t  device_address,
                                uint16_t *out_value,
                                void     *userdata)
{
    const device_map_profile_t *profile = (const device_map_profile_t *)userdata;
    return pool_read_by_device_addr(profile, device_address, out_value);
}

/* ── alarm_event_fn ───────────────────────────────────────────────────── */

/**
 * @brief Thin forwarder – all events go straight to the Alarm Manager.
 */
static void forward_to_manager(const alarm_entry_t *entry,
                               uint16_t              value,
                               alarm_event_t         event,
                               void                 *userdata)
{
    ups_alarm_manager_handle_event(entry, value, event, userdata);
}

/* ── Public API ───────────────────────────────────────────────────────── */

void ups_alarm_register_all(void)
{
    ups_alarm_ctx_count = 0u;

    for (size_t i = 0u; i < ARRAY_SIZE(alarm_registry); i++) {
        uint8_t uid = alarm_registry[i].uid;

        const device_map_profile_t *profile = ups_find_profile_by_uid(uid);
        if (!profile) {
            LOG_WARNING("[Alarm] ups_alarm_register_all: "
                        "no device profile for uid=%u – skipped.", uid);
            continue;
        }

        module_config_t *cfg = NULL;
        for (int j = 0; j < global_config.ups_count; j++) {
            if ((uint8_t)global_config.ups[j].modbus_uid == uid) {
                cfg = &global_config.ups[j];
                break;
            }
        }
        if (!cfg) {
            LOG_WARNING("[Alarm] ups_alarm_register_all: "
                        "no module config for uid=%u – skipped.", uid);
            continue;
        }

        alarm_engine_ctx_t *ctx = &ups_alarm_ctxs[ups_alarm_ctx_count];

        ctx->table      = ups_alarm_table;
        ctx->states     = alarm_registry[i].states;
        ctx->count      = alarm_registry[i].count;
        ctx->read_fn    = alarm_read_register;
        ctx->event_fn   = forward_to_manager;
        ctx->read_data  = (void *)profile;
        ctx->event_data = (void *)cfg;

        if (alarm_bridge_register_ctx(ctx) != 0) {
            LOG_ERROR("[Alarm] ups_alarm_register_all: "
                      "failed to register ctx for uid=%u.", uid);
            continue;
        }

        ups_alarm_ctx_count++;
        LOG_INFO("[Alarm] registered alarm context for uid=%u (%s).",
                 uid, cfg->name);
    }
}
