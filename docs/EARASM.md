# EAR Assembler Reference

## Installation

`pip install ply`


## Running

`python -m earasm [--rom] input1.ear [ input2.ear ... ] output.peg`

## Comments

The assembler supports `/* C-style comments */` and `// C++-style comments`.


## Assembler Directives

* `.lestring "Hello, world!"`: Defines a length-encoded string literal at the current position
* `.db 0x30`: Defines a byte at the current position
* `.db 0x30, 0x31, 0x32`: Defines a sequence of bytes at the current position
* `.db "Hello, world!\0"`: Defines a sequence of bytes at the current position
* `.dw 0x1234`: Defines a 16-bit word at the current position
* `.dw 0x1234, 0xABCD`: Defines a sequence of words at the current position
* `.loc 0x1000`: Sets the assembler's output location
* `.loc 0x1000, 2`: Sets the assemblers output location and `DPC`-compatible stride
* `.align 2`: Moves the assembler position forward until it is aligned according to the alignment value
* `.segment @TEXT`, `.segment @DATA`: Switches to the named segment (must be defined in `LAYOUT`)
* `.scope`: Switches to a new scope for local labels (labels starting with `@.`)
* `.export @symbol`: Marks the named symbol as exported, so it will be in the PEGASUS file's symbol table
* `.export @symbol, "otherName"`: Export the named symbol using a different name in the symbol table


## Labels

There are a few types of labels understood by the EAR assembler:

* Global labels: Labels like `@main` or `@foo` which have normal names and are visible from all scopes
* Local labels: Labels like `@.foo` or `@.loop_end` which are only visible between `.scope` directives
* Automatic labels: Labels like `@`, `@PC`, or `@DATA@` which are provided automatically by the assembler


## Automatic Labels

| Label          | Description
|----------------|-------------
| `@`            | Denotes the current address in the assembler, where the next byte will be placed
| `@DPC@`        | The current value of `DPC` (set by the most recent `.loc` directive)
| `@PC@`         | The value that the `PC` register will hold when this instruction is executing
| `@AFTER@`      | The address of the byte directly after the last byte of the current item/instruction, which may be different from `@PC@` if `DPC` != 0
| `@@`           | Address of the beginning of the current segment
| `@END@`        | Address of the end of the current segment
| `@<seg>@`      | Address of the beginning of a specific segment, e.g. `@TEXT@`
| `@<seg>.END@`  | Address of the end of a specific segment, e.g. `@DATA.END@`
| `<label>.DPC@` | DPC value when the label was defined, e.g. `@foo.DPC@`


## Layout (Segments and Entrypoint)

The built-in layout for the assembler defines two segments:

* `@TEXT`: Default segment. Mapped as `R-X` at virtual address `0x0100`.
* `@DATA`: Segment for writeable data. Mapped as `RW-` at the next page after `@TEXT`.

Also, if there is a global label named `@main`, it is used as the entrypoint.

If the `--rom` argument is passed to the assembler, then a special layout is used:

* `@ROM`: Default segment, starting at 0x0000, for ROM code & data (effectively `R-X`).
* `@ROMDATA`: Segment for read-only data baked into the end of the ROM.
* `@RAM`: Segment for RAM, also starting at 0x0000. This is useful for defining labels, but the contents will be ignored.

In ROM mode, the assembler directly outputs the contents of the ROM segment as the output
file rather than producing a PEGASUS file.

## Numeric Values

In most places where a literal integer value is expected, the following are supported:

* Decimal literals like `42`
* Hexadecimal literals like `0xf00d`
* Octal literals like `0o755`
* Binary literals like `0b11001101`
* Character literals like `'A'`
* Label references like `@PC@`, `@`, or `@.menu_string`
* Equate references like `$PAGE_SIZE`
* Expressions like `@PC@ + 4`, `@data_end - @data_start`, or `@fault_handler >> 8`.

Expressions support order of operations and parentheses. The following operations are supported for
constant expressions:

* Binary operators: `+`, `-`, `*`, `/`, `%`, `<<`, `>>`, `&`, `^`, `|`
* Unary operators: `-`, `~`


## Defining Labels

Labels store the address from where they were defined and can refer to either code or data.
Their names must begin with an at symbol (`@`). Labels starting with `@.` are considered
"local" labels, and they're only visible within the same scope (as defined by the `.scope`
directive). Labels are defined using the label name followed by a colon.

Examples:

```armasm
@main:
	HLT

@.some_string:
.lestring "Hello!\n"
```


## Defining Equates

Equates are assembler constants, and their names must begin with a dollar sign (`$`). Like
labels, you can define "local" equates by starting their name with `$.`. These local equates
are only available within the scope they're defined in (see the `.scope` directive).
Equates are defined using the equate name followed by the assignment operator (`:=`) and then
an expression to compute the value. This expression may reference other equates or labels.

