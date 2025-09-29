#include "bt_core.h"
#include "esp_bt.h"
#include "esp_bt_device.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
#include "esp_log.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "nvs_flash.h"
#include <stdio.h>
#include <string.h>

bt_ctx_t* bt_init(void) {
    /* Initialize NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* Release BLE memory */
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));

    /* Initialize Bluetooth controller */
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT));

    /* Initialize Bluedroid stack */
    esp_bluedroid_config_t bluedroid_cfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    bluedroid_cfg.ssp_en = true; // Enable SSP
    ESP_ERROR_CHECK(esp_bluedroid_init_with_cfg(&bluedroid_cfg));
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));

    if (esp_bluedroid_enable() != ESP_OK) {
        ESP_LOGE("BT_AV", "%s enable bluedroid failed", __func__);
        return nullptr;
    }

    esp_bt_sp_param_t param_type = ESP_BT_SP_IOCAP_MODE;
    esp_bt_io_cap_t iocap = ESP_BT_IO_CAP_IO;
    esp_bt_gap_set_security_param(param_type, &iocap, sizeof(uint8_t));

    bt_ctx_t* ctx = malloc(sizeof(bt_ctx_t));
    ctx->state = BT_STATE_ON;

    return ctx;
}

static bool bt_core_send_msg(bt_ctx_t* ctx, bt_msg_t* msg) {
    if (msg == NULL) {
        return false;
    }

    if (pdTRUE != xQueueSend(ctx->event_queue, msg, 10 / portTICK_PERIOD_MS)) {
        ESP_LOGE("BT_CORE", "%s xQueue send failed", __func__);
        return false;
    }

    return true;
}

bool bt_core_dispatch(bt_ctx_t* ctx, bt_core_cb_t cback, uint16_t event) {
    ESP_LOGD("BT_CORE", "%s event: 0x%x", __func__, event);

    bt_msg_t msg;
    memset(&msg, 0, sizeof(bt_msg_t));

    msg.id = 0; // Not used in this example
    msg.event = event;
    msg.cb = cback;

    return bt_core_send_msg(ctx, &msg);
}

static void bt_core_task_handler(void* arg) {
    bt_ctx_t* ctx = (bt_ctx_t*)arg;
    int event;
    for(;;) {
        if (pdTRUE == xQueueReceive(ctx->event_queue, &event, (TickType_t)portMAX_DELAY)) {
            ESP_LOGI("BT_CORE", "Received event: %d", event);
            // Handle events here
        }
    }
}

void bt_core_start(bt_ctx_t* ctx) {
    ctx->event_queue = xQueueCreate(10, sizeof(bt_msg_t));
    xTaskCreate(bt_core_task_handler, "BtCoreTask", 2048, ctx, 10, &ctx->event_task);
    ESP_LOGI("BT_CORE", "Bluetooth core started");
}


int bt_deinit(bt_ctx_t* ctx) {
    if (esp_bluedroid_disable() != ESP_OK) {
        ESP_LOGE("BT_AV", "%s disable bluedroid failed", __func__);
        return 1;
    }
    if (esp_bluedroid_deinit() != ESP_OK) {
        ESP_LOGE("BT_AV", "%s deinit bluedroid failed", __func__);
        return 1;
    }
    if (esp_bt_controller_disable() != ESP_OK) {
        ESP_LOGE("BT_AV", "%s disable controller failed", __func__);
        return 1;
    }
    if (esp_bt_controller_deinit() != ESP_OK) {
        ESP_LOGE("BT_AV", "%s deinit controller failed", __func__);
        return 1;
    }
    return 0;
}
