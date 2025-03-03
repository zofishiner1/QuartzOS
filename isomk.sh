#!/bin/bash

# Проверка наличия необходимых инструментов
if ! command -v nasm &> /dev/null || ! command -v gcc &> /dev/null || ! command -v ld &> /dev/null || ! command -v grub-mkrescue &> /dev/null; then
    echo "Пожалуйста, установите nasm, gcc, ld и grub."
    exit 1
fi

# Проверка количества переданных аргументов
if [ $# -ne 1 ]; then
    echo "Использование: $0 <kernel.c>"
    exit 1
fi

# Имя файла на C
KERNEL_C=$1

# Фиксированный ассемблерный файл
KERNEL_ASM="kernel.asm"

# Создание объектных файлов
echo "Сборка объектных файлов..."
nasm -f elf32 "$KERNEL_ASM" -o kasm.o
if [ $? -ne 0 ]; then
    echo "Ошибка при сборке $KERNEL_ASM"
    exit 1
fi

gcc -m32 -c -fno-stack-protector "$KERNEL_C" -o kc.o
if [ $? -ne 0 ]; then
    echo "Ошибка при сборке $KERNEL_C"
    exit 1
fi

# Компоновка объектных файлов в исполняемое ядро
echo "Компоновка объектных файлов в ядро..."
ld -m elf_i386 -T link.ld -nostdlib -z noexecstack -o kernel kasm.o kc.o
if [ $? -ne 0 ]; then
    echo "Ошибка при компоновке"
    exit 1
fi

echo "Сборка завершена. Ядро готово к запуску."

# Создание директории для ISO образа
mkdir -p iso/boot/grub

# Копирование ядра в директорию ISO образа
cp kernel iso/boot/

# Создание конфигурационного файла GRUB
cat << EOF > iso/boot/grub/grub.cfg
set timeout=0
set default=0

menuentry "My Custom Kernel" {
    multiboot /boot/kernel
    boot
}
EOF

# Создание ISO образа с использованием grub-mkrescue
echo "Создание ISO образа..."
grub-mkrescue -o QuartzOS.iso iso/
if [ $? -ne 0 ]; then
    echo "Ошибка при создании ISO образа"
    exit 1
fi

echo "ISO образ QuartzOS.iso успешно создан."

# Очистка временных файлов
rm -rf iso

echo "Для запуска ядра используйте команду: qemu-system-i386 -cdrom QuartzOS.iso"