Examples:

```armasm
$PAGE_SHIFT := 8
$PAGE_SIZE := 1 << $PAGE_SHIFT
$ROM_SIZE := @ROM.END@ - @ROM@
$ROM_PAGES := ($ROM_SIZE + $PAGE_SIZE - 1) >> $PAGE_SHIFT
```


## Register Aliases


The 16 general purpose registers can be used by their register number (like `R12`) or by
their aliases (like `ZERO`, `A2`, or `DPC`).


## Instruction Syntax

Instructions are written starting with the mnemonic (like `ADD` or `PSH`). Optionally, there may be
one of `F`, `Y` or `N` directly following (like `ADDF` or `SUBN`). This character controls whether
the `TF` (Toggle Flags) instruction prefix will be emitted. The `F` character tells the assembler to
emit the `TF` prefix for this instruction, while `Y` and `N` tell the assembler to add the `TF`
instruction prefix only if needed to ensure that flags WILL be updated (for `Y`) or WILL NOT be
updated (for `N`). Concretely, this means that `ADDY R3, R4` will be the same as `ADD R3, R4`, but
`ADDY.NE R3, R4` will be the same as `ADDF.NE R3, R4`.

After the mnemonic and optional flag control character, there may optionally be a dot followed by a
condition code. For example, `BRR.EQ` or `HLT.GE`. There are 15 possible conditions, as mentioned
in the EAR_EAR document. When there is no dot and condition code, `.AL` is implied. Therefore,
`INC A0` is the same as `INC.AL A0`.

After the optional condition code comes the sequence of operands. The exact operands depends on the
specific instruction. Normal instructions like `ADD` can be written in a bunch of ways, resulting in
different encodings:

* `ADD R2, R3` -> 2 bytes `(ADD.AL) (R2:R3)`
* `ADD R1, R2, R3` -> 3 bytes `(DR=R1) (ADD.AL) (R2:R3)`
* `ADD R2, 0xf00d` -> 4 bytes `(ADD.AL) (R2:Imm16) (0x0d) (0xf0)`
* `ADD A0, R2, 0xf00d - 1` -> 5 bytes `(DR=R1) (ADD.AL) (R2:Imm16) (0x0c) (0xf0)`
* `ADDN R2, R3` -> 3 bytes `(TF) (ADD.AL) (R2:R3)`
* `ADD.NG R2, R3` -> 3 bytes `(XC) (ADD.NG) (R2:R3)`
* `ADDY.NG R1, R2, 0xf00d` -> 7 bytes `(TF) (DR=R1) (XC) (ADD.NG) (R1:R2) (0x0d) (0xf0)`

Other instructions with special syntax:

* `STB [A0], '?'` -> 3 bytes `(STB.AL) (R1:Imm8) (0x3f)`
* `BRR @PC@` -> 3 bytes `(BRR.AL) (0x00) (0x00)`
* `BRR @foo` -> 3 bytes `(BRR.AL) (0xCD) (0xAB)`, where 0xABCD is calculated by the assembler
* `FCR` is the same as `BRR` from the assembler's POV, just with a different opcode byte
* `RDB R2` -> 2 bytes `(RDB.AL) (R2:0)`
* `RDB R2, (4)` -> 2 bytes `(RDB.AL) (R2:4)`
* `WRB R2` -> 2 bytes `(WRB.AL) (0:R2)`
* `WRB (4), R2` -> 2 bytes `(WRB.AL) (4:R2)`
* `WRB '\n'` -> 3 bytes `(WRB.AL) (0:Imm8) (0x0a)`
* `PSH {R1}` -> 3 bytes `(PSH.AL) (0x02) (0x00)`
* `PSH S0, {R1}` -> 4 bytes `(DR=R7) (PSH.AL) (0x02) (0x00)`
* `PSH {S0-S2}` -> 3 bytes `(PSH.AL) (0x00) (0x07)`
* `PSH {S0, S1, S2}` is equivalent to `PSH {S0-S2}`
* `PSH {R1, R8-R15}` -> 3 bytes `(PSH.AL) (0x02) (0xff)`
* `POP` is the same as `PSH` from the assembler's POV, just with a different opcode byte
* `INC R2` -> 2 bytes `(INC.AL) (R2:1)`
* `INC R2, 4` -> 2 bytes `(INC.AL) (R2:4)`


## Pseudo-instructions

