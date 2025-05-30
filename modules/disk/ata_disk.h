#ifndef ATA_DISK_H
#define ATA_DISK_H

#include <stdint.h>
#include <stdbool.h>

#define SECTOR_SIZE 512
#define MBR_PARTITION_TYPE 0x83

// Структура записи раздела MBR
struct partition_entry {
    uint8_t status;
    uint8_t chs_start[3];
    uint8_t type;
    uint8_t chs_end[3];
    uint32_t lba_start;
    uint32_t sector_count;
} __attribute__((packed));

void read_disk(uint8_t *buffer, uint32_t sector);
void write_disk(uint8_t *buffer, uint32_t sector);
bool initialize_disk(void);

#endif