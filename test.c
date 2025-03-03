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
void itoa(int num, char *str, int base) {
    int i = 0;
    int isNegative = 0;

    if (num == 0) {
        str[i++] = '0';
        str[i] = '\0';
        return;
    }

    if (num < 0 && base == 10) {
        isNegative = 1;
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

    if (isNegative) {
        str[i++] = '-';
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

// Функция для преобразования строки в целое число
int atoi(const char *str) {
    int num = 0;
    int sign = 1;

    if (*str == '-') {
        sign = -1;
        str++;
    } else if (*str == '+') {
        str++;
    }

    while (*str != '\0') {
        if (*str >= '0' && *str <= '9') {
            num = num * 10 + (*str - '0');
            str++;
        } else {
            break;
        }
    }

    return num * sign;
}

// Статический буфер для выделения памяти
char memory_buffer[1024];
int memory_offset = 0;

// Функция для выделения памяти
void* malloc(size_t size) {
    if (memory_offset + size > sizeof(memory_buffer)) {
        return NULL; // Не хватает памяти
    }

    void* ptr = (void*)&memory_buffer[memory_offset];
    memory_offset += size;
    return ptr;
}

// Функция для освобождения памяти (в данном случае не делает ничего)
void free(void* ptr) {
    // В этом примере освобождение памяти не реализовано
}

/* ========================= код драйвера жёсткого диска ========================= */

// Структура для запроса на чтение/запись
typedef struct {
    uint32_t sector; // Номер сектора
    uint32_t count; // Количество секторов для чтения/записи
    void* buffer; // Указатель на буфер для данных
} request_t;

// Определение типов контроллеров
#define AHCI_CLASS 0x01
#define AHCI_SUBCLASS 0x06
#define NVME_CLASS 0x01
#define NVME_SUBCLASS 0x08

// Структура PCI-устройства
typedef struct {
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint32_t bar0;
} pci_device_t;

typedef enum {
    AHCI,
    NVME,
    IDE
} controller_type_t;

// Структура для представления жёсткого диска
typedef struct {
    uint32_t base_port;
    uint16_t sector_size;
    uint32_t num_sectors;
    controller_type_t controller_type;
    uint32_t max_lba;
    volatile uint32_t* mmio_base; // Для NVMe
} disk_t;

// Глобальная переменная для диска
disk_t disk;

// Прототипы функций
uint8_t pci_read_byte(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
pci_device_t detect_storage_controller();

// Функция для записи 32-битного значения в порт ввода/вывода
void outl(uint32_t port, uint32_t value) {
    asm volatile ("outl %1, %0" : : "dN" (port), "a" (value));
}

// Функция для чтения 32-битного значения из порта ввода/вывода
uint32_t inl(uint32_t port) {
    uint32_t ret;
    asm volatile ("inl %1, %0" : "=a" (ret) : "dN" (port));
    return ret;
}

// Функция для чтения слова из порта ввода/вывода
uint16_t inw(uint16_t port) {
    uint16_t ret;
    asm volatile ("inw %1, %0" : "=a" (ret) : "dN" (port));
    return ret;
}

// Функция для записи слова в порт ввода/вывода
void outw(uint16_t port, uint16_t data) {
    asm volatile ("outw %1, %0" : : "dN" (port), "a" (data));
}

// Инициализация драйвера диска
void disk_init(disk_t* disk) {
    pci_device_t controller = detect_storage_controller();
    row++;
    col = 0;
    print_string(vidptr, "Initializing storage controller...", 0x0E);
    row++; col = 0;

    if(controller.class_code == AHCI_CLASS && controller.subclass == AHCI_SUBCLASS) {
        // Настройка для SATA/AHCI
        disk->controller_type = AHCI;
        disk->base_port = controller.bar0 & 0xFFFFFFFC; // Базовый адрес порта
        disk->sector_size = 512;
        disk->max_lba = 0;
        
        print_string(vidptr, "Detected AHCI SATA controller", 0x0A); // Зеленый
        row++; col = 0;
        print_string(vidptr, "Base address: 0x", 0x0A);
        char hex[9];
        itoa(controller.bar0, hex, 16);
        print_string(vidptr, hex, 0x0A);
        row++; col = 0;
    } 
    else if(controller.class_code == NVME_CLASS && controller.subclass == NVME_SUBCLASS) {
        // Настройка для NVMe
        disk->controller_type = NVME;
        disk->mmio_base = (volatile uint32_t*)(controller.bar0 & 0xFFFFFFF0);
        disk->sector_size = 4096; // Типичный размер сектора для NVMe
        disk->max_lba = 0;
        
        print_string(vidptr, "Detected NVMe controller", 0x0B); // Голубой
        row++; col = 0;
        print_string(vidptr, "MMIO Base: 0x", 0x0B);
        char hex[9];
        itoa((uint32_t)disk->mmio_base, hex, 16);
        print_string(vidptr, hex, 0x0B);
        row++; col = 0;
    }
    else {
        // Резервный режим IDE
        disk->controller_type = IDE;
        disk->base_port = 0x1E8;
        disk->sector_size = 512;
        
        print_string(vidptr, "Using legacy IDE mode", 0x0C); // Красный
        row++; col = 0;
    }

    // Общая инициализация
    char sector_size_str[10];
    itoa(disk->sector_size, sector_size_str, 10);
    print_string(vidptr, "Sector size: ", 0x0E);
    print_string(vidptr, sector_size_str, 0x0E);
    print_string(vidptr, " bytes", 0x0E);
    row++; col = 0;
}

// Обнаружение контроллера хранилища через PCI
// Функция для чтения байта из порта ввода/вывода PCI-устройства
uint8_t pci_read_byte(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = (uint32_t)0x00000000 | (bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC);
    outl(0xCF8, address); // Записываем адрес в порт конфигурации
    uint32_t data = inl(0xCFC); // Читаем данные из порта конфигурации
    return (data >> ((offset & 3) * 8)) & 0xFF;
}


pci_device_t detect_storage_controller() {
    pci_device_t dev = {0};
    
    // Сканирование PCI шин
    for(int bus = 0; bus < 256; bus++) {
        for(int slot = 0; slot < 32; slot++) {
            uint8_t class_code = pci_read_byte(bus, slot, 0, 0x0B);
            uint8_t subclass = pci_read_byte(bus, slot, 0, 0x0A);
            
            if((class_code == AHCI_CLASS && subclass == AHCI_SUBCLASS) ||
               (class_code == NVME_CLASS && subclass == NVME_SUBCLASS)) {
                // Чтение BAR0
                dev.bar0 = pci_read_byte(bus, slot, 0, 0x10) |
                          (pci_read_byte(bus, slot, 0, 0x11) << 8) |
                          (pci_read_byte(bus, slot, 0, 0x12) << 16) |
                          (pci_read_byte(bus, slot, 0, 0x13) << 24);
                return dev;
            }
        }
    }
    return dev;
}

// Обработка запроса на чтение
void disk_read(disk_t* disk, request_t* req) {
    print_string(vidptr, "Preparing to read sector...", 0x0E);
    row++;
    col = 0;

    // Установка номера сектора и количества секторов
    uint32_t sector = req->sector;
    uint32_t count = req->count;

    print_string(vidptr, "Sector number: ", 0x0E);
    char sector_str[10];
    itoa(sector, sector_str, 10);
    print_string(vidptr, sector_str, 0x0E);
    row++;
    col = 0;

    print_string(vidptr, "Count of sectors to read: ", 0x0E);
    char count_str[10];
    itoa(count, count_str, 10);
    print_string(vidptr, count_str, 0x0E);
    row++;
    col = 0;

    if (disk->controller_type == IDE) {
        // Установка контроллера в режим чтения (IDE)
        outb(disk->base_port + 0x06, 0xE0 | ((sector >> 24) & 0x0F)); // Выбор диска и режим
        print_string(vidptr, "Sending command to select disk and sector...", 0x0E);
        row++;
        col = 0;

        outb(disk->base_port + 0x03, sector & 0xFF); // LBA Low
        outb(disk->base_port + 0x04, (sector >> 8) & 0xFF); // LBA Mid
        outb(disk->base_port + 0x05, (sector >> 16) & 0xFF); // LBA High
        outb(disk->base_port + 0x02, count); // Количество секторов для чтения
        print_string(vidptr, "Sending sector count and read command...", 0x0E);
        row++;
        col = 0;

        outb(disk->base_port + 0x07, 0x20); // Команда чтения

        print_string(vidptr, "Waiting for disk controller to become ready...", 0x0E);
        row++;
        col = 0;
        while ((inb(disk->base_port + 0x07) & 0x88) != 0x08);
        row++;
        col = 0;
        print_string(vidptr, "Disk controller is ready. Starting data transfer...", 0x0E);
        row++;
        col = 0;

        // Чтение данных
        uint16_t* buffer = (uint16_t*)req->buffer;
        for (int i = 0; i < count * disk->sector_size / 2; i++) {
            uint16_t data = inw(disk->base_port + 0x00);
            buffer[i] = data;
        }

        print_string(vidptr, "Data transfer completed.", 0x0E);
        row++;
        col = 0;
    } else if (disk->controller_type == AHCI) {
        // TODO: Implement AHCI read
        print_string(vidptr, "AHCI read not implemented yet.", 0x04);
        row++; col = 0;
    } else if (disk->controller_type == NVME) {
        // TODO: Implement NVMe read
        print_string(vidptr, "NVMe read not implemented yet.", 0x04);
        row++; col = 0;
    }
}

// Вывод прочитанных данных
void print_read_data(request_t* req, disk_t* disk) {
    print_string(vidptr, "Printing read data...", 0x0E);
    row++;
    col = 0;

    uint16_t* buffer = (uint16_t*)req->buffer;
    for (int i = 0; i < req->count * disk->sector_size / 2; i++) {
        char hex_str[5];
        itoa(buffer[i], hex_str, 16);
        print_string(vidptr, hex_str, 0x0E);
        print_string(vidptr, " ", 0x0E);
        if ((i + 1) % 8 == 0) {
            row++;
            col = 0;
        }
    }

    print_string(vidptr, "Data printing completed.", 0x0E);
    row++;
    col = 0;
}

// Функция для выполнения чтения с диска
void read_disk_command() {
    print_string(vidptr, "Starting disk read command...", 0x0E);
    row++;
    col = 0;

    // Запрашиваем у пользователя номер сектора для чтения
    print_string(vidptr, "Enter sector number to read: ", 0x0E);
    row++;
    col = 0;

    char sector_str[10];
    int sector_index = 0;
    char input_char;

    while ((input_char = get_char()) != '\n') {
        if (input_char >= '0' && input_char <= '9' && sector_index < 9) {
            sector_str[sector_index++] = input_char;
            int offset = (row * 80 + col) * 2;
            vidptr[offset] = input_char;
            vidptr[offset + 1] = 0x0F;
            col++;
        }
    }
    sector_str[sector_index] = '\0';

    int sector = 0;
    if (sector_str[0] != '\0') {
        sector = atoi(sector_str);
    }

    // Инициализируем диск и параметры чтения
    disk_init(&disk);
    request_t req;
    req.sector = sector;
    req.count = 1; // Читаем один сектор
    req.buffer = malloc(disk.sector_size); // Выделение памяти для буфера

    if (req.buffer == NULL) {
        print_string(vidptr, "Failed to allocate memory!", 0x04);
        return;
    }

    print_string(vidptr, "Executing disk read operation...", 0x0E);
    row++;
    col = 0;

    // Выполняем чтение с диска
    disk_read(&disk, &req);

    if (disk.controller_type == IDE) {
        // Проверяем статус контроллера (IDE)
        uint8_t status = inb(disk.base_port + 0x07);
        if (status & 0x01) { // Ошибка
            print_string(vidptr, "Error reading sector.", 0x04);
        } else {
            // Выводим прочитанные данные на экран
            print_string(vidptr, "Read data: ", 0x0E);
            row++;
            col = 0;
            print_read_data(&req, &disk);
        }
    }
     else {
        print_string(vidptr, "Data printing skipped: non-IDE controller.", 0x0E);
    }

    // Освобождаем выделенную память
    free(req.buffer);

    print_string(vidptr, "Disk read command completed.", 0x0E);
    row++;
    col = 0;

    // Готовимся к следующей команде
    row++;
    col = 0;
}

/* ========================= конец кода драйвера жёсткого диска ========================= */

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
    row++; // Переходим на следующую строку
    row++; // Переходим на следующую строку
    col = 0; // Сбрасываем текущий столбец в 0

    /* Инициализация драйвера диска */
    disk_init(&disk);

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
                print_string(vidptr, "Available commands: clear, help, shutdown, reboot, read-disk", 0x0F);
            }  else if (strcmp(command, "shutdown") == 0) {
                shutdown_system();
            } else if (strcmp(command, "reboot") == 0) {
                reboot_system();
            } else if (strcmp(command, "read-disk") == 0) {
                row++;
                col = 0;
                read_disk_command();
            } else {
                clear_screen(vidptr);
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
