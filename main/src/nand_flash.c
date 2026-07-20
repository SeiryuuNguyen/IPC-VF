#include "nand_flash.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "driver/spi_master.h"
#include "esp_system.h"
#include "soc/spi_pins.h"

const char *base_path = "/nand";

void init_nand_flash(spi_nand_flash_device_t **out_handle, spi_device_handle_t *spi_handle){
    //1. Khởi tạo bus SPI
    ESP_LOGI(TAG, "Initialzing SPI bus....");
    spi_bus_config_t bus_cfg = {
        .miso_io_num = W25N02_MISO_PIN,
        .mosi_io_num = W25N02_MOSI_PIN,
        .sclk_io_num = W25N02_CLK_PIN,
        .quadwp_io_num = W25N02_WP_PIN,   
        .quadhd_io_num = W25N02_HOLD_PIN, 
        .max_transfer_sz = 4096 * 2,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    const uint32_t spi_flags = SPI_DEVICE_HALFDUPLEX;

    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = 10 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = W25N02_CS_PIN,
        .queue_size = 10,
        .flags = spi_flags,
    };

    spi_device_handle_t spi;
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &dev_cfg, &spi));

    spi_nand_flash_config_t nand_config = {
        .device_handle = spi,
        .io_mode = SPI_NAND_IO_MODE_SIO,
        .flags = spi_flags,
    };

    assert(dev_cfg.flags == nand_config.flags);
    spi_nand_flash_device_t *nand_device_handle;
    ESP_ERROR_CHECK(spi_nand_flash_init_device(&nand_config, &nand_device_handle));

    *out_handle = nand_device_handle;
    *spi_handle = spi;
}

void deinit_nand_flash(spi_nand_flash_device_t *out_handle, spi_device_handle_t spi_handle){
    ESP_ERROR_CHECK(spi_nand_flash_deinit_device(out_handle));
    ESP_ERROR_CHECK(spi_bus_remove_device(spi_handle));
    ESP_ERROR_CHECK(spi_bus_free(SPI2_HOST));
}
