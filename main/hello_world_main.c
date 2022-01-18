/* Hello World Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/sdmmc_defs.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "esp_err.h"
#include "esp_vfs.h"
#include "esp_vfs_fat.h"

#define SDD_PATH_DEFAULT "/sdcard"

#define PIN_NUM_MISO GPIO_NUM_16
#define PIN_NUM_MOSI GPIO_NUM_4
#define PIN_NUM_CLK  GPIO_NUM_5
#define PIN_NUM_CS   GPIO_NUM_18
#define PIN_NUM_POW GPIO_NUM_2

#define SPI_BUS_FREQ    SDMMC_FREQ_DEFAULT

// TODO: reconsider the number
#define MAX_OPEN_FILE_NUM   10
#define SPI_DMA_CHAN SPI_DMA_CH1

static const char* TAG = "MAIN";

static sdmmc_card_t* _card = NULL;

#define SDCARD_INIT_WITH_DUMMY_CYCLES NO

static esp_err_t card_power_on()
{
    esp_err_t ret;

    // lock CS line
    gpio_config_t gpio_conf;
    gpio_conf.pin_bit_mask = (1ULL<<PIN_NUM_CS);
    gpio_conf.mode = GPIO_MODE_OUTPUT;
    gpio_conf.pull_up_en = (gpio_pullup_t) 0;
    gpio_conf.pull_down_en = (gpio_pulldown_t) 0;
    gpio_conf.intr_type = GPIO_INTR_DISABLE;

    ret = gpio_config(&gpio_conf);
    if (ret) return ret;
    
    ret = gpio_set_level(PIN_NUM_CS, 1);
    if (ret) return ret;

    // init power 
    gpio_conf.pin_bit_mask = (1ULL<<PIN_NUM_POW);

    ret = gpio_config(&gpio_conf);
    if (ret) return ret;

    ret = gpio_set_level(PIN_NUM_POW, 1);
    if (ret) return ret;

    return ret;
}

static esp_err_t card_init(bool doFormat)
{
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.max_freq_khz = SPI_BUS_FREQ;
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .data4_io_num = -1,
        .data5_io_num = -1,
        .data6_io_num = -1,
        .data7_io_num = -1,
        .max_transfer_sz = 0,
        .flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_GPIO_PINS,
        .intr_flags =0
    };

    esp_err_t ret = spi_bus_initialize((spi_host_device_t)host.slot, &bus_cfg, SPI_DMA_CHAN);
    if (ret) {
        ESP_LOGE(TAG, "Failed to initialize bus.");
        return ret;
    }

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_before_mount = doFormat,
        .format_if_mount_failed = false,
        .max_files = MAX_OPEN_FILE_NUM,
        .allocation_unit_size = 32 * 512
    };

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_NUM_CS;
    slot_config.host_id = (spi_host_device_t)host.slot;

    ESP_LOGV(TAG, "Mounting filesystem");
    ret = esp_vfs_fat_sdspi_mount(SDD_PATH_DEFAULT, &host, &slot_config, &mount_config, &_card);

    if (ret) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem. Perhaps formatting is required");
        } else {
            ESP_LOGE(TAG, "Failed to initialize the card (%s). ", esp_err_to_name(ret));
        }
        return ret;
    }
    ESP_LOGI(TAG, "Filesystem mounted");
    sdmmc_card_print_info(stdout, _card);

    return ESP_OK;
}

esp_err_t sdd_init(bool doFormat)
{
    esp_err_t ret;
    // ret= card_power_on();
    // if (ret) return ret;

    ret = card_init(doFormat);

    if (ret) {
        return ret;
    }
    return ESP_OK;
}

static esp_err_t remount_with_format()
{
    esp_err_t err = esp_vfs_fat_sdcard_unmount(SDD_PATH_DEFAULT, _card) == ESP_OK;
    if (err) {
        ESP_LOGE(TAG, "Unmounting failed: %04X", err);
    }
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_before_mount = true,
        .format_if_mount_failed = false,
        .max_files = MAX_OPEN_FILE_NUM,
        .allocation_unit_size = 32 * 512
    };
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.max_freq_khz = SPI_BUS_FREQ;
        sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_NUM_CS;
    slot_config.host_id = (spi_host_device_t)host.slot;
    err = esp_vfs_fat_sdspi_mount(SDD_PATH_DEFAULT, &host, &slot_config, &mount_config, &_card);
    if (err) {
        ESP_LOGE(TAG, "Mounting failed: %04X", err);
        return err;
    }
    return err;
}

static void rnd_files()
{
    int bufsize = 8 * 1024;
    int* buf = malloc(bufsize);
    for (int i = 0; i < bufsize /sizeof(int); i++) {
        buf[i] += i;     
    }

    int written_files = 0;
    int written_bytes = 0;
    char last_filename[48] = "";
    while(true) {
        uint32_t rnd = esp_random();
        char filename[48];
        int reop_attempts = 3;
open:
        sprintf(filename, SDD_PATH_DEFAULT"/%08X.BIN", rnd);

        FILE* f = fopen(filename, "w+");
        if (!f) {
            ESP_LOGE(TAG, "fopen(\"%s\") [written files=%d, bytes=%d]: %s", filename, written_files, written_bytes, strerror(errno));
            // trying to rename - should crash here
            ESP_LOGI(TAG, "Trying to remove last one: %s", last_filename);
            if (remove(last_filename)) {
                goto open;
            }
            else {
                goto fmt;
            }
        }

        strcpy(last_filename, filename);

        ESP_LOGI(TAG, "File %d %s opended", written_files, filename);

        int l;
        int filepos = 0;
wr:

        l = ((float)esp_random()) / UINT32_MAX * bufsize;

        if (fwrite(buf, l, 1, f) == 1) {
            ESP_LOGI(TAG, "Chunk with %d bytes has been written, size %d", l, filepos + l);
            written_bytes += l;
        }
        else {
            ESP_LOGE(TAG, "fwrite: %s", strerror(errno));
            fclose(f);
            continue;
        }

        filepos += l;

        l = esp_random();

        if (l >= UINT32_MAX / 10) {
            if (fclose(f) == 0) {
                written_files++;
            }
            ESP_LOGI(TAG, "File %d %s closed", written_files, filename);
        }
        else {
            vTaskDelay(100 / portTICK_PERIOD_MS);
            goto wr;
        }
        vTaskDelay(100 / portTICK_PERIOD_MS);
        continue;
fmt:
        if(remount_with_format() == ESP_OK) {
            ESP_LOGI(TAG, "Remounted after formatting");
        }
        else {
            ESP_LOGI(TAG, "Remounting after formatting failed");
            vTaskDelay(portMAX_DELAY);
        }
    }
}

void app_main(void)
{
    printf("Hello world!\n");

    sdd_init(true);

    xTaskCreatePinnedToCore(rnd_files, TAG, 4096, NULL,20, NULL, 1);
    
}
