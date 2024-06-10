BUILD=../build/lib
# CFLAGS = -Wall -Werror -O0 -fno-omit-frame-pointer -ggdb -gdwarf-2
CFLAGS += -Wno-error
ULIB = $(BUILD)/ulib.o $(BUILD)/printf.o $(BUILD)/umalloc.o $(BUILD)/syscall.o $(BUILD)/clone.o $(BUILD)/sprintf.o
OBJS=$(ULIB)
ARCHIVE=lib.a

all: $(ARCHIVE)

include $(SCRIPTS)/rules.mk

clean:
	@-rm lib.a -f

### Rule (archive): objects (`*.o`) -> `ARCHIVE.a` (ar)
$(ARCHIVE): $(ULIB)
	@echo + AR "->" $(shell realpath $@ --relative-to .)
	@ar rcsT $@ $(ULIB)

# test:
#     $(foreach var,$(.VARIABLES),$(info $(var) = $($(var))))