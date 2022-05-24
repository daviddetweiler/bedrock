# Bedrock "Emulator"

A retro-inspired, 16-bit emulator along the lines of the microcomputer era.

## Building

Project uses CMake as its build system, but the entire emulator is a single C++ file with no dependencies; it could just
as easily be compiled manually. Run the following commands from the project root to install it to your `PATH` on a Linux
system:
```bash
mkdir build && cd build && cmake .. -DCMAKE_BUILD_TYPE=Release && cmake --build . && sudo cmake --install .
```

## Usage
```
bedrock <disk0-path> <disk1-path>
```

Both paths are mandatory, but either disk can be left "disconnected" by passing `--` as its path.

## Emulator Manual

### Instruction Set Architecture
The emulator's ISA is a 16-bit, word-addressed load/store architecture with a separate I/O bus address space. 16
registers are supported, along with 16 opcodes. All instructions are one word (16 bits) wide, and follow the same
format:
```
+-----------------------------------------------+
| MSB                               LSB         |
+-----------+-----------+-----------+-----------+
| 4 bits    | 4 bits    | 4 bits    | 4 bits    |
+-----------+-----------+-----------+-----------+
| op        | dst       | src1      | src0      |
+-----------+-----------+-----------+-----------+
```

Conceptually, the contents of the memory address space and bus address space can be considered as arrays `mem` and
`bus`, each consisting of 2^16 words. Importantly, bus and memory addresses refer to _words_, not bytes. Similarly,
registers may be conceptualized as a 16-word array `regs`. Finally, a 16-bit program counter `pc` contains the memory
address from which the next instruction word will be read. At startup, memory, registers, and program counter are all
initialized to zero. At each execution cycle, the emulator reads the instruction at `memory[pc]`, increments `pc`, then
executes the fetched instruction word. The following table of opcodes (field `op` of the instruction word) will use
these conceptual arrays and a C-like syntax to explain the effects of each instruction.

```
Opcode  Effect
0x0     if (regs[src1]) { regs[dst] = pc; pc = regs[src0]; }
0x1     regs[dst] = regs[src0];
0x2     regs[dst] = src1 << 4 | src0;
0x3     regs[dst] = memory[regs[src0]];
0x4     memory[regs[src0]] = regs[src1];
0x5     regs[dst] = regs[src0] + regs[src1];
0x6     regs[dst] = regs[src0] - regs[src1];
0x7     regs[dst] = regs[src0] * regs[src1];
0x8     regs[dst] = regs[src1] ? regs[src0] / regs[src1] : 0xffff;
0x9     regs[dst] = regs[src0] << src1;
0xa     regs[dst] = regs[src0] >> src1;
0xb     regs[dst] = regs[src0] & regs[src1];
0xc     regs[dst] = regs[src0] | regs[src1];
0xd     regs[dst] = ~regs[src0];
0xe     regs[dst] = bus[regs[src0]];
0xf     bus[regs[src0]] = regs[src1];
```

### Serial I/O
Bus address `0x0` supports serial I/O routed through `stdin`/`stdout`. The upper 8 bits are ignored on writes and set to
zero on read. Writing to the address will immediately write the lower byte to `stdout`. Reading from the address will
block until a byte is available on `stdin`, then returns that byte.

### Disk Controllers
Bus addresses `0x1`-`0x3` correspond to the disk0 controller, and `0x4`-`0x6`, to disk1. In order, the three bus
addresses that a single controller covers are the following control registers:
```
Bus Offset  Control Register
+0x0        Command/Size
+0x1        Sector
+0x2        Address
```

Reading from `+0x0` will return the number of 512-byte sectors detected in the disk; this value will be zero if no disk
is present. `+0x1` and `+0x2` are read/write and persist their values. Writing to `+0x0` will not change the stored size
value, but sends a command to the controller. Writing `0x0` to it will trigger a disk read, and `0x1`, a disk write. All
other commands are ignored. Whether read or write, the sector being operated on is the sector numbered by `+0x1`, and
the memory base address, that in `+0x2`. For example, if we wrote `0x100` to bus address `0x3`, `0x2` to `0x2`, and
`0x0` to `0x1`, disk0's controller would then read sector `0x2` into memory starting at address `0x100`. Similarly for
writes.

### Machine Halt
Writing a non-zero value to bus address `0x7` will cause the emulator to immediately exit. The address will always
return zero when read.

### Unassigned Bus Addresses
All other bus addresses are read-only (will remain unchanged by bus writes) and will always return `0x0` if read from.

### Firmware
The lower 40 words of the address space (addresses `0x00`-`0x27`) are read-only (will remain unchanged by store
operations) and contain the emulator firmware. Upon startup, the emulator will read the size field of disk0. If
non-zero, it will read sector `0x0` to address `0x0`, then immediately jump to `0x28`. Otherwise, it enters the
bare-bones "interactive bootstrap assembler," meant to be the simplest possible environment in which an operator could
manually bring up the machine. It will accept machine-code instructions in four-digit lowercase hexadecimal, one word
per line. Entering a final, empty line will cause it to jump to the start of the assembled code. Words are written into
memory starting at address `0x28`. Regardless of how control eventually reached `0x28`, the contents of registers are
not generally predictable, though deterministic. As a simple example, the following sequence input into the interactive
assembler, followed by pressing enter twice to enter a blank line, will immediately exit the emulator:
```
2007
f000
```
