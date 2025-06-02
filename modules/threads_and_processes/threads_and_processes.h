#ifndef THREADS_AND_PROCESSES_H
#define THREADS_AND_PROCESSES_H

#include <stdint.h>
#include <stdbool.h>

// Максимальное количество процессов
#define MAX_PROCESSES 32
// Максимальное количество потоков на процесс
#define MAX_THREADS_PER_PROCESS 8
// Размер стека потока (4KB)
#define THREAD_STACK_SIZE 4096

// Состояния процесса/потока
typedef enum {
    PROCESS_NEW,        // Только создан
    PROCESS_READY,      // Готов к выполнению
    PROCESS_RUNNING,    // Выполняется
    PROCESS_BLOCKED,    // Ожидает ресурс
    PROCESS_TERMINATED  // Завершен
} process_state_t;

// Контекст процессора для сохранения при переключении
typedef struct {
    uint32_t eax, ebx, ecx, edx;
    uint32_t esi, edi;
    uint32_t esp, ebp;
    uint32_t eip;
    uint32_t eflags;
    uint32_t cr3; // Указатель на таблицу страниц
} cpu_context_t;

// Дескриптор потока
typedef struct {
    uint32_t id;                // Идентификатор потока
    cpu_context_t context;      // Контекст процессора
    uint8_t* stack;             // Указатель на стек потока
    process_state_t state;      // Состояние потока
    uint32_t priority;          // Приоритет потока (0-255)
    uint32_t time_slice;        // Оставшееся время выполнения
} thread_t;

// Дескриптор процесса
typedef struct {
    uint32_t id;                // Идентификатор процесса
    thread_t* threads[MAX_THREADS_PER_PROCESS]; // Потоки процесса
    uint32_t thread_count;      // Количество потоков
    process_state_t state;      // Состояние процесса
    uint32_t priority;          // Базовый приоритет процесса
    uint32_t* page_directory;   // Таблица страниц процесса
    uint32_t heap_start;        // Начало кучи процесса
    uint32_t heap_end;          // Конец кучи процесса
} process_t;

// Инициализация подсистемы процессов и потоков
void init_process_manager();

// Создание нового процесса
process_t* create_process(void (*entry_point)(), uint32_t priority);

// Создание нового потока в процессе
thread_t* create_thread(process_t* process, void (*entry_point)(), uint32_t priority);

// Переключение на следующий поток
void schedule();

// Завершение текущего потока
void thread_exit();

// Завершение процесса и всех его потоков
void process_exit(process_t* process);

// Получение текущего процесса
process_t* get_current_process();

// Получение текущего потока
thread_t* get_current_thread();

// Блокировка потока
void block_thread(thread_t* thread);

// Разблокировка потока
void unblock_thread(thread_t* thread);

// Установка приоритета потока
void set_thread_priority(thread_t* thread, uint32_t priority);

// Установка приоритета процесса
void set_process_priority(process_t* process, uint32_t priority);

// Объявления функций для переключения контекста
void save_context(cpu_context_t* context);
void restore_context(cpu_context_t* context);

// Объявление глобального массива процессов
extern process_t processes[MAX_PROCESSES];

#endif // THREADS_AND_PROCESSES_H