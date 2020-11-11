import ply.yacc as yacc
from .lex import tokens
from .ast import *
from .insns import *


def pick_binop(lhs, opstr, rhs):
	return BINOP_MAP[opstr](lhs, rhs)

def pick_unop(opstr, unop):
	return UNOP_MAP[opstr](unop)

def pick_insn(mnemonic, cc):
	try:
		return INSN_MAP[mnemonic](cc, toggle_flags=False)
	except KeyError:
		# Handle cases like "MOVF", "MOVY", "MOVN"
		if mnemonic.endswith("F"):
			return INSN_MAP[mnemonic[:-1]](cc, toggle_flags=True)
		elif mnemonic.endswith("Y"):
			return INSN_MAP[mnemnoic[:-1]](cc, write_flags=True)
		elif mnemonic.endswith("N"):
			return INSN_MAP[mnemonic[:-1]](cc, write_flags=False)
		else:
			raise

def make_precedence():
	def str_assoc(assoc):
		if assoc < 0:
			return "left"
		elif assoc > 0:
			return "right"
		else:
			return "nonassoc"
	
	prec_map = dict()
	for oper in OPERATORS:
		key = (oper.PRECEDENCE, oper.ASSOC)
		if key not in prec_map:
			prec_map[key] = [oper]
		else:
			prec_map[key].append(oper)
	
	result = []
	prec_order = sorted(prec_map.keys())[::-1]
	for prec in prec_order:
		result.append((str_assoc(prec[1]),) + tuple(x.TOKEN for x in prec_map[prec]))
	
	return tuple(result)

precedence = make_precedence()


def error(line, msg):
	errmsg = "Syntax Error on line %d: %s" % (line, msg)
	print(errmsg)
	raise SyntaxError(errmsg)



def p_asmlist(p):
	'''asmlist : asmlist line'''
	p[0] = p[1] + [p[2]]

def p_asmlist_single(p):
	'''asmlist : line'''
	p[0] = [p[1]]

