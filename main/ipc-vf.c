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
=======
#include <stdlib.h>
#include "esp_log.h"
#include "nand_flash.h"

void app_main(void)
{
    esp_err_t ret;

    //1. Thiết lập bus SPI và khởi tạo chip SPI Flash bên ngoài
    spi_device_handle_t spi;
    spi_nand_flash_device_t *flash;

    init_nand_flash(&flash, &spi);
    if(flash == NULL){
        return;
    }

    esp_vfs_fat_mount_config_t config = {
        .max_files = 5,
        .format_if_mount_failed = true,
        .allocation_unit_size = 16 * 1024,
    };
    ret = esp_vfs_fat_nand_mount(base_path, flash, &config);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem");
        }
        return;
    }

    // Print FAT FS size information
    uint64_t bytes_total, bytes_free;
    esp_vfs_fat_info(base_path, &bytes_total, &bytes_free);
    ESP_LOGI(TAG, "FAT FS: %" PRIu64 " kB total, %" PRIu64 " kB free", bytes_total / 1024, bytes_free / 1024);

    // Create a file in FAT FS
    ESP_LOGI(TAG, "Opening file");
    FILE *f = fopen("/nand/hello.txt", "wb");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for writing");
        return;
    }
    fprintf(f, "Written using ESP-IDF %s\n", esp_get_idf_version());
    fclose(f);
    ESP_LOGI(TAG, "File written");

    // Open file for reading
    ESP_LOGI(TAG, "Reading file");
    f = fopen("/nand/hello.txt", "rb");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for reading");
        return;
    }
    char line[128];
    fgets(line, sizeof(line), f);
    fclose(f);
    // strip newline
    char *pos = strchr(line, '\n');
    if (pos) {
        *pos = '\0';
    }
    ESP_LOGI(TAG, "Read from file: '%s'", line);

    esp_vfs_fat_info(base_path, &bytes_total, &bytes_free);
    ESP_LOGI(TAG, "FAT FS: %" PRIu64 " kB total, %" PRIu64 " kB free", bytes_total / 1024, bytes_free / 1024);

    esp_vfs_fat_nand_unmount(base_path, flash);

    deinit_nand_flash(flash, spi);
}

