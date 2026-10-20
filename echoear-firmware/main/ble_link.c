#include "ble_link.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_random.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "ble_link";

#define DEVICE_NAME          "Watchling"
#define PASSKEY_MODULO       1000000
#define NOTIFY_RETRY_MS      10
#define NOTIFY_RETRIES       50 /* ~0.5 s of "out of buffers" before a send gives up */

/* Not in a public header; provided by the NimBLE store (NVS backend with CONFIG_BT_NIMBLE_NVS_PERSIST). */
void ble_store_config_init(void);

/* Nordic UART Service, 6E40000x-B5A3-F393-E0A9-E50E24DCCA9E (UUID bytes are little-endian). */
#define NUS_UUID(x) \
    BLE_UUID128_INIT(0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, 0x93, 0xF3, 0xA3, 0xB5, x, 0x00, 0x40, 0x6E)
static const ble_uuid128_t NUS_SERVICE = NUS_UUID(0x01);
static const ble_uuid128_t NUS_RX = NUS_UUID(0x02); /* host writes */
static const ble_uuid128_t NUS_TX = NUS_UUID(0x03); /* device notifies */

static ble_link_rx_cb_t s_on_rx;
static ble_link_pairing_cb_t s_on_pairing;
static uint8_t s_own_addr_type;
static uint16_t s_tx_handle;

/* Written by the NimBLE host task, read by senders. */
static volatile uint16_t s_conn = BLE_HS_CONN_HANDLE_NONE;
static volatile bool s_authenticated;
static volatile bool s_subscribed;
/* The Bluetooth switch in Settings; written by the UI task, applied on the host task. */
static volatile bool s_enabled;
static struct ble_npl_event s_enabled_ev;

static void advertise(void);

/* ---------- GATT ---------- */

static int nus_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        for (const struct os_mbuf *om = ctxt->om; om != NULL; om = SLIST_NEXT(om, om_next)) {
            s_on_rx(om->om_data, om->om_len);
        }
    }
    return 0; /* TX reads return nothing; the value only travels as notifications */
}

static const struct ble_gatt_svc_def SERVICES[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &NUS_SERVICE.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                /* Write with response: an unpaired write fails with "insufficient authentication",
                 * which makes the host start pairing. */
                .uuid = &NUS_RX.u,
                .access_cb = nus_access,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC | BLE_GATT_CHR_F_WRITE_AUTHEN,
            },
            {
                .uuid = &NUS_TX.u,
                .access_cb = nus_access,
                .val_handle = &s_tx_handle,
                .flags = BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC |
                         BLE_GATT_CHR_F_READ_AUTHEN,
            },
            {0},
        },
    },
    {0},
};

/* ---------- GAP ---------- */

static void security_update(uint16_t conn)
{
    struct ble_gap_conn_desc desc;
    s_authenticated = ble_gap_conn_find(conn, &desc) == 0 && desc.sec_state.encrypted &&
                      desc.sec_state.authenticated;
}

static void connection_reset(void)
{
    s_conn = BLE_HS_CONN_HANDLE_NONE;
    s_authenticated = false;
    s_subscribed = false;
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status != 0) {
            advertise();
            return 0;
        }
        if (!s_enabled) {
            /* Switched off while this connection was being set up. */
            ble_gap_terminate(event->connect.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
            return 0;
        }
        s_conn = event->connect.conn_handle;
        ESP_LOGI(TAG, "Connected");
        /* Ask the host to encrypt now: bonded hosts resume, new ones get the passkey prompt. */
        ble_gap_security_initiate(event->connect.conn_handle);
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Disconnected (reason 0x%x)", event->disconnect.reason);
        connection_reset();
        s_on_pairing(false, 0);
        advertise();
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        advertise();
        return 0;

    case BLE_GAP_EVENT_PASSKEY_ACTION:
        if (event->passkey.params.action == BLE_SM_IOACT_DISP) {
            struct ble_sm_io io = {
                .action = BLE_SM_IOACT_DISP,
                .passkey = esp_random() % PASSKEY_MODULO,
            };
            s_on_pairing(true, io.passkey);
            ble_sm_inject_io(event->passkey.conn_handle, &io);
        }
        return 0;

    case BLE_GAP_EVENT_ENC_CHANGE:
        s_on_pairing(false, 0);
        security_update(event->enc_change.conn_handle);
        ESP_LOGI(TAG, "Encryption %s (status %d)", s_authenticated ? "authenticated" : "not authenticated",
                 event->enc_change.status);
        if (!s_authenticated) {
            /* Only MITM-protected links may carry approvals. */
            ble_gap_terminate(event->enc_change.conn_handle, BLE_ERR_AUTH_FAIL);
        }
        return 0;

    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        /* The host lost its bond (e.g. "Forget this device"): drop ours and pair again. */
        struct ble_gap_conn_desc desc;
        if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0) {
            ble_store_util_delete_peer(&desc.peer_id_addr);
        }
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }

    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == s_tx_handle) {
            s_subscribed = event->subscribe.cur_notify;
        }
        return 0;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU %d", event->mtu.value);
        return 0;

    default:
        return 0;
    }
}

