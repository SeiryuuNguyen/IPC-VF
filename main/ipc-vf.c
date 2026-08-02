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
#include "audio_service.h"

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

/* =====================================================================
 * CALLBACK USB CDC: Phân luồng Lệnh và OTA Firmware
 * ===================================================================== */
void usb_cdc_rx_handler(int itf, cdcacm_event_t *event) {
    uint8_t rx_buf[CONFIG_TINYUSB_CDC_RX_BUFSIZE + 1]; 
    size_t rx_size = 0;
    esp_err_t ret = tinyusb_cdcacm_read(itf, rx_buf, CONFIG_TINYUSB_CDC_RX_BUFSIZE, &rx_size);
    
    if (ret == ESP_OK && rx_size > 0) {
        if (ota_in_progress) {
            esp_ota_write(update_handle, rx_buf, rx_size);
            ota_bytes_written += rx_size;
            
            if (ota_bytes_written >= ota_total_size) {
                esp_ota_end(update_handle);
                esp_ota_set_boot_partition(update_partition);
                ota_in_progress = false;
                
                char *msg = "UPDATE_SUCCESS\r\n";
                usb_cdc_send_data(itf, (uint8_t*)msg, strlen(msg));
                
                vTaskDelay(pdMS_TO_TICKS(1000));
                esp_restart(); 
            }
        } else {
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
            
            if (strncmp((char *)msg.buf, "LIST", 4) == 0) {
                DIR *dir = opendir("/nand");
                if (dir) {
                    struct dirent *ent;
                    char list_buf[300];
                    while ((ent = readdir(dir)) != NULL) {
                        char filepath[300]; 
                        snprintf(filepath, sizeof(filepath), "/nand/%s", ent->d_name);
                        
                        struct stat st;
                        uint32_t fsize = 0;
                        if (stat(filepath, &st) == 0) {
                            fsize = st.st_size;
                        }
                        
                        snprintf(list_buf, sizeof(list_buf), "FILE: %s (Size: %lu bytes)\r\n", ent->d_name, (unsigned long)fsize);
                        usb_cdc_send_data(msg.itf, (uint8_t *)list_buf, strlen(list_buf));
                    }
                    closedir(dir);
                }
                char *end = "---END_LIST---\r\n";
                usb_cdc_send_data(msg.itf, (uint8_t *)end, strlen(end));
            }
            else if (strncmp((char *)msg.buf, "GET ", 4) == 0) {
                char filename[64];
                sscanf((char *)msg.buf, "GET %s", filename);
                
                char filepath[128];
                snprintf(filepath, sizeof(filepath), "/nand/%s", filename);
                
                struct stat st;
                if (stat(filepath, &st) == 0) {
                    char size_msg[64];
                    snprintf(size_msg, sizeof(size_msg), "SIZE:%ld\r\n", st.st_size);
                    usb_cdc_send_data(msg.itf, (uint8_t *)size_msg, strlen(size_msg));
                    
                    FILE *f = fopen(filepath, "rb");
                    if (f) {
                        uint8_t file_buf[1024];
                        size_t read_bytes;
                        while ((read_bytes = fread(file_buf, 1, sizeof(file_buf), f)) > 0) {
                            usb_cdc_send_data(msg.itf, file_buf, read_bytes);
                            vTaskDelay(pdMS_TO_TICKS(1)); 
                        }
                        fclose(f);
                    }
                } else {
                    char *err = "ERROR: File not found\r\n";
                    usb_cdc_send_data(msg.itf, (uint8_t *)err, strlen(err));
                }
            }
            else if (strncmp((char *)msg.buf, "UPDATE ", 7) == 0) {
                sscanf((char *)msg.buf, "UPDATE %lu", (unsigned long*)&ota_total_size);
                update_partition = esp_ota_get_next_update_partition(NULL);
                if (update_partition != NULL && ota_total_size > 0) {
                    esp_err_t err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &update_handle);
                    if (err == ESP_OK) {
                        ota_bytes_written = 0;
                        ota_in_progress = true; 
                        char *ready = "READY\r\n";
                        usb_cdc_send_data(msg.itf, (uint8_t *)ready, strlen(ready));
                    }
                }
            }
        }
    }
}

