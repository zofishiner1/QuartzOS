/* kernel.c */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h> // Для стандартной strlen
#include "../modules/disk/ata_disk.h"
#include "../templates/colors.h"
#include "version.h"
#include "../modules/threads_and_processes/threads_and_processes.h"

void* memset(void* ptr, int value, size_t num);

#define MULTIBOOT_INFO_MEM_MAP 0x40

typedef struct multiboot_memory_map {
    uint32_t size;
    uint64_t addr;
    uint64_t len;
    uint32_t type;
} __attribute__((packed)) multiboot_memory_map_t;

typedef struct multiboot_info {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    // ... другие поля по необходимости
    uint32_t mmap_length;
    uint32_t mmap_addr;
} multiboot_info_t;

#ifndef KERNEL_VERSION_SUFFIX
#define KERNEL_VERSION_SUFFIX ""
#endif

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

const char kernel_version[] =
    STR(KERNEL_VERSION_MAJOR) "." STR(KERNEL_VERSION_MINOR) "." STR(KERNEL_VERSION_PATCH) KERNEL_VERSION_SUFFIX "\n";

// Определения для Multiboot
#define MULTIBOOT_HEADER_MAGIC 0x1BADB002
#define MULTIBOOT_HEADER_FLAGS (1 | 2)
#define MULTIBOOT_CHECKSUM -(MULTIBOOT_HEADER_MAGIC + MULTIBOOT_HEADER_FLAGS)

// Определения портов ввода/вывода
#define KEYBOARD_DATA_PORT 0x60
#define KEYBOARD_STATUS_PORT 0x64

// Адрес видеопамяти в VGA-текстовом режиме
char *vidptr = (char*)0xB8000;

// Глобальные переменные для размеров экрана
int SCREEN_WIDTH = 80;
int SCREEN_HEIGHT = 25;

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

// Добавить в раздел функций
int atoi(const char *str) {
    int res = 0;
    while (*str) {
        if (*str >= '0' && *str <= '9') {
            res = res * 10 + (*str - '0');
        }
        str++;
    }
    return res;
}

char *strncpy(char *dest, const char *src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i]; ++i)
        dest[i] = src[i];
    for (; i < n; ++i)
        dest[i] = '\0';
    return dest;
}

void* memset(void* ptr, int value, size_t num) {
    unsigned char* p = ptr;
    while (num--) {
        *p++ = (unsigned char)value;
    }
    return ptr;
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

// Реализация strlen
size_t strlen(const char *str) {
    size_t len = 0;
    while (str[len] != '\0') {
        len++;
    }
    return len;
}

// Реализация strchr
char *strchr(const char *s, int c) {
    while (*s != '\0') {
        if (*s == (char)c) {
            return (char *)s;
        }
        s++;
    }
    return NULL;
}

// Реализация strncat
char *strncat(char *dest, const char *src, size_t n) {
    char *orig_dest = dest;
    
    // Переходим к концу dest
    while (*dest) {
        dest++;
    }
    
    // Копируем не более n символов
    size_t i = 0;
    while (i < n && *src) {
        *dest++ = *src++;
        i++;
    }
    
    // Добавляем терминатор
    *dest = '\0';
    
    return orig_dest;
}

// Функция для обновления положения курсора
void update_cursor(int row, int col, bool visible) {
    // Проверка границ
    if (row < 0) row = 0;
    if (row >= SCREEN_HEIGHT) row = SCREEN_HEIGHT - 1;
    if (col < 0) col = 0;
    if (col >= SCREEN_WIDTH) col = SCREEN_WIDTH - 1;
    
    // Расчет позиции
    uint16_t position = row * SCREEN_WIDTH + col;
    
    // Установка младшего байта
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(position & 0xFF));
    
    // Установка старшего байта
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((position >> 8) & 0xFF));
    
    // Управление видимостью
    if (!visible) {
        outb(0x3D4, 0x0A);
        outb(0x3D5, 0x20); // Скрыть
    } else {
        outb(0x3D4, 0x0A);
        outb(0x3D5, (inb(0x3D5) & 0xC0) | 0x0E); // Показать (сохраняя старшие биты)
        outb(0x3D4, 0x0B);
        outb(0x3D5, (inb(0x3D5) & 0xE0) | 0x0F); // Установить конечную линию
    }
}

