#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "sdkconfig.h"
#include "nand_flash.h" 
#include "can_service.h"

static const char *TAG = "IPC_MAIN";

QueueHandle_t can_raw_queue = NULL;

// THAY ĐỔI: Thêm từ khóa volatile để tránh compiler tối ưu hóa sai biến dùng chung giữa các task
volatile uint32_t count = 0; 

void can_rx_task(void *pvParameters){
    twai_message_t rx_msg;
    while (1) {
        if (twai_receive(&rx_msg, portMAX_DELAY) == ESP_OK) {
            printf("Da nhan duoc CAN\n");
            count++;
            
            // Đảm bảo queue đã được khởi tạo trước khi send
            if (can_raw_queue != NULL) { 
                if (xQueueSend(can_raw_queue, &rx_msg, pdMS_TO_TICKS(10)) != pdPASS) {
                    ESP_LOGW(TAG, "CanRawQueue bị đầy! Rơi bản tin.");
                    printf("miss\n");
                }
            }
        }
    }
}

void app_main(){
    // THAY ĐỔI: Khởi tạo Queue TRƯỚC khi gọi can_init() để tránh mất dữ liệu ngay khi vừa bật CAN
    can_raw_queue = xQueueCreate(100, sizeof(twai_message_t));
    if (can_raw_queue == NULL) {
        ESP_LOGE(TAG, "Lỗi: Không thể khởi tạo can_raw_queue");
        return;
    }

    can_init();
    
    xTaskCreatePinnedToCore(can_rx_task, "CAN_RX_TASK",  4096, NULL, 20, NULL, 0);
    
    while(1){
        printf("So ban tin CAN nhan duoc la: %ld\n", count);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

