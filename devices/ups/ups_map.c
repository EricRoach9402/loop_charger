/**
 * @file ups_map.c
 * @brief UPS register mapping tables – one independent table per physical unit.
 *
 * Table format:  { device_address, pool_address, access, bit_mask, description }
 *
 *  device_address  – FC03 register address on the hardware.
 *  pool_address    – Absolute position in internal_pool[].  Unique per unit;
 *                    derived from the unit's pool base + its sequential offset.
 *  access          – ACCESS_RO / ACCESS_RW / ACCESS_WO for external requests.
 *  bit_mask        – Applied after read; 0xFFFF = pass-through.
 *
 * Pool regions
 * ────────────
 *  UPS#1  (modbus_uid = 1):  0x0000 – 0x0034  (53 registers)
 *  UPS#2  (modbus_uid = 2):  0x0035 – 0x0069  (53 registers)
 *  UPS#3  (modbus_uid = 3):  0x006A – 0x009E  (53 registers)
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
 *  device addr   pool addr    access     mask    description
 * ═══════════════════════════════════════════════════════════════════════════ */
static const device_register_mapping_t ups1_mapping_table[] = {
    { 0x0000,  0x0000,  ACCESS_RO,  0xFFFF,  "UPS1_Warning_Information_1"         },
    { 0x0001,  0x0001,  ACCESS_RO,  0xFFFF,  "UPS1_Warning_Information_2"         },
    { 0x0002,  0x0002,  ACCESS_RO,  0xFFFF,  "UPS1_Warning_Information_3"         },
    { 0x0003,  0x0003,  ACCESS_RO,  0xFFFF,  "UPS1_Warning_Information_4"         },

    { 0x00BC,  0x0004,  ACCESS_RO,  0xFFFF,  "UPS1_P_Battery_Voltage"             },
    { 0x00BD,  0x0005,  ACCESS_RO,  0xFFFF,  "UPS1_P_Battery_Discharging_Current" },
    { 0x00BE,  0x0006,  ACCESS_RO,  0xFFFF,  "UPS1_P_Battery_Charging_Current"    },
    { 0x00BF,  0x0007,  ACCESS_RO,  0xFFFF,  "UPS1_Battery_Capacity"              },
    { 0x00C0,  0x0008,  ACCESS_RO,  0xFFFF,  "UPS1_Battery_Remain_Time"           },
    { 0x00C1,  0x0009,  ACCESS_RO,  0xFFFF,  "UPS1_N_Battery_Voltage"             },
    { 0x00C2,  0x000A,  ACCESS_RO,  0xFFFF,  "UPS1_N_Battery_Discharging_Current" },
    { 0x00C3,  0x000B,  ACCESS_RO,  0xFFFF,  "UPS1_N_Battery_Charging_Current"    },
    { 0x00CB,  0x000C,  ACCESS_RO,  0xFFFF,  "UPS1_Battery_Temperature"           },

    { 0x00CC,  0x000D,  ACCESS_RO,  0xFFFF,  "UPS1_Temperature_PFC"               },
    { 0x00CD,  0x000E,  ACCESS_RO,  0xFFFF,  "UPS1_Temperature_INV"               },
    { 0x00CE,  0x000F,  ACCESS_RO,  0xFFFF,  "UPS1_Temperature_Bypass"            },
    { 0x00CF,  0x0010,  ACCESS_RO,  0xFFFF,  "UPS1_Max_Temperature"               },

    { 0x00D0,  0x0011,  ACCESS_RO,  0xFFFF,  "UPS1_Mode_Information"              },
    { 0x00DE,  0x0012,  ACCESS_RW,  0xFFFF,  "UPS1_OT_Fault_Recovery_Value"       },
    { 0x00DF,  0x0013,  ACCESS_RW,  0xFFFF,  "UPS1_OT_Warning_Trigger_Value"      },
    { 0x00E0,  0x0014,  ACCESS_RW,  0xFFFF,  "UPS1_OT_Warning_Recovery_Value"     },

    { 0x02A2,  0x0015,  ACCESS_RO,  0xFFFF,  "UPS1_Fault_Information"             },

    { 0x0364,  0x0016,  ACCESS_RW,  0xFFFF,  "UPS1_Battery_Shutdown_Voltage"      },
    { 0x036A,  0x0017,  ACCESS_RW,  0xFFFF,  "UPS1_Battery_Low_Voltage"           },

    { 0x03E0,  0x0018,  ACCESS_RO,  0xFFFF,  "UPS1_Protocol_ID_Inquiry"           },
    { 0x03E1,  0x0019,  ACCESS_RO,  0xFFFF,  "UPS1_PFC_FW_Version_1"              },
    { 0x03E2,  0x001A,  ACCESS_RO,  0xFFFF,  "UPS1_PFC_FW_Version_2"              },
    { 0x03E3,  0x001B,  ACCESS_RO,  0xFFFF,  "UPS1_PFC_FW_Version_3"              },
    { 0x03E4,  0x001C,  ACCESS_RO,  0xFFFF,  "UPS1_PFC_FW_Version_4"              },
    { 0x03E5,  0x001D,  ACCESS_RO,  0xFFFF,  "UPS1_PFC_FW_Version_5"              },
    { 0x03E6,  0x001E,  ACCESS_RO,  0xFFFF,  "UPS1_INV_FW_Version_1"              },
    { 0x03E7,  0x001F,  ACCESS_RO,  0xFFFF,  "UPS1_INV_FW_Version_2"              },
    { 0x03E8,  0x0020,  ACCESS_RO,  0xFFFF,  "UPS1_INV_FW_Version_3"              },
    { 0x03E9,  0x0021,  ACCESS_RO,  0xFFFF,  "UPS1_INV_FW_Version_4"              },
    { 0x03EA,  0x0022,  ACCESS_RO,  0xFFFF,  "UPS1_INV_FW_Version_5"              },
    { 0x0403,  0x0023,  ACCESS_RO,  0xFFFF,  "UPS1_COM_FW_Version_1"              },
    { 0x0404,  0x0024,  ACCESS_RO,  0xFFFF,  "UPS1_COM_FW_Version_2"              },
    { 0x0405,  0x0025,  ACCESS_RO,  0xFFFF,  "UPS1_COM_FW_Version_3"              },
    { 0x0406,  0x0026,  ACCESS_RO,  0xFFFF,  "UPS1_COM_FW_Version_4"              },
    { 0x0407,  0x0027,  ACCESS_RO,  0xFFFF,  "UPS1_COM_FW_Version_5"              },
    { 0x0408,  0x0028,  ACCESS_RO,  0xFFFF,  "UPS1_LCD_FW_Version_1"              },
    { 0x0409,  0x0029,  ACCESS_RO,  0xFFFF,  "UPS1_LCD_FW_Version_2"              },
    { 0x040A,  0x002A,  ACCESS_RO,  0xFFFF,  "UPS1_LCD_FW_Version_3"              },
    { 0x040B,  0x002B,  ACCESS_RO,  0xFFFF,  "UPS1_LCD_FW_Version_4"              },
    { 0x040C,  0x002C,  ACCESS_RO,  0xFFFF,  "UPS1_LCD_FW_Version_5"              },
    { 0x0412,  0x002D,  ACCESS_RO,  0xFFFF,  "UPS1_FBPN_1"                        },
    { 0x0413,  0x002E,  ACCESS_RO,  0xFFFF,  "UPS1_FBPN_2"                        },
    { 0x0414,  0x002F,  ACCESS_RO,  0xFFFF,  "UPS1_FBPN_3"                        },
    { 0x0415,  0x0030,  ACCESS_RO,  0xFFFF,  "UPS1_FBPN_4"                        },
    { 0x0416,  0x0031,  ACCESS_RO,  0xFFFF,  "UPS1_FBPN_5"                        },
    { 0x04B8,  0x0032,  ACCESS_RO,  0xFFFF,  "UPS1_Modbus_FW_Version_1"           },
    { 0x04B9,  0x0033,  ACCESS_RO,  0xFFFF,  "UPS1_Modbus_FW_Version_2"           },
    { 0x05B0,  0x0034,  ACCESS_RW,  0xFFFF,  "UPS1_Battery_High_Voltage"          },
};

