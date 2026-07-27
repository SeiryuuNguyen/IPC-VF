#include "nand_flash.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "driver/spi_master.h"
#include "esp_system.h"
#include "soc/spi_pins.h"

static const char* TAG = "NAND";
const char *base_path = "/nand";

void init_nand_flash(spi_nand_flash_device_t **out_handle, spi_device_handle_t *spi_handle){
    // 1. Khởi tạo bus SPI
    ESP_LOGI(TAG, "Initializing SPI bus....");
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
    
    spi_nand_flash_device_t *nand_device_handle;
    ESP_ERROR_CHECK(spi_nand_flash_init_device(&nand_config, &nand_device_handle));
    
    *out_handle = nand_device_handle;
    *spi_handle = spi;

    // 2. Mount FATFS lên SPI NAND Flash
    const esp_vfs_fat_mount_config_t mount_config = {
        .max_files = 4,
        .format_if_mount_failed = true, 
        .allocation_unit_size = 4096 // Đặt cứng 4096 (kích thước chuẩn của block NAND) thay cho CONFIG_WL_SECTOR_SIZE
    };
    
    // Gọi đúng 3 tham số theo định nghĩa của thư viện nand_fatfs
    esp_err_t err = esp_vfs_fat_nand_mount(base_path, nand_device_handle, &mount_config);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Loi mount FATFS: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "NAND Flash da duoc mount tai %s", base_path);
}

void deinit_nand_flash(spi_nand_flash_device_t *out_handle, spi_device_handle_t spi_handle){
    // Phải unmount hệ thống file trước khi gỡ thiết bị SPI
    esp_vfs_fat_nand_unmount(base_path, out_handle);
    
    ESP_ERROR_CHECK(spi_nand_flash_deinit_device(out_handle));
    ESP_ERROR_CHECK(spi_bus_remove_device(spi_handle));
    ESP_ERROR_CHECK(spi_bus_free(SPI2_HOST));
}