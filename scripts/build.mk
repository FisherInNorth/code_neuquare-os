ENTRY=entry

# to make sure the entry.o is the first one
_OBJS+=$(filter-out build/src/kernel/asm/entry.o $(OBJ_NOOPT), $(addprefix $(BUILD)/, $(addsuffix .o, $(basename $(SRCS)))))
OBJS=$(BUILD)/src/kernel/asm/entry.o
OBJS+=$(_OBJS)

$(OBJS): EXTRAFLAG := $(KCSANFLAG)
$(OBJ_NOOPT): EXTRAFLAG := $(CFLAGS2)

include $(SCRIPTS)/rules.mk

syscall_gen:
	@sh ./$(ENTRY)/syscalltbl.sh $(ENTRY)/syscall.tbl $(GENINC)/syscall_num.h $(GENINC)/syscall_func.h $(GENINC)/syscall_def.h $(GENINC)/syscall_cnt.h $(GENINC)/syscall_str.h

kernel-qemu: syscall_gen $(OBJS) $(SCRIPTS)/kernel.ld $(OBJS_KCSAN) $(OBJ_NOOPT)
	$(LD) $(LDFLAGS) -T $(SCRIPTS)/kernel.ld -o kernel-qemu $(OBJS) $(OBJS_KCSAN) $(OBJ_NOOPT)
	@$(OBJDUMP) -S kernel-qemu > $(BUILD)/kernel.asm
	@$(OBJDUMP) -t kernel-qemu | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $(BUILD)/kernel.sym

.PHONY: syscall_gen