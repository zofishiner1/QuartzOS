/* kernel.c */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

// Определяем размер сектора диска
#define SECTOR_SIZE 512

// Определение магических чисел для загрузки ядра
#define MULTIBOOT_HEADER_MAGIC 0x1BADB002
#define MULTIBOOT_HEADER_FLAGS (1 | 2)
#define MULTIBOOT_CHECKSUM -(MULTIBOOT_HEADER_MAGIC + MULTIBOOT_HEADER_FLAGS)

// Определения портов ввода/вывода
#define KEYBOARD_DATA_PORT   0x60
#define KEYBOARD_STATUS_PORT 0x64

// Порты для работы с диском (IDE/ATA)
#define ATA_PRIMARY_CMD_PORT    0x1F0
#define ATA_PRIMARY_CTRL_PORT   0x3F6
#define ATA_SECONDARY_CMD_PORT  0x170
#define ATA_SECONDARY_CTRL_PORT 0x376

// Статусные биты ATA
#define ATA_SR_BSY     0x80   // Busy
#define ATA_SR_DRDY    0x40   // Drive ready
#define ATA_SR_DF      0x20   // Drive write fault
#define ATA_SR_DSC     0x10   // Drive seek complete
#define ATA_SR_DRQ     0x08   // Data request ready
#define ATA_SR_CORR    0x04   // Corrected data
#define ATA_SR_IDX     0x02   // Index
#define ATA_SR_ERR     0x01   // Error

// Команды ATA
#define ATA_CMD_IDENTIFY 0xEC

// Адрес видеопамяти в VGA-текстовом режиме
char *vidptr = (char*)0xB8000;

// Массив для хранения команды
char command[80];
int command_length = 0;

// Координаты курсора на экране
int row = 0;
int col = 0;

// Структура заголовка Multiboot
struct multiboot_header {
    uint32_t magic;
    uint32_t flags;
    uint32_t checksum;
    uint32_t header_addr;
    uint32_t load_addr;
    uint32_t load_end_addr;
    uint32_t bss_end_addr;
    uint32_t entry_addr;
};

// Функция для чтения байта из порта ввода/вывода
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile ("inb %1, %0" : "=a" (ret) : "dN" (port));
    return ret;
}

// Функция для записи байта в порт ввода/вывода
static inline void outb(uint16_t port, uint8_t data) {
    asm volatile ("outb %1, %0" : : "dN" (port), "a" (data));
}

// Функция для чтения слова (16 бит) из порта ввода/вывода
static inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    asm volatile ("inw %w1, %w0" : "=a" (ret) : "Nd" (port));
    return ret;
}

// Функция для записи слова (16 бит) в порт ввода/вывода
static inline void outw(uint16_t port, uint16_t data) {
    asm volatile ("outw %w0, %w1" : : "a" (data), "Nd" (port));
}

// Функция для обновления положения курсора
void update_cursor(int row, int col, bool visible) {
    uint16_t position = (row * 80) + col;

    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(position & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((position >> 8) & 0xFF));

    if (!visible) {
        outb(0x3D4, 0x0A);
        outb(0x3D5, 0x20);
    } else {
        outb(0x3D4, 0x0A);
        outb(0x3D5, 0x07);
    }
}

// Функция для прокрутки экрана вверх
void scroll_screen() {
    for (int i = 0; i < 25 - 1; i++) {
        memcpy(vidptr + i * 80 * 2, vidptr + (i + 1) * 80 * 2, 80 * 2);
    }

    for (int i = 0; i < 80; i++) {
        vidptr[(25 - 1) * 80 * 2 + i * 2] = ' ';
        vidptr[(25 - 1) * 80 * 2 + i * 2 + 1] = 0x07;
    }

    row = 25 - 1;
    col = 0;
    update_cursor(row, col, true);
}

// Функция для очистки экрана
void clear_screen() {
    unsigned int j = 0;

    while (j < 80 * 25 * 2) {
        vidptr[j] = ' ';
        vidptr[j + 1] = 0x07;
        j += 2;
    }
    row = 0;
    col = 0;
    update_cursor(row, col, true);
}

size_t strlen(const char *str) {
    size_t len = 0;
    while (*str != '\0') {
        len++;
        str++;
    }
    return len;
}

