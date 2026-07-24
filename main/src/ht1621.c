#include "ht1621.h"
#include "rom/ets_sys.h" // Dùng cho ets_delay_us()
#include <string.h>

// Macro hỗ trợ
#define WR_0(dev) gpio_set_level(dev->wr_pin, 0)
#define WR_1(dev) gpio_set_level(dev->wr_pin, 1)
#define DATA_0(dev) gpio_set_level(dev->data_pin, 0)
#define DATA_1(dev) gpio_set_level(dev->data_pin, 1)

// Hàm truyền bit mức thấp
static void ht1621_write_bits(ht1621_t *dev, uint8_t data, uint8_t num_bits) {
    for (uint8_t i = 0; i < num_bits; i++) {
        WR_0(dev);
        if (data & (1 << (num_bits - 1 - i))) {
            DATA_1(dev);
        } else {
            DATA_0(dev);
        }
        ets_delay_us(1);
        WR_1(dev);
        ets_delay_us(1);
    }
}

// Gửi lệnh cấu hình (Command mode: ID 100)
static void ht1621_send_cmd(ht1621_t *dev, gpio_num_t cs_pin, uint8_t cmd) {
    gpio_set_level(cs_pin, 0);
    ht1621_write_bits(dev, 0b100, 3); // Command ID
    ht1621_write_bits(dev, cmd, 8);   // Command data
    ht1621_write_bits(dev, 0, 1);     // Dummy bit
    gpio_set_level(cs_pin, 1);
}

// Hàm khởi tạo GPIO và IC
void ht1621_init(ht1621_t *dev) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << dev->cs1_pin) | (1ULL << dev->cs2_pin) | 
                        (1ULL << dev->wr_pin) | (1ULL << dev->data_pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = 0,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    gpio_set_level(dev->cs1_pin, 1);
    gpio_set_level(dev->cs2_pin, 1);
    gpio_set_level(dev->wr_pin, 1);
    gpio_set_level(dev->data_pin, 1);

    gpio_num_t cs_pins[2] = {dev->cs1_pin, dev->cs2_pin};
    for(int i = 0; i < 2; i++) {
        ht1621_send_cmd(dev, cs_pins[i], HT1621_CMD_SYS_EN);
        ht1621_send_cmd(dev, cs_pins[i], HT1621_CMD_RC_256K);
        ht1621_send_cmd(dev, cs_pins[i], HT1621_CMD_BIAS_4COM_13);
        ht1621_send_cmd(dev, cs_pins[i], HT1621_CMD_LCD_ON);
    }
}

// Hàm đẩy dữ liệu liên tiếp
static void ht1621_write_ram(ht1621_t *dev, gpio_num_t cs_pin, uint8_t *buffer) {
    gpio_set_level(cs_pin, 0);
    ht1621_write_bits(dev, 0b101, 3); // Data ID
    ht1621_write_bits(dev, 0x00, 6);  // Start address: 0

    for (int i = 0; i < 16; i++) {
        ht1621_write_bits(dev, buffer[i] & 0x0F, 4); // Nibble thấp
        ht1621_write_bits(dev, (buffer[i] >> 4) & 0x0F, 4); // Nibble cao
    }
    gpio_set_level(cs_pin, 1);
}

void ht1621_update(ht1621_t *dev) {
    ht1621_write_ram(dev, dev->cs1_pin, dev->buffer_ic1);
    ht1621_write_ram(dev, dev->cs2_pin, dev->buffer_ic2);
}

// Hàm đảo 7 bit (bit0 ↔ bit6, bit1 ↔ bit5, ...)
static uint8_t reverse_7bits(uint8_t b) {
    uint8_t r = 0;
    for (int i = 0; i < 7; i++) {
        if (b & (1 << i)) r |= (1 << (6 - i));
    }
    return r;
}

