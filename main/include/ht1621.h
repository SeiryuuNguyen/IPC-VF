#ifndef HT1621_H
#define HT1621_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"

// Các mã lệnh cơ bản của HT1621
#define HT1621_CMD_SYS_EN       0x01
#define HT1621_CMD_LCD_ON       0x03
#define HT1621_CMD_RC_256K      0x18
#define HT1621_CMD_BIAS_4COM_13 0x29

// Định nghĩa danh sách ID cho các Icon
typedef enum {
    ICON_LEFT_TURN_SIGNAL = 0,
    ICON_POSITION_LIGHT,
    ICON_LOW_BEAM,
    ICON_HIGH_BEAM,
    ICON_FOG_LIGHT,
    ICON_PARKING_BRAKE_STATUS,
    ICON_SEAT_BELT_WARNING,
    ICON_DOOR_AJAR_WARNING,
    ICON_AIRBAG_WARNING,
    ICON_12V_BATTERY_FAULT,
    ICON_RIGHT_TURN_SIGNAL,
    ICON_TRIP,
    ICON_KM_TRIP,
    DOT_TRIP,
    ICON_AVERAGE_POWER,
    ICON_KM_H_AVERAGE_POWER,
    DOT_AVERAGE_POWER,
    ICON_AVERAGE_SPEED,
    ICON_KWH_100KM_AVERAGE_SPEED,
    DOT_AVERAGE_SPEED,
    ICON_ODO,
    ICON_KM_ODO,
    ICON_BRAKE_WARNING,
    ICON_BMS_BATTERY_TEMP_ERROR,
    ICON_HIGHT_VOLTAGE_FAULT,
    ICON_POWERTRAIN_FAULT,
    ICON_TCS_WARNING,
    ICON_ABS_WARNING,
    ICON_POWER_LIMITED_FAULT,
    ICON_EPS_WARNING,
    ICON_CHARGING_STATUS,
    ICON_FULLY_CHARGED,
    ICON_CHARGER_NOT_DETECTED,
    ICON_SOC,
    ICON_PERCENT_SOC,
    ICON_RANGE,
    ICON_KM_RANGE,
    ICON_R_POSITION,
    ICON_N_POSITION,
    ICON_D_POSITION,
    ICON_RND_POSITON_LIGHT,
    ICON_READRY,
    ICON_KM_H_SPEED,
    ICON_BATTERY,
    ICON_BATTERY_1OO,
    ICON_BATTERY_90,
    ICON_BATTERY_80,
    ICON_BATTERY_70,
    ICON_BATTERY_60,
    ICON_BATTERY_50,
    ICON_BATTERY_40,
    ICON_BATTERY_30,
    ICON_BATTERY_20,
    ICON_BATTERY_10,
    ICON_MAX
} ht1621_icon_t;

typedef struct {
    gpio_num_t cs1_pin;
    gpio_num_t cs2_pin;
    gpio_num_t wr_pin;
    gpio_num_t data_pin;
    uint8_t buffer_ic1[16];
    uint8_t buffer_ic2[16];
} ht1621_t;

typedef struct {
    uint8_t chip_id;
    uint8_t seg;
    uint8_t com;
} led_pos_t;

typedef struct {
    led_pos_t segments[7];
} digit_map_t;

// Các mảng digit map dùng ngoài
extern const digit_map_t speed_digits[2];
extern const digit_map_t soc_digits[2];
extern const digit_map_t trip_digits[4];
extern const digit_map_t average_power_digits[3];
extern const digit_map_t average_speed_digits[3];
extern const digit_map_t odo_digits[6];
extern const digit_map_t range_digits[3];

// Hàm công khai
void ht1621_init(ht1621_t *dev);
void ht1621_clear(ht1621_t *dev);
void ht1621_update(ht1621_t *dev);
void ht1621_set_led_hardware(ht1621_t *dev, uint8_t chip_id, uint8_t seg, uint8_t com, bool state);
void ht1621_set_icon(ht1621_t *dev, uint8_t icon_id, bool state);
void ht1621_print_single_digit(ht1621_t *dev, const digit_map_t *digit, uint8_t number);

// Các hàm hiển thị cụ thể
void ht1621_print_speed(ht1621_t *dev, uint16_t speed);
void ht1621_print_soc(ht1621_t *dev, uint16_t soc_percent);
void ht1621_print_float_1_decimal(ht1621_t *dev, const digit_map_t *digits, uint8_t num_digits, float value, uint8_t dot_icon_id);
void ht1621_print_trip(ht1621_t *dev, float trip_value);
void ht1621_print_average_power(ht1621_t *dev, float average_power_value);
void ht1621_print_average_speed(ht1621_t *dev, float average_speed_value);
void ht1621_print_odo(ht1621_t *dev, uint32_t odo_value);
void ht1621_print_range(ht1621_t *dev, uint32_t range_value);

#endif