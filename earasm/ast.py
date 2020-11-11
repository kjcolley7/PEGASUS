from .geometry import *
from .isa import *

class Label(object):
	def __init__(self, name, calldpc=None):
		self.name = name
		self.calldpc = calldpc
		self.value = None
	
	def __repr__(self):
		l = repr(self.name)
		if self.calldpc is not None:
			l += ", 0x%X" % self.calldpc
		
		l = "Label(%s)" % l
		if self.value is not None:
			l += " = 0x%X" % self.value
		return l
	
	def __str__(self):
		l = self.name
		if self.calldpc is not None:
			l += "@%d" % self.calldpc
		if self.value is not None:
			l += " = 0x%X" % self.value
		l += ":"
		return l

class Directive(object):
	pass

class DotDW(Directive):
	def __init__(self, vals):
		self.vals = vals
	
	def __repr__(self):
		return "DotDW(%r)" % self.vals
	
	def __len__(self):
		return 2 * len(self.vals)
	
	def assemble(self, context):
		return b"".join(p16(x.value(context)) for x in self.vals)

class DotDB(Directive):
	def __init__(self, vals):
		self.vals = vals
	
	def __repr__(self):
		return "DotDW(%r)" % self.vals
	
	def __len__(self):
		return len(self.vals)
	
	def assemble(self, context):
		return b"".join(p8(x.value(context)) for x in self.vals)

class DotLEString(Directive):
	def __init__(self, s):
		self.s = s
	
	def __repr__(self):
		return "DotLEString(%r)" % self.s
	
	def __len__(self):
		return len(self.s) or 1
	
	def assemble(self, context):
		return pack_lestring(self.s)

class DotLoc(Directive):
	def __init__(self, pc, dpc=None):
		self.pc = pc
		self.dpc = dpc
	
	def __repr__(self):
		argstr = "0x%X" % self.pc
		if self.dpc is not none:
			argstr += ", 0x%X" % self.dpc
		return "DotLoc(%s)" % argstr

class DotSegment(Directive):
	def __init__(self, name):
		self.name = name
	
	def __repr__(self):
		return "DotSegment(%r)" % self.name

class DotScope(Directive):
	def __repr__(self):
		return "DotScope()"

class DotExport(Directive):
	def __init__(self, label, symbol=None):
		self.label = label
		self.symbol = symbol
	
	def __repr__(self):
		s = "DotExport("
		s += repr(self.label)
		if self.symbol is not None:
			s += ", " + repr(self.symbol)
		s += ")"
		return s


class Expr(object):
	pass

class RegExpr(Expr):
	def __init__(self, val):
		if isinstance(val, str):
			self.name = val
			self.num = REGISTER_NUMBERS[val]
		else:
			self.num = val
			self.name = REGISTER_NAMES[val]
	
	def __eq__(self, other):
		return self.num == other.num
	
	def __ne__(self, other):
		return not (self == other)
	
	def __hash__(self):
		return hash(self.num)
	
	def __repr__(self):
		return self.name
	
	def value(self, context=None):
		return self

class NumExpr(Expr):
	def __init__(self, num):
		self.num = num
	
	def __repr__(self):
		return "NumExpr(%d)" % self.num
	
	def value(self, context=None):
		return self.num

class LabelExpr(Expr):
	def __init__(self, name):
		self.name = name
	
	def __repr__(self):
		return "LabelExpr(%r)" % self.name
	
	def value(self, context=None):
		return context.get_label(self.name).value

class LabelDPCExpr(Expr):
	def __init__(self, label):
		self.label = label
	
	def __repr__(self):
		return "LabelDPCExpr(%r)" % self.label.name
	
	def value(self, context=None):
		return context.get_label(self.name).calldpc

class ExprOp(Expr):
	pass

class UnExpr(ExprOp):
	def __init__(self, unop):
		self.unop = unop
	
	def __str__(self):
		u = repr(self.unop)
		if isinstance(self.unop, ExprOp) and self.unop.PRECEDENCE > self.PRECEDENCE:
			u = "(" + u + ")"
		return "%s%s" % (self.OP, u)