// Bảng mapping icon (giữ nguyên)
const led_pos_t icon_map[] = {
    [ICON_LEFT_TURN_SIGNAL]         = {1, 15, 4},
    [ICON_POSITION_LIGHT]           = {1, 15, 3},
    [ICON_LOW_BEAM]                 = {1, 15, 2},
    [ICON_HIGH_BEAM]                = {1, 15, 1},
    [ICON_FOG_LIGHT]                = {1, 16, 3},
    [ICON_PARKING_BRAKE_STATUS]     = {1, 16, 4},
    [ICON_SEAT_BELT_WARNING]        = {1, 21, 4},
    [ICON_DOOR_AJAR_WARNING]        = {1, 21, 3},
    [ICON_AIRBAG_WARNING]           = {1, 21, 2},
    [ICON_12V_BATTERY_FAULT]        = {2, 21, 2},
    [ICON_RIGHT_TURN_SIGNAL]        = {2, 21, 4},
    [ICON_TRIP]                     = {1, 8,  1}, //V
    [ICON_KM_TRIP]                  = {1, 14, 1},//V
    [DOT_TRIP]                      = {1, 12, 1},//V

    [ICON_AVERAGE_POWER]            = {1, 2,  1},//V ICON THU 2
    [ICON_KM_H_AVERAGE_POWER]       = {1, 6,  1},//V
    [DOT_AVERAGE_POWER]             = {1, 4,  1}, //V

    [ICON_AVERAGE_SPEED]            = {2, 2,  4}, // ICON THU 3
    [ICON_KWH_100KM_AVERAGE_SPEED]  = {2, 6,  4},
    [DOT_AVERAGE_SPEED]             = {2, 4,  4},
    [ICON_ODO]                      = {2, 8,  4},
    [ICON_KM_ODO]                   = {2, 18, 4},
    [ICON_BRAKE_WARNING]            = {2, 10, 4},
    [ICON_BMS_BATTERY_TEMP_ERROR]   = {2, 19, 4},
    [ICON_HIGHT_VOLTAGE_FAULT]      = {2, 19, 3},
    [ICON_POWERTRAIN_FAULT]         = {2, 19, 2},
    [ICON_TCS_WARNING]              = {2, 28, 1},
    [ICON_ABS_WARNING]              = {2, 28, 2},
    [ICON_POWER_LIMITED_FAULT]      = {2, 28, 3},
    [ICON_EPS_WARNING]              = {2, 28, 4},
    [ICON_CHARGING_STATUS]          = {1, 21, 1},
    [ICON_FULLY_CHARGED]            = {2, 21, 1},
    [ICON_CHARGER_NOT_DETECTED]     = {2, 21, 3},

    [ICON_SOC]                      = {2, 25, 4},
    [ICON_PERCENT_SOC]              = {1, 28, 1},

    [ICON_RANGE]                    = {2, 23, 4},
    [ICON_KM_RANGE]                 = {2, 27, 4},
    [ICON_R_POSITION]               = {2, 20, 2},
    [ICON_N_POSITION]               = {2, 20, 3},
    [ICON_D_POSITION]               = {2, 20, 4},
    [ICON_RND_POSITON_LIGHT]        = {2, 20, 1},
    [ICON_READRY]                   = {2, 19, 1},
    [ICON_KM_H_SPEED]               = {1, 20, 1},
    [ICON_BATTERY]                  = {1, 26, 4},
    [ICON_BATTERY_1OO]              = {1, 26, 3},
    [ICON_BATTERY_90]               = {1, 26, 2},
    [ICON_BATTERY_80]               = {1, 26, 1},
    [ICON_BATTERY_70]               = {1, 27, 1},
    [ICON_BATTERY_60]               = {1, 27, 2},
    [ICON_BATTERY_50]               = {1, 27, 3},
    [ICON_BATTERY_40]               = {1, 27, 4},
    [ICON_BATTERY_30]               = {1, 28, 3},
    [ICON_BATTERY_20]               = {1, 28, 2},
    [ICON_BATTERY_10]               = {1, 28, 4},
};

