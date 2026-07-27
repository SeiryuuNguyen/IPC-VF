#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "sdkconfig.h"
#include "driver/ledc.h"

// Các thư viện của dự án
#include "usb-cdc.h"
#include "nand_flash.h" 
#include "can_service.h"
#include "ht1621.h"
#include "data_center.h"

static const char* TAG = "IPC-VF";

spi_nand_flash_device_t *nand;
spi_device_handle_t spi;
ht1621_t lcd_dev;

static QueueHandle_t app_queue;

// ================= CÁC BIẾN CHO OTA =================
volatile bool ota_in_progress = false;
esp_ota_handle_t update_handle = 0;
const esp_partition_t *update_partition = NULL;
size_t ota_bytes_written = 0;
size_t ota_total_size = 0;

typedef struct {
    uint8_t itf;
    size_t buf_len;
    uint8_t buf[CONFIG_TINYUSB_CDC_RX_BUFSIZE + 1];
} app_message_t;

uint64_t count = 0;
// Hàm giả định: Trích xuất thời gian (System Time) từ gói tin CAN
// Bạn cần thay thế logic này bằng ID CAN thực tế chứa RTC của xe
uint32_t extract_system_time_from_can() {
    // if (msg->identifier == 0x3F9) { // Giả sử ID 0x3F9 chứa thời gian
    //     return (msg->data[0] << 24) | (msg->data[1] << 16) | (msg->data[2] << 8) | msg->data[3];
    // }
    return count; 
}

