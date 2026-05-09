/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

/* ─── Includes ─────────────────────────────────────────────────────────────── */
#include "gatt_svc.h"
#include "common.h"
#include "led.h"

/* ─── Declaraciones de funciones privadas ───────────────────────────────────── */
static int led_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg);
static int nus_rx_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt, void *arg);
static int nus_tx_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt, void *arg);

/* ─── Automation IO service ─────────────────────────────────────────────────── */
static const ble_uuid16_t  auto_io_svc_uuid = BLE_UUID16_INIT(0x1815);
static uint16_t            led_chr_val_handle;
static const ble_uuid128_t led_chr_uuid =
    BLE_UUID128_INIT(0x23, 0xd1, 0xbc, 0xea, 0x5f, 0x78, 0x23, 0x15, 0xde, 0xef,
                     0x12, 0x12, 0x25, 0x15, 0x00, 0x00);

/* ─── Nordic UART Service (NUS) ─────────────────────────────────────────────── */
/* Service UUID : 6E400001-B5A3-F393-E0A9-E50E24DCCA9E
 * RX UUID      : 6E400002-...  (celular → ESP32, WRITE)
 * TX UUID      : 6E400003-...  (ESP32 → celular, NOTIFY)
 */
static const ble_uuid128_t nus_svc_uuid =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
                     0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e);

static const ble_uuid128_t nus_rx_chr_uuid =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
                     0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e);

static const ble_uuid128_t nus_tx_chr_uuid =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
                     0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e);

static uint16_t nus_rx_chr_val_handle;
static uint16_t nus_tx_chr_val_handle;
static uint16_t nus_conn_handle    = BLE_HS_CONN_HANDLE_NONE;
static bool     nus_notify_enabled = false;

/* ─── Tabla de servicios GATT ───────────────────────────────────────────────── */
static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    /* Automation IO: controla el LED con 0x01 / 0x00 */
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &auto_io_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                .uuid       = &led_chr_uuid.u,
                .access_cb  = led_chr_access,
                .flags      = BLE_GATT_CHR_F_WRITE,
                .val_handle = &led_chr_val_handle,
            },
            {0},
        },
    },

    /* Nordic UART Service: envío y recepción de texto */
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &nus_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            /* RX: recibe mensajes del celular */
            {
                .uuid       = &nus_rx_chr_uuid.u,
                .access_cb  = nus_rx_chr_access,
                .flags      = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
                .val_handle = &nus_rx_chr_val_handle,
            },
            /* TX: envía mensajes al celular */
            {
                .uuid       = &nus_tx_chr_uuid.u,
                .access_cb  = nus_tx_chr_access,
                .flags      = BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &nus_tx_chr_val_handle,
            },
            {0},
        },
    },

    {
        0, /* No more services. */
    },
};

/* ─── Funciones privadas ────────────────────────────────────────────────────── */

static int led_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg) {
    int rc = 0;

    switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_WRITE_CHR:
        if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
            ESP_LOGI(TAG, "escritura LED; conn_handle=%d attr_handle=%d",
                     conn_handle, attr_handle);
        }

        if (attr_handle == led_chr_val_handle) {
            if (ctxt->om->om_len == 1) {
                if (ctxt->om->om_data[0]) {
                    led_on();
                    ESP_LOGI(TAG, "LED encendido!");
                } else {
                    led_off();
                    ESP_LOGI(TAG, "LED apagado!");
                }
            } else {
                goto error;
            }
            return rc;
        }
        goto error;

    default:
        goto error;
    }

error:
    ESP_LOGE(TAG, "acceso inesperado a característica LED, opcode: %d", ctxt->op);
    return BLE_ATT_ERR_UNLIKELY;
}

