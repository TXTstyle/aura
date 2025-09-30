#include <stdio.h>
#include "bt_core.h"
#include "bt_a2dp.h"
#include "freertos/FreeRTOS.h"

void app_main(void) {
    bt_ctx_t* bt_ctx = bt_init();
    if (bt_ctx->state == BT_STATE_UNINITIALIZED) {
        printf("Bluetooth initialization failed\n");
        return;
    }
    printf("Bluetooth initialized successfully\n");
    bt_core_start(bt_ctx);
    bt_core_dispatch(bt_ctx, &bt_a2dp_stack_event, 0, nullptr, 0);

    if (bt_deinit(bt_ctx) != 0) {
        printf("Bluetooth deinitialization failed\n");
        return;
    }
    printf("Bluetooth deinitialized successfully\n");
}
