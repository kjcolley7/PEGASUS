import pytest
from earasm.assembler import *
from earasm.geometry import *

def asm(asmstr, segname="@TEXT"):
	assembler = Assembler()
	assembler.add_input(asmstr)
	segments = assembler.assemble()
	for name, vmaddr, segdata in segments:
		if name == segname:
			return bytes_hex(segdata)
	raise KeyError("Segment not found: %r" % segname)

def test_lestring():
	assert pack_lestring(b"test") == b"\xf4\xe5\xf3t"

def test_nop():
	assert asm("NOP") == "ff"

def test_nop_al():
	assert asm("NOP.AL") == "ff"

def test_nop_eq():
	assert asm("NOP.EQ") == "1f"

def test_nop_ng():
	assert asm("NOP.NG") == "c01f"

def test_add():
	assert asm("ADD R3, R4") == "e034"

def test_add_imm_dec():
	assert asm("ADD R3, 1025") == "e03f0104"

def test_add_imm_hex():
	assert asm("ADD R3, 0x1234") == "e03f3412"

def test_add_imm_oct():
	assert asm("ADD R3, 0o755") == "e03fed01"

def test_add_imm_bin():
	assert asm("ADD R3, 0b1011010011110001") == "e03ff1b4"

def test_add_rd():
	assert asm("ADD R8, R3, R4") == "d8e034"

def test_add_imm_negative():
	assert asm("ADD R3, -10") == "e03ff6ff"

def test_add_regalias():
	assert asm("ADD TMP, RV, FP") == "d1e02a"

def test_add_simm4():
	assert asm("ADD R3, 1") == asm("INC R3")

def test_add_simm4_negative():
	assert asm("ADD R3, -2") == asm("DEC R3, 2")

def test_mov_illegal():
	with pytest.raises(SyntaxError):
		asm("MOV R3, R4, R5")

def test_missing_operand():
	with pytest.raises(SyntaxError):
		asm("MOV R3")

def test_xor():
	assert asm("XOR R3, R4") == "e634"

def test_xor_cc():
	assert asm("XOR.GE R3, R4") == "a634"

def test_xor_tf():
	assert asm("XORF R3, R4") == "c1e634"

def test_xor_tf_cc():
	assert asm("XORF.GE R3, R4") == "c1a634"

def test_ldw():
	assert asm("LDW R4, [R5]") == "f045"

def test_ldw_imm():
	assert asm("LDW R4, [0xabcd]") == "f04fcdab"

def test_stw():
	assert asm("STW [R4], R5") == "f145"

def test_stb():
	assert asm("STB [R4], R5") == "f345"

def test_stb_imm():
	assert asm("STB [R4], 0x42") == "f34f42"

def test_bra():
	assert asm("BRA RD, RA") == "f4dc"

def test_brr():
	assert asm("BRR @") == "f5fdff"

def test_brr_label():
	assert asm("@here: BRR @here") == "f5fdff"

def test_brr_local_label():
	assert asm("@.1: BRR @.1") == "f5fdff"

def test_rdb():
	assert asm("RDB R3, (6)") == "f836"

def test_wrb():
	assert asm("WRB (13), R9") == "f9d9"

def test_psh():
	assert asm("PSH {RV-R4, R6, R8-FP, RA, RD}") == "fa5c37"

def test_pop():
	assert asm("POP {RV-R4, R6, R8-FP, PC, DPC}") == "fb5cc7"

def test_inc():
	assert asm("INC R4, 8") == "fc47"

def test_inc_neg():
	assert asm("INC R4, -8") == "fc48"

def test_bpt():
	assert asm("BPT") == "fd"

def test_hlt():
	assert asm("HLT") == "fe"

def test_ret():
	assert asm("RET") == asm("BRA RD, RA")

def test_inc_zero():
	assert asm("INC R4, 0") == asm("NOP\nNOP")

def test_rdb_implied():
	assert asm("RDB R4") == asm("RDB R4, (0)")

def test_wrb_implied():
	assert asm("WRB R4") == asm("WRB (0), R4")

def test_wrb_implied_imm():
	assert asm("WRB 0x0a") == asm("WRB (0), 0x0a")

def test_inc_implied():
	assert asm("INC R4") == asm("INC R4, 1")

def test_dec():
	assert asm("DEC R4, 5") == asm("INC R4, -5")

def test_dec_implied():
	assert asm("DEC R4") == asm("DEC R4, 1")

def test_neg():
	assert asm("NEG R4") == asm("SUB R4, ZERO, R4")

def test_inv():
	assert asm("INV R4") == asm("XOR R4, -1")