// Định nghĩa các digit map (giữ nguyên)
const led_pos_t speed_led_top = {1, 16, 2};
const led_pos_t speed_led_bot = {1, 16, 1};
const led_pos_t soc_led_top = {1, 25, 1};
const led_pos_t soc_led_bot = {1, 23, 1};

const digit_map_t speed_digits[2] = {
    {
        .segments = {
            [0] = {1, 19, 4},
            [1] = {1, 20, 4},
            [2] = {1, 20, 2},
            [3] = {1, 19, 1},
            [4] = {1, 19, 2},
            [5] = {1, 19, 3},
            [6] = {1, 20, 3}
        }
    },
    {
        .segments = {
            [0] = {1, 17, 4},
            [1] = {1, 18, 4},
            [2] = {1, 18, 2},
            [3] = {1, 17, 1},
            [4] = {1, 17, 2},
            [5] = {1, 17, 3},
            [6] = {1, 18, 3}
        }
    }
};

const digit_map_t soc_digits[2] = {
    {
        .segments = {
            [0] = {1, 24, 4},
            [1] = {1, 25, 4},
            [2] = {1, 25, 2},
            [3] = {1, 24, 1},
            [4] = {1, 24, 2},
            [5] = {1, 24, 3},
            [6] = {1, 25, 3}
        }
    },
    {
        .segments = {
            [0] = {1, 22, 4},
            [1] = {1, 23, 4},
            [2] = {1, 23, 2},
            [3] = {1, 22, 1},
            [4] = {1, 22, 2},
            [5] = {1, 22, 3},
            [6] = {1, 23, 3}
        }
    }
};

const digit_map_t trip_digits[4] = {
    // Thap phan
    {
        .segments = {
            [0] = {1, 13, 4},
            [1] = {1, 14, 4},
            [2] = {1, 14, 2},
            [3] = {1, 13, 1},
            [4] = {1, 13, 2},
            [5] = {1, 13, 3},
            [6] = {1, 14, 3}
        } 
    },
    // Don vi
    {
        .segments = {
            [0] = {1, 11, 4},
            [1] = {1, 12, 4},
            [2] = {1, 12, 2},
            [3] = {1, 11, 1},
            [4] = {1, 11, 2},
            [5] = {1, 11, 3},
            [6] = {1, 12, 3}
        } 
    },
    //Chuc
    {
        .segments = {
            [0] = {1,  9, 4},
            [1] = {1, 10, 4},
            [2] = {1, 10, 2},
            [3] = {1,  9, 1},
            [4] = {1,  9, 2},
            [5] = {1,  9, 3},
            [6] = {1, 10, 3}
        } 
    },
    //Tram
    {
        .segments = {
            [0] = {1, 7, 4},
            [1] = {1, 8, 4},
            [2] = {1, 8, 2},
            [3] = {1, 7, 1},
            [4] = {1, 7, 2},
            [5] = {1, 7, 3},
            [6] = {1, 8, 3}
        } 
    }
};

const digit_map_t average_power_digits[3] = {
    //Thap phan
    {
        .segments = {
            [0] = {1, 5, 4},
            [1] = {1, 6, 4},
            [2] = {1, 6, 2},
            [3] = {1, 5, 1},
            [4] = {1, 5, 2},
            [5] = {1, 5, 3},
            [6] = {1, 6, 3}
        } 
    },
    //don vi
    {
        .segments = {
            [0] = {1, 3, 4},
            [1] = {1, 4, 4},
            [2] = {1, 4, 2},
            [3] = {1, 3, 1},
            [4] = {1, 3, 2},
            [5] = {1, 3, 3},
            [6] = {1, 4, 3}
        } 
    },
    //chuc
    {
        .segments = {
            [0] = {1, 1, 4},
            [1] = {1, 2, 4},
            [2] = {1, 2, 2},
            [3] = {1, 1, 1},
            [4] = {1, 1, 2},
            [5] = {1, 1, 3},
            [6] = {1, 2, 3}
        } 
    }
};

