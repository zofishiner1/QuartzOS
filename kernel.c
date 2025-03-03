/* kernel.c */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define MULTIBOOT_HEADER_MAGIC 0x1BADB002
#define MULTIBOOT_HEADER_FLAGS (1 | 2) // Указание на наличие информации о загружаемом ядре
#define MULTIBOOT_CHECKSUM -(MULTIBOOT_HEADER_MAGIC + MULTIBOOT_HEADER_FLAGS)

/* Определения портов ввода/вывода */
#define KEYBOARD_DATA_PORT   0x60
#define KEYBOARD_STATUS_PORT 0x64

char *vidptr = (char*)0xb8000; // Видео память начинается здесь
char input;
char command[80];     // Буфер для хранения команды
int command_length = 0; // Текущая длина команды
int row = 11;          // Начинаем с 11-й строки
int col = 0;          // Начинаем с первой колонки

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

/* Функция для чтения с порта ввода/вывода */
uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile ("inb %1, %0" : "=a" (ret) : "dN" (port));
    return ret;
}

/* Функция для записи в порт ввода/вывода */
void outb(uint16_t port, uint8_t data) {
    asm volatile ("outb %1, %0" : : "dN" (port), "a" (data));
}

/* Функция для очистки экрана */
void clear_screen(char *vidptr) {
    unsigned int j = 0;

    while (j < 80 * 25 * 2) {
        /* Пустой символ */
        vidptr[j] = ' ';
        /* Байт атрибутов */
        vidptr[j + 1] = 0x07;
        j += 2;
    }
    row = 0; // Сбрасываем текущую строку в 0 после очистки экрана
    col = 0; // Сбрасываем текущий столбец в 0 после очистки экрана
}

/* Функция для вывода строки на экран с заданным цветом */
void print_string(char *vidptr, const char *str, char color) {
    unsigned int i = 0;
    int offset;

    while (str[i] != '\0') {
        offset = (row * 80 + col) * 2; // Вычисляем смещение в видеопамяти

        vidptr[offset] = str[i];          // ASCII отображение
        vidptr[offset + 1] = color;      // Устанавливаем цвет для каждого символа

        col++;                            // Переходим к следующей колонке

        if (col >= 80) {                 // Если достигли конца строки
            col = 0;                      // Переходим на начало следующей строки
            row++;
        }

        if (row >= 25) {                // Если достигли конца экрана
            clear_screen(vidptr);         // Очищаем экран и начинаем сначала
            row = 0;
            col = 0;
        }
        i++;
    }
}

// Функция для преобразования целого числа в строку
void itoa(int num, char *str, int base);

// Таблица преобразования скан-кодов в ASCII
static const char scancode_map[] = {
    0,    0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 0,   0,
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',   0, 'a', 's',
    'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',   0, '\\', 'z', 'x', 'c', 'v',
    'b', 'n', 'm', ',', '.', '/',   0,   '*',   0, ' ',   0,    0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   '7', '8', '9', '-', '4', '5', '6', '+', '1',
    '2', '3', '0', '.'
};

/* Функция для получения символа с клавиатуры */
char get_char() {
    uint8_t scancode;

    /* Ждем, пока клавиатура будет готова */
    do {
        // Ничего не делаем, просто ждем. Проверка статуса порта 0x64
    } while ((inb(KEYBOARD_STATUS_PORT) & 0x01) == 0);

    /* Читаем скан-код из порта данных */
    scancode = inb(KEYBOARD_DATA_PORT);

    /* Проверяем, является ли это кодом отпускания клавиши */
    if (scancode & 0x80) {
        // Это код отпускания, игнорируем его и получаем следующий символ
        return get_char(); // Рекурсивный вызов для получения следующего символа
    }

    // Проверяем, находится ли скан-код в пределах нашей таблицы
    if (scancode < sizeof(scancode_map)) {
        return scancode_map[scancode];
    } else {
        return '?'; // Неизвестный символ
    }
}

// Функция для сравнения двух строк
int strcmp(const char *str1, const char *str2) {
    while (*str1 && (*str1 == *str2)) {
        str1++;
        str2++;
    }
    return *(const unsigned char*)str1 - *(const unsigned char*)str2;
}

// Функция для выключения питания
void shutdown_system() {
    // Уведомляем пользователя о завершении работы
    print_string(vidptr, "Shutting down...", 0x0C); // Светло-красный текст

    // Используем BIOS для выключения питания
    asm volatile (
        "mov $0x00, %%al;"   // Подготовка к вызову BIOS
        "mov $0xFE, %%ah;"   // Команда для выключения питания
        "int $0x10;"         // Вызов прерывания BIOS
        :
        :
        : "%al", "%ah"      // Указываем регистры, которые изменяются
    );

    // Если по какой-то причине BIOS не смог выключить питание,
    // можем просто остановить процессор.
    while (1) {
        asm volatile("hlt"); // Останавливаем процессор до следующего прерывания
    }
}

