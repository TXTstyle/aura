
#include "freertos/idf_additions.h"
typedef enum {
    BT_STATE_UNINITIALIZED = -1,
    BT_STATE_OFF = 0,
    BT_STATE_ON,
    BT_STATE_CONNECTING,
    BT_STATE_CONNECTED,
    BT_STATE_DISCONNECTED,
} bt_state_t;

typedef struct {
    bt_state_t state;
    QueueHandle_t event_queue;
    TaskHandle_t event_task;
} bt_ctx_t;

typedef void (*bt_core_cb_t) (bt_ctx_t* ctx, uint16_t event, void* event_data);

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

bool bt_core_dispatch(bt_ctx_t* ctx, bt_core_cb_t cback, uint16_t event);

// deinitialize Bluetooth and release resources
// ctx willl be freed
int bt_deinit(bt_ctx_t *ctx);