// Функция для прокрутки экрана вверх
void scroll_screen() {
    int bytes_per_row = SCREEN_WIDTH * 2;
    int total_rows = SCREEN_HEIGHT - 1;
    
    // Копируем строки вверх
    for (int i = 0; i < total_rows; i++) {
        for (int j = 0; j < bytes_per_row; j++) {
            vidptr[i * bytes_per_row + j] = vidptr[(i + 1) * bytes_per_row + j];
        }
    }
    
    // Очищаем последнюю строку
    int last_row_offset = total_rows * bytes_per_row;
    for (int i = 0; i < bytes_per_row; i += 2) {
        vidptr[last_row_offset + i] = ' ';
        vidptr[last_row_offset + i + 1] = 0x07; // Атрибут по умолчанию
    }
    
    // Обновляем позицию курсора
    row = total_rows;
    col = 0;
    update_cursor(row, col, true);
}

// Функция для очистки экрана
void clear_screen() {
    int screen_size = SCREEN_WIDTH * SCREEN_HEIGHT * 2;
    for (int i = 0; i < screen_size; i += 2) {
        vidptr[i] = ' ';
        vidptr[i + 1] = 0x07; // Серый на черном
    }
    row = 0;
    col = 0;
    update_cursor(row, col, true);
}

// Функция изменения видеорежима
void set_video_mode(int width, int height) {
    // Проверка допустимых размеров
    if (width < 40) width = 40;
    if (height < 10) height = 10;
    
    // Максимальный размер, помещающийся в видеопамять (32KB)
    int max_size = 0x8000 / 2; // 2 байта на символ
    
    // Автоматическое масштабирование без использования sqrtf
    if (width * height > max_size) {
        // Рассчитываем коэффициент масштабирования
        int scale = (width * height * 10) / max_size + 1;
        
        // Применяем масштабирование
        width = width * 10 / scale;
        height = height * 10 / scale;
        
        // Гарантируем минимальные размеры
        if (width < 40) width = 40;
        if (height < 10) height = 10;
    }
    
    // Обновление глобальных переменных
    SCREEN_WIDTH = width;
    SCREEN_HEIGHT = height;
    
    // Перепрограммирование VGA контроллера
    outb(0x3D4, 0x11);
    outb(0x3D5, 0x00);
    
    // Горизонтальные параметры
    outb(0x3D4, 0x00);
    outb(0x3D5, (width + 5) & 0xFF);
    
    // Вертикальные параметры
    outb(0x3D4, 0x06);
    outb(0x3D5, (height + 2) & 0xFF);
    
    // Включение вертикальной ретрассировки
    outb(0x3D4, 0x11);
    outb(0x3D5, 0x8E);
    
    // Очистка экрана
    clear_screen();
}

// Функция для вывода символа на экран
void print_char(char c, char color) {
    // Обработка управляющих символов
    switch(c) {
        case '\n': // Новая строка
            col = 0;
            row++;
            if (row >= SCREEN_HEIGHT) {
                scroll_screen();
                row = SCREEN_HEIGHT - 1;
            }
            break;
            
        case '\r': // Возврат каретки
            col = 0;
            break;
            
        case '\b': // Backspace
            if (col > 0) {
                col--;
            } else if (row > 0) {
                row--;
                col = SCREEN_WIDTH - 1;
            }
            // Стираем символ
            vidptr[(row * SCREEN_WIDTH + col) * 2] = ' ';
            vidptr[(row * SCREEN_WIDTH + col) * 2 + 1] = color;
            break;
            
        case '\t': // Табуляция
            for (int i = 0; i < 4; i++) {
                print_char(' ', color);
            }
            return; // Не обновлять курсор повторно
            
        default: // Обычный символ
            int offset = (row * SCREEN_WIDTH + col) * 2;
            vidptr[offset] = c;
            vidptr[offset + 1] = color;
            col++;
    }
    
    // Проверка переполнения строки
    if (col >= SCREEN_WIDTH) {
        col = 0;
        row++;
        if (row >= SCREEN_HEIGHT) {
            scroll_screen();
            row = SCREEN_HEIGHT - 1;
        }
    }
    
    // Обновление курсора
    update_cursor(row, col, true);
}

// Функция для вывода строки на экран с заданным цветом
void print_string(const char *str, char color) {
    while (*str) {
        print_char(*str, color);
        str++;
    }
}