| Assembler Syntax        | Bytes | Real Instruction Sequence
|-------------------------|-------|---------------------------
| `RET`                   | 2     | `BRA RD, RA`
| `INC Ra, 0`             | 1     | `NOP`
| `RDB Ra`                | 2     | `RDB Ra, (0)`
| `WRB V8`                | 2-3   | `WRB (0), V8`
| `INC Ra`                | 2     | `INC Ra, 1`
| `DEC Ra`                | 2     | `INC Ra, -1`
| `DEC Ra, imm4`          | 2     | `INC Ra, -imm4`
| `ADD Ra, Rb, imm4`      | 3     | `INC Ra, Rb, imm4`
| `SUB Ra, Rb, imm4`      | 3     | `DEC Ra, Rb, imm4`
| `NEG Ra`                | 3     | `SUB Ra, ZERO, Ra`
| `INV Ra`                | 4     | `XOR Ra, -1`
| `ADR Ra, <label>`       | 3-6   | `ADD Ra, PC, label-@`
| `BRA Va`                | 2-5   | `BRA DPC, Va`
| `FCA Va`                | 2-5   | `FCA DPC, Va`
| `SWP Ra, Rb`            | 6     | `XOR Ra, Rb; XOR Rb, Ra; XOR Ra, Rb`
| `ADC Rd, Vb`            | 4-6   | `INC.CS Rd; ADD Rd, Vb`
| `ADC Rd, Ra, Vb`        | 6-8   | `INC.CS Rd, ZERO, 1; ADD Rd, Ra; ADD Rd, Vb`
| `SBC Rd, Vb`            | 4-6   | `DEC.CS Rd; SUB Rd, Vb`
| `SBC Rd, Ra, Vb`        | 6-8   | `DEC.CS Rd, ZERO, 1; ADD Rd, Ra; SUB Rd, Vb`


## Conventions

The preferred coding convention for EAR assembly is as follows:

### 1. Indentation

Label definitions should be unindented. They should be on their own line, except when
defining data using `.db`/`.dw`/`.lestring`, in which case it's allowed (but not required)
to put the label on the same line as the directive and data. Instructions and directives
which define data should be indented with a single tab character (because tabs are superior to spaces).

Good (easy to see labels, consistent):

```armasm
@foo:
	RET

@some_text:
	.lestring "Hello!"

@the_answer: .db 42
```

Bad (hard to find labels, inconsistent):

```armasm
	@foo:
RET

	@some_text:
	.lestring "Hello!"
	@the_answer:
.db 42
```


### 2. Spaces for alignment

When writing an instruction, it should begin after a single tab for indentation. Instruction names
may be up to 7 characters in length (e.g. `ADDY.EQ`). For shorter instruction mnemonics, spaces
should be used between the instruction mnemonic and its first operand, such that there are 8 total
characters between the tab character and the first character of the operand.

Good (easy to read):

```armasm
@func:
	PSH     {RA, RD}
	FCR     @func2
	CMP     A0, A1
	ADDY.EQ A0, A2, A3
	MOV.CS  A0, A1
	POP     {PC, DPC}

@func2:
	SUB     A1, A0
	ADD     A0, A1, A2
	RET
```

Bad (hard to read):

```armasm
@func:
	PSH {RA, RD}
	FCR @func2
	CMP A0, A1
	ADDY.EQ A0, A2, A3
	MOV.CS A0, A1
	POP {PC, DPC}

@func2:
	SUB A1, A0
	ADD A0, A1, A2
	RET
```


### 3. Stack frames

A leaf function (one that doesn't call other functions) should push any callee-saved registers to
its stack in its prologue and pop them in its epilogue. It should return using the `RET`
pseudo-instruction.

A non-leaf function (one that calls at least one other function) should push `FP`, `RA`, `RD`, and any
callee-saved registers to its stack in its prologue. It should then set `FP` to point to the address of
the saved `FP` on the stack (so the debugger can do backtracing). In the function's epilogue, it should
pop `FP`, `PC`, `DPC`, and the other saved registers. It should NOT use the `RET` pseudo-instruction,
as the `POP` instruction is able to effectively perform the return by popping the saved `RA` and `RD`
values directly into `PC` and `DPC`.

Good:

```armasm
.scope
@triple: //LEAF
	MOV     A1, A0
	ADD     A0, A0
	ADD     A0, A1
	RET

.scope
@swap: //LEAF
	PSH     {S0-S1}
	LDW     S0, [A0]
	LDW     S1, [A1]
	STW     [A0], S1
	STW     [A1], S0
	POP     {S0-S1}
	RET

.scope
$.FPOFF := 2 //S0
@func:
	// Prologue
	PSH     {S0, FP, RA, RD}
	INC     FP, SP, $.FPOFF
	
	// Body
	MOV     S0, A0
	MOV     A0, A1
	FCR     @triple
	
	// Epilogue
	.assert $.FPOFF == 2 //S0
	DEC     SP, FP, $.FPOFF
	POP     {S0, FP, PC, DPC}
```
