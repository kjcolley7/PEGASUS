import pytest
import json
import os
from earasm.assembler import *
from earasm.geometry import *
from earasm.layout import DEFAULT_LAYOUT

os.environ["PYTEST"] = "1"

top_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
with open(os.path.join(top_dir, "bootrom", "rom_layout.json"), "r") as fp:
	ROM_LAYOUT = json.load(fp)

def asm(asmstr, segname="@TEXT", layout=None):
	if layout is None:
		assembler = Assembler()
	else:
		assembler = Assembler(layout)
	assembler.add_input(asmstr)
	segments = assembler.assemble()
	for name, _vmaddr, _vmsize, segdata in segments:
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

def test_add_imm_small():
	assert asm("ADD R3, 2") == asm("INC R3, 2")

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
	assert asm("ADD S0, A1, R10") == "d7e02a"

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

def test_cmp():
	assert asm("CMP R3, R4") == "ed34"

def test_cmp_cc():
	assert asm("CMP.GE R3, R4") == "ad34"

def test_cmpy_cc():
	assert asm("CMPY.GE R3, R4") == asm("CMP.GE R3, R4")

def test_cmpn_cc():
	assert asm("CMPN.GE R3, R4") == h8(PREFIX_TF) + asm("CMP.GE R3, R4")

def test_ldw():
	assert asm("LDW R4, [R5]") == "f045"

def test_ldw_imm():
	assert asm("LDW R4, [0xabcd]") == "f04fcdab"

def test_ldw_rd():
	assert asm("LDW R4, [R5 + R6]") == "d5f046"

def test_ldw_rd_imm():
	assert asm("LDW R4, [R5 + 0xabcd]") == "d5f04fcdab"

def test_ldw_rd_imm_sub():
	assert asm("LDW R4, [R5 - 0xabcd]") == "d5f04f3354"

def test_stw():
	assert asm("STW [R4], R5") == "f154"

def test_stb():
	assert asm("STB [R4], R5") == "f354"

def test_stb_imm():
	with pytest.raises(SyntaxError):
			asm("STB [R4], 0x42")

def test_stb_rd():
	assert asm("STB [R4 + R5], R6") == "d4f365"

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
	assert asm("PSH {R2-R4, R6, R8-FP, RA, RD}") == "fa5c37"

def test_pop():
	assert asm("POP {R2-R4, R6, R8-FP, PC, DPC}") == "fb5cc7"

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

def test_add_imm_zero():
	assert asm("ADD R4, 0\n.db 0x11") == "e04f000011"

def test_inc_zero():
	assert asm("INC R4, 0") == asm("ADD R4, ZERO")

def test_rdc():
	assert asm("RDC R4, MEMBASE_R") == "ee48"

def test_wrc():
	assert asm("WRC MEMBASE_R, R4") == "ef84"

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
	assert asm("ADC R4, R5, 6") == asm(
"""
	MOV R4, ZERO
	INC.CS R4
	ADD R4, R5
	ADD R4, 6
"""
	)

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
	
	MOV R4, ZERO
	DEC.CS R4
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

def test_redefine_equate():
	with pytest.raises(NameError):
		asm("""
		$.equ := 1
		$.equ := 2
		""")

