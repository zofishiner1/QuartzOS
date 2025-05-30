#include "ata_disk.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "../templates/colors.h"

// Объявим внешние функции
extern void print_string(const char *str, uint8_t color);
extern void print_char(char c, uint8_t color);
extern void read_string(char *buffer, int max_length);

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
#define ATA_CMD_FLUSH_CACHE 0xE7

// Размер сектора
#define SECTOR_SIZE 512

// Тип раздела Linux
#define MBR_PARTITION_TYPE 0x83

// Прототипы внутренних функций
void ata_read_sector(uint32_t sector, uint8_t *buffer);
void ata_write_sector(uint32_t sector, uint8_t *buffer);
bool check_partition_table(uint8_t *mbr);
void create_partition_table(uint8_t *mbr, uint32_t total_sectors);
static void ata_wait_drq(void);

// Объявим внешние функции
extern void print_string(const char *str, uint8_t color);
extern void print_char(char c, uint8_t color);
extern void read_string(char *buffer, int max_length);

// Определение read_disk и write_disk
void read_disk(uint8_t *buffer, uint32_t sector) {
    ata_read_sector(sector, buffer);
}

void write_disk(uint8_t *buffer, uint32_t sector) {
    ata_write_sector(sector, buffer);
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

// Реализация memcmp
int memcmp(const void *s1, const void *s2, size_t n) {
    const unsigned char *p1 = s1;
    const unsigned char *p2 = s2;
    
    for (size_t i = 0; i < n; i++) {
        if (p1[i] != p2[i]) {
            return p1[i] - p2[i];
        }
    }
    return 0;
}

// Функция ожидания готовности диска
void ata_wait_ready(uint16_t port) {
    int attempts = 0;
    uint8_t status;
    
    // Ожидание снятия флага BSY
    while (((status = inb(port + 7)) & ATA_SR_BSY)) {
        if (++attempts > 1000000) {
            print_string("Disk busy timeout!\n", LIGHT_RED_ON_BLACK);
            return;
        }
    }
    
    // Проверка на ошибки
    if (status & ATA_SR_ERR) {
        print_string("Disk error detected!\n", LIGHT_RED_ON_BLACK);
    }
}

// Ожидание готовности данных (DRQ)
static void ata_wait_drq(void) {
    int attempts = 0;
    uint8_t status;
    while (1) {
        status = inb(ATA_PRIMARY_CMD_PORT + 7);
        if (status & ATA_SR_ERR) {
            print_string("ATA error in wait_drq.\n", LIGHT_RED_ON_BLACK);
            return;
        }
        if (!(status & ATA_SR_BSY) && (status & ATA_SR_DRQ)) {
            return; // Данные готовы
        }
        if (++attempts > 1000000) {
            print_string("Timeout waiting for DRQ.\n", LIGHT_RED_ON_BLACK);
            return;
        }
    }
}

// Простая реализация itoa
static void itoa(int num, char *str, int base) {
    int i = 0;
    bool isNegative = false;

    if (num == 0) {
        str[i++] = '0';
        str[i] = '\0';
        return;
    }

    if (num < 0 && base == 10) {
        isNegative = true;
        num = -num;
    }

    while (num != 0) {
        int rem = num % base;
        str[i++] = (rem > 9) ? (rem - 10) + 'a' : rem + '0';
        num = num / base;
    }

    if (isNegative)
        str[i++] = '-';

    str[i] = '\0';

    int start = 0;
    int end = i - 1;
    while (start < end) {
        char temp = str[start];
        str[start] = str[end];
        str[end] = temp;
        start++;
        end--;
    }
}

// Функция для чтения сектора
void ata_read_sector(uint32_t sector, uint8_t *buffer) {
    uint16_t *buff = (uint16_t *)buffer;

    // Установка количества секторов (1)
    outb(ATA_PRIMARY_CMD_PORT + 2, 1);
    
    // Отправка номера сектора в LBA
    outb(ATA_PRIMARY_CMD_PORT + 3, sector & 0xFF);
    outb(ATA_PRIMARY_CMD_PORT + 4, (sector >> 8) & 0xFF);
    outb(ATA_PRIMARY_CMD_PORT + 5, (sector >> 16) & 0xFF);
    outb(ATA_PRIMARY_CMD_PORT + 6, 0xE0 | ((sector >> 24) & 0x0F));
    
    // Отправка команды чтения
    outb(ATA_PRIMARY_CMD_PORT + 7, ATA_CMD_READ_SECTORS);
    
    // Ожидание готовности данных
    ata_wait_drq();
    
    // Чтение данных
    for (int i = 0; i < SECTOR_SIZE / 2; i++) {
        buff[i] = inw(ATA_PRIMARY_CMD_PORT);
    }
    
    // Проверка на ошибки
    uint8_t status = inb(ATA_PRIMARY_CMD_PORT + 7);
    if (status & ATA_SR_ERR) {
        uint8_t error = inb(ATA_PRIMARY_CMD_PORT + 1);
        char error_msg[50];
        itoa(error, error_msg, 16);
        print_string("Read error: 0x", LIGHT_RED_ON_BLACK);
        print_string(error_msg, LIGHT_RED_ON_BLACK);
        print_char('\n', LIGHT_RED_ON_BLACK);
    }
}

// Функция для записи сектора
void ata_write_sector(uint32_t sector, uint8_t *buffer) {
    uint16_t *buff = (uint16_t *)buffer;

    // Установка количества секторов (1)
    outb(ATA_PRIMARY_CMD_PORT + 2, 1);
    
    // Отправка номера сектора в LBA
    outb(ATA_PRIMARY_CMD_PORT + 3, sector & 0xFF);
    outb(ATA_PRIMARY_CMD_PORT + 4, (sector >> 8) & 0xFF);
    outb(ATA_PRIMARY_CMD_PORT + 5, (sector >> 16) & 0xFF);
    outb(ATA_PRIMARY_CMD_PORT + 6, 0xE0 | ((sector >> 24) & 0x0F));
    
    // Отправка команды записи
    outb(ATA_PRIMARY_CMD_PORT + 7, ATA_CMD_WRITE_SECTORS);
    
    // Ожидание готовности данных
    ata_wait_drq();
    
    // Запись данных
    for (int i = 0; i < SECTOR_SIZE / 2; i++) {
        outw(ATA_PRIMARY_CMD_PORT, buff[i]);
    }
    
    // Ожидание завершения записи
    ata_wait_ready(ATA_PRIMARY_CMD_PORT);
    
    // Сброс кэша диска
    outb(ATA_PRIMARY_CMD_PORT + 7, ATA_CMD_FLUSH_CACHE);
    ata_wait_ready(ATA_PRIMARY_CMD_PORT);
    
    // Проверка на ошибки
    uint8_t status = inb(ATA_PRIMARY_CMD_PORT + 7);
    if (status & ATA_SR_ERR) {
        uint8_t error = inb(ATA_PRIMARY_CMD_PORT + 1);
        char error_msg[50];
        itoa(error, error_msg, 16);
        print_string("Write error: 0x", LIGHT_RED_ON_BLACK);
        print_string(error_msg, LIGHT_RED_ON_BLACK);
        print_char('\n', LIGHT_RED_ON_BLACK);
    }
}

// Функция для получения информации о диске
bool ata_identify(uint32_t *total_sectors) {
    // Выбираем диск 0
    outb(ATA_PRIMARY_CMD_PORT + 6, 0xA0);
    
    // Ожидание готовности
    ata_wait_ready(ATA_PRIMARY_CMD_PORT);
    
    // Отправляем команду IDENTIFY
    outb(ATA_PRIMARY_CMD_PORT + 7, ATA_CMD_IDENTIFY);
    
    // Проверка наличия диска
    uint8_t status = inb(ATA_PRIMARY_CMD_PORT + 7);
    if (status == 0) {
        print_string("No disk present\n", LIGHT_RED_ON_BLACK);
        return false;
    }
    
    // Ожидание снятия флага BSY
    int timeout = 1000000;
    while ((status & ATA_SR_BSY) && timeout > 0) {
        status = inb(ATA_PRIMARY_CMD_PORT + 7);
        timeout--;
    }
    
    if (timeout == 0) {
        print_string("Timeout during IDENTIFY\n", LIGHT_RED_ON_BLACK);
        return false;
    }
    
    // Проверка ошибок
    if (status & ATA_SR_ERR) {
        print_string("Error during IDENTIFY\n", LIGHT_RED_ON_BLACK);
        return false;
    }
    
    // Чтение данных
    uint16_t buffer[256] = {0};  // Инициализация нулями
    
    for (int i = 0; i < 256; i++) {
        buffer[i] = inw(ATA_PRIMARY_CMD_PORT);
    }
    
    // Безопасное извлечение общего количества секторов
    *total_sectors = (uint32_t)buffer[61] << 16 | buffer[60];
    
    return true;
}

// Проверка наличия разметки диска
bool check_partition_table(uint8_t *mbr) {
    // Проверка сигнатуры MBR
    if (mbr[510] != 0x55 || mbr[511] != 0xAA) {
        print_string("MBR signature not found\n", LIGHT_RED_ON_BLACK);
        return false;
    }

    // Проверка наличия хотя бы одного раздела
    struct partition_entry *partitions = (struct partition_entry*)&mbr[446];
    for (int i = 0; i < 4; i++) {
        if (partitions[i].type != 0) {
            print_string("Partition table exists\n", LIGHT_GREEN_ON_BLACK);
            return true;
        }
    }

    print_string("No partitions found\n", LIGHT_RED_ON_BLACK);
    return false;
}

// Создание MBR с одним разделом
void create_partition_table(uint8_t *mbr, uint32_t total_sectors) {
    // Очистка MBR
    memset(mbr, 0, SECTOR_SIZE);
    
    // Создание раздела
    struct partition_entry *partition = (struct partition_entry*)&mbr[446];
    partition->status = 0x80;  // Активный раздел
    partition->type = MBR_PARTITION_TYPE;
    partition->lba_start = 1;  // Раздел начинается со второго сектора
    partition->sector_count = total_sectors - 1;  // Весь диск кроме MBR
    
    // Установка сигнатуры MBR
    mbr[510] = 0x55;
    mbr[511] = 0xAA;
    
    print_string("Created partition table\n", LIGHT_GREEN_ON_BLACK);
}

// Инициализация диска с автоматическим определением размера
bool initialize_disk() {
    uint8_t mbr[SECTOR_SIZE];
    uint32_t total_sectors = 0;
    
    // Получение информации о диске
    print_string("Identifying disk...\n", WHITE_ON_BLACK);
    if (!ata_identify(&total_sectors)) {
        print_string("Disk identification failed\n", LIGHT_RED_ON_BLACK);
        return false;
    }
    
    // Преобразование в строку для вывода
    char size_str[20];
    itoa(total_sectors, size_str, 10);
    print_string("Total sectors: ", WHITE_ON_BLACK);
    print_string(size_str, LIGHT_GREEN_ON_BLACK);
    print_char('\n', WHITE_ON_BLACK);
    
    // Чтение MBR
    print_string("Reading MBR...\n", WHITE_ON_BLACK);
    read_disk(mbr, 0);
    
    // Проверка разметки
    if (check_partition_table(mbr)) {
        print_string("Partition table already exists\n", LIGHT_GREEN_ON_BLACK);
        return true;
    }
    
    // Создание новой разметки
    print_string("Creating new partition table...\n", WHITE_ON_BLACK);
    
    // Создаем раздел на весь диск
    create_partition_table(mbr, total_sectors);
    
    // Запись MBR на диск
    write_disk(mbr, 0);
    
    // Проверка записи
    uint8_t verify[SECTOR_SIZE];
    read_disk(verify, 0);
    
    if (memcmp(mbr, verify, SECTOR_SIZE) == 0) {
        print_string("Partition table written successfully\n", LIGHT_GREEN_ON_BLACK);
        return true;
    }
    
    print_string("Error writing partition table!\n", LIGHT_RED_ON_BLACK);
    return false;
}