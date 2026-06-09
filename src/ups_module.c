/**
 * @file ups_module.c
 * @brief UPS polling module – Modbus TCP client implementation.
 *
 * Callback design
 * ───────────────
 *  ups_tcp_init_callback    – connect to the UPS TCP server.
 *  ups_tcp_process_callback – drain write queue, then segment-read all
 *                             registers → write to pool.
 *  ups_tcp_error_callback   – disconnect and schedule reconnect.
 *  ups_msg_callback         – execute FC06 / FC16 write on a UPS unit.
 *
 * CMOS bridge integration
 * ───────────────────────
 *  A dedicated CMOS subscriber thread (ups_cmos_bridge.c) receives write /
 *  read commands and enqueues writes via ups_cmd_push().
 *  process_callback drains the queue before each FC03 scan.
 *
 * Thread model
 * ────────────
 *  One pthread per enabled UPS unit.  Each thread calls:
 *    init_callback  → success  → loop: process_callback
 *                   → failure  → retry after UPS_RECONNECT_DELAY_MS
 *
 * Profile selection
 * ─────────────────
 *  Each UPS unit's register profile is looked up by modbus_uid at startup.
 *  Profiles are registered in devices/ups/ups_map.c; order in config.json
 *  has no effect on which profile is selected.
 *
 * Adding a new UPS unit
 * ─────────────────────
 *  1. Add table + profile in devices/ups/ups_map.c.
 *  2. Declare the profile extern in devices/ups/ups_map.h.
 */

#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <stdint.h>

#include "ups_module.h"
#include "ups_cmos_bridge.h"
#include "device_register_map.h"
#include "modbus_tcp_client.h"
#include "ups/ups_map.h"
#include "log.h"

/* ── Tunable constants ────────────────────────────────────────────────── */
#define UPS_RECONNECT_DELAY_MS         5000u
#define UPS_COMM_FAIL_THRESHOLD        5
#define UPS_INTER_SEGMENT_DELAY_US     40000u   /* 40 ms between FC03 frames */
#define UPS_SHUTDOWN_CHECK_INTERVAL_MS 100u     /* granularity for interruptible sleeps */

/* ── Command queue constants ──────────────────────────────────────────── */
#define UPS_CMD_QUEUE_CAPACITY         16u

/* ── Write command entry ──────────────────────────────────────────────── */

typedef struct {
    uint16_t addr;
    uint16_t values[MODBUS_MAX_WRITE_REGISTERS];
    uint16_t count;
    ups_write_mode_t mode;
} ups_write_cmd_t;

/* ── Per-unit command queue ───────────────────────────────────────────── */
typedef struct {
    ups_write_cmd_t entries[UPS_CMD_QUEUE_CAPACITY];
    unsigned int    head;
    unsigned int    tail;
    unsigned int    count;
    pthread_mutex_t lock;
} ups_cmd_queue_t;

/* ── Per-unit runtime state ───────────────────────────────────────────── */
typedef struct {
    module_config_t            *cfg;
    const device_map_profile_t *profile;
    mb_tcp_client_ctx_t         tcp_ctx;
    module_callbacks_t          callbacks;
    pthread_t                   thread;
    volatile sig_atomic_t       running;
    int                         comm_fail_count;
    ups_cmd_queue_t             cmd_queue;
} ups_unit_t;

static ups_unit_t  ups_units[MAX_UPS_COUNT];
static int         ups_unit_count = 0;

/* ── Queue helpers (static) ───────────────────────────────────────────── */

static void queue_init(ups_cmd_queue_t *q)
{
    memset(q, 0, sizeof(*q));
    pthread_mutex_init(&q->lock, NULL);
}

static void queue_destroy(ups_cmd_queue_t *q)
{
    pthread_mutex_destroy(&q->lock);
}

/**
 * @brief Push one write command onto the tail of the queue.
 * @return 0 on success, -1 if the queue is full or count is out of range.
 */
