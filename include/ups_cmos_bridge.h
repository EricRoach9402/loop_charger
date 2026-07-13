/**
 * @file ups_cmos_bridge.h
 * @brief CMOS subscriber bridge for UPS Modbus write commands and pool reads.
 *
 * Responsibilities
 * ────────────────
 *  - Spawns a dedicated CMOS subscriber thread (cmos_sub_spin_ctx is blocking,
 *    so it must never run inside init_callback / process_callback).
 *  - Spawns a publisher poll thread that keeps the read-response publisher alive.
 *  - The HMI only sends cmd + value (it has no notion of uid/addr). Each HMI
 *    command (topic="hmi_ups", type="command", key=<cmd name>) is routed to
 *    its own on_xxx_cmd handler, which owns the register address for that
 *    command, converts value to uint16_t, and delegates the shared
 *    validate/enqueue or validate/read flow to the internal on_write() /
 *    on_read() helpers (see ups_cmos_bridge.c).
 *
 * CMOS message protocol (publisher side)
 * ───────────────────────────────────────
 *  Topic: "hmi_ups", type="command", key=<cmd name> (e.g. "ups_test")
 *    cmos_publish(NULL, "command", "ups_test", "<uint16 value>");
 *
 *  Periodic status push (topic "ups", see cmos_pub_poll_thread):
 *    key=ups_warning_1 / ups_battery_voltage / ... value=<uint16 decimal>
 *
 * Lifecycle
 * ─────────
 *  Call ups_cmos_bridge_start() after start_ups_modules().
 *  Call ups_cmos_bridge_stop()  before stop_ups_modules().
 *
 *  Both functions are called internally by ups_module.c; main.c needs no
 *  changes.
 */

#ifndef UPS_CMOS_BRIDGE_H
#define UPS_CMOS_BRIDGE_H

/**
 * @brief Start the CMOS bridge (subscriber thread + publisher poll thread).
 *
 * Initialises the CMOS publisher for read responses, then spawns both threads.
 *
 * @return 0 on success, -1 on failure.
 */
int ups_cmos_bridge_start(void);

/**
 * @brief Stop the CMOS bridge and release all resources.
 *
 * Cancels the subscriber thread (pthread_cancel – safe because epoll_wait is
 * a cancellation point), joins both threads, and closes the publisher.
 */
void ups_cmos_bridge_stop(void);

#endif /* UPS_CMOS_BRIDGE_H */
