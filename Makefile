# ============== КОНФИГУРАЦИЯ ==============
KERNEL_ASM = kernel/kernel.asm
KERNEL_C = kernel/kernel.c
ATA_DISK_C = modules/disk/ata_disk.c
ATA_DISK_H = modules/disk/ata_disk.h
COLORS_H = kernel/colors.h # Добавлено
OUTPUT_ISO = QuartzOS.iso
LINKER_SCRIPT = kernel/link.ld
BUILD_DIR = build
ISO_DIR = $(BUILD_DIR)/iso
GRUB_CFG = $(ISO_DIR)/boot/grub/grub.cfg
DISK_SIZE ?= 100
RAM_SIZE ?= 16

# ============== ПАРАМЕТРЫ СБОРКИ ==============
CFLAGS = -m32 -ffreestanding -fno-stack-protector -Wall -Wextra -O2 -Ikernel # Добавлено -Ikernel
LDFLAGS = -m elf_i386 -T $(LINKER_SCRIPT) -nostdlib -z noexecstack

# ============== ЦЕЛИ ==============
all: iso qemu

# ============== СБОРКА ОБЪЕКТНЫХ ФАЙЛОВ ==============
$(BUILD_DIR)/kasm.o: $(KERNEL_ASM)
	@echo "🔨 Сборка assembler-файла..."
	@mkdir -p $(BUILD_DIR)
	@nasm -f elf32 $< -o $@

$(BUILD_DIR)/kc.o: $(KERNEL_C) $(COLORS_H) # COLORS_H добавлено в зависимости
	@echo "🔨 Сборка C-файла kernel.c..."
	@mkdir -p $(BUILD_DIR)
	@gcc $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/ata_disk.o: $(ATA_DISK_C) $(ATA_DISK_H)
	@echo "🔨 Сборка C-файла ata_disk.c..."
	@mkdir -p $(BUILD_DIR)
	@gcc $(CFLAGS) -c $(ATA_DISK_C) -o $@

# ============== КОМПОНОВКА ЯДРА ==============
$(BUILD_DIR)/kernel: $(BUILD_DIR)/kasm.o $(BUILD_DIR)/kc.o $(BUILD_DIR)/ata_disk.o
	@echo "🔗 Компоновка ядра..."
	@ld $(LDFLAGS) -o $@ $^

# ============== СОЗДАНИЕ ОБРАЗА ISO ==============
$(GRUB_CFG): templates/grub.cfg
	@echo "📄 Создание grub.cfg..."
	@mkdir -p $(dir $@)
	@cp templates/grub.cfg $@

$(ISO_DIR)/boot/kernel: $(BUILD_DIR)/kernel
	@echo "⚙️ Копирование ядра в ISO..."
	@mkdir -p $(dir $@)
	@cp $< $@

iso: $(GRUB_CFG) $(ISO_DIR)/boot/kernel
	@echo "📀 Создание ISO образа..."
	@grub-mkrescue -o $(OUTPUT_ISO) $(ISO_DIR) 2>/dev/null

# ============== СОЗДАНИЕ И ЗАПУСК QEMU ==============
qemu: iso
	@echo "🚀 Создание образа диска и запуск QEMU..."
	@qemu-img create -f raw quartzos.img ${DISK_SIZE}M
	@qemu-system-i386 -m ${RAM_SIZE} -drive format=raw,file=quartzos.img -cdrom $(OUTPUT_ISO) -boot order=d

	@echo -e "\n✅ Сборка и запуск завершены!\nКоманда для повторного запуска:"
	@echo "qemu-system-i386 -m ${RAM_SIZE} -drive format=raw,file=quartzos.img -cdrom $(OUTPUT_ISO) -boot order=d"

# ============== УБОРКА ==============
clean:
	@echo "🧹 Очистка..."
	@rm -rf $(BUILD_DIR) $(OUTPUT_ISO) quartzos.img

.PHONY: all iso qemu clean
