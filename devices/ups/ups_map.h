/**
 * @file ups_map.h
 * @brief UPS device register profiles – one profile per physical unit.
 *
 * Each profile corresponds to exactly one UPS unit identified by its
 * modbus_uid in config.json.  Pool addresses are absolute and hardcoded
 * in each unit's mapping table in ups_map.c.
 *
 * Adding a new UPS unit
 * ─────────────────────
 *  1. Add a mapping table in ups_map.c with a unique pool_address range.
 *  2. Declare the new profile below (extern const device_map_profile_t …).
 *  3. Add an entry to the uid→profile lookup table in ups_map.c.
 *
 * Adding a new hardware model
 * ───────────────────────────
 *  Same as above.  Each physical unit still gets its own table and profile
 *  regardless of whether the hardware model is shared with other units.
 */

#ifndef UPS_MAP_H
#define UPS_MAP_H

#include "device_register_map.h"

/** UPS#1 – modbus_uid 1, pool region 0x0000–0x0034. */
extern const device_map_profile_t ups1_profile;

/* ── UID → profile registry ───────────────────────────────────────────── */

/**
 * @brief Maps one modbus_uid to its per-unit register profile.
 *
 * The full table lives in ups_map.c.  All additions of new UPS units are
 * confined to ups_map.c/h; ups_module.c requires no modification.
 */
typedef struct {
    uint8_t                     modbus_uid;
    const device_map_profile_t *profile;
} ups_uid_profile_entry_t;

/**
 * @brief Look up the register profile for a given modbus_uid.
 *
 * @param uid  Modbus unit identifier from config.json.
 * @return Pointer to the matching profile, or NULL if not registered.
 */
const device_map_profile_t *ups_find_profile_by_uid(uint8_t uid);

#endif /* UPS_MAP_H */
