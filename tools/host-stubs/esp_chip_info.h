#pragma once
typedef struct {int revision;int cores;} esp_chip_info_t;
void esp_chip_info(esp_chip_info_t*);
