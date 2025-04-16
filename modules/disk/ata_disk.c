#include "ata_disk.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h> // Для memset и memcpy
#include <stdlib.h> // Для strtol

// Внешнее объявление функции print_string, которая объявлена в kernel.c
extern void print_string(const char *str, uint8_t color);
extern void read_string(char *buffer, int max_length);  // Функция для чтения строки с клавиатуры

// Прототипы внутренних функций
void ata_read_sector(uint32_t sector, uint8_t *buffer);
void ata_write_sector(uint32_t sector, uint8_t *buffer);

// Определение read_disk и write_disk (только один раз)
void read_disk(uint8_t *buffer, uint32_t sector) {
    print_string("Reading from disk...\n", 0x0F);
    ata_read_sector(sector, buffer);
}

void write_disk(uint8_t *buffer, uint32_t sector) {
    print_string("Writing to disk...\n", 0x0F);
    ata_write_sector(sector, buffer);
}

// Порты ATA
#define ATA_PRIMARY_CMD_PORT 0x1F0
#define ATA_PRIMARY_CTRL_PORT 0x3F6

// Статусные биты ATA
#define ATA_SR_BSY 0x80
#define ATA_SR_DRDY 0x40
#define ATA_SR_DF 0x20
#define ATA_SR_DSC 0x10
#define ATA_SR_DRQ 0x08
#define ATA_SR_CORR 0x04
#define ATA_SR_IDX 0x02
#define ATA_SR_ERR 0x01

// Команды ATA
#define ATA_CMD_IDENTIFY 0xEC
#define ATA_CMD_READ_SECTORS 0x20
#define ATA_CMD_WRITE_SECTORS 0x30

// Размер сектора
#define SECTOR_SIZE 512

long strtol(const char *str, char **endptr, int base) {
    long result = 0;
    int sign = 1;
    
    // Пропускаем начальные пробелы
    while (*str == ' ' || *str == '\t') str++;
    
    // Обрабатываем знак
    if (*str == '+' || *str == '-') {
        sign = (*str == '-') ? -1 : 1;
        str++;
    }
    
    // Преобразуем строку в число
    while (*str >= '0' && *str <= '9') {
        result = result * base + (*str - '0');
        str++;
    }
    
    // Устанавливаем endptr, если он не NULL
    if (endptr) *endptr = (char*)str;
    
    return result * sign;
}


// Вспомогательные функции для портов
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile ("inb %1, %0" : "=a" (ret) : "dN" (port));
    return ret;
}

static inline void outb(uint16_t port, uint8_t data) {
    asm volatile ("outb %1, %0" : : "dN" (port), "a" (data));
}

static inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    asm volatile ("inw %w1, %w0" : "=a" (ret) : "Nd" (port));
    return ret;
}

static inline void outw(uint16_t port, uint16_t data) {
    asm volatile ("outw %w0, %w1" : : "a" (data), "Nd" (port));
}

// Функция ожидания готовности диска для записи
void ata_wait_ready(uint16_t port) {
    print_string("Waiting for disk to be ready...\n", 0x0F);
    int attempts = 0; // Подсчет попыток, чтобы избежать бесконечного цикла
    while (inb(port + 7) & (ATA_SR_BSY)) { 
        // Если диск занят, продолжаем ждать
        attempts++;
        if (attempts > 10000) {
            print_string("Disk busy for too long, aborting.\n", 0x0F);
            return; // Прерываем, если диск не готов
        }
    }

    while (!(inb(port + 7) & ATA_SR_DRQ)) {
        // Ожидаем, пока диск не будет готов к чтению/записи
        attempts++;
        if (attempts > 10000) {
            print_string("Disk is not ready for data transfer.\n", 0x0F);
            return; // Прерываем, если диск не готов
        }
    }
    print_string("Disk is ready for operation.\n", 0x0F);
}

// Идентификация диска
bool ata_identify(uint16_t *buffer) {
    print_string("Identifying disk...\n", 0x0F);

    // Выбираем диск 0
    outb(ATA_PRIMARY_CMD_PORT + 6, 0xA0);
    // Небольшая задержка
    for (int i = 0; i < 4; i++) inb(ATA_PRIMARY_CTRL_PORT + 2);
    // Отправляем команду IDENTIFY
    outb(ATA_PRIMARY_CMD_PORT + 7, ATA_CMD_IDENTIFY);

    // Ждем готовности
    ata_wait_ready(ATA_PRIMARY_CMD_PORT);

    // Считываем данные в буфер
    for (int i = 0; i < 256; i++) {
        buffer[i] = inw(ATA_PRIMARY_CMD_PORT);
    }

    print_string("Disk identified.\n", 0x0F);
    return true;
}

