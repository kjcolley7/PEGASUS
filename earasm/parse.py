import sys
import ply.yacc as yacc
from .lex import tokens
from .ast import *
from .insns import *

from typing import Optional


def pick_binop(
		lhs: Expr, opstr: str, rhs: Expr,
		loc: Optional[Location] = None
) -> BinExpr:
	return BINOP_MAP[opstr](lhs, rhs, loc=loc)

def pick_unop(
		opstr: str, unop: Expr,
		loc: Optional[Location] = None
) -> UnExpr:
	return UNOP_MAP[opstr](unop, loc=loc)

def pick_insn(
		mnemonic: str, cc: str,
		loc: Optional[Location] = None
) -> Instruction:
	tf = False
	wf = None
	if len(mnemonic) == 4:
		# Handle cases like "MOVF", "MOVY", "MOVN"
		if mnemonic.endswith("F"):
			tf = True
		elif mnemonic.endswith("Y"):
			wf = True
		elif mnemonic.endswith("N"):
			wf = False
		else:
			raise
		mnemonic = mnemonic[:-1]
	
	return INSN_MAP[mnemonic](cc, toggle_flags=tf, write_flags=wf, loc=loc)

def make_precedence() -> tuple[tuple]:
	def str_assoc(assoc: int) -> str:
		if assoc < 0:
			return "left"
		elif assoc > 0:
			return "right"
		else:
			return "nonassoc"
	
	prec_map: dict[tuple[int, int], list[ExprOp]] = {}
	for oper in OPERATORS:
		key = (oper.PRECEDENCE, oper.ASSOC)
		if key not in prec_map:
			prec_map[key] = [oper]
		else:
			prec_map[key].append(oper)
	
	result: list[tuple] = []
	prec_order = sorted(prec_map.keys())[::-1]
	for prec in prec_order:
		result.append((str_assoc(prec[1]),) + tuple(x.TOKEN for x in prec_map[prec]))
	
	return tuple(result)

precedence: tuple[tuple] = make_precedence()


def build_loc(
		filename: str,
		filedata: str,
		lexpos: int,
		lineno: int
) -> Location:
	line_start = filedata.rfind('\n', 0, lexpos) + 1
	line_end = filedata.find('\n', line_start)
	col = lexpos - line_start + 1
	line = filedata[line_start:line_end]
	return Location(filename, lineno, col, line)

def token_location(t) -> Location:
	return build_loc(
		filename=t.lexer.filename,
		filedata=t.lexer.lexdata,
		lexpos=t.lexpos,
		lineno=t.lineno
	)

def parser_location(p, i: int) -> Location:
	return build_loc(
		filename=p.lexer.filename,
		filedata=p.lexer.lexdata,
		lexpos=p.lexpos(i),
		lineno=p.lineno(i)
	)

def error(t, msg: str):
	sys.stderr.write(f"Syntax error: {msg}\n")
	token_location(t).show(sys.stderr)
	
	t.lexer.had_error = True



def p_asmlist(p):
	'''asmlist : asmlist line'''
	p[0] = p[1] + [p[2]]

def p_asmlist_single(p):
	'''asmlist : line'''
	p[0] = [p[1]]

def p_line(p):
	'''line : labeldef
	        | equatedef
	        | insn
	        | directive
	'''
	p[0] = p[1]

def p_vallist_single(p):
	'''vallist : constexpr'''
	p[0] = [p[1]]

def p_vallist_multiple(p):
	'''vallist : vallist COMMA constexpr'''
	p[0] = p[1] + [p[3]]

def p_dir_dw(p):
	'''directive : DOTDW vallist'''
	p[0] = DotDW(p[2], loc=parser_location(p, 1))

def p_dir_db_vallist(p):
	'''directive : DOTDB vallist'''
	p[0] = DotDB(p[2], loc=parser_location(p, 1))

def p_dir_db_string(p):
	'''directive : DOTDB stringexpr'''
	p[0] = DotDB([NumExpr(c) for c in p[2]], loc=parser_location(p, 1))