static int queue_push(ups_cmd_queue_t *q, uint16_t addr,
                      const uint16_t *values, uint16_t count,
                      ups_write_mode_t mode)
{
    if (count == 0 || count > MODBUS_MAX_WRITE_REGISTERS) {
        return -1;
    }

    pthread_mutex_lock(&q->lock);

    if (q->count >= UPS_CMD_QUEUE_CAPACITY) {
        pthread_mutex_unlock(&q->lock);
        return -1;
    }

    ups_write_cmd_t *entry = &q->entries[q->tail];
    entry->addr  = addr;
    entry->count = count;
    entry->mode = mode;
    memcpy(entry->values, values, count * sizeof(uint16_t));

    q->tail  = (q->tail + 1u) % UPS_CMD_QUEUE_CAPACITY;
    q->count++;

    pthread_mutex_unlock(&q->lock);
    return 0;
}

/**
 * @brief Pop one write command from the head of the queue.
 * @return 0 on success, -1 if the queue is empty.
 */
static int queue_pop(ups_cmd_queue_t *q, ups_write_cmd_t *out)
{
    pthread_mutex_lock(&q->lock);

    if (q->count == 0) {
        pthread_mutex_unlock(&q->lock);
        return -1;
    }

    *out    = q->entries[q->head];
    q->head = (q->head + 1u) % UPS_CMD_QUEUE_CAPACITY;
    q->count--;

    pthread_mutex_unlock(&q->lock);
    return 0;
}

/* ── Internal helpers ─────────────────────────────────────────────────── */

static ups_unit_t *ups_unit_from_config(const module_config_t *cfg)
{
    for (int i = 0; i < ups_unit_count; i++) {
        if (ups_units[i].cfg == cfg) {
            return &ups_units[i];
        }
    }
    return NULL;
}

static ups_unit_t *ups_unit_from_uid(uint8_t uid)
{
    for (int i = 0; i < ups_unit_count; i++) {
        if (ups_units[i].cfg &&
            (uint8_t)ups_units[i].cfg->modbus_uid == uid) {
            return &ups_units[i];
        }
    }
    return NULL;
}

/**
 * @brief Sleep for duration_ms, waking early if the unit is signalled to stop.
 */
static void interruptible_sleep_ms(const ups_unit_t *unit, uint32_t duration_ms)
{
    uint32_t elapsed_ms = 0;

    while (elapsed_ms < duration_ms && unit->running) {
        usleep(UPS_SHUTDOWN_CHECK_INTERVAL_MS * 1000u);
        elapsed_ms += UPS_SHUTDOWN_CHECK_INTERVAL_MS;
    }
}

/**
 * @brief Execute a Modbus write (FC06 or FC16) directly on a unit.
 *
 * Shared by ups_msg_callback() and the queue-drain path in process_callback.
 * On failure, logs the error and returns -1 (does NOT increment comm_fail_count
 * – a write failure is an operational error, not a connectivity loss).
 */
static int write_registers_to_device(ups_unit_t *unit,
                                     uint16_t    addr,
                                     const uint16_t *values,
                                     uint16_t    count,
                                     ups_write_mode_t mode)
{
    int result;

    switch (mode) {
    case UPS_WRITE_MODE_FC06:
        if (count != 1) {
            LOG_ERROR("[UPS TCP] %s: FC06 requires count=1 (got %u) at 0x%04X.",
                      unit->cfg->name, count, addr);
            return -1;
        }
        result = mb_tcp_client_write_single_register(
                     &unit->tcp_ctx, addr, values[0]);
        break;
    case UPS_WRITE_MODE_FC16:
        result = mb_tcp_client_write_multiple_registers(
                     &unit->tcp_ctx, addr, count, values);
        break;
    case UPS_WRITE_MODE_AUTO:
    default:
        if (count == 1) {
            result = mb_tcp_client_write_single_register(
                         &unit->tcp_ctx, addr, values[0]);
        } else {
            result = mb_tcp_client_write_multiple_registers(
                         &unit->tcp_ctx, addr, count, values);
        }
        break;
    }

    if (result != MB_TCP_CLIENT_OK) {
        LOG_ERROR("[UPS TCP] %s: write to 0x%04X failed (err %d).",
                  unit->cfg->name, addr, result);
        return -1;
    }

    LOG_DEBUG("[UPS TCP] %s: wrote %u register(s) at 0x%04X.",
              unit->cfg->name, count, addr);
    return 0;
}