// Функция для вывода символа на экран
void print_char(char c, char color) {
    if (c == '\n') {
        col = 0;
        row++;
    } else if (c == '\r') {
        col = 0;
    } else {
        int offset = (row * 80 + col) * 2;
        vidptr[offset] = c;
        vidptr[offset + 1] = color;
        col++;
    }

    if (col >= 80) {
        col = 0;
        row++;
    }

    if (row >= 25) {
        scroll_screen();
        row = 25 - 1;
    }

    update_cursor(row, col, true);
}

// Функция для вывода строки на экран с заданным цветом
void print_string(const char *str, char color) {
    unsigned int i = 0;
    while (str[i] != '\0') {
        print_char(str[i], color);
        i++;
    }
}

// Функция itoa (целое число в массив)
void itoa(int num, char *str, int base) {
    int i = 0;
    bool isNegative = false;

    // Обрабатываем 0 явно, иначе для 0 будет напечатана пустая строка
    if (num == 0) {
        str[i++] = '0';
        str[i] = '\0';
        return;
    }

    // В стандартной функции itoa() отрицательные числа обрабатываются только с
    // основанием 10. В противном случае числа считаются беззнаковыми.
    if (num < 0 && base == 10) {
        isNegative = true;
        num = -num;
    }

    // Обрабатываем отдельные цифры
    while (num != 0) {
        int rem = num % base;
        str[i++] = (rem > 9) ? (rem - 10) + 'a' : rem + '0';
        num = num / base;
    }

    // Если число отрицательное, добавляем '-'
    if (isNegative)
        str[i++] = '-';

    str[i] = '\0'; // Добавляем терминатор строки

    // Переворачиваем строку
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

// Таблица преобразования скан-кодов в ASCII
static const char scancode_map[] = {
    0,    0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 0,   0,
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',   0, 'a', 's',
    'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',   0, '\\', 'z', 'x', 'c', 'v',
    'b', 'n', 'm', ',', '.', '/',   0,   '*',   0, ' ',   0,    0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   '7', '8', '9', '-', '4', '5', '6', '+', '1',
    '2', '3', '0', '.'
};

// Функция для получения символа с клавиатуры
char get_char() {
    uint8_t scancode;

    do {
        // Ждем, пока клавиатура будет готова
    } while ((inb(KEYBOARD_STATUS_PORT) & 0x01) == 0);

    scancode = inb(KEYBOARD_DATA_PORT);

    if (scancode & 0x80) {
        // Код отпускания клавиши
        return 0;
    }

    if (scancode < sizeof(scancode_map)) {
        return scancode_map[scancode];
    } else {
        return 0;
    }
}

// Функция gets для чтения строки с клавиатуры (с защитой от переполнения)
char *gets(char *s, int max_len) {
    int i = 0;
    char c;

    while (i < max_len - 1 && (c = get_char()) != '\n') {
        if (c != 0) {  // Ignore zero characters
            s[i++] = c;
            print_char(c, 0x0F);  // Echo the character
        }
    }
    s[i] = '\0';
    return s;
}

// Функция для сравнения двух строк
int strcmp(const char *str1, const char *str2) {
    while (*str1 && (*str1 == *str2)) {
        str1++;
        str2++;
    }
    return *(const unsigned char*)str1 - *(const unsigned char*)str2;
}

// Функция ожидания, пока диск не станет готов
void ata_wait_ready(uint16_t cmd_port) {
    while (inb(cmd_port + 7) & (ATA_SR_BSY | ATA_SR_DRQ));
}

// Функция идентификации ATA диска
bool ata_identify(uint16_t cmd_port, uint16_t ctrl_port, uint16_t *buffer) {
    // Выбираем диск 0
    outb(cmd_port + 6, 0xA0);
    // Небольшая задержка
    for (int i = 0; i < 4; i++)
        inb(ctrl_port + 2);

    // Отправляем команду IDENTIFY
    outb(cmd_port + 7, ATA_CMD_IDENTIFY);
    // Небольшая задержка
    for (int i = 0; i < 4; i++)
        inb(ctrl_port + 2);

    // Проверяем, существует ли диск
    if (inb(cmd_port + 7) == 0x00)
        return false;

    // Ожидаем, пока диск будет готов
    ata_wait_ready(cmd_port);

    // Читаем 256 слов (512 байт) данных
    for (int i = 0; i < 256; i++) {
        buffer[i] = inw(cmd_port);
    }

    return true;
}

// Драйвер для работы с диском

// Функция для чтения данных с диска
void read_disk(uint8_t *buffer, uint32_t sector) {
    outb(ATA_PRIMARY_CTRL_PORT, 0x00);
    outb(ATA_PRIMARY_CMD_PORT + 6, 0xE0 | ((sector >> 24) & 0x0F));
    outb(ATA_PRIMARY_CMD_PORT + 1, 0x00);  // Features
    outb(ATA_PRIMARY_CMD_PORT + 2, 0x01);  // Sector count
    outb(ATA_PRIMARY_CMD_PORT + 3, (uint8_t)sector);
    outb(ATA_PRIMARY_CMD_PORT + 4, (uint8_t)(sector >> 8));
    outb(ATA_PRIMARY_CMD_PORT + 5, (uint8_t)(sector >> 16));
    outb(ATA_PRIMARY_CMD_PORT + 7, 0x20);  // READ SECTORS

    // Ожидание готовности
    while (1) {
        uint8_t status = inb(ATA_PRIMARY_CMD_PORT + 7);
        if (status & ATA_SR_ERR) {
            print_string("Read Error! Status: 0x", 0x0C);
            char status_str[3];
            itoa(status, status_str, 16);
            print_string(status_str, 0x0C);
            return;
        }
        if ((status & (ATA_SR_BSY | ATA_SR_DRQ)) == ATA_SR_DRQ) break;
    }

    // Чтение данных
    for (int i = 0; i < SECTOR_SIZE/2; i++) {
        ((uint16_t*)buffer)[i] = inw(ATA_PRIMARY_CMD_PORT);
    }

    // Проверка после чтения
    if (inb(ATA_PRIMARY_CMD_PORT + 7) & ATA_SR_ERR) {
        print_string("Post-read Error!", 0x0C);
    } else {
            print_string("\nSector data (512 bytes):\n", 0x0F);
            for (int i = 0; i < SECTOR_SIZE; i++) {
                if (i % 16 == 0) print_string("\n", 0x0F);
                char str[3];
                itoa(buffer[i], str, 16);
                if (strlen(str) == 1) print_string("0", 0x0F);
                print_string(str, 0x0F);
                print_string(" ", 0x0F);
            }
            print_string("\n", 0x0F);

            // Вывод текстовой части данных (если есть)
            print_string("Text part of sector data:\n", 0x0F);
            char* text = (char*)buffer;
            for (int i = 0; i < SECTOR_SIZE; i++) {
                if (text[i] >= 32 && text[i] <= 126) {  // ASCII printable characters
                    print_char(text[i], 0x0F);
                }
            }
            print_string("\n", 0x0F);
    }
}

void my_strcpy(char *dest, const char *src) {
    while (*src) {
        *dest++ = *src++;
    }
    *dest = '\0'; // Добавляем завершающий ноль
}

// Функция для записи данных на диск (побайтово)
void write_disk(uint8_t *buffer, uint32_t sector) {
    print_string("Enter data to write (max 512 bytes): ", 0x0F);
    char input[512];
    gets(input, 512);  // Ввод строки от пользователя с ограничением длины

    // Очистка буфера
    memset(buffer, 0, SECTOR_SIZE);

    // Копирование введенных данных в буфер
    my_strcpy((char*)buffer, input);

    // Выбор контроллера
    outb(ATA_PRIMARY_CTRL_PORT, 0x00);

    // Установка параметров LBA
    outb(ATA_PRIMARY_CMD_PORT + 6, 0xE0 | ((sector >> 24) & 0x0F));
    outb(ATA_PRIMARY_CMD_PORT + 1, 0x00);  // Features (disable IRQ)
    outb(ATA_PRIMARY_CMD_PORT + 2, 0x01);  // Sector count
    outb(ATA_PRIMARY_CMD_PORT + 3, (uint8_t)sector);      // LBA Low
    outb(ATA_PRIMARY_CMD_PORT + 4, (uint8_t)(sector >> 8));  // LBA Mid
    outb(ATA_PRIMARY_CMD_PORT + 5, (uint8_t)(sector >> 16)); // LBA High

    // Отправка команды записи
    outb(ATA_PRIMARY_CMD_PORT + 7, 0x30);  // WRITE SECTORS

    // Ожидание готовности (BSY=0, DRQ=1)
    while (1) {
        uint8_t status = inb(ATA_PRIMARY_CMD_PORT + 7);
        if (status & ATA_SR_ERR) {
            print_string("Write Error! Status: 0x", 0x0C);
            char status_str[3];
            itoa(status, status_str, 16);
            print_string(status_str, 0x0C);
            return;
        }
        if ((status & (ATA_SR_BSY | ATA_SR_DRQ)) == ATA_SR_DRQ) break;
    }

    // Запись данных порциями по 16 бит (оптимально для ATA)
    for (int i = 0; i < SECTOR_SIZE/2; i++) {
        outw(ATA_PRIMARY_CMD_PORT, ((uint16_t*)buffer)[i]);
    }

    // Ожидание завершения операции
    while (inb(ATA_PRIMARY_CMD_PORT + 7) & ATA_SR_BSY);

    // Обязательная проверка ошибок после записи
    uint8_t status = inb(ATA_PRIMARY_CMD_PORT + 7);
    if (status & ATA_SR_ERR) {
        print_string("Post-write Error! Status: 0x", 0x0C);
        char status_str[3];
        itoa(status, status_str, 16);
        print_string(status_str, 0x0C);
    } else {
        col = 0;
        row++;
        print_string("Data written successfully!", 0x0F);
    }
}

// Функция для выключения питания
void shutdown_system() {
    print_string("Shutting down...", 0x0C);

    // Выключаем питание через BIOS
    asm volatile (
        "mov $0x00, %%al;"
        "mov $0xFE, %%ah;"
        "int $0x10;"
        : : : "%ax"
    );

    // Если не получилось, останавливаем процессор
    while (1) {
        asm volatile("hlt");
    }
}

// Функция для перезагрузки системы
void reboot_system() {
    print_string("Rebooting...", 0x0C);

    // Перезагрузка через BIOS
    asm volatile (
        "mov $0x00, %%ax;"
        "int $0x19;"
        : : : "%ax"
    );

    // Если не получилось, останавливаем процессор
    while (1) {
        asm volatile("hlt");
    }
}

// Функция для обработки команд
void process_command(char *cmd) {
    if (strcmp(cmd, "shutdown") == 0) {
        shutdown_system();
    } else if (strcmp(cmd, "reboot") == 0) {
        reboot_system();
    } else if (strcmp(cmd, "read-disk") == 0) {
        uint8_t buffer[SECTOR_SIZE];
        col = 0;
        row++;
        read_disk(buffer, 0);
        col = 1;
        row++;
        print_string(" Disk read complete", 0x0F);
    } else if (strcmp(cmd, "write-disk") == 0) {
        uint8_t buffer[SECTOR_SIZE];
        col = 0;
        row++;
        write_disk(buffer, 0);
    } else if (strcmp(cmd, "clear") == 0) {
        clear_screen();
    } else if (strcmp(cmd, "help") == 0) {
        print_string("Available commands:\n", 0x0F);
        print_string("  shutdown - Shutdown the system\n", 0x0F);
        print_string("  reboot - Reboot the system\n", 0x0F);
        print_string("  read-disk - Read data from disk\n", 0x0F);
        print_string("  write-disk - Write data to disk\n", 0x0F);
    } else {
        print_string("Unlnown command!", 0x04);
    }
}

/* Главная функция */
void kmain(void *multiboot_structure) {
    (void)multiboot_structure;

    clear_screen();
    print_string("Welcome to QuartzOS!\n", 0x0F);
    print_string("Version 0.3", 0x0A);
    print_string("------------------------\n", 0x0F);
    print_string("Type 'help' to see available commands.\n", 0x0F);
    print_string("Now with disk read/write support!\n", 0x09); // Дополнительное сообщение
    print_string("QuartzOS> ", 0x0F);



    bool cursor_visible = true;

    while (1) {
        char ch = get_char();

        if (ch) {
            if (ch == '\n') {
                command[command_length] = '\0';
                print_string("\n", 0x0F);
                process_command(command);
                memset(command, 0, sizeof(command));
                command_length = 0;
                col = 0;
                row++;
                print_string("-QuartzOS> ", 0x0F);
            } else if (ch == '\b') {
                 if (command_length > 0) {
                    command_length--;
                    if (col > 0) {
                        col--;
                    } else {
                        if (row > 0) {
                            row--;
                            col = 80 - 1;
                        }
                    }
                    int offset = (row * 80 + col) * 2;
                    vidptr[offset] = ' ';
                    vidptr[offset + 1] = 0x07;
                    update_cursor(row, col, cursor_visible);
                }
            } else {
                if (command_length < (int)(sizeof(command) - 1)) {
                    command[command_length++] = ch;
                    print_char(ch, 0x0F);
                }                
            }
        }
    }
}
