# GDB may have ./.gdbinit loading disabled by default.  In that case you can
# follow the instructions it prints.  They boil down to adding the following to
# your home directory's ~/.gdbinit file:
#
#   add-auto-load-safe-path /path/to/qemu/.gdbinit

# Load QEMU-specific sub-commands and settings
source scripts/qemu-gdb.py

file x86_64-softmmu/qemu-system-x86_64
set args -nographic
source gdb-script
b pc_memory_init
r
dump_memory_region system_memory
