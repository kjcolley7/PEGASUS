from abc import ABC, abstractmethod
from .geometry import *
from .isa import *

# Typing
from typing import Optional, Self, TextIO


class Location:
	def __init__(self, filename: str, lineno: int, column: int, line: Optional[str]=None):
		self.filename: str = filename
		self.lineno: int = lineno
		self.column: int = column
		self.line: Optional[str] = line
	
	def __repr__(self):
		return "%s(%r, %d, %d)" % (self.__class__.__name__, self.filename, self.lineno, self.column)
	
	def __str__(self):
		return "%s:%d:%d" % (self.filename, self.lineno, self.column)
	
	def _get_arrow(self, prefix: str="") -> str:
		if not self.line:
			return self.column
		
		pos = len(prefix)
		for i, c in enumerate(self.line):
			if i + 1 == self.column:
				break
			
			if c == "\t":
				pos += 1
				pos = (pos + 7) & ~7
			else:
				pos += 1
		
		pos -= len(prefix)
		return " " * len(prefix) + "~" * pos + "^"
	
	def show(self, fp: TextIO):
		fp.write(f"{self.filename}:{self.lineno}:\n")
		if self.line:
			fp.write(f"{self.line}\n")
		fp.write(self._get_arrow() + "\n")


class Locatable(ABC):
	loc: Optional[Location]
	
	def __init__(self, loc: Optional[Location]=None):
		self.loc = loc


class Symbol(Locatable):
	name: str
	value: Optional[int]


class Label(Symbol):
	def __init__(self, name: str, calldpc: Optional[int]=None, loc: Optional[Location]=None):
		super().__init__(loc)
		self.name: str = name
		self.calldpc: Optional[int] = calldpc
		self.value: Optional[int] = None
	
	def __repr__(self):
		l = repr(self.name)
		if self.calldpc is not None:
			l += ", 0x%X" % self.calldpc
		
		l = "%s(%s)" % (self.__class__.__name__, l)
		if self.value is not None:
			l += " = 0x%X" % self.value
		return l
	
	def __str__(self):
		l = self.name
		if self.calldpc:
			l += "@%d" % self.calldpc
		if self.value is not None:
			l += " = 0x%X" % self.value
		return l

class Equate(Symbol):
	def __init__(self, name: str, expr: 'Expr', loc: Optional[Location]=None):
		super().__init__(loc)
		self.name: str = name
		self.expr: 'Expr' = expr
		self.value: Optional[int] = None
	
	def __repr__(self):
		n = "%s(%r, %r)" % (self.__class__.__name__, self.name, self.expr)
		if self.value is not None:
			n += " = 0x%X" % self.value
		return n
	
	def __str__(self):
		n = "%s := %s" % (self.name, self.expr)
		if self.value is not None:
			n += " //0x%X" % self.value
		return n


class Directive(Locatable):
	...


class DotDW(Directive):
	def __init__(self, vals: list['Expr'], loc: Optional[Location]=None):
		super().__init__(loc)
		self.vals: list['Expr'] = vals
	
	def __repr__(self):
		return "DotDW(%r)" % self.vals
	
	def __len__(self):
		return 2 * len(self.vals)
	
	def assemble(self, context: 'Context'):
		return b"".join(p16(x.value(context)) for x in self.vals)

class DotDB(Directive):
	def __init__(self, vals: list['Expr'], loc: Optional[Location]=None):
		super().__init__(loc)
		self.vals: list['Expr'] = vals
	
	def __repr__(self):
		return "DotDW(%r)" % self.vals
	
	def __len__(self):
		return len(self.vals)
	
	def assemble(self, context: 'Context') -> bytes:
		return b"".join(p8(x.value(context)) for x in self.vals)

class DotLEString(Directive):
	def __init__(self, s: str, loc: Optional[Location]=None):
		super().__init__(loc)
		self.s: str = s
	
	def __repr__(self):
		return "DotLEString(%r)" % self.s
	
	def __len__(self):
		return len(self.s) or 1
	
	def assemble(self, context: Optional['Context']=None) -> bytes:
		return pack_lestring(self.s)

class DotLoc(Directive):
	def __init__(self, pc: 'Expr', dpc: Optional['Expr']=None, loc: Optional[Location]=None):
		super().__init__(loc)
		self.pc: 'Expr' = pc
		self.dpc: Optional['Expr'] = dpc
	
	def __repr__(self):
		argstr = repr(self.pc)
		if self.dpc is not None:
			argstr += ", " + repr(self.dpc)
		return "DotLoc(%s)" % argstr

