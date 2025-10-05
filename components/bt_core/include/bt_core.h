#pragma once
#include "esp_avrc_api.h"
#include "esp_bt_defs.h"
#include "esp_gap_bt_api.h"
#include "freertos/idf_additions.h"
#include <stdint.h>

typedef enum {
    BT_STATE_UNINITIALIZED = 0,
    BT_STATE_OFF,
    BT_STATE_ON,
    BT_STATE_CONNECTING,
    BT_STATE_CONNECTED,
    BT_STATE_DISCONNECTED,
    BT_STATE_DISCONNECTING,
    BT_STATE_IDLE,
    BT_STATE_DISCOVERING,
    BT_STATE_DISCOVERED,
    BT_STATE_UNCONNECTED,
} bt_state_t;

typedef enum {
    BT_MEDIA_STATE_IDLE,
    BT_MEDIA_STATE_STARTING,
    BT_MEDIA_STATE_STARTED,
    BT_MEDIA_STATE_STOPPING,
} bt_media_state_t;

typedef struct {
    bt_state_t state;
    bt_state_t a2dp_state;
    bt_media_state_t media_state;
    QueueHandle_t event_queue;
    TaskHandle_t event_task;
    uint8_t peer_bdname[ESP_BT_GAP_MAX_BDNAME_LEN + 1];
    esp_bd_addr_t peer_bda;
    esp_avrc_rn_evt_cap_mask_t avrc_peer_rn_cap;
    int connecting_intv;
    int intv_cnt;
    TimerHandle_t heart_beat_timer;
    uint8_t volume;
    uint32_t pkt_cnt;
} bt_ctx_t;

typedef void (*bt_core_cb_t)(bt_ctx_t* ctx, uint16_t event, void* event_data);

typedef struct {
    uint16_t id;
    uint16_t event;
    bt_core_cb_t cb;
    void* param;
} bt_msg_t;

// initialize Bluetooth and allocate resources
// ctx will be allocated and must be freed with bt_deinit
bt_ctx_t* bt_init(void);

// Starts the bluetooth task and queue
void bt_core_start(bt_ctx_t* ctx);

bool bt_core_dispatch(bt_ctx_t* ctx, bt_core_cb_t cback, uint16_t event,
                      void* params, uint32_t param_len);

// deinitialize Bluetooth and release resources
// ctx willl be freed
int bt_deinit(bt_ctx_t* ctx);
