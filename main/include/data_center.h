#ifndef DATA_CENTER_H
#define DATA_CENTER_H

#include "driver/twai.h"
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"
#include <stdint.h>
#include <stdbool.h>

extern StreamBufferHandle_t can_nand_stream;

// ================= NHÓM 1: TRẠNG THÁI & CẢNH BÁO =================
extern volatile bool icon_turn_left;
extern volatile bool icon_position_light;
extern volatile bool icon_low_beam;
extern volatile bool icon_high_beam;
extern volatile bool icon_fog_light;
extern volatile bool icon_parking_brake;
extern volatile bool icon_seat_belt;
extern volatile bool icon_door_open;
extern volatile bool icon_airbag_fault;
extern volatile bool icon_low_battery;
extern volatile bool icon_turn_right;
extern volatile bool icon_brake_sys_fault;
extern volatile bool icon_bms_temp_fault;
extern volatile bool icon_high_voltage_fault;
extern volatile bool icon_powertrain_fault;
extern volatile bool icon_tcs_fault;
extern volatile bool icon_abs_fault;
extern volatile bool icon_power_limit;
extern volatile bool icon_eps_fault;
extern volatile bool icon_charging;
extern volatile bool icon_charge_full;
extern volatile bool icon_gun_not_confirmed;

// ================= NHÓM 2: THÔNG SỐ VẬN HÀNH =================
extern volatile uint16_t vehicle_speed;       
extern volatile uint16_t vehicle_soc;         
extern volatile uint16_t vehicle_range;       
extern volatile uint32_t vehicle_odo;         
extern volatile float vehicle_trip;           
extern volatile float vehicle_avg_speed;      
extern volatile float vehicle_avg_power;      
extern volatile uint8_t vehicle_gear;         
extern volatile bool vehicle_ready;           

void data_center_init(void);

#endif