/* =====================================================================
 * TASK LƯU TRỮ (NAND LOGGER): Ghi liên tục, nối file 10MB
 * ===================================================================== */
#define MAX_FILE_SIZE (10 * 1024 * 1024) 
#define MAX_FILES_COUNT 24
#define FSYNC_INTERVAL_BYTES (256 * 1024) 

void rotate_oldest_log() {
    DIR *dir = opendir("/nand");
    if (!dir) return;

    struct dirent *ent;
    char oldest_file[300] = "";
    uint32_t oldest_idx = 0xFFFFFFFF;
    int file_count = 0;

    while ((ent = readdir(dir)) != NULL) {
        if (strstr(ent->d_name, "can_") != NULL) {
            file_count++;
            uint32_t idx;
            if (sscanf(ent->d_name, "can_%lu.bin", (unsigned long*)&idx) == 1) {
                if (idx < oldest_idx) {
                    oldest_idx = idx;
                    snprintf(oldest_file, sizeof(oldest_file), "/nand/%s", ent->d_name);
                }
            }
        }
    }
    closedir(dir);

    if (file_count > MAX_FILES_COUNT && strlen(oldest_file) > 0) {
        unlink(oldest_file);
        ESP_LOGI(TAG, "Đã xóa file cũ giải phóng dung lượng: %s", oldest_file);
    }
}

void get_active_log_file(char* filename, uint32_t* current_size) {
    DIR *dir = opendir("/nand");
    if (!dir) {
        strcpy(filename, "/nand/can_1.bin");
        *current_size = 0;
        return;
    }

    struct dirent *ent;
    uint32_t max_idx = 0;
    char temp_name[128];

    while ((ent = readdir(dir)) != NULL) {
        uint32_t idx;
        if (sscanf(ent->d_name, "can_%lu.bin", (unsigned long*)&idx) == 1) {
            if (idx > max_idx) max_idx = idx;
        }
    }
    closedir(dir);

    if (max_idx == 0) {
        strcpy(filename, "/nand/can_1.bin");
        *current_size = 0;
        return;
    }

    snprintf(temp_name, sizeof(temp_name), "/nand/can_%lu.bin", (unsigned long)max_idx);
    struct stat st;
    if (stat(temp_name, &st) == 0) {
        if (st.st_size < MAX_FILE_SIZE) {
            strcpy(filename, temp_name);
            *current_size = st.st_size;
        } else {
            snprintf(filename, 64, "/nand/can_%lu.bin", (unsigned long)(max_idx + 1));
            *current_size = 0;
        }
    } else {
        strcpy(filename, temp_name);
        *current_size = 0;
    }
}

