#pragma once
#include <stdint.h>
#include "bt_core.h"

typedef enum {
    BT_A2DP_STATE_IDLE,
    BT_A2DP_STATE_DISCOVERING,
    BT_A2DP_STATE_DISCOVERED,
    BT_A2DP_STATE_UNCONNECTED,
    BT_A2DP_STATE_CONNECTING,
    BT_A2DP_STATE_CONNECTED,
    BT_A2DP_STATE_DISCONNECTING,
} a2dp_state_t;

void bt_a2dp_stack_event(bt_ctx_t* ctx, uint16_t event, void* event_data);
