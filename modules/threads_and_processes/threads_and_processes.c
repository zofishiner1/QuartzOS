#include "threads_and_processes.h"
#include "../templates/kernel_api.h"
#include <stddef.h>
#include <string.h>

// Глобальные переменные
// Убираем static отсюда!
process_t processes[MAX_PROCESSES];
thread_t threads[MAX_PROCESSES * MAX_THREADS_PER_PROCESS];
uint8_t thread_stacks[MAX_PROCESSES * MAX_THREADS_PER_PROCESS][THREAD_STACK_SIZE];

// Эти переменные могут оставаться static, так как они используются только внутри модуля
static process_t* current_process = NULL;
static thread_t* current_thread = NULL;
static uint32_t next_pid = 1;
static uint32_t next_tid = 1;

// Внутренние функции
static process_t* allocate_process();
static thread_t* allocate_thread();
static void setup_thread_stack(thread_t* thread, void (*entry_point)());
static void idle_thread();

// Инициализация подсистемы процессов и потоков
void init_process_manager() {
    memset(processes, 0, sizeof(processes));
    memset(threads, 0, sizeof(threads));
    
    // Создаем idle-процесс
    process_t* idle_proc = create_process(idle_thread, 0);
    if (idle_proc == NULL) {
        print_string("Failed to create idle process!\n", LIGHT_RED_ON_BLACK);
        return;
    }
    
    current_process = idle_proc;
    current_thread = idle_proc->threads[0];
    
    print_string("Process manager initialized\n", LIGHT_GREEN_ON_BLACK);
}

// Создание нового процесса
process_t* create_process(void (*entry_point)(), uint32_t priority) {
    process_t* proc = allocate_process();
    if (proc == NULL) {
        return NULL;
    }
    
    proc->priority = priority;
    proc->state = PROCESS_READY;
    
    // TODO: Инициализация таблицы страниц и кучи
    
    // Создаем главный поток процесса
    thread_t* main_thread = create_thread(proc, entry_point, priority);
    if (main_thread == NULL) {
        proc->state = PROCESS_TERMINATED;
        return NULL;
    }
    
    return proc;
}

// Создание нового потока в процессе
thread_t* create_thread(process_t* process, void (*entry_point)(), uint32_t priority) {
    if (process == NULL || process->thread_count >= MAX_THREADS_PER_PROCESS) {
        return NULL;
    }
    
    thread_t* thread = allocate_thread();
    if (thread == NULL) {
        return NULL;
    }
    
    thread->priority = priority;
    thread->state = PROCESS_READY;
    thread->time_slice = priority + 1; // Чем выше приоритет, тем больше квант времени
    
    // Настраиваем стек потока
    setup_thread_stack(thread, entry_point);
    
    // Добавляем поток в процесс
    process->threads[process->thread_count++] = thread;
    
    return thread;
}

// Переключение на следующий поток
void schedule() {
    // TODO: Реализация планировщика (Round Robin с приоритетами)
    // Пока просто переключаемся на следующий поток в текущем процессе
    
    if (current_process == NULL || current_process->thread_count == 0) {
        return;
    }
    
    uint32_t next_thread_idx = 0;
    if (current_thread != NULL) {
        // Ищем следующий поток в процессе
        for (uint32_t i = 0; i < current_process->thread_count; i++) {
            if (current_process->threads[i] == current_thread) {
                next_thread_idx = (i + 1) % current_process->thread_count;
                break;
            }
        }
    }
    
    thread_t* next_thread = current_process->threads[next_thread_idx];
    if (next_thread->state != PROCESS_READY && next_thread->state != PROCESS_RUNNING) {
        return;
    }
    
    // Сохраняем контекст текущего потока
    save_context(&current_thread->context);
    
    // Переключаемся на следующий поток
    thread_t* prev_thread = current_thread;
    current_thread = next_thread;
    current_thread->state = PROCESS_RUNNING;
    
    if (prev_thread != current_thread) {
        restore_context(&current_thread->context);
    }
}

// Завершение текущего потока
void thread_exit() {
    if (current_thread == NULL) {
        return;
    }
    
    current_thread->state = PROCESS_TERMINATED;
    
    // TODO: Освобождение ресурсов потока
    
    // Переключаемся на другой поток
    schedule();
}

// Завершение процесса и всех его потоков
void process_exit(process_t* process) {
    if (process == NULL) {
        return;
    }
    
    process->state = PROCESS_TERMINATED;
    
    // Завершаем все потоки процесса
    for (uint32_t i = 0; i < process->thread_count; i++) {
        if (process->threads[i] != NULL) {
            process->threads[i]->state = PROCESS_TERMINATED;
        }
    }
    
    // TODO: Освобождение ресурсов процесса
    
    // Если завершается текущий процесс, переключаемся на другой
    if (process == current_process) {
        current_process = NULL;
        current_thread = NULL;
        schedule();
    }
}