void print_version() {
    print_string(kernel_version, LIGHT_GREEN_ON_BLACK);
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
    print_string("\nShutting down...\n", LIGHT_RED_ON_BLACK);
    asm volatile (
        "mov $0x5307, %%ax\n\t"
        "mov $0x0001, %%bx\n\t"
        "mov $0x0003, %%cx\n\t"
        "int $0x15"
        :
        :
        : "ax", "bx", "cx"
    );
    while (1) {
        asm volatile("hlt");
    }
}


// Функция для перезагрузки системы
void reboot_system(void) {
    print_string("\nRebooting...\n", LIGHT_RED_ON_BLACK);
    asm volatile (
        "mov $0x00, %%ax;"
        "int $0x19;"
        : : : "%ax"
    );
    while (1) {
        asm volatile("hlt");
    }
}

// Глобальные переменные для работы с диском
uint32_t current_partition_offset = 0; // Смещение текущего раздела

// Команда для просмотра разделов диска
void view_partitions() {
    uint8_t mbr[SECTOR_SIZE];
    read_disk(mbr, 0);

    print_string("\nPartition Table:\n", LIGHT_CYAN_ON_BLACK);
    print_string("Num Status Type   Start Sector Sector Count\n", LIGHT_GREEN_ON_BLACK);
    print_string("------------------------------------------\n", DARK_GRAY_ON_BLACK);

    struct partition_entry *partitions = (struct partition_entry*)&mbr[446];
    for (int i = 0; i < 4; i++) {
        if (partitions[i].type != 0) {
            char num_str[3], status_str[3], type_str[5], start_str[10], count_str[10];
            
            itoa(i, num_str, 10);
            itoa(partitions[i].status, status_str, 16);
            itoa(partitions[i].type, type_str, 16);
            itoa(partitions[i].lba_start, start_str, 10);
            itoa(partitions[i].sector_count, count_str, 10);
            
            print_string(" ", WHITE_ON_BLACK);
            print_string(num_str, WHITE_ON_BLACK);
            print_string("  0x", WHITE_ON_BLACK);
            print_string(status_str, LIGHT_BLUE_ON_BLACK);
            print_string("   0x", WHITE_ON_BLACK);
            print_string(type_str, LIGHT_BLUE_ON_BLACK);
            print_string("   ", WHITE_ON_BLACK);
            print_string(start_str, WHITE_ON_BLACK);
            print_string("      ", WHITE_ON_BLACK);
            print_string(count_str, WHITE_ON_BLACK);
            print_char('\n', WHITE_ON_BLACK);
        }
    }
    print_string("\nQuartzOS> ", WHITE_ON_BLACK);
}

