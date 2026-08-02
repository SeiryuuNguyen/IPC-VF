#include "usb-cdc.h"
#include "tusb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Hàm gửi dữ liệu qua USB CDC an toàn (Có chống tràn bộ đệm)
void usb_cdc_send_data(uint8_t itf, const uint8_t *data, size_t len) {
    size_t written = 0;
    while (written < len) {
        // Sử dụng API chuẩn của TinyUSB lõi để kiểm tra khoảng trống bộ đệm
        size_t available_space = tud_cdc_n_write_available(itf);
        
        if (available_space > 0) {
            // Tính toán lượng dữ liệu có thể nhét vừa vào bộ đệm lúc này
            size_t chunk_to_send = (len - written > available_space) ? available_space : (len - written);
            
            tinyusb_cdcacm_write_queue(itf, data + written, chunk_to_send);
            tinyusb_cdcacm_write_flush(itf, 0);
            
            written += chunk_to_send;
        } else {
            // Nếu bộ đệm đầy, cho Task ngủ 1ms để chờ máy tính đọc bớt dữ liệu
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
}

// Các hàm khởi tạo giữ nguyên
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