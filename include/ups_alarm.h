/**
 * @file ups_alarm.h
 * @brief UPS alarm table definitions and alarm bridge registration.
 *
 * Responsibility
 * ──────────────
 *  This file owns: the alarm tables (what to monitor) and the wiring
 *  that connects them to alarm_bridge (how they get evaluated).
 *
 *  It does NOT own: what happens when an alarm fires.  That is
 *  ups_alarm_manager.c's job.
 *
 * Error codes
 * ───────────
 *  Each alarm_entry_t carries a uint16_t error_code, defined below.
 *  Meaning and severity of each code are interpreted by the Alarm Manager.
 *
 * Adding a new alarm condition
 * ────────────────────────────
 *  1. Add an error code constant below (if new).
 *  2. Add one row to the appropriate alarm table in ups_alarm.c.
 *  3. Verify the device_address is present in ups_map.c for that unit.
 *  No changes to alarm_engine.*, alarm_bridge.*, or
 *  ups_alarm_manager.* are needed.
 */

#ifndef UPS_ALARM_H
#define UPS_ALARM_H

/* ── Error codes ──────────────────────────────────────────────────────── */

#define UPS_ERR_WARNING_1         0x0001u  /**< Warning register 1 bit event  */
#define UPS_ERR_WARNING_2         0x0002u  /**< Warning register 2 bit event  */
#define UPS_ERR_BATT_LOW          0x0010u  /**< Battery capacity below limit  */
#define UPS_ERR_BATT_TEMP_HIGH    0x0011u  /**< Battery temperature too high  */
#define UPS_ERR_OUTPUT_VOLT_HIGH  0x0020u  /**< Output voltage out of range   */
#define UPS_ERR_FAULT_CHANGE      0x0030u  /**< Fault register changed        */

/* ── Public API ───────────────────────────────────────────────────────── */

/**
 * @brief Build one alarm_engine_ctx_t per enabled UPS unit and register
 *        each with alarm_bridge via alarm_bridge_register_ctx().
 *
 * Must be called after load_json_config() and before alarm_bridge_start().
 * Units whose modbus_uid has no matching alarm table are skipped with a
 * warning.  Units whose uid has no registered device profile are also
 * skipped with a warning.
 */
void ups_alarm_register_all(void);

#endif /* UPS_ALARM_H */
