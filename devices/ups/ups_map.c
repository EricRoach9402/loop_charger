/**
 * @file ups_map.c
 * @brief UPS register mapping tables – one independent table per physical unit.
 *
 * Table format:  { device_address, pool_address, access, description }
 *
 *  device_address  – FC03 register address on the hardware.
 *  pool_address    – Absolute position in internal_pool[].  Unique per unit;
 *                    derived from the unit's pool base + its sequential offset.
 *  access          – ACCESS_RO / ACCESS_RW / ACCESS_WO for external requests.
 *
 * Pool stores raw register values.  No masking is applied here.
 * Consumers needing a masked view use pool_read_masked_by_device_addr().
 *
 * Rules
 * ─────
 *  • Rows MUST be sorted by device_address ascending (binary search).
 *  • pool_address values must be unique across ALL tables in the project.
 *  • Each table is self-contained; adding a unit never touches another table.
 *
 * Adding a new unit of the same hardware model
 * ────────────────────────────────────────────
 *  1. Copy an existing table, choose a new non-overlapping pool base.
 *  2. Update all pool_address values (new_base + sequential_offset).
 *  3. Add a new device_map_profile_t definition.
 *  4. Declare it extern in ups_map.h.
 *  5. Register it in ups_map.c uid→profile lookup table.
 */

#include "ups_map.h"

#define ARRAY_SIZE(a)  (sizeof(a) / sizeof((a)[0]))

/* ═══════════════════════════════════════════════════════════════════════════
 * UPS#1 – modbus_uid 1 – pool base 0x0000
 *
 *  device addr   pool addr    access     description
 * ═══════════════════════════════════════════════════════════════════════════ */
static const device_register_mapping_t ups1_mapping_table[] = {
    { 0x0000,  0xBB00,  ACCESS_RO,  "UPS1_Warning_Information_1"         },
    { 0x0001,  0xBB01,  ACCESS_RO,  "UPS1_Warning_Information_2"         },
    { 0x0002,  0xBB02,  ACCESS_RO,  "UPS1_Warning_Information_3"         },
    { 0x0003,  0xBB03,  ACCESS_RO,  "UPS1_Warning_Information_4"         },
    { 0x0004,  0xBB04,  ACCESS_RO,  "UPS1_P_Battery_Voltage"             },
    { 0x0005,  0xBB05,  ACCESS_RO,  "UPS1_P_Battery_Discharging_Current" },
    { 0x0006,  0xBB06,  ACCESS_RO,  "UPS1_P_Battery_Charging_Current"    },
    { 0x0007,  0xBB07,  ACCESS_RO,  "UPS1_Battery_Capacity"              },
    { 0x0008,  0xBB08,  ACCESS_RO,  "UPS1_Battery_Remain_Time"           },
    { 0x0009,  0xBB09,  ACCESS_RO,  "UPS1_N_Battery_Voltage"             },
    { 0x000A,  0xBB0A,  ACCESS_RO,  "UPS1_N_Battery_Discharging_Current" },
    { 0x000B,  0xBB0B,  ACCESS_RO,  "UPS1_N_Battery_Charging_Current"    },
    { 0x000C,  0xBB0C,  ACCESS_RO,  "UPS1_Battery_Temperature"           },
    { 0x000D,  0xBB0D,  ACCESS_RO,  "UPS1_Temperature_PFC"               },
    { 0x000E,  0xBB0E,  ACCESS_RO,  "UPS1_Temperature_INV"               },
    { 0x000F,  0xBB0F,  ACCESS_RO,  "UPS1_Temperature_Bypass"            },
    { 0x0010,  0xBB10,  ACCESS_RO,  "UPS1_Max_Temperature"               },
    { 0x0011,  0xBB11,  ACCESS_RO,  "UPS1_Mode_Information"              },
    { 0x0012,  0xBB12,  ACCESS_RW,  "UPS1_OT_Fault_Recovery_Value"       },
    { 0x0013,  0xBB13,  ACCESS_RW,  "UPS1_OT_Warning_Trigger_Value"      },
    { 0x0014,  0xBB14,  ACCESS_RW,  "UPS1_OT_Warning_Recovery_Value"     },
    { 0x0015,  0xBB15,  ACCESS_RW,  "UPS1_OT_Warning_Recovery_Value"     },

};

const device_map_profile_t ups1_profile = {
    .name        = "UPS1",
    .table       = ups1_mapping_table,
    .table_count = ARRAY_SIZE(ups1_mapping_table),
    .read_chunk  = 50,
};

/* ═══════════════════════════════════════════════════════════════════════════
 * UID → profile registry
 *
 * Add one entry here when a new UPS unit is commissioned.
 * ups_module.c requires no modification.
 * ═══════════════════════════════════════════════════════════════════════════ */
static const ups_uid_profile_entry_t ups_uid_profile_map[] = {
    { 1, &ups1_profile }
};
#define UPS_UID_PROFILE_COUNT \
    (int)(sizeof(ups_uid_profile_map) / sizeof(ups_uid_profile_map[0]))

const device_map_profile_t *ups_find_profile_by_uid(uint8_t uid)
{
    for (int i = 0; i < UPS_UID_PROFILE_COUNT; i++) {
        if (ups_uid_profile_map[i].modbus_uid == uid) {
            return ups_uid_profile_map[i].profile;
        }
    }
    return NULL;
}
