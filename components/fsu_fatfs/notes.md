# Fleetsy FatFS modification

It's made in order to use make private routine for formatting SD card partition accessible from outside.

`format_before_mount` field added

```
typedef struct {
    bool format_before_mount;
    bool format_if_mount_failed;
    int max_files;
    size_t allocation_unit_size;
} esp_vfs_fat_mount_config_t;
```

`mount_to_vfs_fat` modified with insert:

```
    if (mount_config->format_before_mount) {
        err = partition_card(mount_config, drv, card, pdrv);
        if (err != ESP_OK) {
            goto fail;
        }
    }
```