class DotAlign(Directive):
	def __init__(self, alignment: 'Expr', loc: Optional[Location]=None):
		super().__init__(loc)
		self.align: 'Expr' = alignment
	
	def __repr__(self):
		return "DotAlign(%r)" % self.align

class DotSegment(Directive):
	def __init__(self, name: str, loc: Optional[Location]=None):
		super().__init__(loc)
		self.name: str = name
	
	def __repr__(self):
		return "DotSegment(%r)" % self.name

class DotScope(Directive):
	def __init__(self, loc: Optional[Location]=None):
		super().__init__(loc)
	
	def __repr__(self):
		return "DotScope()"

class DotExport(Directive):
	def __init__(self, label: 'SymbolExpr', exported_name: Optional[bytes]=None, loc: Optional[Location]=None):
		super().__init__(loc)
		self.label: 'SymbolExpr' = label
		self.exported_name: Optional[bytes] = exported_name
	
	def __repr__(self):
		s = "DotExport("
		s += repr(self.label)
		if self.exported_name is not None:
			s += ", " + repr(self.exported_name.decode("utf8"))
		s += ")"
		return s

class DotImport(Directive):
	def __init__(self, filename: str | bytes, loc: Optional[Location]=None):
		super().__init__(loc)
		if isinstance(filename, bytes):
			filename = filename.decode("utf8")
		self.filename: str = filename
	
	def __repr__(self):
		return "DotImport(%r)" % self.filename


class DotAssert(Directive):
	def __init__(
			self, lhs: 'Expr', cmp_op: str, rhs: 'Expr',
			loc: Optional[Location]=None
	):
		super().__init__(loc)
		self.lhs: 'Expr' = lhs
		self.rhs: 'Expr' = rhs
		self.cmp_op: str = cmp_op
	
	def __repr__(self):
		return "DotAssert(%r %s %r)" % (self.lhs, self.cmp_op, self.rhs)


class Operand(Locatable):
	...

class BaseRegExpr(Operand):
	name: str
	num: int
	is_cross: bool
	
	def __init__(self, name: str, num: int, loc: Optional[Location]=None):
		super().__init__(loc)
		self.name = name
		self.num = num
		self.is_cross = False
	
	def __eq__(self, other: 'BaseRegExpr'):
		return self.num == other.num
	
	def __ne__(self, other: 'BaseRegExpr'):
		return not (self == other)
	
	def __hash__(self):
		return hash(self.num)
	
	def __repr__(self):
		return self.name
	
	def cross(self) -> Self:
		self.is_cross = True
		return self
	
	def value(self, context=None) -> Self:
		return self

class RegExpr(BaseRegExpr):
	def __init__(self, val: str | int, loc: Optional[Location]=None):
		if isinstance(val, str):
			super().__init__(val, REGISTER_NUMBERS[val], loc=loc)
		else:
			super().__init__(REGISTER_NAMES[val], val, loc=loc)

class CRegExpr(BaseRegExpr):
	def __init__(self, val: str | int, loc: Optional[Location]=None):
		if isinstance(val, str):
			super().__init__(val, CONTROL_REGISTER_NUMBERS[val], loc=loc)
		else:
			super().__init__(CONTROL_REGISTER_NAMES[val], val, loc=loc)

class Expr(Operand):
	@abstractmethod
	def value(self, context: Optional['Context']=None) -> int:
		...

class NumExpr(Expr):
	def __init__(self, num: int, loc: Optional[Location]=None):
		super().__init__(loc)
		self.num: int = num
	
	def __repr__(self):
		return "NumExpr(%d)" % self.num
	
	def value(self, context: Optional['Context']=None) -> int:
		return self.num

class SymbolExpr(Expr):
	def __init__(self, name: str, loc: Optional[Location]=None):
		super().__init__(loc)
		self.name: str = name
	
	def __repr__(self):
		return "LabelExpr(%r)" % self.name
	
	def value(self, context: Optional['Context']=None) -> int:
		return context.get_value(self.name)

class LabelDPCExpr(Expr):
	def __init__(self, name: str, loc: Optional[Location]=None):
		super().__init__(loc)
		self.name: str = name
	
	def __repr__(self):
		return "LabelDPCExpr(%r)" % self.name
	
	def value(self, context: Optional['Context']=None) -> int:
		return context.get_dpc_value(self.name)