static int nus_rx_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt, void *arg) {
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (len == 0 || len > 512) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    char buf[513];
    os_mbuf_copydata(ctxt->om, 0, len, buf);
    buf[len] = '\0';

    /* Eliminar saltos de línea y espacios al final que envía la app */
    for (int i = (int)len - 1; i >= 0 && (buf[i] == '\n' || buf[i] == '\r' || buf[i] == ' '); i--) {
        buf[i] = '\0';
    }

    /* Normalizar a minúsculas para comparaciones case-insensitive */
    for (int i = 0; buf[i]; i++) {
        if (buf[i] >= 'A' && buf[i] <= 'Z') {
            buf[i] += ('a' - 'A');
        }
    }

    ESP_LOGI(TAG, "NUS RX: \"%s\"", buf);

    if (strcmp(buf, "encender") == 0) {
        led_on();
        ESP_LOGI(TAG, "NUS: LED encendido");
        nus_send_string("LED encendido\n");
    } else if (strcmp(buf, "apagar") == 0) {
        led_off();
        ESP_LOGI(TAG, "NUS: LED apagado");
        nus_send_string("LED apagado\n");
    } else {
        static const struct {
            const char *cmd;
            uint8_t     r, g, b;
        } colores[] = {
            { "blanco",   255, 255, 255 },
            { "rojo",     255,   0,   0 },
            { "verde",      0, 255,   0 },
            { "azul",       0,   0, 255 },
            { "celeste",    0, 160, 255 },
            { "amarillo", 255, 200,   0 },
            { "naranja",  255,  80,   0 },
            { "morado",   180,   0, 255 },
            { "rosa",     255,   0, 120 },
        };

        bool encontrado = false;
        for (int i = 0; i < (int)(sizeof(colores) / sizeof(colores[0])); i++) {
            if (strcmp(buf, colores[i].cmd) == 0) {
                led_set_color(colores[i].r, colores[i].g, colores[i].b);
                char resp[32];
                snprintf(resp, sizeof(resp), "Color: %s\n", colores[i].cmd);
                ESP_LOGI(TAG, "NUS color: %s", colores[i].cmd);
                nus_send_string(resp);
                encontrado = true;
                break;
            }
        }

        if (!encontrado) {
            nus_send_string("Comandos:\n"
                            "  encender / apagar\n"
                            "  Blanco  Rojo    Verde\n"
                            "  Azul    Celeste Amarillo\n"
                            "  Naranja Morado  Rosa\n");
        }
    }

    return 0;
}

static int nus_tx_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt, void *arg) {
    /* Solo notify — sin acceso directo */
    return BLE_ATT_ERR_UNLIKELY;
}

/* ─── Funciones públicas ────────────────────────────────────────────────────── */

void nus_send_string(const char *str) {
    if (!nus_notify_enabled || nus_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return;
    }

    struct os_mbuf *om = ble_hs_mbuf_from_flat(str, strlen(str));
    if (om == NULL) {
        ESP_LOGE(TAG, "NUS TX: sin memoria para mbuf");
        return;
    }

    int rc = ble_gatts_notify_custom(nus_conn_handle, nus_tx_chr_val_handle, om);
    if (rc != 0) {
        ESP_LOGE(TAG, "NUS TX: error al enviar notificación, código: %d", rc);
    }
}

void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg) {
    char buf[BLE_UUID_STR_LEN];

    switch (ctxt->op) {
    case BLE_GATT_REGISTER_OP_SVC:
        ESP_LOGD(TAG, "servicio registrado %s con handle=%d",
                 ble_uuid_to_str(ctxt->svc.svc_def->uuid, buf), ctxt->svc.handle);
        break;
    case BLE_GATT_REGISTER_OP_CHR:
        ESP_LOGD(TAG, "característica registrada %s def_handle=%d val_handle=%d",
                 ble_uuid_to_str(ctxt->chr.chr_def->uuid, buf),
                 ctxt->chr.def_handle, ctxt->chr.val_handle);
        break;
    case BLE_GATT_REGISTER_OP_DSC:
        ESP_LOGD(TAG, "descriptor registrado %s con handle=%d",
                 ble_uuid_to_str(ctxt->dsc.dsc_def->uuid, buf), ctxt->dsc.handle);
        break;
    default:
        assert(0);
        break;
    }
}

void gatt_svr_subscribe_cb(struct ble_gap_event *event) {
    if (event->subscribe.conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ESP_LOGI(TAG, "suscripción; conn_handle=%d attr_handle=%d",
                 event->subscribe.conn_handle, event->subscribe.attr_handle);
    }

    if (event->subscribe.attr_handle == nus_tx_chr_val_handle) {
        nus_conn_handle    = event->subscribe.conn_handle;
        nus_notify_enabled = event->subscribe.cur_notify;
        ESP_LOGI(TAG, "NUS TX notify %s",
                 nus_notify_enabled ? "activado" : "desactivado");
    }
}

int gatt_svc_init(void) {
    int rc = 0;

    ble_svc_gatt_init();

    rc = ble_gatts_count_cfg(gatt_svr_svcs);
    if (rc != 0) {
        return rc;
    }

    rc = ble_gatts_add_svcs(gatt_svr_svcs);
    if (rc != 0) {
        return rc;
    }

    return 0;
}