def p_line(p):
	'''line : labeldef
	        | pinsn
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
	p[0] = DotDW(p[2])

def p_dir_db_vallist(p):
	'''directive : DOTDB vallist'''
	p[0] = DotDB(p[2])

def p_dir_db_string(p):
	'''directive : DOTDB STRING'''
	p[0] = DotDB([NumExpr(c) for c in p[2]])

def p_dir_lestring(p):
	'''directive : DOTLESTRING STRING'''
	p[0] = DotLEString(p[2])

def p_dir_loc(p):
	'''directive : DOTLOC constexpr'''
	p[0] = DotLoc(p[2])

def p_dir_loc_with_dpc(p):
	'''directive : DOTLOC constexpr COMMA constexpr'''
	p[0] = DotLoc(p[2], p[4])

def p_dir_segment(p):
	'''directive : DOTSEGMENT labelref'''
	p[0] = DotSegment("@" + p[2].name)

def p_dir_scope(p):
	'''directive : DOTSCOPE'''
	p[0] = DotScope()

def p_dir_export(p):
	'''directive : DOTEXPORT labelref'''
	p[0] = DotExport(p[2])

def p_dir_export_explicit(p):
	'''directive : DOTEXPORT labelref COMMA STRING'''
	p[0] = DotExport(p[2], p[4])

def p_pinsn_insn(p):
	'''pinsn : insn'''
	p[0] = p[1]

def p_labeldef(p):
	'''labeldef : LABEL COLON'''
	p[0] = Label(p[1], None)

# For optional production rules
def p_empty(p):
	'''empty :'''
	pass

# Exclude DPC, as that's not legal in Ry
def p_ry(p):
	'''ry : ZERO
	      | TMP
	      | RV
	      | R3
	      | R4
	      | R5
	      | R6
	      | R7
	      | R8
	      | R9
	      | FP
	      | SP
	      | RA
	      | RD
	      | PC
	'''
	p[0] = RegExpr(p[1])

def p_reg_ry(p):
	'''reg : ry'''
	p[0] = p[1]

def p_reg_dpc(p):
	'''reg : DPC'''
	p[0] = RegExpr(p[1])

def p_constant_number(p):
	'''constant : NUMBER'''
	p[0] = NumExpr(p[1])

def p_labelref(p):
	'''labelref : LABEL'''
	p[0] = LabelExpr(p[1])

def p_constant_labelref(p):
	'''constant : labelref'''
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
	p[0] = pick_binop(*p[1:])

def p_constexpr_unop_pre(p):
	'''constexpr : DASH constexpr %prec UMINUS
	             | TILDE constexpr
	'''
	p[0] = pick_unop(*p[1:])

# Register (excluding DPC) or a constant value
def p_vy(p):
	'''vy : ry
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
	p[0] = pick_insn(p[1], p[2])

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
	        | mn_shl rdxy
	        | mn_sru rdxy
	        | mn_srs rdxy
	        | pmn_adc rdxy
	        | pmn_sbc rdxy
	'''
	p[0] = p[1].set_operands(**p[2])

# MOV Rx, Vy
def p_insn_mov(p):
	'''insn : mn_mov reg COMMA vy'''
	p[0] = p[1].set_operands(Rx=p[2], Vy=p[4])

# CMP Rx, Vy
def p_insn_cmp(p):
	'''insn : mn_cmp reg COMMA vy'''
	p[0] = p[1].set_operands(Rx=p[2], Vy=p[4])

# LDW Rx, [Vy]
# LDB Rx, [Vy]
def p_insn_load(p):
	'''insn : mn_ldw reg COMMA LBRACKET vy RBRACKET
	        | mn_ldb reg COMMA LBRACKET vy RBRACKET
	'''
	p[0] = p[1].set_operands(Rx=p[2], Vy=p[5])

# STW [Rx], Vy
def p_insn_stw(p):
	'''insn : mn_stw LBRACKET reg RBRACKET COMMA vy'''
	p[0] = p[1].set_operands(Rx=p[3], Vy=p[6])

# STB [Rx], V8
def p_insn_stb(p):
	'''insn : mn_stb LBRACKET reg RBRACKET COMMA vy'''
	p[0] = p[1].set_operands(Rx=p[3], V8=p[6])

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
def p_insn_bra_fcar_explicit(p):
	'''insn : mn_bra_fca reg COMMA vy'''
	p[0] = p[1].set_operands(Rx=p[2], Vy=p[4])

# BRA Vy
# FCA Vy
def p_insn_bra_fcar_implied(p):
	'''insn : mn_bra_fca vy'''
	p[0] = p[1].set_operands(Vy=p[2])

# BRR Label
# FCR Label
def p_insn_bra_fcr_label(p):
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
	'''regrange : reg DASH reg'''
	if p[3].num <= p[1].num:
		error(p.lineno(1), "Register range high bound must be greater than low bound.")
	p[0] = set([RegExpr(x) for x in range(p[1].num, p[3].num + 1)])

def p_regrange_single(p):
	'''regrange : reg'''
	p[0] = set([p[1]])

def p_regrangelist_multiple(p):
	'''regrangelist : regrangelist COMMA regrange'''
	p[0] = p[1] | p[3]

def p_regrangelist_single(p):
	'''regrangelist : regrange'''
	p[0] = p[1]

# Example: {R1-R4, R7}
def p_regset(p):
	'''regset : LBRACE regrangelist RBRACE'''
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
	'''insn : pmn_adr reg COMMA labelref'''
	p[0] = p[1].set_operands(Rx=p[2], Label=p[4])

# SWP Ra, Rb
def p_pinsn_swp(p):
	'''pinsn : pmn_swp ry COMMA ry'''
	p[0] = p[1].set_operands(Ra=p[2], Rb=p[4])

# Handle parser errors
def p_error(p):
	if p is not None:
		error(p.lineno, "Unexpected token '%s' [%s]" % (p.value, p.type))
	else:
		raise SyntaxError("Premature end of file")


# Build parser
parser = yacc.yacc(tabmodule="earparsetab")