/* ── Callbacks ────────────────────────────────────────────────────────── */

/**
 * @brief init_callback – establish Modbus TCP connection to the UPS.
 *
 * @param cfg  Module configuration.
 * @return 0 on success, -1 on failure.
 */
int ups_tcp_init_callback(module_config_t *cfg)
{
    if (!cfg) {
        LOG_ERROR("[UPS TCP] Invalid configuration.");
        return -1;
    }

    ups_unit_t *unit = ups_unit_from_config(cfg);
    if (!unit) {
        LOG_ERROR("[UPS TCP] %s: unit not found.", cfg->name);
        return -1;
    }

    mb_tcp_client_config_t client_cfg = {
        .remote_host          = cfg->ip,
        .port                 = (cfg->port > 0) ? (uint16_t)cfg->port : 502u,
        .unit_id              = (uint8_t)cfg->modbus_uid,
        .connect_timeout_sec  = 5,
        .response_timeout_ms  = 1000,
        .logv                 = NULL,
        .log_userdata         = NULL,
    };

    LOG_INFO("[UPS TCP] %s: connecting to %s:%d uid=%d",
             cfg->name, cfg->ip, cfg->port, cfg->modbus_uid);

    if (mb_tcp_client_connect(&unit->tcp_ctx, &client_cfg) != 0) {
        LOG_ERROR("[UPS TCP] %s: connection failed.", cfg->name);
        return -1;
    }

    unit->comm_fail_count = 0;
    cfg->connection_state = CONNECTION_CONNECTED;

    LOG_INFO("[UPS TCP] %s: connected.", cfg->name);
    return 0;
}

/**
 * @brief process_callback – drain write queue, then read all mapped registers.
 *
 * Phase 1: drain the per-unit write queue.  Each queued command is executed
 *          as FC06 (single) or FC16 (multi) before the read scan begins.
 *          A write failure is logged but does not abort the read phase.
 *
 * Phase 2: consecutive device addresses are batched into one FC03 request.
 *          On any read failure the fail counter increments; after
 *          UPS_COMM_FAIL_THRESHOLD consecutive failures the callback returns
 *          -1 to trigger error_callback.
 *
 * @param cfg  Module configuration.
 * @return 0 on success, -1 on communication failure.
 */
int ups_tcp_process_callback(module_config_t *cfg)
{
    if (!cfg) {
        return -1;
    }

    ups_unit_t *unit = ups_unit_from_config(cfg);
    if (!unit) {
        return -1;
    }

    /* ── Phase 1: drain the write command queue ─────────────────────── */
    ups_write_cmd_t cmd;
    while (queue_pop(&unit->cmd_queue, &cmd) == 0) {
        if (write_registers_to_device(unit, cmd.addr, cmd.values, cmd.count, cmd.mode) != 0) {
            LOG_WARNING("[UPS TCP] %s: queued write to 0x%04X failed, "
                        "continuing scan.", cfg->name, cmd.addr);
        }
    }

    /* ── Phase 2: segmented FC03 read scan ──────────────────────────── */
    const device_map_profile_t *profile = unit->profile;

    if (profile->table_count == 0) {
        return 0;
    }

    uint16_t buf[MODBUS_MAX_READ_REGISTERS] = {0};

    size_t seg_start = 0;
    size_t seg_len   = 1;

    for (size_t i = 1; i <= profile->table_count; i++) {

        bool contiguous = (i < profile->table_count) &&
                          (profile->table[i].device_address ==
                           profile->table[i - 1].device_address + 1);
        bool chunk_full = (seg_len >= profile->read_chunk);

        if (contiguous && !chunk_full) {
            seg_len++;
            continue;
        }

        uint16_t start = profile->table[seg_start].device_address;
        uint16_t count = (uint16_t)seg_len;

        int result = mb_tcp_client_read_holding_registers(
                         &unit->tcp_ctx, start, count, buf);

        if (result != MB_TCP_CLIENT_OK) {
            unit->comm_fail_count++;
            LOG_WARNING("[UPS TCP] %s read 0x%04X len %u failed (err %d, fail %d/%d)",
                        cfg->name, start, count, result,
                        unit->comm_fail_count, UPS_COMM_FAIL_THRESHOLD);

            if (unit->comm_fail_count >= UPS_COMM_FAIL_THRESHOLD) {
                return -1;
            }
        } else {
            unit->comm_fail_count = 0;
            device_map_read_to_pool(profile, buf, start, (int)count);
        }

        usleep(UPS_INTER_SEGMENT_DELAY_US);

        seg_start = i;
        seg_len   = 1;
    }

    usleep((useconds_t)(cfg->rtu_poll_interval_ms * 1000u));
    return 0;
}