def p_dir_lestring(p):
	'''directive : DOTLESTRING stringexpr'''
	p[0] = DotLEString(p[2], loc=parser_location(p, 1))

def p_stringexpr_single(p):
	'''stringexpr : STRING'''
	p[0] = p[1]

def p_stringexpr_multiple(p):
	'''stringexpr : stringexpr PLUS STRING'''
	p[0] = p[1] + p[3]

def p_dir_loc(p):
	'''directive : DOTLOC constexpr'''
	p[0] = DotLoc(p[2], loc=parser_location(p, 1))

def p_dir_align(p):
	'''directive : DOTALIGN constexpr'''
	p[0] = DotAlign(p[2], loc=parser_location(p, 1))

def p_dir_loc_with_dpc(p):
	'''directive : DOTLOC constexpr COMMA constexpr'''
	p[0] = DotLoc(p[2], p[4], loc=parser_location(p, 1))

def p_dir_segment(p):
	'''directive : DOTSEGMENT labelref'''
	p[0] = DotSegment(p[2].name, loc=parser_location(p, 1))

def p_dir_scope(p):
	'''directive : DOTSCOPE'''
	p[0] = DotScope(loc=parser_location(p, 1))

def p_dir_export(p):
	'''directive : DOTEXPORT labelref'''
	p[0] = DotExport(p[2], loc=parser_location(p, 1))

def p_dir_export_explicit(p):
	'''directive : DOTEXPORT labelref COMMA stringexpr'''
	p[0] = DotExport(p[2], p[4], loc=parser_location(p, 1))

def p_dir_import(p):
	'''directive : DOTIMPORT stringexpr'''
	p[0] = DotImport(p[2], loc=parser_location(p, 1))

def p_cmpop(p):
	'''cmpop : EQEQ
	         | BANGEQ
	         | LESS
	         | LESSEQ
	         | GREATER
	         | GREATEREQ
	'''
	p[0] = p[1]

def p_dir_assert(p):
	'''directive : DOTASSERT constexpr cmpop constexpr'''
	p[0] = DotAssert(p[2], p[3], p[4], loc=parser_location(p, 1))

def p_labeldef(p):
	'''labeldef : LABEL COLON'''
	p[0] = Label(p[1], loc=parser_location(p, 1))

def p_equatedef(p):
	'''equatedef : EQUATE ASSIGN constexpr'''
	p[0] = Equate(p[1], p[3], loc=parser_location(p, 1))

# For optional production rules
def p_empty(p):
	'''empty :'''
	pass

def p_ry(p):
	'''ry : ry_normal
	      | reg_cross
	'''
	p[0] = p[1]

# Exclude DPC, as that's not legal in Ry
def p_ry_normal(p):
	'''ry_normal : ZERO
	             | A0
	             | A1
	             | A2
	             | A3
	             | A4
	             | A5
	             | S0
	             | S1
	             | S2
	             | FP
	             | SP
	             | RA
	             | RD
	             | PC
	'''
	p[0] = RegExpr(p[1], loc=parser_location(p, 1))

def p_reg(p):
	'''reg : reg_normal
	       | reg_cross
	'''
	p[0] = p[1]

def p_reg_ry(p):
	'''reg_normal : ry_normal'''
	p[0] = p[1]

def p_reg_dpc(p):
	'''reg_normal : DPC'''
	p[0] = RegExpr(p[1], loc=parser_location(p, 1))

def p_reg_cross(p):
	'''reg_cross : BANG reg_normal'''
	p[0] = p[2].cross()

def p_creg(p):
	'''creg : creg_normal
	        | creg_cross
	'''
	p[0] = p[1]

