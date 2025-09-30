#include "bt_a2dp.h"
#include "bt_core.h"
#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"
#include "esp_gap_bt_api.h"
#include "esp_log.h"
#include "sdkconfig.h"

static bt_ctx_t* bt_ctx = nullptr;

static char* bda2str(esp_bd_addr_t bda, char* str, size_t size) {
    if (bda == NULL || str == NULL || size < 18)
        return NULL;

    sprintf(str, "%02x:%02x:%02x:%02x:%02x:%02x", bda[0], bda[1], bda[2],
            bda[3], bda[4], bda[5]);
    return str;
}

static bool get_name_from_eir(uint8_t* eir, uint8_t* bdname,
                              uint8_t* bdname_len) {
    uint8_t* rmt_bdname = NULL;
    uint8_t rmt_bdname_len = 0;

    if (!eir) {
        return false;
    }

    /* get complete or short local name from eir data */
    rmt_bdname = esp_bt_gap_resolve_eir_data(
        eir, ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME, &rmt_bdname_len);
    if (!rmt_bdname) {
        rmt_bdname = esp_bt_gap_resolve_eir_data(
            eir, ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME, &rmt_bdname_len);
    }

    if (rmt_bdname) {
        if (rmt_bdname_len > ESP_BT_GAP_MAX_BDNAME_LEN) {
            rmt_bdname_len = ESP_BT_GAP_MAX_BDNAME_LEN;
        }

        if (bdname) {
            memcpy(bdname, rmt_bdname, rmt_bdname_len);
            bdname[rmt_bdname_len] = '\0';
        }
        if (bdname_len) {
            *bdname_len = rmt_bdname_len;
        }
        return true;
    }

    return false;
}

static void filter_inquiry_scan_result(esp_bt_gap_cb_param_t* param) {
    char bda_str[18];
    uint32_t cod = 0;    /* class of device */
    int32_t rssi = -129; /* invalid value */
    uint8_t* eir = NULL;
    esp_bt_gap_dev_prop_t* p;

    // handle the discovery results
    ESP_LOGI("BT_A2DP", "Scanned device: %s",
             bda2str(param->disc_res.bda, bda_str, 18));

    // iterate through device properties
    for (int i = 0; i < param->disc_res.num_prop; i++) {
        p = param->disc_res.prop + i;
        switch (p->type) {
        case ESP_BT_GAP_DEV_PROP_COD:
            cod = *(uint32_t*)(p->val);
            ESP_LOGI("BT_A2DP", "--Class of Device: 0x%" PRIx32, cod);
            break;
        case ESP_BT_GAP_DEV_PROP_RSSI:
            rssi = *(int8_t*)(p->val);
            ESP_LOGI("BT_A2DP", "--RSSI: %" PRId32, rssi);
            break;
        case ESP_BT_GAP_DEV_PROP_EIR:
            eir = (uint8_t*)(p->val);
            break;
        case ESP_BT_GAP_DEV_PROP_BDNAME:
        default:
            break;
        }
    }

    // search for device with MAJOR service class as "rendering" in COD
    if (!esp_bt_gap_is_valid_cod(cod) ||
        !(esp_bt_gap_get_cod_srvc(cod) & ESP_BT_COD_SRVC_RENDERING)) {
        return;
    }

    /* search for target device in its Extended Inqury Response */
    const char* remote_device_name = CONFIG_BT_A2DP_REMOTE_NAME;
    if (eir) {
        get_name_from_eir(eir, bt_ctx->peer_bdname, NULL);
        if (strcmp((char*)bt_ctx->peer_bdname, remote_device_name) == 0) {
            ESP_LOGI("BT_A2DP", "Found a target device, address %s, name %s",
                     bda_str, bt_ctx->peer_bdname);
            bt_ctx->a2dp_state = BT_STATE_DISCOVERED;
            memcpy(bt_ctx->peer_bda, param->disc_res.bda, ESP_BD_ADDR_LEN);
            ESP_LOGI("BT_A2DP", "Cancel device discovery ...");
            esp_bt_gap_cancel_discovery();
        }
    }
}