// Получение текущего процесса
process_t* get_current_process() {
    return current_process;
}

// Получение текущего потока
thread_t* get_current_thread() {
    return current_thread;
}

// Блокировка потока
void block_thread(thread_t* thread) {
    if (thread != NULL) {
        thread->state = PROCESS_BLOCKED;
        if (thread == current_thread) {
            schedule();
        }
    }
}

// Разблокировка потока
void unblock_thread(thread_t* thread) {
    if (thread != NULL && thread->state == PROCESS_BLOCKED) {
        thread->state = PROCESS_READY;
    }
}

// Установка приоритета потока
void set_thread_priority(thread_t* thread, uint32_t priority) {
    if (thread != NULL) {
        thread->priority = priority;
        thread->time_slice = priority + 1;
    }
}

// Установка приоритета процесса
void set_process_priority(process_t* process, uint32_t priority) {
    if (process != NULL) {
        process->priority = priority;
        // Обновляем приоритеты всех потоков процесса
        for (uint32_t i = 0; i < process->thread_count; i++) {
            if (process->threads[i] != NULL) {
                set_thread_priority(process->threads[i], priority);
            }
        }
    }
}

// ========== Внутренние функции ==========

// Выделение структуры процесса
static process_t* allocate_process() {
    for (uint32_t i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].state == PROCESS_TERMINATED || processes[i].state == PROCESS_NEW) {
            processes[i].id = next_pid++;
            processes[i].state = PROCESS_READY;
            processes[i].thread_count = 0;
            return &processes[i];
        }
    }
    return NULL;
}

// Выделение структуры потока
static thread_t* allocate_thread() {
    for (uint32_t i = 0; i < MAX_PROCESSES * MAX_THREADS_PER_PROCESS; i++) {
        if (threads[i].state == PROCESS_TERMINATED || threads[i].state == PROCESS_NEW) {
            threads[i].id = next_tid++;
            threads[i].state = PROCESS_READY;
            threads[i].stack = thread_stacks[i];
            return &threads[i];
        }
    }
    return NULL;
}

// Настройка стека потока
static void setup_thread_stack(thread_t* thread, void (*entry_point)()) {
    if (thread == NULL || entry_point == NULL) {
        return;
    }
    
    // Инициализируем стек потока
    uint8_t* stack_top = thread->stack + THREAD_STACK_SIZE;
    
    // Выравниваем стек по 16 байтам
    stack_top = (uint8_t*)((uint32_t)stack_top & ~0xF);
    
    // Сохраняем адрес возврата (точку входа в поток)
    *((uint32_t*)stack_top - 1) = (uint32_t)entry_point;
    
    // Настраиваем контекст потока
    thread->context.esp = (uint32_t)stack_top - sizeof(uint32_t);
    thread->context.ebp = thread->context.esp;
    thread->context.eip = (uint32_t)entry_point;
    thread->context.eflags = 0x202; // IF=1, остальные флаги по умолчанию
}

// Поток бездействия (заглушка)
static void idle_thread() {
    while (1) {
        asm volatile("hlt");
    }
}

// ============== Функции переключения контекста ==============
// Реализация функций из threads_and_processes.h

void save_context(cpu_context_t* context) {
    asm volatile (
        "movl %%eax, 0(%0)\n\t"
        "movl %%ebx, 4(%0)\n\t"
        "movl %%ecx, 8(%0)\n\t"
        "movl %%edx, 12(%0)\n\t"
        "movl %%esi, 16(%0)\n\t"
        "movl %%edi, 20(%0)\n\t"
        "movl %%esp, 24(%0)\n\t"
        "movl %%ebp, 28(%0)\n\t"
        "movl (%%esp), %%eax\n\t"
        "movl %%eax, 32(%0)\n\t"  // eip
        "pushfl\n\t"
        "popl %%eax\n\t"
        "movl %%eax, 36(%0)\n\t"  // eflags
        "movl %%cr3, %%eax\n\t"
        "movl %%eax, 40(%0)\n\t"  // cr3
        :
        : "r" (context)
        : "eax", "memory"
    );
}

void restore_context(cpu_context_t* context) {
    asm volatile (
        "movl 40(%0), %%eax\n\t"
        "movl %%eax, %%cr3\n\t"
        "movl 24(%0), %%esp\n\t"
        "movl 28(%0), %%ebp\n\t"
        "movl 36(%0), %%eax\n\t"
        "pushl %%eax\n\t"
        "popfl\n\t"
        "movl 0(%0), %%eax\n\t"
        "movl 4(%0), %%ebx\n\t"
        "movl 8(%0), %%ecx\n\t"
        "movl 12(%0), %%edx\n\t"
        "movl 16(%0), %%esi\n\t"
        "movl 20(%0), %%edi\n\t"
        "pushl 32(%0)\n\t"  // push eip
        "ret\n\t"
        :
        : "r" (context)
        : "memory"
    );
}