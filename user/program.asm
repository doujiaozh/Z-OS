BITS 64

section .rodata
global user_program_start
global user_program_end
global user_multi_start
global user_multi_end
global user_forktest_start
global user_forktest_end
global user_guitest_start
global user_guitest_end
global user_postest_start
global user_postest_end
global user_guitest2_start
global user_guitest2_end
global user_nettest2_start
global user_nettest2_end

user_program_start:
    incbin "user_init.elf"
user_program_end:

user_multi_start:
    incbin "user_multi.elf"
user_multi_end:

user_forktest_start:
    incbin "user_forktest.elf"
user_forktest_end:

user_guitest_start:
    incbin "user_guitest.elf"
user_guitest_end:

user_postest_start:
    incbin "user_postest.elf"
user_postest_end:

user_guitest2_start:
    incbin "user_guitest2.elf"
user_guitest2_end:

user_nettest2_start:
    incbin "user_nettest2.elf"
user_nettest2_end:
