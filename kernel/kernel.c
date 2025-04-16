/* kernel.c */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h> // Для стандартной strlen
#include "../modules/disk/ata_disk.h"

// Определения для Multiboot
#define MULTIBOOT_HEADER_MAGIC 0x1BADB002
#define MULTIBOOT_HEADER_FLAGS (1 | 2)
#define MULTIBOOT_CHECKSUM -(MULTIBOOT_HEADER_MAGIC + MULTIBOOT_HEADER_FLAGS)

// Определения портов ввода/вывода
#define KEYBOARD_DATA_PORT 0x60
#define KEYBOARD_STATUS_PORT 0x64

// Адрес видеопамяти в VGA-текстовом режиме
char *vidptr = (char*)0xB8000;

// Массив для хранения команды
char command[80];
int command_length = 0;

// Координаты курсора на экране
int row = 0;
int col = 0;

// Функция для сравнения двух строк
int strcmp(const char *str1, const char *str2) {
    while (*str1 && (*str1 == *str2)) {
        str1++;
        str2++;
    }
    return *(const unsigned char*)str1 - *(const unsigned char*)str2;
}

int strncmp(const char *s1, const char *s2, size_t n) {
    while (n && *s1 && (*s1 == *s2)) {
        ++s1; ++s2; --n;
    }
    if (n == 0) return 0;
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

char *strncpy(char *dest, const char *src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i]; ++i)
        dest[i] = src[i];
    for (; i < n; ++i)
        dest[i] = '\0';
    return dest;
}


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

// Функция для вывода символа на экран
void print_char(char c, char color) {
    if (c == '\n') {
        col = 0;
        row++;
        if (row >= 25) {
            scroll_screen();
            row = 25 - 1;
        }
    } else if (c == '\r') {
        col = 0;
    } else if (c == '\b') {
        if (col > 0) {
            col--;
        } else if (row > 0) {
            row--;
            col = 79;
        }
        int offset = (row * 80 + col) * 2;
        vidptr[offset] = ' ';
        vidptr[offset + 1] = color;
        update_cursor(row, col, true);
        return;
    } else {
        int offset = (row * 80 + col) * 2;
        vidptr[offset] = c;
        vidptr[offset + 1] = color;
        col++;
        if (col >= 80) {
            col = 0;
            row++;
            if (row >= 25) {
                scroll_screen();
                row = 25 - 1;
            }
        }
    }
    update_cursor(row, col, true);
}

// Функция для вывода строки на экран с заданным цветом
extern void print_string(const char *str, char color) {
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

// Таблица преобразования скан-кодов в ASCII
static const char scancode_map[] = {
    0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 0, 0,
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0, 'a', 's',
    'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\', 'z', 'x', 'c', 'v',
    'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' ', 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, '7', '8', '9', '-', '4', '5', '6', '+', '1',
    '2', '3', '0', '.'
};

// Функция для получения символа с клавиатуры
char get_char() {
    uint8_t scancode;
    do {
    } while ((inb(KEYBOARD_STATUS_PORT) & 0x01) == 0);
    scancode = inb(KEYBOARD_DATA_PORT);
    if (scancode & 0x80) {
        return 0;
    }
    if (scancode < sizeof(scancode_map)) {
        return scancode_map[scancode];
    } else {
        return 0;
    }
}

extern void read_string(char *buffer, int max_length) {
    int i = 0;
    while (i < max_length - 1) {
        char c = get_char();
        if (c == '\n' || c == '\r') {
            break;
        }
        buffer[i++] = c;
        print_char(c, 0x0F); // echo input
    }
    buffer[i] = '\0';
}

// Функция для чтения строки с клавиатуры (с защитой от переполнения)
char *gets(char *s, int max_len) {
    int i = 0;
    char c;
    while (i < max_len - 1 && (c = get_char()) != '\n') {
        if (c != 0) {
            s[i++] = c;
            print_char(c, 0x0F);
        }
    }
    s[i] = '\0';
    return s;
}

// Функция для выключения питания
void shutdown_system(void) {
    print_string("\nShutting down...\n", 0x0C);
    asm volatile (
        "mov $0x00, %%al;"
        "mov $0xFE, %%ah;"
        "int $0x10;"
        : : : "%ax"
    );
    while (1) {
        asm volatile("hlt");
    }
}

// Функция для перезагрузки системы
void reboot_system(void) {
    print_string("\nRebooting...\n", 0x0C);
    asm volatile (
        "mov $0x00, %%ax;"
        "int $0x19;"
        : : : "%ax"
    );
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
        print_string("\nReading disk...\n", 0x0F);
        read_disk(buffer, 0); // Чтение первого сектора
        print_string("Disk read complete\n", 0x0F);
    } else if (strcmp(cmd, "write-disk") == 0) {
        uint8_t buffer[SECTOR_SIZE];
        memset(buffer, 0, SECTOR_SIZE);  // Очищаем буфер
    
        // Запрашиваем данные для записи
        print_string("Enter data to write (max 512 characters): ", 0x0F);
        read_string((char *)buffer, SECTOR_SIZE);  // Читаем строку с клавиатуры и записываем в буфер
    
        uint32_t sector = 0;  // Заменить на номер сектора, который нужно записать
        write_disk(buffer, sector);  // Запись на диск        
    } else if (strcmp(cmd, "clear") == 0) {
        clear_screen();
    } else if (strcmp(cmd, "help") == 0) {
        print_string("\nAvailable commands:\n", 0x0F);
        print_string("  shutdown - Shutdown the system\n", 0x0F);
        print_string("  reboot - Reboot the system\n", 0x0F);
        print_string("  read-disk - Read data from disk\n", 0x0F);
        print_string("  write-disk <data> - Write data to disk\n", 0x0F);
        print_string("  clear - Clear the screen\n", 0x0F);
    } else {
        print_string("\nUnknown command!\n", 0x04);
    }
    print_string("QuartzOS> ", 0x0F);
}

/* Главная функция */
void kmain(void *multiboot_structure) {
    (void)multiboot_structure;

    clear_screen();
    print_string("Welcome to QuartzOS!\n", 0x0F);
    print_string("Version 0.4\n", 0x0A);
    print_string("------------------------\n", 0x0F);
    print_string("Type 'help' to see available commands.\n", 0x0F);
    print_string("Now with disk read/write support!\n", 0x09);
    print_string("QuartzOS> ", 0x0F);

    while (1) {
        char c = get_char();
        if (c == '\n') {
            print_char('\n', 0x0F); // Выводим перенос строки после Enter
            command[command_length] = '\0'; // Обеспечиваем терминатор строки
            process_command(command);
            memset(command, 0, sizeof(command));
            command_length = 0;
        } else if (c == '\b') {
            if (command_length > 0) {
                command_length--;
                print_char('\b', 0x0F); // Backspace
                print_char(' ', 0x0F);  // Очистка символа
                print_char('\b', 0x0F); // Возврат курсора назад
            }
        } else if (c != 0) {
            if (command_length < (int)(sizeof(command) - 1)) {
                command[command_length++] = c;
                print_char(c, 0x0F);
            }
        }
    }
}
