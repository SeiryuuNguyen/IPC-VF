#ifndef USB_CDC_H
#define USB_CDC_H

#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_cdc_acm.h"

typedef void (*tinyusb_cdc_rx_callback)(int itf, cdcacm_event_t *event);

typedef struct {
    uint8_t cdc_port;
    tinyusb_cdc_rx_callback rx_callback;
} tinyusb_cdc_config_t;

void usb_cdc_init(tinyusb_cdc_config_t *config);
void usb_cdc_send_data(uint8_t itf, const uint8_t *data, size_t len);
void usb_cdc_deinit(void);

#endif