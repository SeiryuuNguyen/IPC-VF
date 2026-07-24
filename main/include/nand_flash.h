#ifndef NAND_FLASH_H
#define NAND_FLASH_H

#include "esp_vfs_fat.h"
#include "spi_nand_flash.h"
#include "esp_vfs_fat_nand.h"

<<<<<<< HEAD
static const char *TAG = "NAND";
=======
>>>>>>> feature/can-service
extern const char *base_path;

#define W25N02_MISO_PIN    GPIO_NUM_13
#define W25N02_MOSI_PIN    GPIO_NUM_11
#define W25N02_CLK_PIN     GPIO_NUM_12
#define W25N02_CS_PIN      GPIO_NUM_10
#define W25N02_WP_PIN      GPIO_NUM_14
#define W25N02_HOLD_PIN    GPIO_NUM_9

void init_nand_flash(spi_nand_flash_device_t **out_handle, spi_device_handle_t *spi_handle);
void deinit_nand_flash(spi_nand_flash_device_t *out_handle, spi_device_handle_t spi_handle);

#endif