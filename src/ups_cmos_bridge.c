/**
 * @file ups_cmos_bridge.c
 * @brief CMOS ↔ UPS Modbus bridge.
 *
 * Two threads are started by ups_cmos_bridge_start():
 *
 *  1. cmos_sub_thread  – runs cmos_sub_spin_ctx() (blocking).
 *       The HMI only sends cmd + value (no uid/addr).  Each HMI command has
 *       its own on_xxx_cmd handler (e.g. on_test_cmd) that owns the register
 *       address, converts value to uint16_t, and delegates the shared
 *       validate/enqueue (or validate/read) flow to on_write().
 *
 *  2. cmos_pub_poll_thread – calls cmos_pub_poll() every 10 ms.
 *       Keeps the read-response publisher alive for new subscriber connections.
 *
 * cmos_sub_spin_ctx() cannot be stopped gracefully (it loops on while(1)).
 * pthread_cancel() is used; epoll_wait() is a POSIX cancellation point so the
 * thread exits cleanly, and the cleanup handler calls cmos_sub_destroy().
 */

#include "ups_cmos_bridge.h"
#include "ups_module.h"
#include "cmos.h"
#include "device_register_map.h"
#include "ups/ups_map.h"
#include "log.h"

#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>

/* ── CMOS connection constants ────────────────────────────────────────── */
#define BRIDGE_MASTER_IP        "127.0.0.1"
#define BRIDGE_MASTER_PORT      10000
#define BRIDGE_PUB_PORT         12000       /* listen port for read responses  */
#define BRIDGE_NODE_NAME        "ups_node"  /* node name for master registration */
#define BRIDGE_SUB_HMI_TOPIC    "hmi_ups"   /* topic for write/read requests   */
#define BRIDGE_PUB_TOPIC        "ups"       /* topic for read responses        */
#define BRIDGE_PUB_POLL_US      500000u     /* 500 ms poll interval             */

/* this project only supports one UPS module, so uid is always fixed */
#define UPS_BRIDGE_DEFAULT_UID  1u

/* ── Module-level state ───────────────────────────────────────────────── */
static pthread_t        g_sub_thread;
static pthread_t        g_pub_poll_thread;
static volatile int     g_bridge_running = 0;

/* ── Static Function Prototypes ─────────────────────────────────────────── */
static int on_write(uint8_t uid, uint16_t addr, uint16_t val);
static void on_test_cmd(const char *topic, const char *value);
static void cleanup_sub_ctx(void *arg);
static void *cmos_sub_thread(void *arg);
static void *cmos_pub_poll_thread(void *arg);
static void publish_pool_register(const module_config_t *cfg,
                                    const char *type,
                                    const char *key,
                                    uint16_t    pool_address);
static void publish_all_pool_register(const module_config_t *cfg);

/* ── Public API ───────────────────────────────────────────────────────── */

int ups_cmos_bridge_start(void)
{
    if (cmos_pub_init(BRIDGE_MASTER_IP, BRIDGE_MASTER_PORT,
                      BRIDGE_NODE_NAME, BRIDGE_PUB_TOPIC,
                      BRIDGE_PUB_PORT) != 0) {
        LOG_ERROR("[CMOS Bridge] publisher init failed "
                  "(master=%s:%d port=%d).",
                  BRIDGE_MASTER_IP, BRIDGE_MASTER_PORT, BRIDGE_PUB_PORT);
        return -1;
    }

    g_bridge_running = 1;

    if (pthread_create(&g_pub_poll_thread, NULL,
                       cmos_pub_poll_thread, global_config.ups) != 0) {
        LOG_ERROR("[CMOS Bridge] failed to start publisher poll thread.");
        g_bridge_running = 0;
        cmos_pub_close();
        return -1;
    }

    if (pthread_create(&g_sub_thread, NULL, cmos_sub_thread, NULL) != 0) {
        LOG_ERROR("[CMOS Bridge] failed to start subscriber thread.");
        g_bridge_running = 0;
        pthread_join(g_pub_poll_thread, NULL);
        cmos_pub_close();
        return -1;
    }

    LOG_INFO("[CMOS Bridge] started (pub port=%d).", BRIDGE_PUB_PORT);
    return 0;
}

