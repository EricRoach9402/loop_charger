/**
 * @file ups_alarm_manager.h
 * @brief Alarm Manager – owns all business behaviour for UPS alarm events.
 *
 * Responsibility boundary
 * ────────────────────────
 *  alarm_engine.c decides WHETHER an alarm is triggered or cleared.
 *  This Manager decides WHAT TO DO about it: logging, severity mapping,
 *  CMOS publish, MQTT, DB write, rate limiting, escalation, etc.
 *
 *  alarm_engine.c knows nothing about this file.  This file knows about
 *  alarm_engine.h (to receive events) and about the rest of the project
 *  (log.h, cmos.h, etc.) to act on them.  This is where project-specific
 *  complexity is expected to grow; the engine stays untouched as that
 *  happens.
 *
 * Adding a new action (e.g. CMOS publish on trigger)
 * ────────────────────────────────────────────────────
 *  Edit ups_alarm_manager_handle_event() only.  No changes to alarm_engine.*
 *  or to ups_alarm_table.c are needed.
 */

#ifndef UPS_ALARM_MANAGER_H
#define UPS_ALARM_MANAGER_H

#include "alarm_engine.h"
#include "config_loader.h"

/**
 * @brief Receive one alarm event and decide what action to take.
 *
 * This is the function wired into alarm_engine_ctx_t.event_fn.  It must stay
 * fast and non-blocking, since it is called from the polling thread.
 *
 * @param entry     The alarm table entry that produced the event.
 * @param value     Register value at the time of evaluation.
 * @param event     ALARM_EVENT_TRIGGER or ALARM_EVENT_CLEAR.
 * @param userdata  Passed verbatim from alarm_engine_ctx_t.event_data;
 *                  expected to be a const module_config_t * for this project.
 */
void ups_alarm_manager_handle_event(const alarm_entry_t *entry,
                                    uint16_t              value,
                                    alarm_event_t         event,
                                    void                 *userdata);

/**
 * @brief Close the SQLite log database.
 *
 * Must be called once during application shutdown to flush the WAL journal
 * and release the database connection.  Safe to call even if log_init()
 * was never triggered (i.e. no alarm event occurred).
 */
void ups_alarm_manager_close(void);

#endif /* UPS_ALARM_MANAGER_H */