static void bt_a2dp_gap_cb(esp_bt_gap_cb_event_t event,
                           esp_bt_gap_cb_param_t* param) {
    switch (event) {
    /* when device discovered a result, this event comes */
    case ESP_BT_GAP_DISC_RES_EVT: {
        if (bt_ctx->a2dp_state == BT_STATE_DISCOVERING) {
            filter_inquiry_scan_result(param);
        }
        break;
    }
    /* when discovery state changed, this event comes */
    case ESP_BT_GAP_DISC_STATE_CHANGED_EVT: {
        if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) {
            if (bt_ctx->a2dp_state == BT_STATE_DISCOVERED) {
                bt_ctx->a2dp_state = BT_STATE_CONNECTING;
                ESP_LOGI("BT_A2DP", "Device discovery stopped.");
                ESP_LOGI("BT_A2DP", "a2dp connecting to peer: %s",
                         bt_ctx->peer_bdname);
                /* connect source to peer device specified by Bluetooth Device
                 * Address */
                esp_a2d_source_connect(bt_ctx->peer_bda);
            } else {
                /* not discovered, continue to discover */
                ESP_LOGI("BT_A2DP",
                         "Device discovery failed, continue to discover...");
                esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 10,
                                           0);
            }
        } else if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STARTED) {
            ESP_LOGI("BT_A2DP", "Discovery started.");
        }
        break;
    }
    /* when authentication completed, this event comes */
    case ESP_BT_GAP_AUTH_CMPL_EVT: {
        if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI("BT_A2DP", "authentication success: %s",
                     param->auth_cmpl.device_name);
            ESP_LOG_BUFFER_HEX("BT_A2DP", param->auth_cmpl.bda,
                               ESP_BD_ADDR_LEN);
        } else {
            ESP_LOGE("BT_A2DP", "authentication failed, status: %d",
                     param->auth_cmpl.stat);
        }
        break;
    }

    /* when Security Simple Pairing user confirmation requested, this event
     * comes */
    case ESP_BT_GAP_CFM_REQ_EVT:
        ESP_LOGI("BT_A2DP",
                 "ESP_BT_GAP_CFM_REQ_EVT Please compare the numeric value: "
                 "%06" PRIu32,
                 param->cfm_req.num_val);
        esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
        break;
    /* when Security Simple Pairing passkey notified, this event comes */
    case ESP_BT_GAP_KEY_NOTIF_EVT:
        ESP_LOGI("BT_A2DP", "ESP_BT_GAP_KEY_NOTIF_EVT passkey: %06" PRIu32,
                 param->key_notif.passkey);
        break;
    /* when Security Simple Pairing passkey requested, this event comes */
    case ESP_BT_GAP_KEY_REQ_EVT:
        ESP_LOGI("BT_A2DP", "ESP_BT_GAP_KEY_REQ_EVT Please enter passkey!");
        break;

    /* when GAP mode changed, this event comes */
    case ESP_BT_GAP_MODE_CHG_EVT:
        ESP_LOGI("BT_A2DP", "ESP_BT_GAP_MODE_CHG_EVT mode: %d",
                 param->mode_chg.mode);
        break;
    case ESP_BT_GAP_GET_DEV_NAME_CMPL_EVT:
        if (param->get_dev_name_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI("BT_A2DP",
                     "ESP_BT_GAP_GET_DEV_NAME_CMPL_EVT device name: %s",
                     param->get_dev_name_cmpl.name);
        } else {
            ESP_LOGI("BT_A2DP",
                     "ESP_BT_GAP_GET_DEV_NAME_CMPL_EVT failed, state: %d",
                     param->get_dev_name_cmpl.status);
        }
        break;

    /* other */
    default: {
        ESP_LOGI("BT_A2DP", "GAP event: %d", event);
        break;
    }
    }
}

static void bt_av_volume_changed(void) {
    if (esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_TEST,
                                           &bt_ctx->avrc_peer_rn_cap,
                                           ESP_AVRC_RN_VOLUME_CHANGE)) {
        esp_avrc_ct_send_register_notification_cmd(1, ESP_AVRC_RN_VOLUME_CHANGE,
                                                   0);
    }
}