void ups_cmos_bridge_stop(void)
{
    g_bridge_running = 0;

    pthread_cancel(g_sub_thread);
    pthread_join(g_sub_thread, NULL);

    pthread_join(g_pub_poll_thread, NULL);

    cmos_pub_close();

    LOG_INFO("[CMOS Bridge] stopped.");
}

/* ── Register access core (shared by every on_xxx_cmd handler) ───────── */

/**
 * @brief Validate and enqueue a single-register write.
 *
 * Looks up the profile for uid, checks that addr is mapped and not
 * ACCESS_RO, then pushes the write command via ups_cmd_push().
 *
 * Every on_xxx_cmd handler should call this instead of repeating the
 * lookup/validate/enqueue sequence itself.  Must stay non-blocking.
 *
 * @return 0 on success, -1 on validation failure or queue-full.
 */
static int on_write(uint8_t uid, uint16_t addr, uint16_t val)
{
    const device_map_profile_t *profile = ups_find_profile_by_uid(uid);
    if (!profile) {
        LOG_WARNING("[CMOS Bridge] write: unknown uid=%u", uid);
        return -1;
    }

    const device_register_mapping_t *entry = device_find_slot(profile, addr);
    if (!entry) {
        LOG_WARNING("[CMOS Bridge] write: addr 0x%04X not mapped in profile "
                    "uid=%u", addr, uid);
        return -1;
    }

    if (entry->access == ACCESS_RO) {
        LOG_WARNING("[CMOS Bridge] write: addr 0x%04X is ACCESS_RO (uid=%u) "
                    "– rejected", addr, uid);
        return -1;
    }

    if (ups_cmd_push(uid, addr, &val, 1, UPS_WRITE_MODE_AUTO) != 0) {
        LOG_ERROR("[CMOS Bridge] write: command queue full for uid=%u "
                  "addr=0x%04X", uid, addr);
        return -1;
    }

    LOG_DEBUG("[CMOS Bridge] write queued: uid=%u addr=0x%04X val=%u",
              uid, addr, val);
    return 0;
}

/* ── CMOS callbacks ───────────────────────────────────────────────────── */

static void on_test_cmd(const char *topic, const char *value)
{
    uint16_t addr = 0x0012;
    uint16_t val  = (uint16_t)strtoul(value, NULL, 0);

    (void)topic;

    LOG_INFO("[CMOS Bridge] test command received: '%s'", value);

    on_write(UPS_BRIDGE_DEFAULT_UID, addr, val);
}
/* ── Thread cleanup handler ───────────────────────────────────────────── */

/**
 * @brief pthread cleanup handler – destroys the subscriber context on cancel.
 */
static void cleanup_sub_ctx(void *arg)
{
    cmos_sub_ctx_t *ctx = (cmos_sub_ctx_t *)arg;
    cmos_sub_destroy(ctx);
    LOG_INFO("[CMOS Bridge] subscriber context destroyed.");
}

/* ── Thread functions ─────────────────────────────────────────────────── */

/**
 * @brief CMOS subscriber thread.
 *
 * Subscribes to BRIDGE_SUB_HMI_TOPIC once per HMI command (type="command",
 * key=<cmd name>), each routed to its own on_xxx_cmd handler, then enters
 * cmos_sub_spin_ctx() which blocks indefinitely.  The thread is stopped via
 * pthread_cancel(); epoll_wait() inside spin_ctx is a cancellation point so
 * the thread exits cleanly.
 */