def p_creg_normal(p):
	'''creg_normal : CR0
	               | CR1
	               | CR2
	               | CR3
	               | CR4
	               | CR5
	               | CR6
	               | CR7
	               | CR8
	               | CR9
	               | CR10
	               | CR11
	               | CR12
	               | CR13
	               | CR14
	               | CR15
	'''
	p[0] = CRegExpr(p[1], loc=parser_location(p, 1))

def p_creg_cross(p):
	'''creg_cross : BANG creg_normal'''
	p[0] = p[2].cross()

def p_constant_number(p):
	'''constant : NUMBER'''
	p[0] = NumExpr(p[1], loc=parser_location(p, 1))

def p_symbolref(p):
	'''symbolref : labelref
	             | equateref
	'''
	p[0] = p[1]

def p_labelref(p):
	'''labelref : LABEL'''
	p[0] = SymbolExpr(p[1], loc=parser_location(p, 1))

def p_equateref(p):
	'''equateref : EQUATE'''
	p[0] = SymbolExpr(p[1], loc=parser_location(p, 1))

def p_constant_symbolref(p):
	'''constant : symbolref'''
	p[0] = p[1]

def p_constexpr_single(p):
	'''constexpr : constant'''
	p[0] = p[1]

def p_constexpr_paren(p):
	'''constexpr : LPAREN constexpr RPAREN'''
	p[0] = p[2]

def p_constexpr_binop(p):
	'''constexpr : constexpr PLUS constexpr
	             | constexpr DASH constexpr
	             | constexpr STAR constexpr
	             | constexpr SLASH constexpr
	             | constexpr PERCENT constexpr
	             | constexpr LSHIFT constexpr
	             | constexpr RSHIFT constexpr
	             | constexpr AMPERSAND constexpr
	             | constexpr CARAT constexpr
	             | constexpr PIPE constexpr
	'''
	p[0] = pick_binop(*p[1:], loc=parser_location(p, 2))

def p_constexpr_unop_pre(p):
	'''constexpr : DASH constexpr %prec UMINUS
	             | TILDE constexpr
	'''
	p[0] = pick_unop(*p[1:], loc=parser_location(p, 1))

# Register (excluding DPC) or a constant value
def p_vy(p):
	'''vy : vy_normal
	      | reg_cross
	'''
	p[0] = p[1]

def p_vy_normal(p):
	'''vy_normal : ry_normal
	             | constexpr
	'''
	p[0] = p[1]

# Rx, Vy
def p_rdxy_rxy(p):
	'''rdxy : reg COMMA vy'''
	p[0] = dict(Rx=p[1], Vy=p[3])

# Rd, Rx, Vy
def p_rdxy_rdxy(p):
	'''rdxy : reg COMMA reg COMMA vy'''
	p[0] = dict(Rd=p[1], Rx=p[3], Vy=p[5])

# Rdx:Rd, Rx, Vy
def p_rdxy_rddxy(p):
	'''rdxy : reg COLON reg COMMA reg COMMA vy'''
	if p[3].is_cross and not p[1].is_cross:
		error(p[2], "Rdx and Rx can only both be cross or both be normal")
		raise SyntaxError
	if p[1].is_cross:
		p[3].cross()
	p[0] = dict(Rdx=p[1], Rd=p[3], Rx=p[5], Vy=p[7])

# Condition code
def p_cond(p):
	'''cond : EQ
	        | NE
	        | GT
	        | LE
	        | LT
	        | GE
	        | AL
	        | NG
	        | PS
	        | BG
	        | SE
	        | SM
	        | BE
	        | OD
	        | EV
	'''
	p[0] = p[1]

# In "INS.CC", this is the ".CC" part
def p_cc_explicit(p):
	'''cc : DOT cond'''
	p[0] = p[2]

def p_cc_implied(p):
	'''cc : empty'''
	p[0] = "AL"