void bt_av_notify_evt_handler(uint8_t event_id,
                              esp_avrc_rn_param_t* event_parameter) {
    switch (event_id) {
    /* when volume changed locally on target, this event comes */
    case ESP_AVRC_RN_VOLUME_CHANGE: {
        ESP_LOGI("BT_A2DP_RC", "Volume changed: %d", event_parameter->volume);
        ESP_LOGI("BT_A2DP_RC", "Set absolute volume: volume %d",
                 event_parameter->volume + 5);
        esp_avrc_ct_send_set_absolute_volume_cmd(1,
                                                 event_parameter->volume + 5);
        bt_av_volume_changed();
        break;
    }
    /* other */
    default:
        break;
    }
}

// AVRC controller event handler
static void bt_a2dp_hdl_avrc_ct_evt(bt_ctx_t* ctx, uint16_t event,
                                    void* p_param) {
    ESP_LOGD("BT_A2DP_RC", "%s evt %d", __func__, event);
    esp_avrc_ct_cb_param_t* rc = (esp_avrc_ct_cb_param_t*)(p_param);

    switch (event) {
    /* when connection state changed, this event comes */
    case ESP_AVRC_CT_CONNECTION_STATE_EVT: {
        uint8_t* bda = rc->conn_stat.remote_bda;
        ESP_LOGI(
            "BT_A2DP_RC",
            "AVRC conn_state event: state %d, [%02x:%02x:%02x:%02x:%02x:%02x]",
            rc->conn_stat.connected, bda[0], bda[1], bda[2], bda[3], bda[4],
            bda[5]);

        if (rc->conn_stat.connected) {
            esp_avrc_ct_send_get_rn_capabilities_cmd(0);
        } else {
            ctx->avrc_peer_rn_cap.bits = 0;
        }
        break;
    }
    /* when passthrough responded, this event comes */
    case ESP_AVRC_CT_PASSTHROUGH_RSP_EVT: {
        ESP_LOGI("BT_A2DP_RC",
                 "AVRC passthrough response: key_code 0x%x, key_state %d, "
                 "rsp_code %d",
                 rc->psth_rsp.key_code, rc->psth_rsp.key_state,
                 rc->psth_rsp.rsp_code);
        break;
    }
    /* when metadata responded, this event comes */
    case ESP_AVRC_CT_METADATA_RSP_EVT: {
        ESP_LOGI("BT_A2DP_RC", "AVRC metadata response: attribute id 0x%x, %s",
                 rc->meta_rsp.attr_id, rc->meta_rsp.attr_text);
        free(rc->meta_rsp.attr_text);
        break;
    }
    /* when notification changed, this event comes */
    case ESP_AVRC_CT_CHANGE_NOTIFY_EVT: {
        ESP_LOGI("BT_A2DP_RC", "AVRC event notification: %d",
                 rc->change_ntf.event_id);
        bt_av_notify_evt_handler(rc->change_ntf.event_id,
                                 &rc->change_ntf.event_parameter);
        break;
    }
    /* when indicate feature of remote device, this event comes */
    case ESP_AVRC_CT_REMOTE_FEATURES_EVT: {
        ESP_LOGI("BT_A2DP_RC",
                 "AVRC remote features %" PRIx32 ", TG features %x",
                 rc->rmt_feats.feat_mask, rc->rmt_feats.tg_feat_flag);
        break;
    }
    /* when get supported notification events capability of peer device, this
     * event comes */
    case ESP_AVRC_CT_GET_RN_CAPABILITIES_RSP_EVT: {
        ESP_LOGI("BT_A2DP_RC", "remote rn_cap: count %d, bitmask 0x%x",
                 rc->get_rn_caps_rsp.cap_count,
                 rc->get_rn_caps_rsp.evt_set.bits);
        ctx->avrc_peer_rn_cap.bits = rc->get_rn_caps_rsp.evt_set.bits;

        bt_av_volume_changed();
        break;
    }
    /* when set absolute volume responded, this event comes */
    case ESP_AVRC_CT_SET_ABSOLUTE_VOLUME_RSP_EVT: {
        ESP_LOGI("BT_A2DP_RC", "Set absolute volume response: volume %d",
                 rc->set_volume_rsp.volume);
        break;
    }
    /* other */
    default: {
        ESP_LOGE("BT_A2DP_RC", "%s unhandled event: %d", __func__, event);
        break;
    }
    }
}