static void advertise(void)
{
    if (!s_enabled) {
        return;
    }
    /* Flags + the 128-bit service UUID already take 21 of the 31 advertising bytes, so the
     * name goes in the scan response; hosts scanning actively still show it. */
    struct ble_hs_adv_fields fields = {
        .flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP,
        .uuids128 = &NUS_SERVICE,
        .num_uuids128 = 1,
        .uuids128_is_complete = 1,
    };
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Advertising data rejected (%d)", rc);
        return;
    }
    struct ble_hs_adv_fields rsp = {
        .name = (const uint8_t *)DEVICE_NAME,
        .name_len = sizeof(DEVICE_NAME) - 1,
        .name_is_complete = 1,
    };
    rc = ble_gap_adv_rsp_set_fields(&rsp);
    if (rc != 0) {
        ESP_LOGE(TAG, "Scan response rejected (%d)", rc);
        return;
    }
    struct ble_gap_adv_params params = {
        .conn_mode = BLE_GAP_CONN_MODE_UND,
        .disc_mode = BLE_GAP_DISC_MODE_GEN,
    };
    rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER, &params, gap_event, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGE(TAG, "Advertising failed to start (%d)", rc);
    }
}

/* Host task: applies the Bluetooth switch (see ble_link_set_enabled). */
static void enabled_changed(struct ble_npl_event *ev)
{
    /* Before the host syncs there is nothing to start or stop; on_sync reads s_enabled. */
    if (!ble_hs_synced()) {
        return;
    }
    if (s_enabled) {
        advertise();
        return;
    }
    int rc = ble_gap_adv_stop();
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGW(TAG, "Advertising did not stop (%d)", rc);
    }
    if (s_conn != BLE_HS_CONN_HANDLE_NONE) {
        /* The disconnect event resets the link state; advertise() then stays quiet. */
        rc = ble_gap_terminate(s_conn, BLE_ERR_REM_USER_CONN_TERM);
        if (rc != 0 && rc != BLE_HS_ENOTCONN) {
            ESP_LOGW(TAG, "Disconnect failed (%d)", rc);
        }
    }
}

static void on_sync(void)
{
    ble_hs_util_ensure_addr(0);
    ble_hs_id_infer_auto(0, &s_own_addr_type);
    advertise();
}

static void on_reset(int reason)
{
    ESP_LOGW(TAG, "Host reset (reason %d)", reason);
    connection_reset();
}

static void host_task(void *param)
{
    nimble_port_run(); /* returns only after nimble_port_stop() */
    nimble_port_freertos_deinit();
}

/* ---------- public API ---------- */

esp_err_t ble_link_start(ble_link_rx_cb_t on_rx, ble_link_pairing_cb_t on_pairing, bool enabled)
{
    s_on_rx = on_rx;
    s_on_pairing = on_pairing;
    s_enabled = enabled;

    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        return err;
    }
    ble_npl_event_init(&s_enabled_ev, enabled_changed, NULL);

    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    /* LESC passkey entry: we display the code, the host types it. */
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_DISPLAY_ONLY;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    if (ble_gatts_count_cfg(SERVICES) != 0 || ble_gatts_add_svcs(SERVICES) != 0) {
        return ESP_FAIL;
    }
    ble_svc_gap_device_name_set(DEVICE_NAME);
    ble_store_config_init();

    nimble_port_freertos_init(host_task);
    return ESP_OK;
}

void ble_link_set_enabled(bool enabled)
{
    if (enabled == s_enabled) {
        return;
    }
    s_enabled = enabled;
    ESP_LOGI(TAG, "Bluetooth %s", enabled ? "on" : "off");
    /* Applied on the host task, in order with the GAP events that may advertise or connect. */
    ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &s_enabled_ev);
}

bool ble_link_ready(void)
{
    return s_conn != BLE_HS_CONN_HANDLE_NONE && s_authenticated && s_subscribed;
}

bool ble_link_send(const uint8_t *data, size_t len)
{
    uint16_t conn = s_conn;
    if (!ble_link_ready()) {
        return false;
    }
    size_t chunk_max = ble_att_mtu(conn) - 3;
    for (size_t off = 0; off < len;) {
        size_t n = len - off < chunk_max ? len - off : chunk_max;
        int rc = BLE_HS_ENOMEM;
        for (int attempt = 0; attempt < NOTIFY_RETRIES && rc == BLE_HS_ENOMEM; attempt++) {
            struct os_mbuf *om = ble_hs_mbuf_from_flat(data + off, n);
            rc = om == NULL ? BLE_HS_ENOMEM : ble_gatts_notify_custom(conn, s_tx_handle, om);
            if (rc == BLE_HS_ENOMEM) {
                vTaskDelay(pdMS_TO_TICKS(NOTIFY_RETRY_MS));
            }
        }
        if (rc != 0) {
            return false;
        }
        off += n;
    }
    return true;
}
