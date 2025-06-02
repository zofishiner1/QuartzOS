#ifndef KERNEL_API_H
#define KERNEL_API_H

#include <stddef.h>  // Добавим для size_t
#include "colors.h"

// Объявления функций ядра
void print_string(const char *str, uint8_t color);
void print_char(char c, uint8_t color);
void* memset(void* ptr, int value, size_t num);  // Теперь size_t определен

#endif // KERNEL_API_H