// callback function for AVRCP controller
static void bt_a2dp_rc_ct_cb(esp_avrc_ct_cb_event_t event,
                             esp_avrc_ct_cb_param_t* param) {
    switch (event) {
    case ESP_AVRC_CT_CONNECTION_STATE_EVT:
    case ESP_AVRC_CT_PASSTHROUGH_RSP_EVT:
    case ESP_AVRC_CT_METADATA_RSP_EVT:
    case ESP_AVRC_CT_CHANGE_NOTIFY_EVT:
    case ESP_AVRC_CT_REMOTE_FEATURES_EVT:
    case ESP_AVRC_CT_GET_RN_CAPABILITIES_RSP_EVT:
    case ESP_AVRC_CT_SET_ABSOLUTE_VOLUME_RSP_EVT: {
        bt_core_dispatch(bt_ctx, bt_a2dp_hdl_avrc_ct_evt, event, param,
                         sizeof(esp_avrc_ct_cb_param_t));
        break;
    }
    default: {
        ESP_LOGE("BT_A2DP_RC", "Invalid AVRC event: %d", event);
        break;
    }
    }
}

static void bt_a2dp_av_sm_hdlr(bt_ctx_t* ctx, uint16_t event, void* param) {
    ESP_LOGI("BT_A2DP", "%s state: %d, event: 0x%x", __func__, ctx->a2dp_state,
             event);

    /* select handler according to different states */
    switch (ctx->a2dp_state) {
    case BT_STATE_DISCOVERING:
    case BT_STATE_DISCOVERED:
        break;
    case BT_STATE_UNCONNECTED:
        bt_BT_state_unconnected_hdlr(event, param);
        break;
    case BT_STATE_CONNECTING:
        bt_BT_state_connecting_hdlr(event, param);
        break;
    case BT_STATE_CONNECTED:
        bt_BT_state_connected_hdlr(event, param);
        break;
    case BT_STATE_DISCONNECTING:
        bt_app_av_state_disconnecting_hdlr(event, param);
        break;
    default:
        ESP_LOGE("BT_A2DP", "%s invalid state: %d", __func__, ctx->a2dp_state);
        break;
    }
}

static void bt_a2dp_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t* param) {
    bt_core_dispatch(bt_ctx, bt_a2dp_av_sm_hdlr, event, param,
                     sizeof(esp_a2d_cb_param_t));
}

void bt_a2dp_stack_event(bt_ctx_t* ctx, uint16_t event, void* event_data) {
    ESP_LOGD("BT_A2DP", "%s event received: %d", __func__, event);

    switch (event) {
    case 0: { // Stack up event
        bt_ctx = ctx;
        const char* device_name = CONFIG_BT_A2DP_HOST_NAME;
        esp_bt_gap_set_device_name(device_name);
        esp_bt_gap_register_callback(bt_a2dp_gap_cb);

        esp_avrc_ct_init();
        esp_avrc_ct_register_callback(bt_a2dp_rc_ct_cb);

        esp_avrc_rn_evt_cap_mask_t evt_set = {0};
        esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_SET, &evt_set,
                                           ESP_AVRC_RN_VOLUME_CHANGE);
        ESP_ERROR_CHECK(esp_avrc_tg_set_rn_evt_cap(&evt_set));

        esp_a2d_source_init();
        esp_a2d_register_callback(&bt_a2dp_cb);
        esp_a2d_source_register_data_callback(bt_app_a2d_data_cb);

        /* Avoid the state error of s_a2d_state caused by the connection initiated by the peer device. */
        esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
        esp_bt_gap_get_device_name();

        ESP_LOGI("BT_A2DP", "Starting device discovery...");
        ctx->a2dp_state = BT_STATE_DISCOVERING;
        esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 10, 0);

        /* create and start heart beat timer */
        do {
            int tmr_id = 0;
            ctx->heart_beat_timer = xTimerCreate("connTmr", (10000 / portTICK_PERIOD_MS),
                                 pdTRUE, (void*)&tmr_id, bt_app_a2d_heart_beat);
            xTimerStart(ctx->heart_beat_timer, portMAX_DELAY);
        } while (0);
        break;
    }

    /* other */
    default: {
        ESP_LOGE("BT_A2DP", "%s unhandled event: %d", __func__, event);
        break;
    }
    }
}