class ExprOp(Expr):
	PRECEDENCE: int
	ASSOC: int
	OP: str
	TOKEN: str

class UnExpr(ExprOp):
	def __init__(self, x: Expr, loc: Optional[Location]=None):
		super().__init__(loc)
		self.x: Expr = x
	
	def __str__(self):
		u = repr(self.x)
		if isinstance(self.x, ExprOp) and self.x.PRECEDENCE > self.PRECEDENCE:
			u = "(" + u + ")"
		return "%s%s" % (self.OP, u)

class NegExpr(UnExpr):
	PRECEDENCE = 2
	ASSOC = 1
	OP = "-"
	TOKEN = "UMINUS"
	
	def value(self, context: Optional['Context']=None) -> int:
		return -self.x.value(context)

class InvExpr(UnExpr):
	PRECEDENCE = 2
	ASSOC = 1
	OP = "~"
	TOKEN = "TILDE"
	
	def value(self, context: Optional['Context']=None) -> int:
		return ~self.x.value(context)

class BinExpr(ExprOp):
	def __init__(self, lhs: Expr, rhs: Expr, loc: Optional[Location]=None):
		super().__init__(loc)
		self.lhs: Expr = lhs
		self.rhs: Expr = rhs
	
	def repr_operands(self) -> tuple[str, str]:
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
	
	def value(self, context: Optional['Context']=None) -> int:
		return self.lhs.value(context) + self.rhs.value(context)

class SubExpr(BinExpr):
	PRECEDENCE = 4
	ASSOC = -1
	OP = "-"
	TOKEN = "DASH"
	
	def value(self, context: Optional['Context']=None) -> int:
		return self.lhs.value(context) - self.rhs.value(context)

class MulExpr(BinExpr):
	PRECEDENCE = 3
	ASSOC = -1
	OP = "*"
	TOKEN = "STAR"
	
	def value(self, context: Optional['Context']=None) -> int:
		return self.lhs.value(context) * self.rhs.value(context)

class DivExpr(BinExpr):
	PRECEDENCE = 3
	ASSOC = -1
	OP = "/"
	TOKEN = "SLASH"
	
	def value(self, context: Optional['Context']=None) -> int:
		return self.lhs.value(context) // self.rhs.value(context)

class ModExpr(BinExpr):
	PRECEDENCE = 3
	ASSOC = -1
	OP = "%"
	TOKEN = "PERCENT"
	
	def value(self, context: Optional['Context']=None) -> int:
		return self.lhs.value(context) % self.rhs.value(context)

class LShiftExpr(BinExpr):
	PRECEDENCE = 5
	ASSOC = -1
	OP = "<<"
	TOKEN = "LSHIFT"
	
	def value(self, context: Optional['Context']=None) -> int:
		return self.lhs.value(context) << self.rhs.value(context)

class RShiftExpr(BinExpr):
	PRECEDENCE = 5
	ASSOC = -1
	OP = ">>"
	TOKEN = "RSHIFT"
	
	def value(self, context: Optional['Context']=None) -> int:
		return self.lhs.value(context) >> self.rhs.value(context)

class AndExpr(BinExpr):
	PRECEDENCE = 8
	ASSOC = -1
	OP = "&"
	TOKEN = "AMPERSAND"
	
	def value(self, context: Optional['Context']=None) -> int:
		return self.lhs.value(context) & self.rhs.value(context)

class XorExpr(BinExpr):
	PRECEDENCE = 9
	ASSOC = -1
	OP = "^"
	TOKEN = "CARAT"
	
	def value(self, context: Optional['Context']=None) -> int:
		return self.lhs.value(context) ^ self.rhs.value(context)

class OrExpr(BinExpr):
	PRECEDENCE = 10
	ASSOC = -1
	OP = "|"
	TOKEN = "PIPE"
	
	def value(self, context: Optional['Context']=None) -> int:
		return self.lhs.value(context) | self.rhs.value(context)


BINOPS: list[type[BinExpr]] = [
	AddExpr, SubExpr, MulExpr, DivExpr, ModExpr,
	LShiftExpr, RShiftExpr, AndExpr, XorExpr, OrExpr
]

BINOP_MAP: dict[str, type[BinExpr]] = {
	x.OP: x for x in BINOPS
}

UNOPS: list[type[UnExpr]] = [
	NegExpr, InvExpr
]

UNOP_MAP: dict[str, type[UnExpr]] = {
	x.OP: x for x in UNOPS
}

OPERATORS = BINOPS + UNOPS