# An instruction mnemonic with optional condition code after it
def p_mnem(p):
	'''mn_add : ADD cc
	   mn_sub : SUB cc
	   mn_mlu : MLU cc
	   mn_mls : MLS cc
	   mn_dvu : DVU cc
	   mn_dvs : DVS cc
	   mn_xor : XOR cc
	   mn_and : AND cc
	   mn_orr : ORR cc
	   mn_shl : SHL cc
	   mn_sru : SRU cc
	   mn_srs : SRS cc
	   mn_mov : MOV cc
	   mn_cmp : CMP cc
	   mn_rdc : RDC cc
	   mn_wrc : WRC cc
	   mn_ldw : LDW cc
	   mn_stw : STW cc
	   mn_ldb : LDB cc
	   mn_stb : STB cc
	   mn_bra : BRA cc
	   mn_brr : BRR cc
	   mn_fca : FCA cc
	   mn_fcr : FCR cc
	   mn_rdb : RDB cc
	   mn_wrb : WRB cc
	   mn_psh : PSH cc
	   mn_pop : POP cc
	   mn_inc : INC cc
	   mn_bpt : BPT cc
	   mn_hlt : HLT cc
	   mn_nop : NOP cc
	   pmn_ret : RET cc
	   pmn_dec : DEC cc
	   pmn_neg : NEG cc
	   pmn_inv : INV cc
	   pmn_adr : ADR cc
	   pmn_swp : SWP cc
	   pmn_adc : ADC cc
	   pmn_sbc : SBC cc
	'''
	p[0] = pick_insn(p[1], p[2], loc=parser_location(p, 1))

# The standard instruction format: "INS [Rd,] Rx, Vy"
def p_insn_rdxy(p):
	'''insn : mn_add rdxy
	        | mn_sub rdxy
	        | mn_mlu rdxy
	        | mn_mls rdxy
	        | mn_dvu rdxy
	        | mn_dvs rdxy
	        | mn_xor rdxy
	        | mn_and rdxy
	        | mn_orr rdxy
	        | pmn_adc rdxy
	        | pmn_sbc rdxy
	'''
	p[0] = p[1].set_operands(**p[2])

# SHL/SRU/SRS Rx, V8
def p_insn_shifts(p):
	'''insn : mn_shl reg COMMA vy
	        | mn_sru reg COMMA vy
	        | mn_srs reg COMMA vy
	'''
	p[0] = p[1].set_operands(Rx=p[2], V8=p[4])

# SHL/SRU/SRS Rd, Rx, V8
def p_insn_shifts_rd(p):
	'''insn : mn_shl reg COMMA reg COMMA vy
	        | mn_sru reg COMMA reg COMMA vy
	        | mn_srs reg COMMA reg COMMA vy
	'''
	p[0] = p[1].set_operands(Rd=p[2], Rx=p[4], V8=p[6])

# MOV Rx, Vy
def p_insn_mov(p):
	'''insn : mn_mov reg COMMA vy'''
	p[0] = p[1].set_operands(Rx=p[2], Vy=p[4])

# CMP Rx, Vy
def p_insn_cmp(p):
	'''insn : mn_cmp reg COMMA vy'''
	p[0] = p[1].set_operands(Rx=p[2], Vy=p[4])

# RDC Rx, CReg
def p_insn_rdc(p):
	'''insn : mn_rdc reg COMMA creg'''
	p[0] = p[1].set_operands(Rx=p[2], Vy=p[4])

# WRC CReg, Ry
def p_insn_wrc(p):
	'''insn : mn_wrc creg COMMA reg'''
	p[0] = p[1].set_operands(Rx=p[2], Vy=p[4])

# LDW Rx, [Vy]
# LDB Rx, [Vy]
def p_insn_load(p):
	'''insn : mn_ldw reg COMMA LBRACKET vy RBRACKET
	        | mn_ldb reg COMMA LBRACKET vy RBRACKET
	'''
	p[0] = p[1].set_operands(Rx=p[2], Vy=p[5])