/**
 * @brief error_callback – disconnect and wait before the next reconnect attempt.
 *
 * @param cfg               Module configuration.
 * @param connection_state  New connection state (CONNECTION_DISCONNECTED).
 * @return 0 (reconnect is managed by the thread loop).
 */
int ups_tcp_error_callback(module_config_t *cfg, int connection_state)
{
    if (!cfg) {
        return 0;
    }

    ups_unit_t *unit = ups_unit_from_config(cfg);
    if (unit) {
        mb_tcp_client_disconnect(&unit->tcp_ctx);
        unit->comm_fail_count = 0;
    }

    cfg->connection_state = (connection_state_t)connection_state;

    LOG_WARNING("[UPS TCP] %s: disconnected (state=%d). Retrying in %u ms …",
                cfg->name, connection_state, UPS_RECONNECT_DELAY_MS);

    if (unit) {
        interruptible_sleep_ms(unit, UPS_RECONNECT_DELAY_MS);
    } else {
        usleep(UPS_RECONNECT_DELAY_MS * 1000u);
    }
    return 0;
}

/**
 * @brief msg_callback – execute a Modbus write on a UPS unit.
 *
 * Intended as a synchronous write path via the module_callbacks_t framework.
 * In this module, all writes are routed through ups_cmd_push() → queue drain
 * inside process_callback; msg_callback is registered but never invoked.
 *
 * Retained for framework completeness.  If a direct synchronous write path
 * is needed in the future, call this via unit->callbacks.msg_callback().
 *
 * @param cfg    Module configuration of the target UPS.
 * @param addr   Device register address to write.
 * @param values Values to write (host byte order).
 * @param count  Number of registers (1 → FC06, >1 → FC16).
 * @return 0 on success, -1 on failure.
 */
int ups_msg_callback(module_config_t *cfg,
                     uint16_t addr, uint16_t *values, size_t count)
{
    if (!cfg || !values || count == 0) {
        return -1;
    }

    ups_unit_t *unit = ups_unit_from_config(cfg);
    if (!unit) {
        LOG_ERROR("[UPS TCP] %s: msg_callback – unit not found.", cfg->name);
        return -1;
    }

    return write_registers_to_device(unit, addr, values, (uint16_t)count, UPS_WRITE_MODE_AUTO);
}

/* ── Thread function ──────────────────────────────────────────────────── */

/**
 * @brief Poll thread for one UPS unit.
 *
 *  connect → segment reads → write pool → wait for next poll interval
 *          → on failure: disconnect → wait → reconnect
 */
static void *ups_tcp_thread(void *arg)
{
    ups_unit_t *unit = (ups_unit_t *)arg;
    module_config_t *cfg  = unit->cfg;

    LOG_INFO("[UPS TCP] Thread started: %s (profile=%s uid=%d)",
             cfg->name, unit->profile->name, cfg->modbus_uid);

    while (unit->running) {

        /* init --------------------------------------------------------- */
        if (unit->callbacks.init_callback(cfg) != 0) {
            LOG_ERROR("[UPS TCP] %s: init failed, will retry.", cfg->name);
            interruptible_sleep_ms(unit, UPS_RECONNECT_DELAY_MS);
            continue;
        }

        /* process loop ------------------------------------------------- */
        while (unit->running) {
            if (unit->callbacks.process_callback(cfg) != 0) {
                if (unit->callbacks.error_callback) {
                    unit->callbacks.error_callback(cfg, CONNECTION_DISCONNECTED);
                }
                break; /* break inner loop → re-init */
            }
        }
    }

    mb_tcp_client_disconnect(&unit->tcp_ctx);
    LOG_INFO("[UPS TCP] Thread stopped: %s", cfg->name);
    return NULL;
}

