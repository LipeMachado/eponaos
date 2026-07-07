ASM     := nasm
CC      := x86_64-elf-gcc
OBJCOPY := x86_64-elf-objcopy
QEMU    := qemu-system-x86_64
CLANG_FORMAT := clang-format

BOOT  := boot
BUILD := build

CFLAGS := -std=c17 -O2 -ffreestanding -fno-stack-protector -fno-pie -fno-pic \
          -mno-red-zone -mno-sse -mno-mmx -mno-80387 -mcmodel=large \
          -Wall -Wextra -Iinclude

KOBJS := $(BUILD)/entry.o $(BUILD)/main.o $(BUILD)/gpu.o $(BUILD)/serial.o \
           $(BUILD)/gdt.o $(BUILD)/gdt_flush.o \
           $(BUILD)/idt.o $(BUILD)/isr.o $(BUILD)/isr_stub.o \
           $(BUILD)/pic.o $(BUILD)/pit.o $(BUILD)/keyboard.o $(BUILD)/mouse.o \
		   $(BUILD)/pmm.o $(BUILD)/paging.o $(BUILD)/heap.o $(BUILD)/string.o \
		   $(BUILD)/scheduler.o $(BUILD)/switch.o $(BUILD)/pci.o $(BUILD)/ata.o \
		   $(BUILD)/vfs.o $(BUILD)/fat.o $(BUILD)/rtl8139.o $(BUILD)/net.o \
		   $(BUILD)/shell.o $(BUILD)/syscall.o $(BUILD)/syscall_asm.o \
		   $(BUILD)/elf.o $(BUILD)/gui.o $(BUILD)/cursor.o $(BUILD)/term.o

STAGE1     := $(BUILD)/stage1.bin
STAGE2     := $(BUILD)/stage2.bin
KERNEL_ELF := $(BUILD)/kernel.elf
KERNEL_BIN := $(BUILD)/kernel.bin
IMG        := $(BUILD)/eponaos.img
DATA_IMG   := $(BUILD)/data.img
QEMUFLAGS  := -drive format=raw,file=$(IMG) -drive format=raw,file=$(DATA_IMG) \
              -vga std \
              -netdev user,id=net0 -device rtl8139,netdev=net0 \
              -serial stdio -no-reboot

.PHONY: all run debug clean format
all: $(IMG) $(DATA_IMG) $(BUILD_USER)

$(BUILD)/entry.o: kernel/entry.asm | $(BUILD)
	$(ASM) -f elf64 $< -o $@
$(BUILD)/main.o: kernel/main.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/string.o: lib/string.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/gpu.o: drivers/gpu.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/serial.o: drivers/serial.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/gdt.o: kernel/gdt.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/gdt_flush.o: kernel/gdt_flush.asm | $(BUILD)
	$(ASM) -f elf64 $< -o $@
$(BUILD)/idt.o: kernel/idt.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/isr.o: kernel/isr.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/isr_stub.o: kernel/isr.asm | $(BUILD)
	$(ASM) -f elf64 $< -o $@
$(BUILD)/pic.o: kernel/pic.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/pit.o: kernel/pit.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/keyboard.o: drivers/keyboard.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/mouse.o: drivers/mouse.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/pci.o: drivers/pci.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/ata.o: drivers/ata.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/rtl8139.o: drivers/rtl8139.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/net.o: net/net.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/shell.o: kernel/shell.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/gui.o: kernel/gui.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/cursor.o: drivers/cursor.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/term.o: kernel/term.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/vfs.o: fs/vfs.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/fat.o: fs/fat.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/pmm.o: mm/pmm.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/paging.o: mm/paging.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/heap.o: mm/heap.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/scheduler.o: kernel/scheduler.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/switch.o: kernel/switch.asm | $(BUILD)
	$(ASM) -f elf64 $< -o $@
$(BUILD)/syscall.o: kernel/syscall.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/syscall_asm.o: kernel/syscall_asm.asm | $(BUILD)
	$(ASM) -f elf64 $< -o $@
$(BUILD)/elf.o: kernel/elf.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

# user-space programs
USER_CFLAGS := -std=c17 -ffreestanding -nostdlib -fno-stack-protector -fno-pie -fno-pic \
               -mno-red-zone -mno-sse -mno-mmx -mno-80387 -mcmodel=large -Wall -Wextra \
               -I user -I include
BUILD_USER := $(BUILD)/test.elf $(BUILD)/shell.elf $(BUILD)/edit.elf $(BUILD)/spin.elf

$(BUILD)/test.o: user/test.S user/link.ld | $(BUILD)
	nasm -f elf64 user/test.S -o $@
$(BUILD)/crt0.o: user/crt0.S | $(BUILD)
	nasm -f elf64 user/crt0.S -o $@
$(BUILD)/test_c.o: user/test.c | $(BUILD)
	$(CC) $(USER_CFLAGS) -c $< -o $@
