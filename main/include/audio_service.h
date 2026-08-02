#ifndef AUDIO_SERVICE_H
#define AUDIO_SERVICE_H

#include <stdint.h>
#include "esp_err.h"

typedef enum {
    AUDIO_EFFECT_TURN_SIGNAL = 0, // Xi nhan
    AUDIO_EFFECT_LOW_BATTERY,     // Pin yếu
    AUDIO_EFFECT_SYSTEM_FAULT,    // Lỗi hệ thống
    AUDIO_EFFECT_COUNT,
} audio_effect_t;

esp_err_t audio_service_start(void);
esp_err_t audio_play(audio_effect_t effect);

#endif