static void *cmos_sub_thread(void *arg)
{
    (void)arg;

    cmos_sub_ctx_t *ctx = cmos_sub_create(BRIDGE_MASTER_IP,
                                           BRIDGE_MASTER_PORT,
                                           "ups_sub");
    if (!ctx) {
        LOG_ERROR("[CMOS Bridge] failed to create subscriber context.");
        return NULL;
    }

    pthread_cleanup_push(cleanup_sub_ctx, ctx);

    /*
    cmos_sub_add(ctx, BRIDGE_SUB_HMI_TOPIC, NULL, "command", "ups_test",  on_test_cmd);
    cmos_sub_add(ctx, BRIDGE_SUB_HMI_TOPIC, NULL, "command", "ups_shutdown",  on_shutdown_cmd);
    cmos_sub_add(ctx, BRIDGE_SUB_HMI_TOPIC, NULL, "command", "on_time",  on_time_cmd);
    cmos_sub_add(ctx, BRIDGE_SUB_HMI_TOPIC, NULL, "command", "off_time",  on_off_time_cmd);
    cmos_sub_add(ctx, BRIDGE_SUB_HMI_TOPIC, NULL, "command", "trigger",  on_trigger_cmd);
    */

    LOG_INFO("[CMOS Bridge] subscriber thread ready, "
             "topic='%s' (write + read).", BRIDGE_SUB_HMI_TOPIC);

    cmos_sub_spin_ctx(ctx);  /* blocks – exited only by pthread_cancel() */

    pthread_cleanup_pop(1);
    return NULL;
}

/**
 * @brief Read one pool register and publish its value to BRIDGE_PUB_TOPIC.
 *
 * Converts the cached uint16_t pool value to a decimal string and forwards
 * it to cmos_publish().  Intended for periodic push from cmos_pub_poll_thread.
 *
 * @param type  CMOS message type field.
 * @param key   CMOS message key field (e.g. register address string).
 * @param pool_address  Absolute index into internal_pool[].
 */
static void publish_pool_register(const module_config_t *cfg,
                                  const char *type,
                                  const char *key,
                                  uint16_t    pool_address)
{
    uint16_t val = 0;

    const char *state = (cfg->connection_state == CONNECTION_CONNECTED)
                        ? "Alive"
                        : "Disconnect";

    if (!pool_read_register(pool_address, &val)) {
        LOG_WARNING("[CMOS Bridge] publish_pool_register: "
                    "pool_address 0x%04X out of range.", pool_address);
        return;
    }

    char val_str[8];
    snprintf(val_str, sizeof(val_str), "%u", val);

    cmos_publish(state, type, key, val_str);
}

/**
 * @brief Publish every mapped register of one unit's profile to CMOS.
 *
 * Drives the periodic status push directly from the unit's
 * device_map_profile_t table (see devices/ups/ups_map.c) instead of a
 * hand-written per-register list: for every row, table[i].description is
 * used as the CMOS key and table[i].pool_address selects the value.
 *
 * @param cfg  Module configuration for the unit being published (selects
 *             the profile via cfg->modbus_uid and the Alive/Disconnect
 *             state via cfg->connection_state).
 */
static void publish_all_pool_register(const module_config_t *cfg)
{
    if (!cfg || !cfg->enabled) {
        return;
    }

    const device_map_profile_t *profile =
        ups_find_profile_by_uid((uint8_t)cfg->modbus_uid);
    if (!profile) {
        LOG_WARNING("[CMOS Bridge] publish_all_pool_register: "
                    "profile not found for uid=%d.", cfg->modbus_uid);
        return;
    }

    for (size_t i = 0; i < profile->table_count; i++) {
        publish_pool_register(cfg, NULL,
                              profile->table[i].description,
                              profile->table[i].pool_address);
    }
}
/**
 * @brief Publisher poll thread.
 *
 * cmos_pub_poll() is non-blocking (epoll_wait timeout=0) so calling it at
 * 10 ms intervals is sufficient to accept new read-response subscribers.
 * Exits when g_bridge_running is cleared by ups_cmos_bridge_stop().
 */
static void *cmos_pub_poll_thread(void *arg)
{
    module_config_t *ups = (module_config_t *)arg;

    LOG_INFO("[CMOS Bridge] publisher poll thread started, "
             "resp topic='%s'.", BRIDGE_PUB_TOPIC);

    while (g_bridge_running) {
        cmos_pub_poll();
        for (int i = 0; i < global_config.ups_count; i++) {
            publish_all_pool_register(&ups[i]);
        }

        usleep(BRIDGE_PUB_POLL_US);
    }

    return NULL;
}