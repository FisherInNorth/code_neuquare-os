ifeq ($(PLATFORM), qemu_virt)
DIRS-y += src/platform/qemu
endif

ifeq ($(PLATFORM), qemu_sifive_u)
DIRS-y += src/platform/hifive
endif