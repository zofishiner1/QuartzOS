#!/bin/bash
set -e

# ================== КОНФИГУРАЦИЯ ==================
KERNEL_ASM="kernel.asm"
KERNEL_C="$1"
OUTPUT_ISO="QuartzOS.iso"
LINKER_SCRIPT="link.ld"
BUILD_DIR="build"
ISO_DIR="$BUILD_DIR/iso"
GRUB_CFG="$ISO_DIR/boot/grub/grub.cfg"

# ============== СОЗДАНИЕ ДИРЕКТОРИЙ ================
mkdir -p "$BUILD_DIR" "$ISO_DIR/boot/grub"

# ============= ПРОВЕРКА ЗАВИСИМОСТЕЙ ==============
check_dependency() {
    if ! command -v $1 &> /dev/null; then
        echo "ОШИБКА: Необходимо установить $2"
        echo "Рекомендуемая команда: $3"
        exit 1
    fi
}

check_dependency nasm NASM "sudo apt-get install nasm"
check_dependency gcc GCC "sudo apt-get install gcc"
check_dependency ld Binutils "sudo apt-get install binutils"
check_dependency grub-mkrescue GRUB "sudo apt-get install grub2-common"
check_dependency qemu-img QEMU "sudo apt-get install qemu-utils"
check_dependency qemu-system-i386 QEMU "sudo apt-get install qemu-system"

# ================ ПАРАМЕТРЫ СБОРКИ ================
CFLAGS="-m32 -ffreestanding -fno-stack-protector -Wall -Wextra -O2"
LDFLAGS="-m elf_i386 -T $LINKER_SCRIPT -nostdlib -z noexecstack"

# ============== ЗАПРОС ПАРАМЕТРОВ У ПОЛЬЗОВАТЕЛЯ ==============
read -p "Размер образа диска (МБ, например 100): " DISK_SIZE
read -p "Объем оперативной памяти (МБ, например 16): " RAM_SIZE

# ============== СБОРКА ОБЪЕКТНЫХ ФАЙЛОВ ==============
echo "🔨 Сборка объектных файлов..."
nasm -f elf32 "$KERNEL_ASM" -o "$BUILD_DIR/kasm.o"
gcc $CFLAGS -c "$KERNEL_C" -o "$BUILD_DIR/kc.o"

# ============== КОМПОНОВКА ЯДРА ==============
echo "🔗 Компоновка ядра..."
ld $LDFLAGS -o "$BUILD_DIR/kernel" "$BUILD_DIR/kasm.o" "$BUILD_DIR/kc.o"

# ============== СОЗДАНИЕ ОБРАЗА ISO ==============
echo "📀 Подготовка ISO образа..."
cat > "$GRUB_CFG" << EOF
set timeout=0
set default=0
menuentry "QuartzOS" {
    multiboot /boot/kernel
    boot
}
EOF

cp "$BUILD_DIR/kernel" "$ISO_DIR/boot/"
grub-mkrescue -o "$OUTPUT_ISO" "$ISO_DIR" 2>/dev/null

# ============== СОЗДАНИЕ И ЗАПУСК QEMU ==============
echo "🚀 Создание образа диска и запуск QEMU..."
qemu-img create -f raw quartzos.img "${DISK_SIZE}M"
qemu-system-i386 -m "$RAM_SIZE" -drive format=raw,file=quartzos.img -cdrom "$OUTPUT_ISO" -boot order=d


echo -e "\n✅ Сборка и запуск завершены!\nКоманда для повторного запуска:"
echo "qemu-system-i386 -m $RAM_SIZE -drive format=raw,file=quartzos.img -cdrom $OUTPUT_ISO -boot order=d"