# LDW Rx, [Rd + Vy]
# LDB Rx, [Rd + Vy]
def p_insn_load_rd(p):
	'''insn : mn_ldw reg COMMA LBRACKET reg PLUS vy RBRACKET
	        | mn_ldb reg COMMA LBRACKET reg PLUS vy RBRACKET
	'''
	p[0] = p[1].set_operands(Rd=p[5], Rx=p[2], Vy=p[7])

# LDW Rx, [Rd - Expr]
# LDB Rx, [Rd - Expr]
def p_insn_load_rd_minus(p):
	'''insn : mn_ldw reg COMMA LBRACKET reg DASH constexpr RBRACKET
	        | mn_ldb reg COMMA LBRACKET reg DASH constexpr RBRACKET
	'''
	p[0] = p[1].set_operands(Rd=p[5], Rx=p[2], Vy=NegExpr(p[7], loc=parser_location(p, 6)))

# STW [Vy], Rx
# STB [Vy], Rx
def p_insn_store(p):
	'''insn : mn_stw LBRACKET vy RBRACKET COMMA reg
	        | mn_stb LBRACKET vy RBRACKET COMMA reg
	'''
	p[0] = p[1].set_operands(Rx=p[6], Vy=p[3])

# STW [Rd + Vy], Rx
# STB [Rd + Vy], Rx
def p_insn_store_rd(p):
	'''insn : mn_stw LBRACKET reg PLUS vy RBRACKET COMMA reg
	        | mn_stb LBRACKET reg PLUS vy RBRACKET COMMA reg
	'''
	p[0] = p[1].set_operands(Rd=p[3], Rx=p[8], Vy=p[5])

# STW [Rd - Expr], Rx
# STB [Rd - Expr], Rx
def p_insn_store_rd_minus(p):
	'''insn : mn_stw LBRACKET reg DASH constexpr RBRACKET COMMA reg
	        | mn_stb LBRACKET reg DASH constexpr RBRACKET COMMA reg
	'''
	p[0] = p[1].set_operands(Rd=p[3], Rx=p[8], Vy=p[5])

# BRA and FCA both have the same syntax, so group them together
def p_mnem_bra_fca(p):
	'''mn_bra_fca : mn_bra
	              | mn_fca
	'''
	p[0] = p[1]

def p_mnem_brr_fcr(p):
	'''mn_brr_fcr : mn_brr
	              | mn_fcr
	'''
	p[0] = p[1]

# BRA Rx, Vy
# FCA Rx, Vy
def p_insn_bra_fca_explicit(p):
	'''insn : mn_bra_fca reg_normal COMMA vy_normal'''
	p[0] = p[1].set_operands(Rx=p[2], Vy=p[4])

# BRA Vy
# FCA Vy
def p_insn_bra_fcar_implied(p):
	'''insn : mn_bra_fca vy_normal'''
	p[0] = p[1].set_operands(Vy=p[2])

# BRR Label
# FCR Label
def p_insn_brr_fcr_label(p):
	'''insn : mn_brr_fcr labelref'''
	p[0] = p[1].set_operands(Label=p[2])

# RDB Rx, (Port)
def p_insn_rdb(p):
	'''insn : mn_rdb reg COMMA LPAREN constexpr RPAREN'''
	# Port number will be range checked later
	p[0] = p[1].set_operands(Rx=p[2], Port=p[5])

# WRB (Port), V8
def p_insn_wrb(p):
	'''insn : mn_wrb LPAREN constexpr RPAREN COMMA vy'''
	# Port number will be range checked later
	# V8 will be truncated if out of range
	p[0] = p[1].set_operands(Port=p[3], V8=p[6])

def p_regrange_multiple(p):
	'''regrange : reg_normal DASH reg_normal'''
	if p[3].num < p[1].num:
		error(p[2], "Register range high bound can't be lower than low bound.")
		raise SyntaxError
	p[0] = set([RegExpr(x, loc=parser_location(p, 2)) for x in range(p[1].num, p[3].num + 1)])

def p_regrange_single(p):
	'''regrange : reg_normal'''
	p[0] = set([p[1]])