// Функция для чтения сектора
void ata_read_sector(uint32_t sector, uint8_t *buffer) {
    uint16_t *buff = (uint16_t *)buffer;

    print_string("Waiting for disk to be ready for reading...\n", 0xF0);
    ata_wait_ready(ATA_PRIMARY_CMD_PORT);
    print_string("Disk is ready for reading.\n", 0xF0);

    // Отправляем команду чтения
    print_string("Sending read command to disk...\n", 0xF0);
    outb(ATA_PRIMARY_CMD_PORT + 6, 0xA0);  // Устанавливаем диск
    outb(ATA_PRIMARY_CMD_PORT + 7, ATA_CMD_READ_SECTORS);

    // Отправляем номер сектора
    print_string("Sending sector number for reading...\n", 0xF0);
    outb(ATA_PRIMARY_CMD_PORT + 2, (uint8_t)(sector & 0xFF));
    outb(ATA_PRIMARY_CMD_PORT + 3, (uint8_t)((sector >> 8) & 0xFF));
    outb(ATA_PRIMARY_CMD_PORT + 4, (uint8_t)((sector >> 16) & 0xFF));
    outb(ATA_PRIMARY_CMD_PORT + 5, (uint8_t)((sector >> 24) & 0xFF));

    // Считываем данные сектора
    print_string("Reading data from sector...\n", 0xF0);
    for (int i = 0; i < SECTOR_SIZE / 2; i++) {
        buff[i] = inw(ATA_PRIMARY_CMD_PORT);
    }

    // Ожидаем завершения
    print_string("Waiting for read operation to complete...\n", 0xF0);
    ata_wait_ready(ATA_PRIMARY_CMD_PORT);

    // Проверка на ошибки
    uint8_t status = inb(ATA_PRIMARY_CMD_PORT + 7);
    if (status & ATA_SR_ERR) {
        print_string("Error while reading from disk!\n", 0xF0);
    } else {
        print_string("Disk read completed successfully.\n", 0xF0);
    }
}

// Функция для записи сектора
void ata_write_sector(uint32_t sector, uint8_t *buffer) {
    uint16_t *buff = (uint16_t *)buffer;

    print_string("Waiting for disk to be ready for writing...\n", 0xF0);
    ata_wait_ready(ATA_PRIMARY_CMD_PORT);
    print_string("Disk is ready for writing.\n", 0xF0);

    // Отправляем команду записи
    print_string("Sending write command to disk...\n", 0xF0);
    outb(ATA_PRIMARY_CMD_PORT + 6, 0xA0);  // Устанавливаем диск
    outb(ATA_PRIMARY_CMD_PORT + 7, ATA_CMD_WRITE_SECTORS);

    // Отправляем номер сектора
    print_string("Sending sector number...\n", 0xF0);
    outb(ATA_PRIMARY_CMD_PORT + 2, (uint8_t)(sector & 0xFF));
    outb(ATA_PRIMARY_CMD_PORT + 3, (uint8_t)((sector >> 8) & 0xFF));
    outb(ATA_PRIMARY_CMD_PORT + 4, (uint8_t)((sector >> 16) & 0xFF));
    outb(ATA_PRIMARY_CMD_PORT + 5, (uint8_t)((sector >> 24) & 0xFF));

    // Отправляем данные сектора
    print_string("Sending data to the sector...\n", 0xF0);
    for (int i = 0; i < SECTOR_SIZE / 2; i++) {
        outw(ATA_PRIMARY_CMD_PORT, buff[i]);
    }

    // Ожидаем завершения записи
    print_string("Waiting for write operation to complete...\n", 0xF0);
    ata_wait_ready(ATA_PRIMARY_CMD_PORT);

    // Проверка на ошибки
    uint8_t status = inb(ATA_PRIMARY_CMD_PORT + 7);
    if (status & ATA_SR_ERR) {
        print_string("Error while writing to disk!\n", 0xF0);
    } else {
        print_string("Disk write completed successfully.\n", 0xF0);
    }
}

// Запрос диска и сектора для записи
void select_disk_and_sector() {
    print_string("Enter the disk number (0 for primary disk): ", 0xF0);
    char disk_choice[1];
    read_string(disk_choice, 1);
    uint8_t disk = disk_choice[0] - '0';  // Преобразуем символ в число
    (void)disk;

    print_string("Enter the sector number to write to: ", 0xF0);
    char sector_choice[10];
    read_string(sector_choice, 10);
    uint32_t sector = strtol(sector_choice, NULL, 10);  // Преобразуем строку в число

    print_string("Enter data to write to disk (max 512 characters): ", 0xF0);
    uint8_t buffer[SECTOR_SIZE];
    read_string((char *)buffer, SECTOR_SIZE);

    // Запись на выбранный диск и сектор
    write_disk(buffer, sector);
}