void count_task(void *pvParameters){
    while(1){
        count++;
        extract_system_time_from_can();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/* =====================================================================
 * CALLBACK USB CDC: Phân luồng Lệnh và OTA Firmware
 * ===================================================================== */
void usb_cdc_rx_handler(int itf, cdcacm_event_t *event) {
    uint8_t rx_buf[CONFIG_TINYUSB_CDC_RX_BUFSIZE + 1]; 
    size_t rx_size = 0;
    esp_err_t ret = tinyusb_cdcacm_read(itf, rx_buf, CONFIG_TINYUSB_CDC_RX_BUFSIZE, &rx_size);
    
    if (ret == ESP_OK && rx_size > 0) {
        if (ota_in_progress) {
            // Đang Update Firmware -> Ghi thẳng vào phân vùng OTA
            esp_ota_write(update_handle, rx_buf, rx_size);
            ota_bytes_written += rx_size;
            
            if (ota_bytes_written >= ota_total_size) {
                esp_ota_end(update_handle);
                esp_ota_set_boot_partition(update_partition);
                ota_in_progress = false;
                
                char *msg = "UPDATE_SUCCESS\r\n";
                usb_cdc_send_data(itf, (uint8_t*)msg, strlen(msg));
                
                vTaskDelay(pdMS_TO_TICKS(1000));
                esp_restart(); // Reset mạch để chạy Firmware mới
            }
        } else {
            // Chế độ bình thường -> Gửi lệnh vào Queue
            app_message_t tx_msg = { .buf_len = rx_size, .itf = itf };
            memcpy(tx_msg.buf, rx_buf, rx_size);
            xQueueSendFromISR(app_queue, &tx_msg, NULL); 
        }
    }
}

/* =====================================================================
 * TASK QUẢN LÝ LỆNH USB (LIST, GET, UPDATE)
 * ===================================================================== */
void usb_cdc_task(void *pvParameters) {
    app_message_t msg;
    while (1) {
        if (xQueueReceive(app_queue, &msg, portMAX_DELAY)) {
            msg.buf[msg.buf_len] = '\0'; 
            
            // Lệnh 1: LIST - Liệt kê file
            if (strncmp((char *)msg.buf, "LIST", 4) == 0) {
                DIR *dir = opendir("/nand");
                if (dir) {
                    struct dirent *ent;
                    // Tăng từ 256 lên 300 để tránh cảnh báo Format Truncation
                    char list_buf[300]; 
                    while ((ent = readdir(dir)) != NULL) {
                        snprintf(list_buf, sizeof(list_buf), "FILE: %s\r\n", ent->d_name);
                        usb_cdc_send_data(msg.itf, (uint8_t *)list_buf, strlen(list_buf));
                    }
                    closedir(dir);
                }
                char *end = "---END_LIST---\r\n";
                usb_cdc_send_data(msg.itf, (uint8_t *)end, strlen(end));
            }
            
            // Lệnh 2: GET <filename> - Gửi file cho PC
            else if (strncmp((char *)msg.buf, "GET ", 4) == 0) {
                char filename[64];
                sscanf((char *)msg.buf, "GET %s", filename);
                
                char filepath[128];
                snprintf(filepath, sizeof(filepath), "/nand/%s", filename);
                
                struct stat st;
                if (stat(filepath, &st) == 0) {
                    // Gửi kích thước file trước
                    char size_msg[64];
                    snprintf(size_msg, sizeof(size_msg), "SIZE:%ld\r\n", st.st_size);
                    usb_cdc_send_data(msg.itf, (uint8_t *)size_msg, strlen(size_msg));
                    
                    // Gửi dữ liệu nhị phân
                    FILE *f = fopen(filepath, "rb");
                    uint8_t file_buf[512];
                    size_t read_bytes;
                    while ((read_bytes = fread(file_buf, 1, sizeof(file_buf), f)) > 0) {
                        usb_cdc_send_data(msg.itf, file_buf, read_bytes);
                        vTaskDelay(pdMS_TO_TICKS(1)); 
                    }
                    fclose(f);
                } else {
                    char *err = "ERROR: File not found\r\n";
                    usb_cdc_send_data(msg.itf, (uint8_t *)err, strlen(err));
                }
            }
            
            // Lệnh 3: UPDATE <size> - Chuẩn bị nhận OTA Firmware
            else if (strncmp((char *)msg.buf, "UPDATE ", 7) == 0) {
                sscanf((char *)msg.buf, "UPDATE %d", &ota_total_size);
                update_partition = esp_ota_get_next_update_partition(NULL);
                if (update_partition != NULL && ota_total_size > 0) {
                    esp_err_t err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &update_handle);
                    if (err == ESP_OK) {
                        ota_bytes_written = 0;
                        ota_in_progress = true; // Block các lệnh khác, bắt đầu nhận nhị phân
                        char *ready = "READY\r\n";
                        usb_cdc_send_data(msg.itf, (uint8_t *)ready, strlen(ready));
                    }
                }
            }
        }
    }
}

/* =====================================================================
 * TASK LƯU TRỮ (NAND LOGGER): Buffer 32KB, Timeout 3s & Xoay vòng file
 * ===================================================================== */
#define BUFFER_SIZE (32 * 1024) 
#define MAX_FILE_SIZE (10 * 1024 * 1024) // 10MB
#define MAX_FILES_COUNT 25 // Giới hạn tổng dung lượng ~250MB để tránh đầy NAND

// Hàm hỗ trợ: Xóa file cũ nhất khi đầy thẻ
void rotate_oldest_log() {
    DIR *dir = opendir("/nand");
    if (!dir) return;

    struct dirent *ent;
    // Tăng kích thước mảng lên 300
    char oldest_file[300] = ""; 
    uint32_t oldest_time = 0xFFFFFFFF;
    int file_count = 0;

    while ((ent = readdir(dir)) != NULL) {
        if (strstr(ent->d_name, "log_") != NULL) {
            file_count++;
            // Sửa thành unsigned int để khớp với định dạng %u của sscanf
            unsigned int file_time; 
            if (sscanf(ent->d_name, "log_%u.bin", &file_time) == 1) {
                if (file_time < oldest_time) {
                    oldest_time = file_time;
                    snprintf(oldest_file, sizeof(oldest_file), "/nand/%s", ent->d_name);
                }
            }
        }
    }
    closedir(dir);

    // Nếu số file vượt quá giới hạn, tiến hành xóa file
    if (file_count > MAX_FILES_COUNT && strlen(oldest_file) > 0) {
        unlink(oldest_file);
        ESP_LOGI(TAG, "Đã xóa file cũ để giải phóng dung lượng: %s", oldest_file);
    }
}

void nand_logger_task(void *arg) {
    twai_message_t can_msg;
    FILE *f = NULL;
    char current_filename[64] = "";
    uint32_t current_file_size = 0;
    
    // Cấp phát Buffer 32KB vào bộ nhớ PSRAM ngoài
    uint8_t *ram_buffer = (uint8_t *)malloc(BUFFER_SIZE);
    if (!ram_buffer) {
        ESP_LOGE(TAG, "Lỗi: Không đủ RAM để cấp phát 128KB! Hãy kiểm tra lại cấu hình PSRAM.");
        vTaskDelete(NULL);
    }
    size_t buffer_idx = 0;

    while(1) {
        // Đợi dữ liệu CAN với Timeout 3 giây (3000 ticks)
        if (xQueueReceive(can_raw_queue_for_NAND, &can_msg, pdMS_TO_TICKS(3000)) == pdTRUE) {
            
            // Nếu chưa có file nào đang mở, cần tạo file mới
            if (strlen(current_filename) == 0) {
                uint32_t sys_time = extract_system_time_from_can();
                if (sys_time != 0) {
                    // Chuyển %u thành %lu và ép kiểu sys_time sang unsigned long
                    snprintf(current_filename, sizeof(current_filename), "/nand/log_%lu.bin", (unsigned long)sys_time);
                    rotate_oldest_log(); // Kiểm tra dọn dẹp trước khi tạo
                    current_file_size = 0;
                }
            }

            // Ghi dữ liệu gói CAN vào Buffer RAM
            if (buffer_idx + sizeof(twai_message_t) <= BUFFER_SIZE) {
                memcpy(&ram_buffer[buffer_idx], &can_msg, sizeof(twai_message_t));
                buffer_idx += sizeof(twai_message_t);
            }

            // KHI BUFFER ĐẦY 128KB -> GHI XUỐNG NAND
            if (buffer_idx >= BUFFER_SIZE && strlen(current_filename) > 0) {
                f = fopen(current_filename, "ab"); // Mở file ở chế độ nối tiếp
                if (f) {
                    fwrite(ram_buffer, 1, buffer_idx, f);
                    fclose(f);
                    current_file_size += buffer_idx;
                }
                buffer_idx = 0; // Xóa đệm
                
                // Nếu file đã đạt giới hạn 10MB, xóa tên để lần sau tạo file mới
                if (current_file_size >= MAX_FILE_SIZE) {
                    current_filename[0] = '\0';
                }
            }
            
        } else {
            // TIMEOUT 3 GIÂY (Không nhận được dữ liệu mạng CAN)
            if (buffer_idx > 0 && strlen(current_filename) > 0) {
                ESP_LOGI(TAG, "Mạng CAN tĩnh lặng 3s. Tiến hành ghi %d bytes tồn đọng xuống NAND.", buffer_idx);
                f = fopen(current_filename, "ab");
                if (f) {
                    fwrite(ram_buffer, 1, buffer_idx, f);
                    fclose(f); // Đóng file ngay lập tức theo yêu cầu
                    current_file_size += buffer_idx;
                }
                buffer_idx = 0;
                
                // Kiểm tra giới hạn 10MB
                if (current_file_size >= MAX_FILE_SIZE) {
                    current_filename[0] = '\0';
                }
            }
        }
    }
}

/* =====================================================================
 * Hàm khởi tạo icon màn hình
 * ===================================================================== */
void icon_init(void){
    ht1621_set_icon(&lcd_dev, ICON_TRIP, 1);
    ht1621_set_icon(&lcd_dev, ICON_KM_TRIP, 1);
    ht1621_set_icon(&lcd_dev, ICON_AVERAGE_POWER, 1);
    ht1621_set_icon(&lcd_dev, ICON_KM_H_AVERAGE_POWER, 1);
    ht1621_set_icon(&lcd_dev, ICON_AVERAGE_SPEED, 1);
    ht1621_set_icon(&lcd_dev, ICON_KWH_100KM_AVERAGE_SPEED, 1);
    ht1621_set_icon(&lcd_dev, ICON_ODO, 1);
    ht1621_set_icon(&lcd_dev, ICON_KM_ODO, 1);
    ht1621_set_icon(&lcd_dev, ICON_SOC, 1);
    ht1621_set_icon(&lcd_dev, ICON_PERCENT_SOC, 1);
    ht1621_set_icon(&lcd_dev, ICON_RANGE, 1);
    ht1621_set_icon(&lcd_dev, ICON_KM_RANGE, 1);
    ht1621_set_icon(&lcd_dev, ICON_RND_POSITON_LIGHT, 1);
    ht1621_set_icon(&lcd_dev, ICON_KM_H_SPEED, 1);
    ht1621_set_icon(&lcd_dev, ICON_BATTERY, 1);
}

/* =====================================================================
 * TASK HIỂN THỊ (DISPLAY)
 * ===================================================================== */
void display_task(void *arg) {
    twai_message_t can_msg;
    uint16_t vehicle_speed = 0;
    uint16_t vehicle_soc = 0;
    uint8_t vehicle_gear= 0;
    bool vehicle_turn_left = 0;
    bool vehicle_turn_right = 0;
    bool vehicle_beam_low = 0;
    bool vehicle_beam_high = 0;

    while(1) {
        if (xQueueReceive(can_raw_queue_for_display, &can_msg, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (can_msg.identifier == 0x1A6) vehicle_speed = can_msg.data[0];
            if (can_msg.identifier == 0x2B4) vehicle_soc = can_msg.data[0];
            switch(can_msg.identifier){
                case 0x40D:
                    vehicle_speed = can_msg.data[2] << 1 | ((can_msg.data[3] >> 7) & 0x01);
                break;
                case 0x162:
                    vehicle_gear = (can_msg.data[2] >> 2) & 0x07;
                break;
                case 0x106:
                    vehicle_turn_left = can_msg.data[2] & 0x01;
                    vehicle_turn_right = (can_msg.data[2] >> 1) & 0x01;
                    vehicle_beam_high = (can_msg.data[2] >> 6) & 0x01;
                    vehicle_beam_low = (can_msg.data[2] >> 4) & 0x01;
                break;
                case 0x37D:
                    vehicle_soc = (uint16_t)(((can_msg.data[2] << 2) | ((can_msg.data[3] >> 6) & 0x03)) * 0.1);
                break;
            }
        }
        ht1621_clear(&lcd_dev);
        icon_init();
        ht1621_print_speed(&lcd_dev, vehicle_speed);
        ht1621_print_soc(&lcd_dev, vehicle_soc);
        if(vehicle_gear == 2){
            ht1621_set_icon(&lcd_dev, ICON_R_POSITION, 1);
            ht1621_set_icon(&lcd_dev, ICON_N_POSITION, 0);
            ht1621_set_icon(&lcd_dev, ICON_D_POSITION, 0);
        } else if(vehicle_gear == 3){
            ht1621_set_icon(&lcd_dev, ICON_R_POSITION, 0);
            ht1621_set_icon(&lcd_dev, ICON_N_POSITION, 1);
            ht1621_set_icon(&lcd_dev, ICON_D_POSITION, 0);
        } else if(vehicle_gear == 4){
            ht1621_set_icon(&lcd_dev, ICON_R_POSITION, 0);
            ht1621_set_icon(&lcd_dev, ICON_N_POSITION, 0);
            ht1621_set_icon(&lcd_dev, ICON_D_POSITION, 1);
        }
        ht1621_set_icon(&lcd_dev, ICON_LEFT_TURN_SIGNAL, vehicle_turn_left);
        ht1621_set_icon(&lcd_dev, ICON_RIGHT_TURN_SIGNAL, vehicle_turn_right);
        ht1621_set_icon(&lcd_dev, ICON_HIGH_BEAM, vehicle_beam_high);
        ht1621_set_icon(&lcd_dev, ICON_LOW_BEAM, vehicle_beam_low);
        ht1621_update(&lcd_dev);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

//Ham khoi tao backlight
void backlight_init(void){
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_LOW_SPEED_MODE,
        .timer_num        = LEDC_TIMER_0,
        .duty_resolution  = LEDC_TIMER_10_BIT,
        .freq_hz          = 1000,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));
    ledc_channel_config_t ledc_channel = {
        .speed_mode     = LEDC_LOW_SPEED_MODE,
        .channel        = LEDC_CHANNEL_0,
        .timer_sel      = LEDC_TIMER_0,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = GPIO_NUM_36,
        .duty           = 0,
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 1023));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0));
}

