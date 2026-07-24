#include "can_service.h"
#include "esp_log.h"
#include "esp_task_wdt.h"

static const char *TAG = "CAN_SERVICE";

void can_init(void){
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_IO, CAN_RX_IO, TWAI_MODE_NORMAL);
    g_config.rx_queue_len = 256; 
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS(); // Tốc độ 500kbps
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << CAN_EN_IO),
        .mode = GPIO_MODE_OUTPUT,
        .intr_type = GPIO_INTR_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    gpio_config(&io_conf);

    gpio_set_level(CAN_EN_IO, 0);

    esp_err_t err = twai_driver_install(&g_config, &t_config, &f_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "twai_driver_install lỗi: %s", esp_err_to_name(err));
        return;
    }

    err = twai_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "twai_start lỗi: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "TWAI đã RUNNING");
}