def test_scope():
	asm("""
	$.equ := 1
	@.label:
		NOP
	.scope
	$.equ := 2
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
	assert asm(".db 3+4+5") == h8(3+4+5)

def test_constexpr_2():
	with pytest.raises(ZeroDivisionError):
		asm(".dw 1/0")

def test_constexpr_3():
	assert asm(".db 5/2") == h8(5//2)

def test_constexpr_4():
	assert asm("""
	@start:
		NOP
		MOV R4, R5
	@end:
	.segment @DATA
		.db @end - @start
	""", "@DATA") == h8(3)

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
	assembler.add_input(".loc @, 1\nADD R2, R3, R4\nRET")
	assembler.add_input("NOP\nNOP")
	segments = assembler.assemble()
	for name, _vmaddr, _vmsize, segdata in segments:
		if name == "@TEXT":
			data = bytes_hex(segdata)
	assert data == "d200e0003400f400dcffff"

def test_loc_order():
	assert asm("""
	.loc 0
		.dw 0x0011
		.dw 0x2233
	""", "@ROM", ROM_LAYOUT) == h16(0x0011) + h16(0x2233)

def test_equate():
	assert asm("""
	$foo := 0x1234
	$bar := $foo + 3
	$baz := $bar + (@test2 - @test)
	
	@test:
		.dw $foo
	@test2:
		.dw $bar
		.dw $baz
	""") == h16(0x1234) + h16(0x1237) + h16(0x1239)

def test_cond_alias():
	assert asm("NOP.ZR") == asm("NOP.EQ")

def test_label_dpc():
	assert asm("""
	.dw @foo
	.dw @foo.DPC@
	.loc 0x1234, 0x5867
	@foo:
	.loc @ + 123
	""") == h16(0x1234) + h16(0x5867)

def test_equate_forward_reference():
	assert asm("""
	$DIST := @bar - @foo
	.db $DIST
	
	@foo:
	.loc @ + 123
	@bar:
	""") == h8(123)

def test_end_label():
	assert asm("""
	.db @END@ - @@
	""") == h8(1)

def test_seg_end_label():
	assert asm("""
	.db @TEXT.END@ - @TEXT@
	""") == h8(1)

def test_xr_mov():
	assert asm("MOV R1, !R2") == h8(PREFIX_XY) + asm("MOV R1, R2")

def test_xr_add():
	assert asm("ADD R1, !R2, !R3") == h8(PREFIX_XX) + h8(PREFIX_XY) + asm("ADD R1, R2, R3")

def test_xr_rdc():
	assert asm("RDC A0, !MEMBASE_R") == h8(PREFIX_XY) + asm("RDC A0, MEMBASE_R")

def test_xr_ldw():
	assert asm("LDW R1, [!R2]") == h8(PREFIX_XY) + asm("LDW R1, [R2]")

def test_xr_stw():
	assert asm("STW [!R1], !R2") == h8(PREFIX_XX) + h8(PREFIX_XY) + asm("STW [R1], R2")

def test_xw_mov():
	assert asm("MOV !R1, R2") == h8(PREFIX_XX) + asm("MOV R1, R2")

def test_xw_add():
	assert asm("ADD !R1, R2, R3") == h8(PREFIX_XZ) + asm("ADD R1, R2, R3")

def test_xw_wrc():
	assert asm("WRC !MEMBASE_R, A0") == h8(PREFIX_XX) + asm("WRC MEMBASE_R, A0")

def test_xw_ldw():
	assert asm("LDW !R1, [R2]") == h8(PREFIX_XX) + asm("LDW R1, [R2]")

def test_xr_xw_add():
	assert asm("ADD !R1, !R2") == h8(PREFIX_XX) + h8(PREFIX_XY) + asm("ADD R1, R2")

def test_xr_xw_ldw():
	assert asm("LDW !R1, [!R2]") == h8(PREFIX_XX) + h8(PREFIX_XY) + asm("LDW R1, [R2]")

def test_cross_psh():
	assert asm("PSH !{R2-R15}") == h8(PREFIX_XY) + asm("PSH {R2-R15}")

def test_cross_psh_rd():
	assert asm("PSH R1, !{R2-R15}") == h8(PREFIX_XY) + h8(PREFIX_DR(1)) + asm("PSH {R2-R15}")

def test_cross_psh_rd_cross():
	assert asm("PSH !R1, !{R2-R15}") == h8(PREFIX_XY) + h8(PREFIX_XZ) + h8(PREFIX_DR(1)) + asm("PSH {R2-R15}")

def test_cross_pop():
	assert asm("POP !{R2-R15}") == h8(PREFIX_XY) + asm("POP {R2-R15}")

def test_cross_pop_rd():
	assert asm("POP R1, !{R2-R15}") == h8(PREFIX_XY) + h8(PREFIX_DR(1)) + asm("POP {R2-R15}")

def test_cross_pop_rd_cross():
	assert asm("POP !R1, !{R2-R15}") == h8(PREFIX_XY) + h8(PREFIX_XZ) + h8(PREFIX_DR(1)) + asm("POP {R2-R15}")

def test_sparse_locs():
	assert asm("""
	.segment @ROMDATA
	.loc 0x200
	@first_page:
	.loc @ + 0x100
	@first_page_end:
	
	.loc 0xFE00
	@almost_last_page:
	.loc @ + 0x100
	@almost_last_page_end:
	
	.loc @first_page_end
	
	.segment @ROM
		.db 0xAB
	
	.segment @ROMDATA
	@after_seg_switch:
		.db 42
	
	.segment @ROM
		.dw @after_seg_switch
	""", "@ROM", ROM_LAYOUT) == "ab" + h16(0x0300)

def test_assert_1():
	assert asm("""
	.assert 1 == 1
	.assert 1 != 42
	.assert 1 < 42
	.assert 1 <= 1
	.assert 1 <= 42
	.assert 42 > 1
	.assert 1 >= 1
	.assert 42 >= 1
	.db "OK"
	""") == b"OK".hex()

def test_assert_2():
	strs = [
		".assert 1 == 42",
		".assert 1 != 1",
		".assert 1 < 1",
		".assert 42 < 1",
		".assert 42 <= 1",
		".assert 1 > 1",
		".assert 1 > 42",
		".assert 1 >= 42",
	]
	for s in strs:
		with pytest.raises(AssertionError):
			asm(s)

def test_equate_here_label():
	with pytest.raises(AssertionError):
		asm("""
		$start := @
			.db "Hello"
		$end := @
		.assert $end - $start == 5
		""", "@ROM", ROM_LAYOUT)

def test_label_loc():
	actual = asm("""
	@pos1:
		.db "Hello"
	@pos2:
		.db "World"
	@pos3:
		.db "!"
	
	// Alias to pos2
	@.pos:
	.loc @pos2
	@world_str:
	.loc @.pos
	
	@pos4:
		.db "\n"
	
	@world_str_addr:
		.dw @world_str
		.dw 0xABCD
	""", "@ROM", layout=ROM_LAYOUT)
	
	expected = b"HelloWorld!\n".hex() + h16(len("Hello")) + h16(0xABCD)
	assert actual == expected

def test_psh_range_single():
	assert asm("PSH {R2-R2}") == asm("PSH {R2}")

def test_mov_xr_dpc():
	assert asm("MOV R1, !DPC") == h8(PREFIX_XY) + "ec1f"

@pytest.mark.xfail(reason="Label resolution shouldn't be tied to segments")
def test_scope_changed_segment():
	asm("""
	.scope
	@func:
		ADR     A0, @.msg
	.segment @ROMDATA
	@.msg:
		.lestring "TEST!"
	""", "@ROM", ROM_LAYOUT)

def test_shl():
	assert asm("SHL R4, R5") == "e945"

def test_shl_v8():
	assert asm("SHL R4, 8") == "e94f08"

def test_shl_rd():
	assert asm("SHL R4, R5, R6") == "d4e956"

def test_shl_rd_v8():
	assert asm("SHL R4, R5, 8") == "d4e95f08"

def test_mlu_rxy():
	assert asm("MLU R5, R6") == "e256"

def test_mlu_rdxy():
	assert asm("MLU R4, R5, R6") == "d4e256"

def test_mlu_rddxy():
	assert asm("MLU R3:R4, R5, R6") == "d4d3e256"

def test_wrc_xx():
	assert asm("WRC !MEMBASE_R, A0") == h8(PREFIX_XX) + "ef81"