const digit_map_t average_speed_digits[3] = {
    //Thap phan
    {
        .segments = {
            [0] = {2, 5, 4},
            [1] = {2, 6, 3},
            [2] = {2, 6, 1},
            [3] = {2, 5, 1},
            [4] = {2, 5, 2},
            [5] = {2, 5, 3},
            [6] = {2, 6, 2}
        } 
    },
    // don vi
    {
        .segments = {
            [0] = {2, 3, 4},
            [1] = {2, 4, 3},
            [2] = {2, 4, 1},
            [3] = {2, 3, 1},
            [4] = {2, 3, 2},
            [5] = {2, 3, 3},
            [6] = {2, 4, 2}
        } 
    },
    //chuc
    {
        .segments = {
            [0] = {2,  1, 4},
            [1] = {2,  2, 3},
            [2] = {2,  2, 1},
            [3] = {2,  1, 1},
            [4] = {2,  1, 2},
            [5] = {2,  1, 3},
            [6] = {2,  2, 2}
        } 
    }
};

const digit_map_t odo_digits[6] = {
    //don vi
    {
        .segments = {
            [0] = {2, 17, 4},
            [1] = {2, 18, 3},
            [2] = {2, 18, 1},
            [3] = {2, 17, 1},
            [4] = {2, 17, 2},
            [5] = {2, 17, 3},
            [6] = {2, 18, 2},
        }
    },
    //chuc
    {
        .segments = {
            [0] = {2, 15, 4},
            [1] = {2, 16, 3},
            [2] = {2, 16, 1},
            [3] = {2, 15, 1},
            [4] = {2, 15, 2},
            [5] = {2, 15, 3},
            [6] = {2, 16, 2},
        }
    },
    //tram
    {
        .segments = {
            [0] = {2, 13, 4},
            [1] = {2, 14, 3},
            [2] = {2, 14, 1},
            [3] = {2, 13, 1},
            [4] = {2, 13, 2},
            [5] = {2, 13, 3},
            [6] = {2, 14, 2},
        }
    },
    //nghin
    {
        .segments = {
            [0] = {2, 11, 4},
            [1] = {2, 12, 3},
            [2] = {2, 12, 1},
            [3] = {2, 11, 1},
            [4] = {2, 11, 2},
            [5] = {2, 11, 3},
            [6] = {2, 12, 2},
        }
    },
    // chuc nghin
    {
        .segments = {
            [0] = {2,  9, 4},
            [1] = {2, 10, 3},
            [2] = {2, 10, 1},
            [3] = {2,  9, 1},
            [4] = {2,  9, 2},
            [5] = {2,  9, 3},
            [6] = {2, 10, 2},
        }
    },
    //tram nghin
    {
        .segments = {
            [0] = {2, 7, 4},
            [1] = {2, 8, 3},
            [2] = {2, 8, 1},
            [3] = {2, 7, 1},
            [4] = {2, 7, 2},
            [5] = {2, 7, 3},
            [6] = {2, 8, 2},
        }
    }
};

const digit_map_t range_digits[3] = {
    //don vi
    {
        .segments = {
            [0] = {2, 26, 4},
            [1] = {2, 27, 3},
            [2] = {2, 27, 1},
            [3] = {2, 26, 1},
            [4] = {2, 26, 2},
            [5] = {2, 26, 3},
            [6] = {2, 27, 2},
        }
    },
    //chuc
    {
        .segments = {
            [0] = {2, 24, 4},
            [1] = {2, 25, 3},
            [2] = {2, 25, 1},
            [3] = {2, 24, 1},
            [4] = {2, 24, 2},
            [5] = {2, 24, 3},
            [6] = {2, 25, 2},
        }
    },
    //tram
    {
        .segments = {
            [0] = {2, 22, 4},
            [1] = {2, 23, 3},
            [2] = {2, 23, 1},
            [3] = {2, 22, 1},
            [4] = {2, 22, 2},
            [5] = {2, 22, 3},
            [6] = {2, 23, 2},
        }
    },
};

