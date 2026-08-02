#include "can_service.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "data_center.h"
#include "audio_service.h"

static const char *TAG = "CAN_SERVICE";

static void can_process_task(void *arg){
    twai_message_t rx_msg; // Đã thêm lại biến lưu gói tin CAN
    
    while (1) {
        esp_err_t ret = twai_receive(&rx_msg, pdMS_TO_TICKS(100)); // Đã thêm lại hàm nhận
        
        if (ret == ESP_OK) {
            switch (rx_msg.identifier) {
                // --------- BẢNG 1: TRẠNG THÁI ---------
                case 0x300: 
                    if (rx_msg.data[0] == 1 && !icon_turn_left) audio_play(AUDIO_EFFECT_TURN_SIGNAL);
                    icon_turn_left = rx_msg.data[0]; 
                    break;
                case 0x301: icon_position_light = rx_msg.data[0]; break;
                case 0x302: icon_low_beam = rx_msg.data[0]; break;
                case 0x303: icon_high_beam = rx_msg.data[0]; break;
                case 0x304: icon_fog_light = rx_msg.data[0]; break;
                case 0x305: icon_parking_brake = rx_msg.data[0]; break;
                case 0x306: icon_seat_belt = rx_msg.data[0]; break;
                case 0x307: icon_door_open = rx_msg.data[0]; break;
                case 0x308: icon_airbag_fault = rx_msg.data[0]; break;
                case 0x309: 
                    if (rx_msg.data[0] == 1 && !icon_low_battery) audio_play(AUDIO_EFFECT_LOW_BATTERY);
                    icon_low_battery = rx_msg.data[0]; 
                    break;
                case 0x30A: 
                    if (rx_msg.data[0] == 1 && !icon_turn_right) audio_play(AUDIO_EFFECT_TURN_SIGNAL);
                    icon_turn_right = rx_msg.data[0]; 
                    break;
                case 0x30B: icon_brake_sys_fault = rx_msg.data[0]; break;
                case 0x30C: icon_bms_temp_fault = rx_msg.data[0]; break;
                case 0x30D: icon_high_voltage_fault = rx_msg.data[0]; break;
                case 0x30E: icon_powertrain_fault = rx_msg.data[0]; break;
                case 0x30F: 
                    if (rx_msg.data[0] == 1 && !icon_tcs_fault) audio_play(AUDIO_EFFECT_SYSTEM_FAULT);
                    icon_tcs_fault = rx_msg.data[0]; 
                    break;
                case 0x310: icon_abs_fault = rx_msg.data[0]; break;
                case 0x311: icon_power_limit = rx_msg.data[0]; break;
                case 0x312: icon_eps_fault = rx_msg.data[0]; break;
                case 0x313: icon_charging = rx_msg.data[0]; break;
                case 0x314: icon_charge_full = rx_msg.data[0]; break;
                case 0x315: icon_gun_not_confirmed = rx_msg.data[0]; break;

                // --------- BẢNG 2: THÔNG SỐ ---------
                case 0x400: vehicle_speed = ((rx_msg.data[1] << 8) | rx_msg.data[0]) / 10; break;
                case 0x401: vehicle_soc = ((rx_msg.data[1] << 8) | rx_msg.data[0]) / 10; break;
                case 0x402: vehicle_range = (rx_msg.data[1] << 8) | rx_msg.data[0]; break;
                case 0x403: vehicle_odo = ((rx_msg.data[3] << 24) | (rx_msg.data[2] << 16) | (rx_msg.data[1] << 8) | rx_msg.data[0]) / 10; break;
                case 0x404: {
                    uint32_t trip_raw = (rx_msg.data[2] << 16) | (rx_msg.data[1] << 8) | rx_msg.data[0];
                    vehicle_trip = trip_raw / 10.0f;
                    break;
                }
                case 0x405: vehicle_avg_speed = ((rx_msg.data[1] << 8) | rx_msg.data[0]) / 10.0f; break;
                case 0x406: vehicle_avg_power = ((rx_msg.data[1] << 8) | rx_msg.data[0]) / 10.0f; break;
                case 0x407: vehicle_gear = rx_msg.data[0]; break;
                case 0x408: vehicle_ready = rx_msg.data[0]; break;
            }

            xStreamBufferSend(can_nand_stream, &rx_msg, sizeof(twai_message_t), 0);
        } else if (ret == ESP_ERR_TIMEOUT) {
            twai_status_info_t status_info;
            twai_get_status_info(&status_info);
            if (status_info.state == TWAI_STATE_BUS_OFF) {
                ESP_LOGE(TAG, "Bus-Off, recovery...");
                twai_initiate_recovery(); 
            }
        }
    }
}

void can_init(void){
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_IO, CAN_RX_IO, TWAI_MODE_NORMAL);
    g_config.rx_queue_len = 256; 
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS(); 
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
    
    ESP_LOGI(TAG, "TWAI RUNNING");
    xTaskCreatePinnedToCore(can_process_task, "can_process_task", 4096, NULL, 5, NULL, 1);
}