// Функция для обработки команд
void process_command(char *cmd) {
    // Команда shutdown
    if (strcmp(cmd, "shutdown") == 0) {
        shutdown_system();
    } 
    // Команда reboot
    else if (strcmp(cmd, "reboot") == 0) {
        reboot_system();
    } 
    else if (strcmp(cmd, "kernel-version") == 0) {
        print_version();
        print_string("\nQuartzOS> ", WHITE_ON_BLACK);
    }
    // Команда изменения размера экрана
    else if (strncmp(cmd, "resize", 6) == 0) {
        char *width_str = cmd + 7;
        char *height_str = strchr(width_str, ' ');
        
        if (height_str) {
            *height_str = '\0'; // Разделяем строку
            height_str++;
            
            int new_width = atoi(width_str);
            int new_height = atoi(height_str);
            
            // Проверка допустимых размеров
            if (new_width >= 40 && new_width <= 200 && 
                new_height >= 10 && new_height <= 60) {
                
                set_video_mode(new_width, new_height);
                
                // Формируем сообщение о новом размере
                char msg[60];
                strncpy(msg, "\nScreen resized to ", sizeof(msg));
                
                char width_num[10];
                itoa(new_width, width_num, 10);
                strncat(msg, width_num, sizeof(msg) - strlen(msg));
                
                strncat(msg, "x", sizeof(msg) - strlen(msg));
                
                char height_num[10];
                itoa(new_height, height_num, 10);
                strncat(msg, height_num, sizeof(msg) - strlen(msg));
                
                strncat(msg, "\nQuartzOS> ", sizeof(msg) - strlen(msg));
                
                print_string(msg, LIGHT_GREEN_ON_BLACK);
            } else {
                print_string("\nInvalid size! Valid range: 40-200 x 10-60\nQuartzOS> ", LIGHT_RED_ON_BLACK);
            }
        } else {
            print_string("\nUsage: resize <width> <height>\nQuartzOS> ", LIGHT_RED_ON_BLACK);
        }
    }
    // Команда read-disk
    else if (strncmp(cmd, "read-disk", 9) == 0) {
        char *mode = cmd + 10;
        uint32_t sector = 0;
        
        if (strncmp(mode, "abs ", 4) == 0) {
            sector = atoi(mode + 4);
            current_partition_offset = 0;
        } 
        else if (strncmp(mode, "rel ", 4) == 0) {
            sector = atoi(mode + 4) + current_partition_offset;
        }
        else {
            print_string("\nUsage: read-disk [abs|rel] <sector>\n", LIGHT_RED_ON_BLACK);
            print_string("QuartzOS> ", WHITE_ON_BLACK);
            return;
        }
        
        uint8_t buffer[SECTOR_SIZE];
        print_string("\nReading sector ", WHITE_ON_BLACK);
        
        char num_str[10];
        itoa(sector, num_str, 10);
        print_string(num_str, WHITE_ON_BLACK);
        print_string("...\n", WHITE_ON_BLACK);
        
        read_disk(buffer, sector);
        
        // Вывод прочитанных данных в HEX
        print_string("\nHEX dump:\n", LIGHT_CYAN_ON_BLACK);
        print_string("Offset  00 01 02 03 04 05 06 07  08 09 0A 0B 0C 0D 0E 0F\n", LIGHT_GREEN_ON_BLACK);
        print_string("------  -----------------------------------------------\n", DARK_GRAY_ON_BLACK);
        
        for (int i = 0; i < SECTOR_SIZE; i += 16) {
            // Вывод смещения
            char offset[6];
            itoa(i, offset, 16);
            print_string("0x", DARK_GRAY_ON_BLACK);
            // Дополнение нулями до 4 символов
            int len = strlen(offset);
            for (int pad = 0; pad < 4 - len; pad++) {
                print_char('0', DARK_GRAY_ON_BLACK);
            }
            print_string(offset, LIGHT_BLUE_ON_BLACK);
            print_string(": ", DARK_GRAY_ON_BLACK);
            
            // HEX вывод (16 байт)
            for (int j = 0; j < 16; j++) {
                if (i + j < SECTOR_SIZE) {
                    uint8_t byte = buffer[i + j];
                    char hex[3];
                    hex[0] = "0123456789ABCDEF"[(byte >> 4) & 0x0F];
                    hex[1] = "0123456789ABCDEF"[byte & 0x0F];
                    hex[2] = '\0';
                    print_string(hex, LIGHT_BLUE_ON_BLACK);
                } else {
                    print_string("  ", WHITE_ON_BLACK);
                }
                
                print_char(' ', WHITE_ON_BLACK);
                
                // Разделитель после 8 байт
                if (j == 7) print_string(" ", WHITE_ON_BLACK);
            }
            
            print_char('\n', WHITE_ON_BLACK);
        }
        
        // Пустая строка между HEX и ASCII
        print_char('\n', WHITE_ON_BLACK);
        
        // Вывод прочитанных данных в ASCII
        print_string("ASCII representation:\n", LIGHT_CYAN_ON_BLACK);
        print_string("--------------------------------------------------\n", DARK_GRAY_ON_BLACK);
        
        for (int i = 0; i < SECTOR_SIZE; i += 64) {
            for (int j = 0; j < 64; j++) {
                if (i + j < SECTOR_SIZE) {
                    uint8_t byte = buffer[i + j];
                    if (byte >= 32 && byte < 127) {
                        print_char(byte, WHITE_ON_BLACK);
                    } else {
                        print_char('.', DARK_GRAY_ON_BLACK);
                    }
                }
            }
            print_char('\n', WHITE_ON_BLACK);
        }
        
        print_string("\nDisk read complete\nQuartzOS> ", WHITE_ON_BLACK);
    }
    // Команда write-disk
    else if (strncmp(cmd, "write-disk", 10) == 0) {
        char *mode = cmd + 11;
        uint32_t sector = 0;
        
        if (strncmp(mode, "abs ", 4) == 0) {
            sector = atoi(mode + 4);
            current_partition_offset = 0;
        } 
        else if (strncmp(mode, "rel ", 4) == 0) {
            sector = atoi(mode + 4) + current_partition_offset;
        }
        else {
            print_string("\nUsage: write-disk [abs|rel] <sector>\n", LIGHT_RED_ON_BLACK);
            print_string("QuartzOS> ", WHITE_ON_BLACK);
            return;
        }
        
        uint8_t buffer[SECTOR_SIZE];
        memset(buffer, 0, SECTOR_SIZE);
        print_string("Enter data: ", WHITE_ON_BLACK);
        read_string((char *)buffer, SECTOR_SIZE);
        
        write_disk(buffer, sector);
        print_string("\nData written to disk\nQuartzOS> ", LIGHT_GREEN_ON_BLACK);
    }
    // Команда select-part
    else if (strncmp(cmd, "select-part", 11) == 0) {
        uint32_t partition_num = atoi(cmd + 12);
        
        uint8_t mbr[SECTOR_SIZE];
        read_disk(mbr, 0);
        
        struct partition_entry *partitions = (struct partition_entry*)&mbr[446];
        if (partition_num < 4 && partitions[partition_num].type != 0) {
            current_partition_offset = partitions[partition_num].lba_start;
            
            char msg[50], offset_str[10];
            strncpy(msg, "\nSelected partition ", sizeof(msg));
            itoa(partition_num, offset_str, 10);
            strncat(msg, offset_str, sizeof(msg) - strlen(msg));
            strncat(msg, " (offset: ", sizeof(msg) - strlen(msg));
            itoa(current_partition_offset, offset_str, 10);
            strncat(msg, offset_str, sizeof(msg) - strlen(msg));
            strncat(msg, ")\nQuartzOS> ", sizeof(msg) - strlen(msg));
            
            print_string(msg, LIGHT_GREEN_ON_BLACK);
        } else {
            print_string("\nInvalid partition number!\nQuartzOS> ", LIGHT_RED_ON_BLACK);
        }
    }
    // Команда view-part
    else if (strcmp(cmd, "view-part") == 0) {
        view_partitions();
    }
    else if (strcmp(cmd, "ps") == 0) {
        print_string("\nRunning processes:\n", WHITE_ON_BLACK);
        print_string("PID   State     Threads\n", LIGHT_GREEN_ON_BLACK);
        print_string("----------------------\n", DARK_GRAY_ON_BLACK);
    
        for (int i = 0; i < MAX_PROCESSES; i++) {
            if (processes[i].state != PROCESS_TERMINATED && 
                processes[i].state != PROCESS_NEW) {
            
                // Убрали неиспользуемую state_str
                char pid_str[6], threads_str[4];
                itoa(processes[i].id, pid_str, 10);
                itoa(processes[i].thread_count, threads_str, 10);
            
                // Преобразуем состояние в строку
                const char* state;
                switch(processes[i].state) {
                    case PROCESS_READY: state = "READY"; break;
                    case PROCESS_RUNNING: state = "RUNNING"; break;
                    case PROCESS_BLOCKED: state = "BLOCKED"; break;
                    default: state = "UNKNOWN";
                }
            
                print_string(pid_str, WHITE_ON_BLACK);
                print_string("    ", WHITE_ON_BLACK);
                print_string(state, LIGHT_BLUE_ON_BLACK);
                print_string("     ", WHITE_ON_BLACK);
                print_string(threads_str, WHITE_ON_BLACK);
                print_char('\n', WHITE_ON_BLACK);
            }
        }
        print_string("\nQuartzOS> ", WHITE_ON_BLACK);
    }
    else if (strncmp(cmd, "kill ", 5) == 0) {
        uint32_t pid = atoi(cmd + 5);
        bool found = false;
        
        for (int i = 0; i < MAX_PROCESSES; i++) {
            if (processes[i].id == pid && 
                processes[i].state != PROCESS_TERMINATED) {
                process_exit(&processes[i]);
                print_string("Process terminated\n", LIGHT_GREEN_ON_BLACK);
                found = true;
                break;
            }
        }
        
        if (!found) {
            print_string("Process not found or already terminated\n", LIGHT_RED_ON_BLACK);
        }
        print_string("QuartzOS> ", WHITE_ON_BLACK);
    }
    // Команда clear
    else if (strcmp(cmd, "clear") == 0) {
        clear_screen();
    }
    // Команда help
    else if (strcmp(cmd, "help") == 0) {
        print_string("\nAvailable commands:\n", WHITE_ON_BLACK);
        print_string("  shutdown     - Shutdown the system\n", LIGHT_CYAN_ON_BLACK);
        print_string("  reboot       - Reboot the system\n", LIGHT_CYAN_ON_BLACK);
        print_string("  resize w h   - Change screen size (40-200 x 10-60)\n", LIGHT_CYAN_ON_BLACK);
        print_string("  read-disk    - Read data from disk [abs|rel] <sector>\n", LIGHT_CYAN_ON_BLACK);
        print_string("  write-disk   - Write data to disk [abs|rel] <sector>\n", LIGHT_CYAN_ON_BLACK);
        print_string("  view-part    - View disk partitions\n", LIGHT_CYAN_ON_BLACK);
        print_string("  select-part  - Select active partition\n", LIGHT_CYAN_ON_BLACK);
        print_string("  kernel-version - display kernel version\n", LIGHT_CYAN_ON_BLACK);
        print_string("  clear        - Clear the screen\n", LIGHT_CYAN_ON_BLACK);
        print_string("  help         - Show this help\n", LIGHT_CYAN_ON_BLACK);
        print_string("\nQuartzOS> ", WHITE_ON_BLACK);
    }
    // Неизвестная команда
    else {
        print_string("\nUnknown command! Type 'help' for available commands\nQuartzOS> ", LIGHT_RED_ON_BLACK);
    }
}

