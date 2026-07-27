#include "usb-cdc.h"

void usb_cdc_send_data(uint8_t itf, const uint8_t *data, size_t len) {
    // Đẩy dữ liệu vào hàng đợi của USB CDC
    tinyusb_cdcacm_write_queue(itf, data, len);
    // Yêu cầu USB xả hàng đợi (gửi đi)
    tinyusb_cdcacm_write_flush(itf, 0);
}

void usb_cdc_init(tinyusb_cdc_config_t *config){
    const tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();
    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));
    tinyusb_config_cdcacm_t acm_cfg = {
        .cdc_port = config->cdc_port,
        .callback_rx = config->rx_callback, 
        .callback_rx_wanted_char = NULL,
        .callback_line_state_changed = NULL,
        .callback_line_coding_changed = NULL
    };
    ESP_ERROR_CHECK(tinyusb_cdcacm_init(&acm_cfg));
}

void usb_cdc_deinit(void){
    tinyusb_driver_uninstall();
}