$(BUILD)/test.elf: $(BUILD)/crt0.o $(BUILD)/test_c.o user/link.ld | $(BUILD)
	x86_64-elf-ld -T user/link.ld -o $@ $(BUILD)/crt0.o $(BUILD)/test_c.o
	@echo "==> user test_c.elf created"

$(BUILD)/edit_c.o: user/edit.c
	$(CC) $(USER_CFLAGS) -c $< -o $@
$(BUILD)/edit.elf: $(BUILD)/crt0.o $(BUILD)/edit_c.o user/link.ld | $(BUILD)
	x86_64-elf-ld -T user/link.ld -o $@ $(BUILD)/crt0.o $(BUILD)/edit_c.o
	@echo "==> user edit.elf created"

$(BUILD)/shell_c.o: user/shell.c | $(BUILD)
	$(CC) $(USER_CFLAGS) -c $< -o $@
$(BUILD)/shell.elf: $(BUILD)/crt0.o $(BUILD)/shell_c.o user/link.ld | $(BUILD)
	x86_64-elf-ld -T user/link.ld -o $@ $(BUILD)/crt0.o $(BUILD)/shell_c.o
	@echo "==> user shell.elf created"

$(BUILD)/spin_c.o: user/spin.c | $(BUILD)
	$(CC) $(USER_CFLAGS) -c $< -o $@
$(BUILD)/spin.elf: $(BUILD)/crt0.o $(BUILD)/spin_c.o user/link.ld | $(BUILD)
	x86_64-elf-ld -T user/link.ld -o $@ $(BUILD)/crt0.o $(BUILD)/spin_c.o
	@echo "==> user spin.elf created"

$(KERNEL_ELF): $(KOBJS) linker.ld
	$(CC) -nostdlib -no-pie -T linker.ld -o $@ $(KOBJS) -lgcc

$(KERNEL_BIN): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $< $@
	@SIZE=$$(wc -c < $@); PAD=$$(( (512 - SIZE % 512) % 512 )); \
	 if [ $$PAD -ne 0 ]; then dd if=/dev/zero bs=1 count=$$PAD >> $@ 2>/dev/null; fi
	@echo "==> kernel.bin: $$(wc -c < $@) bytes / $$(( $$(wc -c < $@) / 512 )) setores"

$(STAGE1): $(BOOT)/stage1.asm | $(BUILD)
	$(ASM) -f bin $< -o $@
$(STAGE2): $(BOOT)/stage2.asm $(KERNEL_BIN) | $(BUILD)
	@KS=$$(( $$(wc -c < $(KERNEL_BIN)) / 512 )); \
	 echo "==> stage2 com KERNEL_SECTORS=$$KS"; \
	 $(ASM) -f bin -D KERNEL_SECTORS=$$KS $< -o $@

$(IMG): $(STAGE1) $(STAGE2) $(KERNEL_BIN)
	cat $(STAGE1) $(STAGE2) $(KERNEL_BIN) > $@

$(BUILD):
	mkdir -p $(BUILD)

# disco FAT32 com arquivos de teste
$(DATA_IMG): $(BUILD_USER) | $(BUILD)
	dd if=/dev/zero bs=1M count=4 of=$@ 2>/dev/null
	mkfs.fat -F 32 $@ >/dev/null 2>&1
	echo "Hello from EponaOS! FAT32 works!" | mcopy -i $@ - ::HELLO.TXT
	echo "Another file for listing test." | mcopy -i $@ - ::README.TXT
	mcopy -i $@ $(BUILD)/test.elf ::TEST.ELF
	mcopy -i $@ $(BUILD)/shell.elf ::SHELL.ELF
	mcopy -i $@ $(BUILD)/edit.elf ::EDIT.ELF
	mcopy -i $@ $(BUILD)/spin.elf ::SPIN.ELF
	mcopy -i $@ packages/repo.epk ::REPO.EPK
	mcopy -i $@ packages/shell.epk ::SHELL.EPK
	mcopy -i $@ packages/test.epk ::TEST.EPK
	for f in assets/cursors/*.cur; do \
		[ -f "$$f" ] && { mmd -i $@ ::CURSORS 2>/dev/null || true; mcopy -i $@ "$$f" ::CURSORS/ ; } \
	done 2>/dev/null || true
	@echo "==> data.img criado (FAT32)"

run: $(IMG) $(DATA_IMG)
	$(QEMU) $(QEMUFLAGS) -d guest_errors
debug: $(IMG) $(DATA_IMG)
	$(QEMU) $(QEMUFLAGS) -S -s

format:
	$(CLANG_FORMAT) -i kernel/*.c lib/*.c include/*.h 2>/dev/null || true
	@echo "==> C formatado (asm/make sem formatador; editorconfig cuida do whitespace)"

clean:
	rm -rf $(BUILD)/*