void nand_logger_task(void *arg) {
    char current_filename[64] = "";
    uint32_t current_file_size = 0;
    uint8_t write_chunk[4096]; 
    
    // Biến theo dõi dung lượng để fsync định kỳ
    size_t bytes_since_last_sync = 0; 
    
    get_active_log_file(current_filename, &current_file_size);
    FILE *f = fopen(current_filename, "ab"); 

    if (f) {
        ESP_LOGI(TAG, "Bắt đầu ghi vào: %s (Size: %lu bytes)", current_filename, (unsigned long)current_file_size);
    }

    while(1) {
        size_t received_bytes = xStreamBufferReceive(can_nand_stream, write_chunk, sizeof(write_chunk), pdMS_TO_TICKS(3000));
        
        if (received_bytes > 0) {
            if (f == NULL) {
                rotate_oldest_log(); 
                f = fopen(current_filename, "ab");
            }

            if (f) {
                size_t written_bytes = fwrite(write_chunk, 1, received_bytes, f);
                
                current_file_size += written_bytes;
                bytes_since_last_sync += written_bytes;

                // CHỈ FSYNC KHI ĐỦ 256KB (Giảm tải 99% cho NAND)
                if (bytes_since_last_sync >= FSYNC_INTERVAL_BYTES) {
                    fflush(f);
                    fsync(fileno(f)); 
                    bytes_since_last_sync = 0;
                    // In log để bạn biết file đang lớn lên
                    // ESP_LOGI(TAG, "Đã đồng bộ mốc %lu bytes", (unsigned long)current_file_size);
                }

                if (current_file_size >= MAX_FILE_SIZE || written_bytes < received_bytes) {
                    // Đóng file an toàn
                    fflush(f);
                    fsync(fileno(f));
                    fclose(f);
                    f = NULL;
                    bytes_since_last_sync = 0;
                    
                    uint32_t next_idx = 1;
                    sscanf(current_filename, "/nand/can_%lu.bin", (unsigned long*)&next_idx);
                    next_idx++;
                    snprintf(current_filename, sizeof(current_filename), "/nand/can_%lu.bin", (unsigned long)next_idx);
                    current_file_size = 0;
                    ESP_LOGI(TAG, "Tạo file 10MB mới: %s", current_filename);
                }
            }
        } else {
            // Khi tắt máy xe (Mạng CAN tĩnh lặng), vét sạch dữ liệu và đồng bộ hiển thị
            if (f) {
                fflush(f);
                fsync(fileno(f));
                bytes_since_last_sync = 0;
            }
        }
    }
}

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
    while(1) {
        // ht1621_clear(&lcd_dev);
        icon_init();
        
        // ----------------- VẼ ĐÈN BÁO TRẠNG THÁI (ICON) -----------------
        ht1621_set_icon(&lcd_dev, ICON_LEFT_TURN_SIGNAL, icon_turn_left);
        ht1621_set_icon(&lcd_dev, ICON_POSITION_LIGHT, icon_position_light);
        ht1621_set_icon(&lcd_dev, ICON_LOW_BEAM, icon_low_beam);
        ht1621_set_icon(&lcd_dev, ICON_HIGH_BEAM, icon_high_beam);
        ht1621_set_icon(&lcd_dev, ICON_FOG_LIGHT, icon_fog_light);
        ht1621_set_icon(&lcd_dev, ICON_PARKING_BRAKE_STATUS, icon_parking_brake);
        ht1621_set_icon(&lcd_dev, ICON_SEAT_BELT_WARNING, icon_seat_belt);
        ht1621_set_icon(&lcd_dev, ICON_DOOR_AJAR_WARNING, icon_door_open);
        ht1621_set_icon(&lcd_dev, ICON_AIRBAG_WARNING, icon_airbag_fault);
        ht1621_set_icon(&lcd_dev, ICON_12V_BATTERY_FAULT, icon_low_battery);
        ht1621_set_icon(&lcd_dev, ICON_RIGHT_TURN_SIGNAL, icon_turn_right);
        ht1621_set_icon(&lcd_dev, ICON_BRAKE_WARNING, icon_brake_sys_fault);
        ht1621_set_icon(&lcd_dev, ICON_BMS_BATTERY_TEMP_ERROR, icon_bms_temp_fault);
        ht1621_set_icon(&lcd_dev, ICON_HIGHT_VOLTAGE_FAULT, icon_high_voltage_fault);
        ht1621_set_icon(&lcd_dev, ICON_POWERTRAIN_FAULT, icon_powertrain_fault);
        ht1621_set_icon(&lcd_dev, ICON_TCS_WARNING, icon_tcs_fault);
        ht1621_set_icon(&lcd_dev, ICON_ABS_WARNING, icon_abs_fault);
        ht1621_set_icon(&lcd_dev, ICON_POWER_LIMITED_FAULT, icon_power_limit);
        ht1621_set_icon(&lcd_dev, ICON_EPS_WARNING, icon_eps_fault);
        ht1621_set_icon(&lcd_dev, ICON_CHARGING_STATUS, icon_charging);
        ht1621_set_icon(&lcd_dev, ICON_FULLY_CHARGED, icon_charge_full);
        ht1621_set_icon(&lcd_dev, ICON_CHARGER_NOT_DETECTED, icon_gun_not_confirmed);
        
        // Trạng thái số xe (0=N, 1=R, 2=D)
        ht1621_set_icon(&lcd_dev, ICON_N_POSITION, (vehicle_gear == 0));
        ht1621_set_icon(&lcd_dev, ICON_R_POSITION, (vehicle_gear == 1));
        ht1621_set_icon(&lcd_dev, ICON_D_POSITION, (vehicle_gear == 2));
        ht1621_set_icon(&lcd_dev, ICON_READRY, vehicle_ready);

        // ----------------- VẠCH PIN TUYẾN TÍNH -----------------
        ht1621_set_icon(&lcd_dev, ICON_BATTERY_10,  (vehicle_soc >= 10));
        ht1621_set_icon(&lcd_dev, ICON_BATTERY_20,  (vehicle_soc >= 20));
        ht1621_set_icon(&lcd_dev, ICON_BATTERY_30,  (vehicle_soc >= 30));
        ht1621_set_icon(&lcd_dev, ICON_BATTERY_40,  (vehicle_soc >= 40));
        ht1621_set_icon(&lcd_dev, ICON_BATTERY_50,  (vehicle_soc >= 50));
        ht1621_set_icon(&lcd_dev, ICON_BATTERY_60,  (vehicle_soc >= 60));
        ht1621_set_icon(&lcd_dev, ICON_BATTERY_70,  (vehicle_soc >= 70));
        ht1621_set_icon(&lcd_dev, ICON_BATTERY_80,  (vehicle_soc >= 80));
        ht1621_set_icon(&lcd_dev, ICON_BATTERY_90,  (vehicle_soc >= 90));
        ht1621_set_icon(&lcd_dev, ICON_BATTERY_1OO, (vehicle_soc >= 100)); // Lưu ý: Tên biến kết thúc bằng chữ 'O' theo khai báo cũ của bạn

        // Các Icon chữ đi kèm buộc phải hiển thị tĩnh
        ht1621_set_icon(&lcd_dev, ICON_KM_H_SPEED, 1);
        ht1621_set_icon(&lcd_dev, ICON_PERCENT_SOC, 1);
        ht1621_set_icon(&lcd_dev, ICON_KM_RANGE, 1);
        ht1621_set_icon(&lcd_dev, ICON_KM_ODO, 1);
        ht1621_set_icon(&lcd_dev, ICON_KM_TRIP, 1);
        ht1621_set_icon(&lcd_dev, ICON_BATTERY, 1);

        // ----------------- VẼ THÔNG SỐ VẬN HÀNH (DATA) -----------------
        ht1621_print_speed(&lcd_dev, vehicle_speed);
        ht1621_print_soc(&lcd_dev, vehicle_soc);
        ht1621_print_range(&lcd_dev, vehicle_range);
        ht1621_print_odo(&lcd_dev, vehicle_odo);
        ht1621_print_trip(&lcd_dev, vehicle_trip);
        ht1621_print_average_speed(&lcd_dev, vehicle_avg_speed);
        ht1621_print_average_power(&lcd_dev, vehicle_avg_power);

        ht1621_update(&lcd_dev);
        
        // Làm mới màn hình ở tốc độ khoảng ~20 FPS
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

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
    backlight_init();
    app_queue = xQueueCreate(10, sizeof(app_message_t));
    data_center_init();
    audio_service_start();

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
    xTaskCreatePinnedToCore(nand_logger_task, "nand_task",    8192, NULL, 3, NULL, 0); 
    xTaskCreatePinnedToCore(usb_cdc_task,     "usb_task",     8192, NULL, 2, NULL, 0);

    while(1){
        // audio_play(AUDIO_EFFECT_TURN_SIGNAL);
        // vTaskDelay(pdMS_TO_TICKS(10000));
        // audio_play(AUDIO_EFFECT_LOW_BATTERY);
        // vTaskDelay(pdMS_TO_TICKS(10000));
        // audio_play(AUDIO_EFFECT_SYSTEM_FAULT);
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}