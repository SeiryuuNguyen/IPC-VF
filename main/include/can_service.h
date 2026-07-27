#ifndef CAN_SERVICE_H
#define CAN_SERVICE_H

#include "driver/gpio.h"
#include "driver/twai.h"

#define CAN_TX_IO   GPIO_NUM_2
#define CAN_RX_IO   GPIO_NUM_1
#define CAN_EN_IO   GPIO_NUM_34

void can_init(void);

#endif 