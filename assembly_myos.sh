#!/bin/bash

if ! command -v nasm &> /dev/null || ! command -v gcc &> /dev/null || ! command -v ld &> /dev/null; then
    echo "Пожалуйста, установите nasm, gcc и ld."
    exit 1
fi

# создание объектных файлов
echo "Сборка объектных файлов..."
nasm -f elf32 kernel.asm -o kasm.o
if [ $? -ne 0 ]; then
    echo "Ошибка при сборке kernel.asm"
    exit 1
fi

gcc -m32 -c kernel.c -o kc.o
if [ $? -ne 0 ]; then
    echo "Ошибка при сборке kernel.c"
    exit 1
fi

# компоновка объектных файлов в исполняемое ядро
echo "Компоновка объектных файлов в ядро..."
ld -m elf_i386 -T link.ld -o kernel kasm.o kc.o
if [ $? -ne 0 ]; then
    echo "Ошибка при компоновке"
    exit 1
fi

echo "Сборка завершена. Ядро готово к запуску."

# Запуск. расскомментировать при необходимости
# qemu-system-i386 -kernel kernel

echo "Для запуска ядра используйте команду: qemu-system-i386 -kernel kernel"