def test_adr():
	assert asm("@here: ADR R4, @here") == asm("@here: ADD R4, PC, @here - @PC@")

def test_bra():
	assert asm("BRA R4") == asm("BRA DPC, R4")

def test_fcr():
	assert asm("FCR @") == "f7fdff"

def test_swp():
	assert asm("SWP R4, R5") == asm("XOR R4, R5\nXOR R5, R4\nXOR R4, R5")

def test_swp_tf():
	assert asm("SWPF R4, R5") == asm("XOR R4, R5\nXOR R5, R4\nXORF R4, R5")

def test_swp_cc():
	assert asm("SWP.GE R4, R5") == asm("XOR.GE R4, R5\nXOR.GE R5, R4\nXOR.GE R4, R5")

def test_swp_tf_cc():
	assert asm("SWPF.GE R4, R5") == asm("XOR.GE R4, R5\nXOR.GE R5, R4\nXORF.GE R4, R5")

def test_adc_xy():
	assert asm("ADC R4, R5") == asm("INC.CS R4\nADD R4, R5")

def test_adc_dxy():
	assert asm("ADC R4, R5, 6") == asm("INC.CS R4, ZERO, 1\nADD R4, R5\nADD R4, 6")

def test_adc_cc_xy():
	assert asm("ADC.EQ R4, R5") == asm(
"""
	BRR.NE @.after
	
	ADC R4, R5
	
@.after:
"""
	)

def test_adc_cc_dxy():
	assert asm("ADC.EQ R4, R5, R6") == asm(
"""
	BRR.NE @.after
	
	ADC R4, R5, R6
	
@.after:
"""
	)

def test_sbc_xy():
	assert asm("SBC R4, R5") == asm("DEC.CS R4\nSUB R4, R5")

def test_sbc_cc_dxy():
	assert asm("SBC.EQ R4, R5, R6") == asm(
"""
	BRR.NE @.after
	
	DEC.CS R4, ZERO, 1
	ADD R4, R5
	SUB R4, R6
	
@.after:
"""
	)

def test_redefine_label():
	with pytest.raises(NameError):
		asm("""
		@.label:
			NOP
		@.label:
			NOP
		""")

def test_scope():
	asm("""
	@.label:
		NOP
	.scope
	@.label:
		NOP
	""")

def test_dotdb():
	assert asm(".db 0x42") == "42"

def test_dotdb_multiple():
	assert asm(".db 0x42, 0xca, 0xfe, 0xba, 0xbe") == "42cafebabe"

def test_dotdb_string():
	assert asm('.db "hello"') == bytes_hex(b"hello")

def test_dotdb_char():
	assert asm(".db 'A' + 1") == bytes_hex(b"B")

def test_dotdb_char_single_quote():
	assert asm(".db '\\''") == bytes_hex(b"'")

def test_dotdb_char_newline():
	assert asm(".db '\\n'") == bytes_hex(b"\n")

def test_dotdw():
	assert asm(".dw -2") == "feff"

def test_dotloc():
	assert asm(".loc 0x1200\n@farlabel:.loc @@\n.dw @farlabel") == "0012"

def test_dotloc_dpc():
	assert asm('.loc @, 1\n.db "hello"') == "680065006c006c006f"

def test_segment():
	assert asm("""
		NOP
	.segment @DATA
		.db "hel"
	.segment @TEXT
		NOP
	.segment @DATA
		.db "lo"
	.segment @TEXT
	""", "@DATA") == bytes_hex(b"hello")

def test_constexpr_1():
	assert asm(".dw 3+4+5") == h16(3+4+5)

def test_constexpr_2():
	with pytest.raises(ZeroDivisionError):
		asm(".dw 1/0")

def test_constexpr_3():
	assert asm(".dw 5/2") == h16(5//2)

def test_constexpr_4():
	assert asm("""
	@start:
		NOP
		MOV R4, R5
	@end:
	.segment @DATA
		.dw @end - @start
	""", "@DATA") == h16(3)

def test_segment_labels():
	expected = asm("ADD R4, PC, 0x200-0x105")
	actual = asm("""
		ADR R4, @hello
	.segment @DATA
	@hello:
		.lestring "hello"
	""")
	assert actual == expected

def test_dpc_reset():
	assembler = Assembler()
	assembler.add_input(".loc @, 1\nADD RV, R3, R4\nRET")
	assembler.add_input("NOP\nNOP")
	segments = assembler.assemble()
	for name, vmaddr, segdata in segments:
		if name == "@TEXT":
			data = bytes_hex(segdata)
	assert data == "d200e0003400f400dcffff"
