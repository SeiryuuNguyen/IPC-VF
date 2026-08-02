#include "data_center.h"

StreamBufferHandle_t can_nand_stream = NULL;

volatile bool icon_turn_left = false;
volatile bool icon_position_light = false;
volatile bool icon_low_beam = false;
volatile bool icon_high_beam = false;
volatile bool icon_fog_light = false;
volatile bool icon_parking_brake = false;
volatile bool icon_seat_belt = false;
volatile bool icon_door_open = false;
volatile bool icon_airbag_fault = false;
volatile bool icon_low_battery = false;
volatile bool icon_turn_right = false;
volatile bool icon_brake_sys_fault = false;
volatile bool icon_bms_temp_fault = false;
volatile bool icon_high_voltage_fault = false;
volatile bool icon_powertrain_fault = false;
volatile bool icon_tcs_fault = false;
volatile bool icon_abs_fault = false;
volatile bool icon_power_limit = false;
volatile bool icon_eps_fault = false;
volatile bool icon_charging = false;
volatile bool icon_charge_full = false;
volatile bool icon_gun_not_confirmed = false;

volatile uint16_t vehicle_speed = 0;       
volatile uint16_t vehicle_soc = 0;         
volatile uint16_t vehicle_range = 0;       
volatile uint32_t vehicle_odo = 0;         
volatile float vehicle_trip = 0.0f;           
volatile float vehicle_avg_speed = 0.0f;      
volatile float vehicle_avg_power = 0.0f;      
volatile uint8_t vehicle_gear = 0;         
volatile bool vehicle_ready = false;

void data_center_init(void){
    can_nand_stream = xStreamBufferCreate(32768, 512); 
}