// Обновленная функция print_memory_info
void print_memory_info(multiboot_info_t *mbi) {
    if (!(mbi->flags & MULTIBOOT_INFO_MEM_MAP)) {
        print_string("No memory map provided\n", LIGHT_RED_ON_BLACK);
        return;
    }

    print_string("Memory map:\n", WHITE_ON_BLACK);
    print_string("Base Address       Length          Type\n", LIGHT_GREEN_ON_BLACK);
    print_string("----------------------------------------\n", DARK_GRAY_ON_BLACK);

    multiboot_memory_map_t *mmap = (multiboot_memory_map_t *)mbi->mmap_addr;
    uint32_t mmap_end = mbi->mmap_addr + mbi->mmap_length;

    while ((uint32_t)mmap < mmap_end) {
        char buffer[32];
        
        // Вывод адреса
        print_string("0x", WHITE_ON_BLACK);
        itoa((uint32_t)(mmap->addr >> 32), buffer, 16);
        print_string(buffer, LIGHT_BLUE_ON_BLACK);
        itoa((uint32_t)(mmap->addr & 0xFFFFFFFF), buffer, 16);
        print_string(buffer, LIGHT_BLUE_ON_BLACK);
        print_string("  ", WHITE_ON_BLACK);
        
        // Вывод длины
        print_string("0x", WHITE_ON_BLACK);
        itoa((uint32_t)(mmap->len >> 32), buffer, 16);
        print_string(buffer, LIGHT_BLUE_ON_BLACK);
        itoa((uint32_t)(mmap->len & 0xFFFFFFFF), buffer, 16);
        print_string(buffer, LIGHT_BLUE_ON_BLACK);
        print_string("  ", WHITE_ON_BLACK);
        
        // Вывод типа
        itoa(mmap->type, buffer, 10);
        print_string(buffer, LIGHT_BLUE_ON_BLACK);
        print_string("\n", WHITE_ON_BLACK);
        
        mmap = (multiboot_memory_map_t *)((uint32_t)mmap + mmap->size + sizeof(mmap->size));
    }
}

