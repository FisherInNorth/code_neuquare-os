.section .text
.global _entry
# sbi put the mhartid in a0
_entry:
        # keep each CPU's hartid in its tp register, for cpuid().
        mv tp, a0

        # set up a stack for C.
        la sp, stack0
        li t0, 1024*4
        mv t1, a0
        # sp = stack0 + (hartid * 4096)
        addi t1, t1, 1
        mul t0, t0, t1
        add sp, sp, t0

        # jump to main
        call main

spin:
        j spin
