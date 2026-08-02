#include "audio_service.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

// Cấu hình chân phần cứng chuẩn theo mạch của bạn
#define AUDIO_I2S_MCLK GPIO_NUM_7
#define AUDIO_I2S_BCLK GPIO_NUM_4
#define AUDIO_I2S_WS GPIO_NUM_5
#define AUDIO_I2S_DOUT GPIO_NUM_6
#define AUDIO_AMP_ENABLE GPIO_NUM_33

#define AUDIO_SOURCE_RATE_HZ 24000U
#define AUDIO_OUTPUT_RATE_HZ 48000U
#define AUDIO_MASTER_VOLUME_PERCENT 15U
#define AUDIO_COMMAND_QUEUE_LENGTH 8U
#define AUDIO_SOURCE_BLOCK_SAMPLES 128U
#define AUDIO_OUTPUT_REPEAT (AUDIO_OUTPUT_RATE_HZ / AUDIO_SOURCE_RATE_HZ)
#define AUDIO_SINE_TABLE_SIZE 48U

static const char *TAG = "audio";
static QueueHandle_t s_audio_queue;
static i2s_chan_handle_t s_i2s_tx;

/* Bảng tra cứu sóng sin mượt mà để tạo âm thanh */
static const int16_t s_sine_table[AUDIO_SINE_TABLE_SIZE] = {
         0,   4277,   8481,  12539,  16384,  19947,  23170,  25996,
     28378,  30273,  31651,  32487,  32767,  32487,  31651,  30273,
     28378,  25996,  23170,  19947,  16384,  12539,   8481,   4277,
         0,  -4277,  -8481, -12539, -16384, -19947, -23170, -25996,
    -28378, -30273, -31651, -32487, -32767, -32487, -31651, -30273,
    -28378, -25996, -23170, -19947, -16384, -12539,  -8481,  -4277,
};