// Пример функции для потока
static void sample_thread_function() {
    int counter = 0;
    while (1) {
        char msg[50];
        char count_str[10];
        itoa(counter, count_str, 10);
        strncpy(msg, "Thread counter: ", sizeof(msg));
        strncat(msg, count_str, sizeof(msg) - strlen(msg));
        strncat(msg, "\n", sizeof(msg) - strlen(msg));
        
        print_string(msg, LIGHT_CYAN_ON_BLACK);
        
        counter++;
        
        // Эмуляция работы
        for (volatile int i = 0; i < 10000000; i++);
    }
}

/* Главная функция */
void kmain(uint32_t magic, multiboot_info_t *mbi) {
    // Проверка магического числа Multiboot
    if (magic != 0x2BADB002) {
        print_string("Invalid Multiboot magic number!\n", LIGHT_RED_ON_BLACK);
        return;
    }

    // Инициализация экрана
    clear_screen();
    set_video_mode(80, 25);
    
    // Вывод информации о памяти
    print_string("\nFetching memory info...\n", WHITE_ON_BLACK);
    print_memory_info(mbi);

    // Инициализация диска с повторной попыткой
    print_string("\nInitializing disk...\n", WHITE_ON_BLACK);
    bool disk_ok = false;
    int attempts = 0;
    const int max_attempts = 2;

    while (attempts < max_attempts && !disk_ok) {
        disk_ok = initialize_disk();
        attempts++;
        
        if (!disk_ok) {
            if (attempts < max_attempts) {
                print_string("Disk initialization failed! Retrying...\n", LIGHT_RED_ON_BLACK);
                // Небольшая задержка перед повторной попыткой
                for (volatile int i = 0; i < 10000000; i++);
            } else {
                print_string("Fatal: Disk initialization failed after ", LIGHT_RED_ON_BLACK);
                char attempts_str[3];
                itoa(max_attempts, attempts_str, 10);
                print_string(attempts_str, LIGHT_RED_ON_BLACK);
                print_string(" attempts\n", LIGHT_RED_ON_BLACK);
                print_string("Rebooting system in 3 seconds...\n", LIGHT_RED_ON_BLACK);
                
                // Обратный отсчет перед перезагрузкой
                for (int i = 3; i > 0; i--) {
                    char count_str[2];
                    itoa(i, count_str, 10);
                    print_string(count_str, LIGHT_RED_ON_BLACK);
                    print_string("... ", LIGHT_RED_ON_BLACK);
                    for (volatile int j = 0; j < 10000000; j++);
                }
                
                reboot_system();
            }
        }
    }

    if (disk_ok) {
        print_string("Disk ready\n", LIGHT_GREEN_ON_BLACK);
    }

    // Инициализация менеджера процессов
    print_string("\nInitializing process manager...\n", WHITE_ON_BLACK);
    init_process_manager();
    
    // Создаем новый процесс
    print_string("Creating sample process...\n", WHITE_ON_BLACK);
    process_t* sample_proc = create_process(sample_thread_function, 10);
    
    if (sample_proc) {
        char msg[60];
        char pid_str[10];
        itoa(sample_proc->id, pid_str, 10);
        strncpy(msg, "Process created! PID: ", sizeof(msg));
        strncat(msg, pid_str, sizeof(msg) - strlen(msg));
        strncat(msg, "\n", sizeof(msg) - strlen(msg));
        
        print_string(msg, LIGHT_GREEN_ON_BLACK);
        
        // Добавляем еще один поток в тот же процесс
        thread_t* second_thread = create_thread(sample_proc, sample_thread_function, 5);
        if (second_thread) {
            strncpy(msg, "Second thread created in process\n", sizeof(msg));
            print_string(msg, LIGHT_GREEN_ON_BLACK);
        }
    }

    print_string("\nQuartzOS Booted Successfully!\n", LIGHT_GREEN_ON_LIGHT_RED);
    print_string("Version: ", LIGHT_BLUE_ON_GREEN);
    print_version();
    // Основной цикл оболочки
    print_string("\nQuartzOS> ", WHITE_ON_BLACK);
    while (1) {
        char c = get_char();
        if (c == '\n') {
            print_char('\n', WHITE_ON_BLACK);
            command[command_length] = '\0';
            process_command(command);
            memset(command, 0, sizeof(command));
            command_length = 0;
            print_string("QuartzOS> ", WHITE_ON_BLACK);
        } else if (c == '\b') {
            if (command_length > 0) {
                command_length--;
                print_char('\b', WHITE_ON_BLACK);
                print_char(' ', WHITE_ON_BLACK);
                print_char('\b', WHITE_ON_BLACK);
            }
        } else if (c != 0 && command_length < (int)(sizeof(command) - 1)) {
            command[command_length++] = c;
            print_char(c, WHITE_ON_BLACK);
        }
    }
}