const device_map_profile_t ups1_profile = {
    .name        = "UPS1",
    .table       = ups1_mapping_table,
    .table_count = ARRAY_SIZE(ups1_mapping_table),
    .read_chunk  = 50,
};

/* ═══════════════════════════════════════════════════════════════════════════
 * UPS#2 – modbus_uid 2 – pool base 0x0035
 *
 *  device addr   pool addr    access     mask    description
 * ═══════════════════════════════════════════════════════════════════════════ */
static const device_register_mapping_t ups2_mapping_table[] = {
    { 0x0000,  0x0035,  ACCESS_RO,  0xFFFF,  "UPS2_Warning_Information_1"         },
    { 0x0001,  0x0036,  ACCESS_RO,  0xFFFF,  "UPS2_Warning_Information_2"         },
    { 0x0002,  0x0037,  ACCESS_RO,  0xFFFF,  "UPS2_Warning_Information_3"         },
    { 0x0003,  0x0038,  ACCESS_RO,  0xFFFF,  "UPS2_Warning_Information_4"         },

    { 0x00BC,  0x0039,  ACCESS_RO,  0xFFFF,  "UPS2_P_Battery_Voltage"             },
    { 0x00BD,  0x003A,  ACCESS_RO,  0xFFFF,  "UPS2_P_Battery_Discharging_Current" },
    { 0x00BE,  0x003B,  ACCESS_RO,  0xFFFF,  "UPS2_P_Battery_Charging_Current"    },
    { 0x00BF,  0x003C,  ACCESS_RO,  0xFFFF,  "UPS2_Battery_Capacity"              },
    { 0x00C0,  0x003D,  ACCESS_RO,  0xFFFF,  "UPS2_Battery_Remain_Time"           },
    { 0x00C1,  0x003E,  ACCESS_RO,  0xFFFF,  "UPS2_N_Battery_Voltage"             },
    { 0x00C2,  0x003F,  ACCESS_RO,  0xFFFF,  "UPS2_N_Battery_Discharging_Current" },
    { 0x00C3,  0x0040,  ACCESS_RO,  0xFFFF,  "UPS2_N_Battery_Charging_Current"    },
    { 0x00CB,  0x0041,  ACCESS_RO,  0xFFFF,  "UPS2_Battery_Temperature"           },

    { 0x00CC,  0x0042,  ACCESS_RO,  0xFFFF,  "UPS2_Temperature_PFC"               },
    { 0x00CD,  0x0043,  ACCESS_RO,  0xFFFF,  "UPS2_Temperature_INV"               },
    { 0x00CE,  0x0044,  ACCESS_RO,  0xFFFF,  "UPS2_Temperature_Bypass"            },
    { 0x00CF,  0x0045,  ACCESS_RO,  0xFFFF,  "UPS2_Max_Temperature"               },

    { 0x00D0,  0x0046,  ACCESS_RO,  0xFFFF,  "UPS2_Mode_Information"              },
    { 0x00DE,  0x0047,  ACCESS_RW,  0xFFFF,  "UPS2_OT_Fault_Recovery_Value"       },
    { 0x00DF,  0x0048,  ACCESS_RW,  0xFFFF,  "UPS2_OT_Warning_Trigger_Value"      },
    { 0x00E0,  0x0049,  ACCESS_RW,  0xFFFF,  "UPS2_OT_Warning_Recovery_Value"     },

    { 0x02A2,  0x004A,  ACCESS_RO,  0xFFFF,  "UPS2_Fault_Information"             },

    { 0x0364,  0x004B,  ACCESS_RW,  0xFFFF,  "UPS2_Battery_Shutdown_Voltage"      },
    { 0x036A,  0x004C,  ACCESS_RW,  0xFFFF,  "UPS2_Battery_Low_Voltage"           },

    { 0x03E0,  0x004D,  ACCESS_RO,  0xFFFF,  "UPS2_Protocol_ID_Inquiry"           },
    { 0x03E1,  0x004E,  ACCESS_RO,  0xFFFF,  "UPS2_PFC_FW_Version_1"              },
    { 0x03E2,  0x004F,  ACCESS_RO,  0xFFFF,  "UPS2_PFC_FW_Version_2"              },
    { 0x03E3,  0x0050,  ACCESS_RO,  0xFFFF,  "UPS2_PFC_FW_Version_3"              },
    { 0x03E4,  0x0051,  ACCESS_RO,  0xFFFF,  "UPS2_PFC_FW_Version_4"              },
    { 0x03E5,  0x0052,  ACCESS_RO,  0xFFFF,  "UPS2_PFC_FW_Version_5"              },
    { 0x03E6,  0x0053,  ACCESS_RO,  0xFFFF,  "UPS2_INV_FW_Version_1"              },
    { 0x03E7,  0x0054,  ACCESS_RO,  0xFFFF,  "UPS2_INV_FW_Version_2"              },
    { 0x03E8,  0x0055,  ACCESS_RO,  0xFFFF,  "UPS2_INV_FW_Version_3"              },
    { 0x03E9,  0x0056,  ACCESS_RO,  0xFFFF,  "UPS2_INV_FW_Version_4"              },
    { 0x03EA,  0x0057,  ACCESS_RO,  0xFFFF,  "UPS2_INV_FW_Version_5"              },
    { 0x0403,  0x0058,  ACCESS_RO,  0xFFFF,  "UPS2_COM_FW_Version_1"              },
    { 0x0404,  0x0059,  ACCESS_RO,  0xFFFF,  "UPS2_COM_FW_Version_2"              },
    { 0x0405,  0x005A,  ACCESS_RO,  0xFFFF,  "UPS2_COM_FW_Version_3"              },
    { 0x0406,  0x005B,  ACCESS_RO,  0xFFFF,  "UPS2_COM_FW_Version_4"              },
    { 0x0407,  0x005C,  ACCESS_RO,  0xFFFF,  "UPS2_COM_FW_Version_5"              },
    { 0x0408,  0x005D,  ACCESS_RO,  0xFFFF,  "UPS2_LCD_FW_Version_1"              },
    { 0x0409,  0x005E,  ACCESS_RO,  0xFFFF,  "UPS2_LCD_FW_Version_2"              },
    { 0x040A,  0x005F,  ACCESS_RO,  0xFFFF,  "UPS2_LCD_FW_Version_3"              },
    { 0x040B,  0x0060,  ACCESS_RO,  0xFFFF,  "UPS2_LCD_FW_Version_4"              },
    { 0x040C,  0x0061,  ACCESS_RO,  0xFFFF,  "UPS2_LCD_FW_Version_5"              },
    { 0x0412,  0x0062,  ACCESS_RO,  0xFFFF,  "UPS2_FBPN_1"                        },
    { 0x0413,  0x0063,  ACCESS_RO,  0xFFFF,  "UPS2_FBPN_2"                        },
    { 0x0414,  0x0064,  ACCESS_RO,  0xFFFF,  "UPS2_FBPN_3"                        },
    { 0x0415,  0x0065,  ACCESS_RO,  0xFFFF,  "UPS2_FBPN_4"                        },
    { 0x0416,  0x0066,  ACCESS_RO,  0xFFFF,  "UPS2_FBPN_5"                        },
    { 0x04B8,  0x0067,  ACCESS_RO,  0xFFFF,  "UPS2_Modbus_FW_Version_1"           },
    { 0x04B9,  0x0068,  ACCESS_RO,  0xFFFF,  "UPS2_Modbus_FW_Version_2"           },
    { 0x05B0,  0x0069,  ACCESS_RW,  0xFFFF,  "UPS2_Battery_High_Voltage"          },
};

