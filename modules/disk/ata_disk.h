#ifndef ATA_DISK_H
#define ATA_DISK_H

#include <stdint.h>

// Размер сектора
#define SECTOR_SIZE 512

// Прототипы функций
void read_disk(uint8_t *buffer, uint32_t sector);
void write_disk(uint8_t *buffer, uint32_t sector);

#endif // ATA_DISK_H
