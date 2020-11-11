## Installation

`pip install ply`


## Running

`python -m earasm input1.ear [ input2.ear ... ] output.peg`


## Assembler Syntax

Directives:

* `.db 0xFA, 'A', 2 + 2 - 1; "string literal"`
  * Defines one or more literal bytes
* `.dw 0xF00D, @some_label`
  * Defines one or more literal words (16-bit values)
* `.lestring "string literal"`
  * Defines a length-encoded string
* `.export @<symname> [, "symbol name"]`
  * Exports a symbol in the symbol table, optionally with a different symbol name than label name
* `.loc <NEWPC> [, <NEWDPC>]`
  * Redefines the assembler's assumed value of PC and optionally DPC
* `.scope`
  * Switches to a new scope for local labels (labels starting with `@.`)
* `.segment @<segname>`
  * Switches to the named segment

Instruction suffixes:

* `F`: Adds the `TF` (Toggle Flags) instruction prefix (e.g. `MOVF`)
* `Y`: Adds the `TF` instruction prefix if necessary to ensure that flags will be written (e.g. `MOVY.EQ`)
* `N`: Adds the `TF` instruction prefix if necessary to ensure that flags will NOT be writtne (e.g. `INCN`)
* `.<condition code>`: Sets the condition that must be met for the instruction to execute. Default is `AL` (always)

Automatic labels:

* `@`: Address of current position
* `@@`: Address of the start of the current segment
* `@DPC@`: Current DPC value used by the assembler
* `@PC@`: The value that PC will hold when this instruction executes, which points to the next code byte after this instruction
* `@AFTER@`: Address of the byte directly after the end of the current item
  * For instructions, this points to the byte directly after the last code byte in this instruction, which may be different than `@PC@` when DPC != 0.
* `@<segname>@`: Address of the beginning of a segment (e.g. `@TEXT@`)

Segments:

* `@TEXT`: Default segment. Mapped as `EAR_PROT_READ | EAR_PROT_EXECUTE` at virtual address 0x0100.
* `@DATA`: Segment for writeable data. Mapped as `EAR_PROT_READ | EAR_PROT_WRITE` at the next page after `@TEXT`.

Entrypoint:

* `@main`: Name of symbol to export to define the entrypoint.

Comments:

* `/* C style */`
* `// C++ style`
