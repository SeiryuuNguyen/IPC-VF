#ifndef DATA_CENTER_H
#define DATA_CENTER_H

#include "driver/twai.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

extern QueueHandle_t can_raw_queue_for_NAND;
extern QueueHandle_t can_raw_queue_for_display;

void data_center_init(void);

#endif