// Функция для перезагрузки системы
void reboot_system() {
    // Уведомляем пользователя о перезагрузке
    print_string(vidptr, "Rebooting...", 0x0C); // Светло-красный текст

    // Используем BIOS для перезагрузки
    asm volatile (
        "mov $0x00, %%ax;"   // Подготовка к вызову BIOS
        "int $0x19;"         // Команда для перезагрузки (BIOS)
        :
        :
        : "%ax"             // Указываем регистр, который изменяется
    );

    // Если BIOS не поддерживает перезагрузку через int 0x19,
    // можно использовать альтернативные методы, такие как сброс контроллера прерываний.
    // Однако, это может не работать на всех системах.

    // Альтернативный метод через сброс контроллера прерываний (PIC)
    // asm volatile (
    //     "mov $0x64, %%dx;"  // Адрес контроллера прерываний
    //     "mov $0xFE, %%al;"  // Команда для сброса контроллера
    //     "outb %%al, %%dx;"  // Выполнение команды
    //     :
    //     :
    //     : "%ax", "%dx"     // Указываем регистры, которые изменяются
    // );

    // Если перезагрузка не произошла, останавливаем процессор.
    while (1) {
        asm volatile("hlt"); // Останавливаем процессор до следующего прерывания
    }
}

// Структура для хранения регистров процессора
struct regs {
    unsigned char ah, al, bh, bl, ch, cl, dh, dl;
    unsigned short ax, bx, cx, dx, si, di;
    unsigned short es, ds, fs, gs;
    unsigned short ip, cs, flags;
};

// Функция для преобразования целого числа в строку
void itoa(int num, char *str, int base) {
    int i = 0;
    if (num == 0) {
        str[i++] = '0';
        str[i] = '\0';
        return;
    }

    if (num < 0) {
        str[i++] = '-';
        num = -num;
    }

    while (num > 0) {
        int digit = num % base;
        if (digit < 10) {
            str[i++] = digit + '0';
        } else {
            str[i++] = digit - 10 + 'a';
        }
        num /= base;
    }

    str[i] = '\0';

    // Переворачиваем строку
    int len = i;
    for (i = 0; i < len / 2; i++) {
        char temp = str[i];
        str[i] = str[len - i - 1];
        str[len - i - 1] = temp;
    }
}


void kmain(void) {

    /* Этот цикл очищает экран */
    clear_screen(vidptr);

    /* Примеры вывода строк на экран с различными цветами */
    row = 0; // Сбрасываем текущую строку в 0
    col = 0; // Сбрасываем текущий столбец в 0
    print_string(vidptr, "Hello world!", 0x07); // Светло-серый текст
    row++; // Переходим на следующую строку
    col = 0; // Сбрасываем текущий столбец в 0
    print_string(vidptr, "Welcome to the QuartzOS!", 0x0A); // Светло-зеленый текст
    row++; // Переходим на следующую строку
    col = 0; // Сбрасываем текущий столбец в 0
    print_string(vidptr, "This is a custom kernel. Version 0.2", 0x0C); // Светло-красный текст
    row++; // Переходим на следующую строку
    col = 0; // Сбрасываем текущий столбец в 0
    print_string(vidptr, "By Quartz", 0x07);
    row = 10;          // Начинаем с 11-й строки
    col = 0;          // Начинаем с первой колонки
    print_string(vidptr , "Enter command: ", 0x0F); // Белый текст

    while (1) {
        input = get_char();

        if (input == '\n') {
            // Команда введена, обрабатываем ее
            command[command_length] = '\0'; // Завершаем строку нулем

            if (strcmp(command, "clear") == 0) {
                clear_screen(vidptr);
                row = 0; // Начинаем с первой строки после очистки
                col = 0; // Сбрасываем колонку
                print_string(vidptr, "Enter command: ", 0x0F); // Белый текст
                continue; // Переходим к следующей итерации цикла
            } else if (strcmp(command, "help") == 0) {
                row++; // Переходим на новую строку
                col = 0; // Сбрасываем колонку
                print_string(vidptr, "Available commands: clear, help, shutdown, reboot", 0x0F);
            } else if (strcmp(command, "shutdown") == 0) {
                shutdown_system();
            } else if (strcmp(command, "reboot") == 0) {
                reboot_system();
            } else {
                row++; // Переходим на новую строку
                col = 0; // Сбрасываем колонку
                print_string(vidptr, "Unknown command.", 0x04); // Красный текст
            }

            // Сбрасываем для следующей команды
            command_length = 0;
            col = 0;
            row++;
            print_string(vidptr, "Enter command: ", 0x0F); // Белый текст
        } else {
            if (input != 0) {
                // Добавляем символ в буфер команды
                command[command_length++] = input;

                // Эхо-вывод символа на экран
                int offset = (row * 80 + col) * 2;
                vidptr[offset] = input;
                vidptr[offset + 1] = 0x0F;
                col++;


                // Обработка конца строки
                if (col >= 80) {
                    col = 0;
                    row++;
                }

                // Обработка конца экрана
                if (row >= 25) {
                    clear_screen(vidptr);
                    row = 0;
                }
            }
        }
    }

    return;
}