// Bảng mã Font 7 đoạn (chuẩn)
const uint8_t font_7seg[10] = {0x7E, 0x30, 0x6D, 0x79, 0x33, 0x5B, 0x5F, 0x70, 0x7F, 0x7B};

// Hàm set LED theo tọa độ vật lý (không thay đổi)
void ht1621_set_led_hardware(ht1621_t *dev, uint8_t chip_id, uint8_t seg, uint8_t com, bool state) {
    if (chip_id < 1 || chip_id > 2 || seg < 1 || seg > 32 || com < 1 || com > 4) return;

    uint8_t *target_buffer = (chip_id == 1) ? dev->buffer_ic1 : dev->buffer_ic2;
    uint8_t seg_idx = seg - 1;
    uint8_t byte_idx = seg_idx / 2;
    uint8_t bit_shift = 4 - com; // COM1->bit3, COM4->bit0

    if (seg_idx % 2 != 0) bit_shift += 4;

    if (state) target_buffer[byte_idx] |= (1 << bit_shift);
    else target_buffer[byte_idx] &= ~(1 << bit_shift);
}

void ht1621_clear(ht1621_t *dev) {
    memset(dev->buffer_ic1, 0, 16);
    memset(dev->buffer_ic2, 0, 16);
    ht1621_update(dev);
}

void ht1621_set_icon(ht1621_t *dev, uint8_t icon_id, bool state) {
    led_pos_t pos = icon_map[icon_id];
    ht1621_set_led_hardware(dev, pos.chip_id, pos.seg, pos.com, state);
}

// In một chữ số với font đã đảo bit để khắc phục lỗi mirror
void ht1621_print_single_digit(ht1621_t *dev, const digit_map_t *digit, uint8_t number) {
    uint8_t pattern = 0x00;
    if (number <= 9) {
        pattern = font_7seg[number];
        pattern = reverse_7bits(pattern); // Đảo bit để đúng với phần cứng
    }
    for (uint8_t i = 0; i < 7; i++) {
        bool bit_state = (pattern >> i) & 0x01;
        led_pos_t pos = digit->segments[i];
        ht1621_set_led_hardware(dev, pos.chip_id, pos.seg, pos.com, bit_state);
    }
}

// In tốc độ (0-199) với blanking hàng chục khi speed < 10
void ht1621_print_speed(ht1621_t *dev, uint16_t speed) {
    if (speed > 199) speed = 199;

    uint8_t units = speed % 10;
    uint8_t tens = (speed / 10) % 10;

    // Luôn in hàng đơn vị
    ht1621_print_single_digit(dev, &speed_digits[0], units);

    // In hàng chục: nếu speed < 10 thì tắt (truyền 0xFF để tắt)
    if (speed < 10) {
        ht1621_print_single_digit(dev, &speed_digits[1], 0xFF);
    } else {
        ht1621_print_single_digit(dev, &speed_digits[1], tens);
    }

    // Hàng trăm: 2 thanh riêng
    bool show_hundred = (speed >= 100);
    ht1621_set_led_hardware(dev, speed_led_top.chip_id, speed_led_top.seg, speed_led_top.com, show_hundred);
    ht1621_set_led_hardware(dev, speed_led_bot.chip_id, speed_led_bot.seg, speed_led_bot.com, show_hundred);
}

// In SOC (0-199) với blanking hàng chục khi soc < 10
void ht1621_print_soc(ht1621_t *dev, uint16_t soc_percent) {
    if (soc_percent > 199) soc_percent = 199;

    uint8_t units = soc_percent % 10;
    uint8_t tens = (soc_percent / 10) % 10;

    ht1621_print_single_digit(dev, &soc_digits[0], units);

    if (soc_percent < 10) {
        ht1621_print_single_digit(dev, &soc_digits[1], 0xFF);
    } else {
        ht1621_print_single_digit(dev, &soc_digits[1], tens);
    }

    bool show_hundred = (soc_percent >= 100);
    ht1621_set_led_hardware(dev, soc_led_top.chip_id, soc_led_top.seg, soc_led_top.com, show_hundred);
    ht1621_set_led_hardware(dev, soc_led_bot.chip_id, soc_led_bot.seg, soc_led_bot.com, show_hundred);
}