/* ── Public API ───────────────────────────────────────────────────────── */

/**
 * @brief Start all enabled UPS modules.
 *
 * Selects each unit's register profile by modbus_uid, assigns callbacks,
 * spawns one polling thread per enabled UPS unit, then starts the CMOS bridge.
 *
 * @param ups       Array of UPS configurations from global_config.
 * @param ups_count Number of entries in the array.
 * @return 0 on success, -1 if any unit fails to start.
 */
int start_ups_modules(module_config_t ups[], int ups_count)
{
    int status = 0;

    ups_unit_count = 0;

    for (int i = 0; i < ups_count; i++) {
        if (!ups[i].enabled) {
            LOG_INFO("[UPS] %s is disabled. Skipping.", ups[i].name);
            continue;
        }

        const device_map_profile_t *profile =
            ups_find_profile_by_uid((uint8_t)ups[i].modbus_uid);

        if (!profile) {
            LOG_ERROR("[UPS] %s: no profile registered for modbus_uid=%d.",
                      ups[i].name, ups[i].modbus_uid);
            status = -1;
            continue;
        }

        ups_unit_t *unit      = &ups_units[ups_unit_count];
        unit->cfg             = &ups[i];
        unit->profile         = profile;
        unit->running         = 1;
        unit->comm_fail_count = 0;
        memset(&unit->tcp_ctx, 0, sizeof(unit->tcp_ctx));
        queue_init(&unit->cmd_queue);

        unit->callbacks.init_callback    = ups_tcp_init_callback;
        unit->callbacks.process_callback = ups_tcp_process_callback;
        unit->callbacks.error_callback   = ups_tcp_error_callback;
        unit->callbacks.msg_callback     = ups_msg_callback;
        unit->callbacks.start_callback   = NULL;

        ups_unit_count++;

        LOG_INFO("[UPS] Starting %s (profile=%s uid=%d).",
                 unit->cfg->name,
                 unit->profile->name,
                 unit->cfg->modbus_uid);

        if (pthread_create(&unit->thread, NULL, ups_tcp_thread, unit) != 0) {
            LOG_ERROR("[UPS] Failed to create thread for %s.", unit->cfg->name);
            queue_destroy(&unit->cmd_queue);
            unit->running = 0;
            ups_unit_count--;
            status = -1;
        }
    }

    if (ups_cmos_bridge_start() != 0) {
        LOG_ERROR("[UPS] CMOS bridge failed to start.");
        status = -1;
    }

    return status;
}

/**
 * @brief Stop all running UPS modules and join their threads.
 *
 * Stops the CMOS bridge first, then signals all unit threads to exit and
 * waits for them.
 *
 * @return 0 on success.
 */
int stop_ups_modules(void)
{
    ups_cmos_bridge_stop();

    for (int i = 0; i < ups_unit_count; i++) {
        ups_units[i].running = 0;
    }

    for (int i = 0; i < ups_unit_count; i++) {
        pthread_join(ups_units[i].thread, NULL);
        queue_destroy(&ups_units[i].cmd_queue);
        LOG_INFO("[UPS] %s stopped.", ups_units[i].cfg->name);
    }

    ups_unit_count = 0;
    return 0;
}

/**
 * @brief Enqueue a write command for the UPS unit identified by uid.
 *
 * Called from the CMOS bridge thread.  Thread-safe via per-unit queue mutex.
 *
 * @return 0 on success, -1 if unit not found or queue is full.
 */
int ups_cmd_push(uint8_t uid, uint16_t addr,
                 const uint16_t *values, uint16_t count,
                 ups_write_mode_t mode)
{
    if (!values || count == 0 || count > MODBUS_MAX_WRITE_REGISTERS) {
        return -1;
    }

    ups_unit_t *unit = ups_unit_from_uid(uid);
    if (!unit) {
        LOG_WARNING("[UPS] ups_cmd_push: uid=%u not found.", uid);
        return -1;
    }

    return queue_push(&unit->cmd_queue, addr, values, count, mode);
}
