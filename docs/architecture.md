# VM Architecture
- Does it matter? You can always write a translator.
- Register-oriented!
- Operate within a fixed arena of memory (64MiB sounds sufficient)

## Instruction set
```
hypercall

move
load_immediate
load
store

bitwise_or
bitwise_and
bitwise_not

add
subtract
multiply
divide

shift_left
shift_right

; Still deciding how best to do control flow
jump
jump_and_link

; 16 base instructions
```