// Hàm in số thực (đã bỏ debug)
void ht1621_print_float_1_decimal(ht1621_t *dev, const digit_map_t *digits, uint8_t num_digits, float value, uint8_t dot_icon_id) {
    uint32_t int_val = (uint32_t)(value * 10.0f);
    for (uint8_t i = 0; i < num_digits; i++) {
        if (int_val == 0 && i >= 2) {
            ht1621_print_single_digit(dev, &digits[i], 0xFF);
        } else {
            uint8_t digit_val = int_val % 10;
            ht1621_print_single_digit(dev, &digits[i], digit_val);
            int_val = int_val / 10;
        }
    }
    ht1621_set_icon(dev, dot_icon_id, true);
}

// --- Sửa lỗi: nhận float (không phải con trỏ) ---
void ht1621_print_trip(ht1621_t *dev, float trip_value) {
    ht1621_print_float_1_decimal(dev, trip_digits, 4, trip_value, DOT_TRIP);
}

void ht1621_print_average_power(ht1621_t *dev, float average_power_value) {
    ht1621_print_float_1_decimal(dev, average_power_digits, 3, average_power_value, DOT_AVERAGE_POWER);
}

void ht1621_print_average_speed(ht1621_t *dev, float average_speed_value) {
    ht1621_print_float_1_decimal(dev, average_speed_digits, 3, average_speed_value, DOT_AVERAGE_SPEED);
}

// --- Sửa print_range: sửa chỉ số và thêm blanking ---
void ht1621_print_range(ht1621_t *dev, uint32_t range_value) {
    if (range_value > 999) range_value = 999; // giới hạn 3 chữ số

    uint8_t digits[3];
    uint32_t temp = range_value;
    for (int i = 0; i < 3; i++) {
        digits[i] = temp % 10; // digits[0]=đơn vị, [1]=chục, [2]=trăm
        temp /= 10;
    }

    // Blanking: tắt số 0 vô nghĩa ở hàng trăm, chục
    bool blank_hundred = (range_value < 100);
    bool blank_tens    = (range_value < 10);

    // In hàng trăm (index 2)
    if (blank_hundred) {
        ht1621_print_single_digit(dev, &range_digits[2], 0xFF);
    } else {
        ht1621_print_single_digit(dev, &range_digits[2], digits[2]);
    }

    // In hàng chục (index 1)
    if (blank_tens) {
        ht1621_print_single_digit(dev, &range_digits[1], 0xFF);
    } else {
        ht1621_print_single_digit(dev, &range_digits[1], digits[1]);
    }

    // In hàng đơn vị (index 0) – luôn hiển thị
    ht1621_print_single_digit(dev, &range_digits[0], digits[0]);
}

// --- Sửa print_odo: thêm blanking ---
void ht1621_print_odo(ht1621_t *dev, uint32_t odo_value) {
    if (odo_value > 999999) odo_value = 999999;

    uint8_t digits[6];
    uint32_t temp = odo_value;
    for (int i = 0; i < 6; i++) {
        digits[i] = temp % 10; // digits[0]=đơn vị, ... [5]=trăm nghìn
        temp /= 10;
    }

    // Xác định vị trí bắt đầu hiển thị (bỏ qua các số 0 vô nghĩa)
    int start = 5; // bắt đầu từ hàng trăm nghìn
    while (start > 0 && digits[start] == 0) {
        start--;
    }

    // In từ vị trí start xuống 0
    for (int i = start; i >= 0; i--) {
        ht1621_print_single_digit(dev, &odo_digits[i], digits[i]);
    }

    // Tắt các digit còn lại (từ start+1 đến 5)
    for (int i = start + 1; i < 6; i++) {
        ht1621_print_single_digit(dev, &odo_digits[i], 0xFF);
    }
}