/* =====================================================================
 * HÀM MAIN
 * ===================================================================== */
void app_main(void) {
    app_queue = xQueueCreate(10, sizeof(app_message_t));
    backlight_init();
    data_center_init();

    init_nand_flash(&nand, &spi);

    lcd_dev.cs1_pin   = GPIO_NUM_40;
    lcd_dev.cs2_pin   = GPIO_NUM_41;
    lcd_dev.wr_pin    = GPIO_NUM_38;
    lcd_dev.data_pin  = GPIO_NUM_39;
    ht1621_init(&lcd_dev);
    ht1621_clear(&lcd_dev);

    tinyusb_cdc_config_t usb_cfg = {
        .cdc_port = TINYUSB_CDC_ACM_0,
        .rx_callback = usb_cdc_rx_handler,
    };
    usb_cdc_init(&usb_cfg);
    can_init();

    xTaskCreatePinnedToCore(display_task,     "display_task", 4096, NULL, 4, NULL, 1);
    xTaskCreatePinnedToCore(nand_logger_task, "nand_task",    8192, NULL, 3, NULL, 0); // Tăng Stack vì có xử lý File System
    xTaskCreatePinnedToCore(usb_cdc_task,     "usb_task",     4096, NULL, 2, NULL, 0);
    xTaskCreatePinnedToCore(count_task, "count_task", 2048, NULL, 1, NULL, 1);
}