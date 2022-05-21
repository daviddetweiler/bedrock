# VM Architecture
- Does it matter? You can always write a translator.
	- And it would be an interesting project to see how changes in architecture affect things
	- But you also want to target this thing manually at the machine code level
- I want the experience of bootstrapping from machine code up to higher-level stuff
- A decent macro assembler is "good enough" for my purposes
- But why a VM? Why not just do that whole process but writing x86 machine code? You will eventually have to compile
  down to it anyways.
  - The advantage of a VM is that I don't have to be beholden to the peculiarities of a specific architecture / platform.
	- Like SEH or the calling convention. Or even the loader format.
	- The advantage of the VM is that I get to define all of that
	- The disadvantage is that I am responsible for defining all of that: what are my design constraints?
	  I have no transistors to be concerned with.

## Design constraints
- The architecture should not be excessively high-level
- The architecture should be simple to execute machine code on
- The architecture should execute machine code quickly

The latter two constraints already discard any high-level interpreted language. Questions of actual machine code format
are subject to different constraints: code size and decoding performance. Those don't seem to matter much, either. The
bytecode can always be translated down to x86 machine code. I don't think you can entirely run from the platform in this
case. Either your fancy bytecode has to be translated down to x86 machine code anyways, or it's being interpreted. VMs
need justifying reasons to exist. The processor is kind of already there. And because of the different design
constraints, VMs often bear little resemblance to the machine architectures they are inspired by.

## Instructions
- 4-bit register IDs
- 4-bit opcodes (so no special registers)
- 16-bit instruction words
- 16-bit addressing (to allow us to comfortably use an arena allocation)

### Instruction word
```
+--------+-------------+----------+----------+
|   4    |      4      |     4    |     4    |
+--------+-------------+----------+----------+
| opcode | destination | source-1 | source-0 |
+--------+-------------+----------+----------+
|   *    |      *      |      immediate      |
+--------+-------------+----------+----------+
```

### Bootstrapping
Forth has many issues, but it is the ideal self-bootstrapping system. In fact: I have a better idea. A machine-code
program that reads in characters on input, assuming they are capital hexadecimal digits. It writes them out as bytes
into memory, then upon encountering an `X`, jumps to the code. Great for self-bootstrapping and incredibly simple.

### Peripherals!
- Should have a terminal
	- Is it functionally a serial line? I.e. single-byte? Yes!
- Basically, we have a COM1 and COM2, COM1 is hard-wired to a terminal
- Should have internal storage
	- Old-school sector addressing
	- Programs too big to fit in RAM shall be handled by overlays
- The two unassigned instructions are for peripheral ops
