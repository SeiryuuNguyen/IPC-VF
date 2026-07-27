#include "data_center.h"

QueueHandle_t can_raw_queue_for_NAND = NULL;
QueueHandle_t can_raw_queue_for_display = NULL;

void data_center_init(void){
    can_raw_queue_for_display = xQueueCreate(100, sizeof(twai_message_t));
    can_raw_queue_for_NAND = xQueueCreate(100, sizeof(twai_message_t));
}