static esp_err_t audio_write(const int16_t *samples, size_t sample_count)
{
    size_t bytes_written = 0;
    size_t bytes_requested = sample_count * sizeof(samples[0]);
    esp_err_t err = i2s_channel_write(s_i2s_tx, samples, bytes_requested,
                                      &bytes_written, 1000);
    if (err != ESP_OK) {
        return err;
    }
    return bytes_written == bytes_requested ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

static esp_err_t audio_write_silence(uint32_t duration_ms)
{
    static const int16_t silence[AUDIO_SOURCE_BLOCK_SAMPLES *
                                 AUDIO_OUTPUT_REPEAT * 2] = {0};
    uint32_t frames_remaining =
        (AUDIO_OUTPUT_RATE_HZ * duration_ms) / 1000U;

    while (frames_remaining > 0U) {
        uint32_t frames = frames_remaining;
        uint32_t max_frames = AUDIO_SOURCE_BLOCK_SAMPLES * AUDIO_OUTPUT_REPEAT;
        if (frames > max_frames) {
            frames = max_frames;
        }
        esp_err_t err = audio_write(silence, frames * 2U);
        if (err != ESP_OK) {
            return err;
        }
        frames_remaining -= frames;
    }
    return ESP_OK;
}

static esp_err_t audio_play_tone(uint16_t frequency_hz, uint16_t duration_ms,
                                 uint8_t effect_gain_percent)
{
    int16_t output[AUDIO_SOURCE_BLOCK_SAMPLES * AUDIO_OUTPUT_REPEAT * 2];
    uint32_t total_source_samples =
        (AUDIO_SOURCE_RATE_HZ * duration_ms) / 1000U;
    uint32_t attack_samples = AUDIO_SOURCE_RATE_HZ / 100U;
    uint32_t release_samples = AUDIO_SOURCE_RATE_HZ / 50U;
    uint32_t phase = 0;
    uint32_t generated = 0;

    if (total_source_samples == 0U || frequency_hz == 0U) {
        return audio_write_silence(duration_ms);
    }

    while (generated < total_source_samples) {
        uint32_t source_count = total_source_samples - generated;
        if (source_count > AUDIO_SOURCE_BLOCK_SAMPLES) {
            source_count = AUDIO_SOURCE_BLOCK_SAMPLES;
        }

        size_t output_index = 0;
        for (uint32_t index = 0; index < source_count; ++index) {
            uint32_t absolute_index = generated + index;
            uint32_t envelope = 32767U;
            if (absolute_index < attack_samples) {
                envelope = (absolute_index * 32767U) / attack_samples;
            }
            uint32_t remaining = total_source_samples - absolute_index - 1U;
            if (remaining < release_samples) {
                uint32_t release_envelope =
                    (remaining * 32767U) / release_samples;
                if (release_envelope < envelope) {
                    envelope = release_envelope;
                }
            }

            uint32_t table_index =
                (phase * AUDIO_SINE_TABLE_SIZE) / AUDIO_SOURCE_RATE_HZ;
            int64_t scaled = (int64_t)s_sine_table[table_index] *
                             AUDIO_MASTER_VOLUME_PERCENT *
                             effect_gain_percent * envelope;
            scaled /= (100LL * 100LL * 32767LL);
            int16_t sample = (int16_t)scaled;

            for (uint32_t repeat = 0; repeat < AUDIO_OUTPUT_REPEAT; ++repeat) {
                output[output_index++] = sample;
                output[output_index++] = sample;
            }

            phase += frequency_hz;
            while (phase >= AUDIO_SOURCE_RATE_HZ) {
                phase -= AUDIO_SOURCE_RATE_HZ;
            }
        }

        esp_err_t err = audio_write(output, output_index);
        if (err != ESP_OK) {
            return err;
        }
        generated += source_count;
    }
    return ESP_OK;
}

static esp_err_t audio_render_effect(audio_effect_t effect)
{
    esp_err_t err = ESP_OK;

    switch (effect) {
    case AUDIO_EFFECT_TURN_SIGNAL:
        // Xi nhan: Tíc - Tắc
        err = audio_play_tone(600, 100, 100);
        if (err == ESP_OK) err = audio_write_silence(400);
        if (err == ESP_OK) err = audio_play_tone(400, 100, 100);
        break;
        
    case AUDIO_EFFECT_LOW_BATTERY:
        // Cảnh báo pin yếu: Bíp Bíp Bíp
        for (int repeat = 0; repeat < 3 && err == ESP_OK; ++repeat) {
            err = audio_play_tone(1200, 200, 100);
            if (err == ESP_OK && repeat < 2) err = audio_write_silence(150);
        }
        break;
        
    case AUDIO_EFFECT_SYSTEM_FAULT:
        // Lỗi hệ thống: Âm thanh dồn dập
        for (int repeat = 0; repeat < 5 && err == ESP_OK; ++repeat) {
            err = audio_play_tone(2500, 100, 100);
            if (err == ESP_OK && repeat < 4) err = audio_write_silence(50);
        }
        break;
        
    default:
        err = ESP_ERR_INVALID_ARG;
        break;
    }
    return err;
}

static void audio_task(void *argument)
{
    (void)argument;

    while (true) {
        audio_effect_t effect;
        if (xQueueReceive(s_audio_queue, &effect, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        // Bật Amply
        gpio_set_level(AUDIO_AMP_ENABLE, 1);
        vTaskDelay(pdMS_TO_TICKS(10));

        esp_err_t err = audio_render_effect(effect);
        esp_err_t silence_err = audio_write_silence(40);
        
        /* Chờ tín hiệu DMA cuối cùng được đẩy ra DAC rồi mới tắt Amply */
        vTaskDelay(pdMS_TO_TICKS(50));
        gpio_set_level(AUDIO_AMP_ENABLE, 0);

        if (err == ESP_OK) {
            err = silence_err;
        }
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Effect %d completed", effect);
        } else {
            ESP_LOGE(TAG, "Effect %d failed: %s", effect, esp_err_to_name(err));
        }
    }
}

esp_err_t audio_service_start(void)
{
    if (s_audio_queue != NULL || s_i2s_tx != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    const gpio_config_t amp_config = {
        .pin_bit_mask = 1ULL << AUDIO_AMP_ENABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&amp_config);
    if (err == ESP_OK) {
        err = gpio_set_level(AUDIO_AMP_ENABLE, 0);
    }
    if (err != ESP_OK) {
        return err;
    }

    i2s_chan_config_t channel_config =
        I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    err = i2s_new_channel(&channel_config, &s_i2s_tx, NULL);
    if (err != ESP_OK) {
        s_i2s_tx = NULL;
        return err;
    }

    i2s_std_config_t standard_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_OUTPUT_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = AUDIO_I2S_MCLK,
            .bclk = AUDIO_I2S_BCLK,
            .ws = AUDIO_I2S_WS,
            .dout = AUDIO_I2S_DOUT,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    standard_config.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;

    err = i2s_channel_init_std_mode(s_i2s_tx, &standard_config);
    if (err == ESP_OK) {
        err = i2s_channel_enable(s_i2s_tx);
    }
    if (err != ESP_OK) {
        i2s_del_channel(s_i2s_tx);
        s_i2s_tx = NULL;
        return err;
    }

    s_audio_queue = xQueueCreate(AUDIO_COMMAND_QUEUE_LENGTH,
                                 sizeof(audio_effect_t));
    if (s_audio_queue == NULL) {
        i2s_channel_disable(s_i2s_tx);
        i2s_del_channel(s_i2s_tx);
        s_i2s_tx = NULL;
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(audio_task, "audio", 4096, NULL, 5, NULL) != pdPASS) {
        vQueueDelete(s_audio_queue);
        s_audio_queue = NULL;
        i2s_channel_disable(s_i2s_tx);
        i2s_del_channel(s_i2s_tx);
        s_i2s_tx = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Audio Service Started: I2S=%u Hz, volume=%u%%",
             AUDIO_OUTPUT_RATE_HZ, AUDIO_MASTER_VOLUME_PERCENT);
    return ESP_OK;
}

esp_err_t audio_play(audio_effect_t effect)
{
    if (s_audio_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (effect < 0 || effect >= AUDIO_EFFECT_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    return xQueueSend(s_audio_queue, &effect, 0) == pdTRUE ?
           ESP_OK : ESP_ERR_TIMEOUT;
}