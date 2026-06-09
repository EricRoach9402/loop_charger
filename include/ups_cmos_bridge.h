/**
 * @file ups_cmos_bridge.h
 * @brief CMOS subscriber bridge for UPS Modbus write commands and pool reads.
 *
 * Responsibilities
 * ────────────────
 *  - Spawns a dedicated CMOS subscriber thread (cmos_sub_spin_ctx is blocking,
 *    so it must never run inside init_callback / process_callback).
 *  - Spawns a publisher poll thread that keeps the read-response publisher alive.
 *  - CMOS write callback parses the message, validates register access, and
 *    enqueues the command via ups_cmd_push().
 *  - CMOS read callback reads the cached pool value and publishes the result.
 *
 * CMOS message protocol (publisher side)
 * ───────────────────────────────────────
 *  Topic: "ups"
 *
 *  Write single register:
 *    cmos_publish(NULL, "modbus", "write", "uid=1,addr=0x00DE,val=100");
 *
 *  Read register (result published to topic "ups_resp"):
 *    cmos_publish(NULL, "modbus", "read", "uid=1,addr=0x00D0");
 *
 * Read response (subscribe to topic "ups_resp"):
 *    type=modbus_resp  key=<addr-hex>  value=<uint16 decimal>
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