def p_regrangelist_multiple(p):
	'''regrangelist : regrangelist COMMA regrange'''
	p[0] = p[1] | p[3]

def p_regrangelist_single(p):
	'''regrangelist : regrange'''
	p[0] = p[1]

def p_regset(p):
	'''regset : regset_normal
	          | regset_cross
	'''
	p[0] = p[1]

# Example: {R1-R4, R7}
def p_regset_normal(p):
	'''regset_normal : LBRACE regrangelist RBRACE'''
	p[0] = p[2]

# Example: !{R1-R4, R7}
def p_regset_cross(p):
	'''regset_cross : BANG regset_normal'''
	for r in p[2]:
		assert not r.is_cross
		r.cross()
	p[0] = p[2]

# PSH/POP {Regs16}
def p_insn_push_pop(p):
	'''insn : mn_psh regset
	        | mn_pop regset
	'''
	p[0] = p[1].set_operands(Regs16=p[2])

# PSH/POP Rd, {Regs16}
def p_insn_push_pop_rd(p):
	'''insn : mn_psh reg COMMA regset
	        | mn_pop reg COMMA regset
	'''
	p[0] = p[1].set_operands(Rd=p[2], Regs16=p[4])

# INC Rx, SImm4
def p_insn_inc(p):
	'''insn : mn_inc reg COMMA constexpr'''
	p[0] = p[1].set_operands(Rx=p[2], SImm4=p[4])

# BPT/HLT/NOP
def p_insn_bare(p):
	'''insn : mn_bpt
	        | mn_hlt
	        | mn_nop
	'''
	p[0] = p[1]


# Pseudo-instructions

# RET
def p_insn_ret(p):
	'''insn : pmn_ret'''
	p[0] = p[1]

# INC Rx, 0: handled when emitted

# RDB Rx
def p_insn_rdb_implied(p):
	'''insn : mn_rdb reg'''
	p[0] = p[1].set_operands(Rx=p[2])

# WRB V8
def p_insn_wrb_implied(p):
	'''insn : mn_wrb vy'''
	p[0] = p[1].set_operands(V8=p[2])

# INC Rx
def p_insn_inc_implied(p):
	'''insn : mn_inc reg'''
	p[0] = p[1].set_operands(Rx=p[2])

# DEC Rx
def p_insn_dec_implied(p):
	'''insn : pmn_dec reg'''
	p[0] = p[1].set_operands(Rx=p[2])

# DEC Rx, SImm4
def p_insn_dec(p):
	'''insn : pmn_dec reg COMMA constexpr'''
	# SImm4 is range checked later
	p[0] = p[1].set_operands(Rx=p[2], SImm4=p[4])

# INC/DEC Rd, Rx, SImm4
def p_insn_incdec_rd(p):
	'''insn : mn_inc reg COMMA reg COMMA constexpr
	        | pmn_dec reg COMMA reg COMMA constexpr
	'''
	p[0] = p[1].set_operands(Rd=p[2], Rx=p[4], SImm4=p[6])

# ADD/SUB Rd, Rx, SImm4: handled when emitted

# NEG/INV Rx
def p_insn_neg_inv(p):
	'''insn : pmn_neg reg
	        | pmn_inv reg
	'''
	p[0] = p[1].set_operands(Rx=p[2])

# ADR Rx, Label
def p_insn_adr(p):
	'''insn : pmn_adr reg COMMA constexpr'''
	p[0] = p[1].set_operands(Rx=p[2], Label=p[4])

# SWP Ra, Rb
def p_pinsn_swp(p):
	'''insn : pmn_swp ry COMMA ry'''
	p[0] = p[1].set_operands(Ra=p[2], Rb=p[4])

# Handle parser errors
def p_error(p):
	if not p:
		print("Premature end of file")
		raise SyntaxError
	
	error(p, "Unexpected token '%s' [%s]" % (p.value, p.type))


# Build parser
parser = yacc.yacc(tabmodule="earparsetab")