class NegExpr(UnExpr):
	PRECEDENCE = 2
	ASSOC = 1
	OP = "-"
	TOKEN = "UMINUS"
	
	def value(self, context=None):
		return -self.unop.value(context)

class InvExpr(UnExpr):
	PRECEDENCE = 2
	ASSOC = 1
	OP = "~"
	TOKEN = "TILDE"
	
	def value(self, context=None):
		return ~self.unop.value(context)

class BinExpr(ExprOp):
	def __init__(self, lhs, rhs):
		self.lhs = lhs
		self.rhs = rhs
	
	def repr_operands(self):
		l = repr(self.lhs)
		if isinstance(self.lhs, ExprOp) and self.lhs.PRECEDENCE > self.PRECEDENCE:
			l = "(" + l + ")"
		
		r = repr(self.rhs)
		if isinstance(self.rhs, ExprOp) and self.rhs.PRECEDENCE > self.PRECEDENCE:
			r = "(" + r + ")"
		
		return (l, r)
	
	def __str__(self):
		l, r = self.repr_operands()
		return "%s %s %s" % (l, self.OP, r)

class AddExpr(BinExpr):
	PRECEDENCE = 4
	ASSOC = -1
	OP = "+"
	TOKEN = "PLUS"
	
	def value(self, context=None):
		return self.lhs.value(context) + self.rhs.value(context)

class SubExpr(BinExpr):
	PRECEDENCE = 4
	ASSOC = -1
	OP = "-"
	TOKEN = "DASH"
	
	def value(self, context=None):
		return self.lhs.value(context) - self.rhs.value(context)

class MulExpr(BinExpr):
	PRECEDENCE = 3
	ASSOC = -1
	OP = "*"
	TOKEN = "STAR"
	
	def value(self, context=None):
		return self.lhs.value(context) * self.rhs.value(context)

class DivExpr(BinExpr):
	PRECEDENCE = 3
	ASSOC = -1
	OP = "/"
	TOKEN = "SLASH"
	
	def value(self, context=None):
		return self.lhs.value(context) // self.rhs.value(context)

class ModExpr(BinExpr):
	PRECEDENCE = 3
	ASSOC = -1
	OP = "%"
	TOKEN = "PERCENT"
	
	def value(self, context=None):
		return self.lhs.value(context) % self.rhs.value(context)

class LShiftExpr(BinExpr):
	PRECEDENCE = 5
	ASSOC = -1
	OP = "<<"
	TOKEN = "LSHIFT"
	
	def value(self, context=None):
		return self.lhs.value(context) << self.rhs.value(context)

class RShiftExpr(BinExpr):
	PRECEDENCE = 5
	ASSOC = -1
	OP = ">>"
	TOKEN = "RSHIFT"
	
	def value(self, context=None):
		return self.lhs.value(context) >> self.rhs.value(context)

class AndExpr(BinExpr):
	PRECEDENCE = 8
	ASSOC = -1
	OP = "&"
	TOKEN = "AMPERSAND"
	
	def value(self, context=None):
		return self.lhs.value(context) & self.rhs.value(context)

class XorExpr(BinExpr):
	PRECEDENCE = 9
	ASSOC = -1
	OP = "^"
	TOKEN = "CARAT"
	
	def value(self, context=None):
		return self.lhs.value(context) ^ self.rhs.value(context)

class OrExpr(BinExpr):
	PRECEDENCE = 10
	ASSOC = -1
	OP = "|"
	TOKEN = "PIPE"
	
	def value(self, context=None):
		return self.lhs.value(context) | self.rhs.value(context)

BINOPS = [
	AddExpr, SubExpr, MulExpr, DivExpr, ModExpr,
	LShiftExpr, RShiftExpr, AndExpr, XorExpr, OrExpr
]

BINOP_MAP = {
	x.OP: x for x in BINOPS
}

UNOPS = [
	NegExpr, InvExpr
]

UNOP_MAP = {
	x.OP: x for x in UNOPS
}

OPERATORS = BINOPS + UNOPS