const device_map_profile_t ups2_profile = {
    .name        = "UPS2",
    .table       = ups2_mapping_table,
    .table_count = ARRAY_SIZE(ups2_mapping_table),
    .read_chunk  = 50,
};

/* ═══════════════════════════════════════════════════════════════════════════
 * UPS#3 – modbus_uid 3 – pool base 0x006A
 *
 *  device addr   pool addr    access     mask    description
 * ═══════════════════════════════════════════════════════════════════════════ */
static const device_register_mapping_t ups3_mapping_table[] = {
    { 0x0000,  0x006A,  ACCESS_RO,  0xFFFF,  "UPS3_Warning_Information_1"         },
    { 0x0001,  0x006B,  ACCESS_RO,  0xFFFF,  "UPS3_Warning_Information_2"         },
    { 0x0002,  0x006C,  ACCESS_RO,  0xFFFF,  "UPS3_Warning_Information_3"         },
    { 0x0003,  0x006D,  ACCESS_RO,  0xFFFF,  "UPS3_Warning_Information_4"         },

    { 0x00BC,  0x006E,  ACCESS_RO,  0xFFFF,  "UPS3_P_Battery_Voltage"             },
    { 0x00BD,  0x006F,  ACCESS_RO,  0xFFFF,  "UPS3_P_Battery_Discharging_Current" },
    { 0x00BE,  0x0070,  ACCESS_RO,  0xFFFF,  "UPS3_P_Battery_Charging_Current"    },
    { 0x00BF,  0x0071,  ACCESS_RO,  0xFFFF,  "UPS3_Battery_Capacity"              },
    { 0x00C0,  0x0072,  ACCESS_RO,  0xFFFF,  "UPS3_Battery_Remain_Time"           },
    { 0x00C1,  0x0073,  ACCESS_RO,  0xFFFF,  "UPS3_N_Battery_Voltage"             },
    { 0x00C2,  0x0074,  ACCESS_RO,  0xFFFF,  "UPS3_N_Battery_Discharging_Current" },
    { 0x00C3,  0x0075,  ACCESS_RO,  0xFFFF,  "UPS3_N_Battery_Charging_Current"    },
    { 0x00CB,  0x0076,  ACCESS_RO,  0xFFFF,  "UPS3_Battery_Temperature"           },

    { 0x00CC,  0x0077,  ACCESS_RO,  0xFFFF,  "UPS3_Temperature_PFC"               },
    { 0x00CD,  0x0078,  ACCESS_RO,  0xFFFF,  "UPS3_Temperature_INV"               },
    { 0x00CE,  0x0079,  ACCESS_RO,  0xFFFF,  "UPS3_Temperature_Bypass"            },
    { 0x00CF,  0x007A,  ACCESS_RO,  0xFFFF,  "UPS3_Max_Temperature"               },

    { 0x00D0,  0x007B,  ACCESS_RO,  0xFFFF,  "UPS3_Mode_Information"              },
    { 0x00DE,  0x007C,  ACCESS_RW,  0xFFFF,  "UPS3_OT_Fault_Recovery_Value"       },
    { 0x00DF,  0x007D,  ACCESS_RW,  0xFFFF,  "UPS3_OT_Warning_Trigger_Value"      },
    { 0x00E0,  0x007E,  ACCESS_RW,  0xFFFF,  "UPS3_OT_Warning_Recovery_Value"     },

    { 0x02A2,  0x007F,  ACCESS_RO,  0xFFFF,  "UPS3_Fault_Information"             },

    { 0x0364,  0x0080,  ACCESS_RW,  0xFFFF,  "UPS3_Battery_Shutdown_Voltage"      },
    { 0x036A,  0x0081,  ACCESS_RW,  0xFFFF,  "UPS3_Battery_Low_Voltage"           },

    { 0x03E0,  0x0082,  ACCESS_RO,  0xFFFF,  "UPS3_Protocol_ID_Inquiry"           },
    { 0x03E1,  0x0083,  ACCESS_RO,  0xFFFF,  "UPS3_PFC_FW_Version_1"              },
    { 0x03E2,  0x0084,  ACCESS_RO,  0xFFFF,  "UPS3_PFC_FW_Version_2"              },
    { 0x03E3,  0x0085,  ACCESS_RO,  0xFFFF,  "UPS3_PFC_FW_Version_3"              },
    { 0x03E4,  0x0086,  ACCESS_RO,  0xFFFF,  "UPS3_PFC_FW_Version_4"              },
    { 0x03E5,  0x0087,  ACCESS_RO,  0xFFFF,  "UPS3_PFC_FW_Version_5"              },
    { 0x03E6,  0x0088,  ACCESS_RO,  0xFFFF,  "UPS3_INV_FW_Version_1"              },
    { 0x03E7,  0x0089,  ACCESS_RO,  0xFFFF,  "UPS3_INV_FW_Version_2"              },
    { 0x03E8,  0x008A,  ACCESS_RO,  0xFFFF,  "UPS3_INV_FW_Version_3"              },
    { 0x03E9,  0x008B,  ACCESS_RO,  0xFFFF,  "UPS3_INV_FW_Version_4"              },
    { 0x03EA,  0x008C,  ACCESS_RO,  0xFFFF,  "UPS3_INV_FW_Version_5"              },
    { 0x0403,  0x008D,  ACCESS_RO,  0xFFFF,  "UPS3_COM_FW_Version_1"              },
    { 0x0404,  0x008E,  ACCESS_RO,  0xFFFF,  "UPS3_COM_FW_Version_2"              },
    { 0x0405,  0x008F,  ACCESS_RO,  0xFFFF,  "UPS3_COM_FW_Version_3"              },
    { 0x0406,  0x0090,  ACCESS_RO,  0xFFFF,  "UPS3_COM_FW_Version_4"              },
    { 0x0407,  0x0091,  ACCESS_RO,  0xFFFF,  "UPS3_COM_FW_Version_5"              },
    { 0x0408,  0x0092,  ACCESS_RO,  0xFFFF,  "UPS3_LCD_FW_Version_1"              },
    { 0x0409,  0x0093,  ACCESS_RO,  0xFFFF,  "UPS3_LCD_FW_Version_2"              },
    { 0x040A,  0x0094,  ACCESS_RO,  0xFFFF,  "UPS3_LCD_FW_Version_3"              },
    { 0x040B,  0x0095,  ACCESS_RO,  0xFFFF,  "UPS3_LCD_FW_Version_4"              },
    { 0x040C,  0x0096,  ACCESS_RO,  0xFFFF,  "UPS3_LCD_FW_Version_5"              },
    { 0x0412,  0x0097,  ACCESS_RO,  0xFFFF,  "UPS3_FBPN_1"                        },
    { 0x0413,  0x0098,  ACCESS_RO,  0xFFFF,  "UPS3_FBPN_2"                        },
    { 0x0414,  0x0099,  ACCESS_RO,  0xFFFF,  "UPS3_FBPN_3"                        },
    { 0x0415,  0x009A,  ACCESS_RO,  0xFFFF,  "UPS3_FBPN_4"                        },
    { 0x0416,  0x009B,  ACCESS_RO,  0xFFFF,  "UPS3_FBPN_5"                        },
    { 0x04B8,  0x009C,  ACCESS_RO,  0xFFFF,  "UPS3_Modbus_FW_Version_1"           },
    { 0x04B9,  0x009D,  ACCESS_RO,  0xFFFF,  "UPS3_Modbus_FW_Version_2"           },
    { 0x05B0,  0x009E,  ACCESS_RW,  0xFFFF,  "UPS3_Battery_High_Voltage"          },
};

const device_map_profile_t ups3_profile = {
    .name        = "UPS3",
    .table       = ups3_mapping_table,
    .table_count = ARRAY_SIZE(ups3_mapping_table),
    .read_chunk  = 50,
};

/* ═══════════════════════════════════════════════════════════════════════════
 * UID → profile registry
 *
 * Add one entry here when a new UPS unit is commissioned.
 * ups_module.c requires no modification.
 * ═══════════════════════════════════════════════════════════════════════════ */
static const ups_uid_profile_entry_t ups_uid_profile_map[] = {
    { 1, &ups1_profile },
    { 2, &ups2_profile },
    { 3, &ups3_profile },
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
