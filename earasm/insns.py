from abc import ABC, abstractmethod
from .geometry import *
from .isa import *
from .ast import *

from typing import Optional, Self


class Instruction(Locatable):
	OPCODE: int
	
	def __init__(
			self,
			cc: str="AL",
			toggle_flags: bool=False,
			write_flags: Optional[bool]=None,
			loc: Optional[Location]=None
	):
		super().__init__(loc=loc)
		self.cc: str = cc
		self.operands: dict[str, Operand] = dict()
		self.pc: Optional[int] = None
		self.xc_prefix: bool = CC_MAP[cc] >= 8
		
		if write_flags is not None:
			assert(toggle_flags == False)
			
			will_write_flags = True
			if cc not in ("AL", "SP") and self.OPCODE != InsnCMP.OPCODE:
				will_write_flags = False
			
			if write_flags != will_write_flags:
				toggle_flags = True
		
		self.tf_prefix: bool = toggle_flags
		self.xx_prefix: Optional[bool] = None
		self.xy_prefix: Optional[bool] = None
		self.xz_prefix: Optional[bool] = None
		self.rd_prefix: bool = False
		self.rdx_prefix: bool = False
	
	def get_prefix_bytes(self) -> bytearray:
		pfx = bytearray()
		if self.xx_prefix is True:
			pfx.append(PREFIX_XX)
		if self.xy_prefix is True:
			pfx.append(PREFIX_XY)
		if self.xz_prefix is True:
			pfx.append(PREFIX_XZ)
		if self.tf_prefix:
			pfx.append(PREFIX_TF)
		if self.rd_prefix:
			pfx.append(PREFIX_DR(self.rd.num))
		if self.rdx_prefix:
			pfx.append(PREFIX_DR(self.rdx.num))
		if self.xc_prefix:
			pfx.append(PREFIX_XC)
		return pfx
	
	def got_operands(self):
		pass
	
	def set_operands(self, **kwargs: dict[str, Operand]) -> Self:
		self.operands = kwargs
		self.got_operands()
		return self
	
	def get_mnemonic(self) -> str:
		mnem = self.__class__.__name__
		if mnem.startswith("Insn"):
			mnem = mnem[4:]
		if self.tf_prefix:
			mnem += "F"
		return mnem
	
	def get_opcode(self) -> int:
		return self.OPCODE
	
	def make_opbyte(self) -> int:
		return ((CC_MAP[self.cc] & 7) << 5) | (self.get_opcode() & 0x1F)
	
	def __repr__(self):
		argstr = repr(self.cc)
		if self.operands:
			argstr += ", " + ", ".join("%s=%r" % (k, v) for k, v in self.operands.items())
		return "%s(%s)" % (self.get_mnemonic(), argstr)
	
	@abstractmethod
	def assemble(self, context: 'Context') -> bytes:
		...

class InsnRxy(Instruction):
	ALLOWS_RDX: bool = False
	
	def needs_dr_prefix(self) -> bool:
		# DR prefix always needed if Rdx is present
		if "Rdx" in self.operands:
			return True
		
		# DR prefix needed if Rd is not Rx
		if "Rd" in self.operands:
			return self.rd != self.rx
		return False
	
	def got_operands(self):
		if not self.ALLOWS_RDX and "Rdx" in self.operands:
			raise ValueError("%s does not allow Rdx operand" % self.get_mnemonic())
		
		self.rd: Optional[RegExpr] = None
		self.rdx: Optional[RegExpr] = None
		self.rx: BaseRegExpr = self.operands["Rx"]
		try:
			self.vy: BaseRegExpr | Expr = self.operands["Vy"]
		except KeyError:
			self.vy: RegExpr | Expr = self.operands["V8"]
		
		if self.rx.is_cross:
			self.xx_prefix = True
		if isinstance(self.vy, BaseRegExpr) and self.vy.is_cross:
			self.xy_prefix = True
		
		if "Rd" in self.operands:
			self.rd = self.operands["Rd"]
			
			# DR prefix byte needed?
			if self.needs_dr_prefix():
				self.rd_prefix = True
				if self.rd.is_cross:
					self.xz_prefix = True
		
		if "Rdx" in self.operands:
			self.rdx: RegExpr = self.operands["Rdx"]
			if self.rdx == self.rd:
				# Rdx can't be the same as Rd
				raise ValueError("Rdx cannot be the same as Rd")
			self.rdx_prefix = True
	
	def has_imm(self) -> bool:
		return isinstance(self.vy, Expr)
	
	def imm_len(self) -> int:
		if not self.has_imm():
			return 0
		if "Vy" in self.operands:
			return 2
		elif "V8" in self.operands:
			return 1
		else:
			assert False, "Invalid operand name for immediate value: %r" % self.operands.keys()
	
	def __len__(self):
		# Instruction prefixes, opcode byte, regpair byte, optional imm8/16
		return len(self.get_prefix_bytes()) + 1 + 1 + self.imm_len()
	
	def assemble(self, context: 'Context') -> bytes:
		# Encode prefixes
		ret = self.get_prefix_bytes()
		
		# Encode opcode byte
		ret.append(self.make_opbyte())
		
		# Encode regpair byte
		if isinstance(self.vy, Expr):
			ry = RegExpr("DPC")
		else:
			ry = self.vy
		ret.append((self.rx.num << 4) | ry.num)
		
		# Encode Imm8/16 (if present)
		n = self.imm_len()
		if n == 2:
			ret.extend(p16(self.vy.value(context)))
		elif n == 1:
			# Truncate value if too large
			immval = self.vy.value(context) & 0xFF
			ret.append(immval)
		return bytes(ret)

class InsnAddOrSub(InsnRxy):
	def __init__(
			self,
			cc: str="AL",
			toggle_flags: bool=False,
			write_flags: Optional[bool]=None,
			loc: Optional[Location]=None
	):
		super().__init__(cc, toggle_flags, write_flags, loc=loc)
		self._inc: Optional['InsnINC'] = None
		self._is_inc: Optional[bool] = None
	
	def _make_inc(self):
		operands: dict[str, Operand] = {}
		if self.rd is not None:
			operands["Rd"] = self.rd
		operands["Rx"] = self.rx
		if isinstance(self.vy, Expr):
			operands["SImm4"] = self.vy if self.OPCODE == 0x00 else NegExpr(self.vy)
		self._inc = InsnINC(self.cc, self.tf_prefix).set_operands(**operands)
		self._is_inc = True
	
	def is_inc(self) -> bool:
		if self._is_inc is None:
			self._is_inc = False
			try:
				imm = self.vy.value()
				if (-8 <= imm <= -1) or (1 <= imm <= 8):
					self._make_inc()
					self._is_inc = True
			except AttributeError:
				pass
			except TypeError:
				pass
		
		return self._is_inc
	
	def _wrap_inc(method):
		def _proxy_inc(self: 'InsnAddOrSub', *args, **kwargs):
			if self.is_inc():
				return getattr(self._inc, method)(*args, **kwargs)
			else:
				return getattr(InsnRxy, method)(self, *args, **kwargs)
		
		return _proxy_inc
	
	__len__ = _wrap_inc("__len__")
	assemble = _wrap_inc("assemble")
	get_mnemonic = _wrap_inc("get_mnemnoic")
	__repr__ = _wrap_inc("__repr__")

class InsnADD(InsnAddOrSub):
	OPCODE = 0x00

class InsnSUB(InsnAddOrSub):
	OPCODE = 0x01

class InsnMLU(InsnRxy):
	OPCODE = 0x02
	ALLOWS_RDX = True

class InsnMLS(InsnRxy):
	OPCODE = 0x03
	ALLOWS_RDX = True

class InsnDVU(InsnRxy):
	OPCODE = 0x04
	ALLOWS_RDX = True

class InsnDVS(InsnRxy):
	OPCODE = 0x05
	ALLOWS_RDX = True

class InsnXOR(InsnRxy):
	OPCODE = 0x06

class InsnAND(InsnRxy):
	OPCODE = 0x07

class InsnORR(InsnRxy):
	OPCODE = 0x08

class InsnSHL(InsnRxy):
	OPCODE = 0x09

class InsnSRU(InsnRxy):
	OPCODE = 0x0A

class InsnSRS(InsnRxy):
	OPCODE = 0x0B

class InsnMOV(InsnRxy):
	OPCODE = 0x0C

class InsnCMP(InsnRxy):
	OPCODE = 0x0D

class InsnRDC(InsnRxy):
	OPCODE = 0x0E

class InsnWRC(InsnRxy):
	OPCODE = 0x0F

class InsnLoadStore(InsnRxy):
	def needs_dr_prefix(self):
		return self.rd != RegExpr("ZERO")

class InsnLDW(InsnLoadStore):
	OPCODE = 0x10

class InsnSTW(InsnLoadStore):
	OPCODE = 0x11

class InsnLDB(InsnLoadStore):
	OPCODE = 0x12

class InsnSTB(InsnLoadStore):
	OPCODE = 0x13

class InsnBRA(InsnRxy):
	OPCODE = 0x14
	
	def got_operands(self):
		# Parser doesn't allow cross-regs with BRA/FCA
		self.operands["Rx"] = self.operands.get("Rx", RegExpr("DPC"))
		InsnRxy.got_operands(self)

class InsnBRR(Instruction):
	OPCODE = 0x15
	
	def got_operands(self):
		if "Imm16" in self.operands:
			self.imm16 = NumExpr(self.operands["Imm16"])
		else:
			label = self.operands["Label"]
			
			# Target is label - @PC@
			pc = SymbolExpr("@PC@")
			self.imm16 = SubExpr(label, pc)
		
		Instruction.got_operands(self)
	
	def __len__(self):
		# Instruction prefixes, opcode byte, Imm16
		return len(self.get_prefix_bytes()) + 1 + 2
	
	def assemble(self, context: 'Context') -> bytes:
		# Encode prefixes
		ret = self.get_prefix_bytes()
		
		# Encode opcode byte
		ret.append(self.make_opbyte())
		
		# Encode Imm16
		ret.extend(p16(self.imm16.value(context)))
		return bytes(ret)

class InsnFCA(InsnBRA):
	OPCODE = 0x16

class InsnFCR(InsnBRR):
	OPCODE = 0x17

class InsnRDB(Instruction):
	OPCODE = 0x18
	
	def got_operands(self):
		self.rx: RegExpr = self.operands["Rx"]
		if self.rx.is_cross:
			self.xx_prefix = True
		self.port: Expr = self.operands.get("Port", NumExpr(0))
	
	def __len__(self):
		# Instruction prefixes, opcode byte, Rx:Port byte
		return len(self.get_prefix_bytes()) + 1 + 1
	
	def assemble(self, context: 'Context') -> bytes:
		# Encode prefixes
		ret = self.get_prefix_bytes()
		
		# Encode opcode byte
		ret.append(self.make_opbyte())
		
		# Encode Rx:Port byte
		port = self.port.value(context)
		if port < 0 or port >= 16:
			raise ValueError("Port number in RDB out of range 0-15: %d" % port)
		ret.append((self.rx.num << 4) | port)
		return bytes(ret)

class InsnWRB(Instruction):
	OPCODE = 0x19
	
	def got_operands(self):
		self.port: Expr = self.operands.get("Port", NumExpr(0))
		self.v8 = self.operands["V8"]
		if isinstance(self.v8, RegExpr) and self.v8.is_cross:
			self.xy_prefix = True
	
	def has_imm(self):
		return isinstance(self.v8, Expr)
	
	def __len__(self):
		# Instruction prefixes, opcode byte, Port:V8 byte
		size = len(self.get_prefix_bytes()) + 1 + 1
		if self.has_imm():
			# Imm8
			size += 1
		return size
	
	def assemble(self, context):
		# Encode prefixes
		ret = self.get_prefix_bytes()
		
		# Encode opcode byte
		ret.append(self.make_opbyte())
		
		# Encode Port:Ry byte
		port = self.port.value(context)
		if port < 0 or port >= 16:
			raise ValueError("Port number in RDB out of range [0, 15]: %d" % port)
		if isinstance(self.v8, Expr):
			ry = RegExpr("DPC")
		else:
			ry = self.v8
		ret.append((port << 4) | ry.num)
		
		# Encode Imm8 (if present)
		if isinstance(self.v8, Expr):
			ret.append(self.v8.value(context))
		return bytes(ret)

class InsnRegset(Instruction):
	def got_operands(self):
		self.regset: set[RegExpr] = self.operands["Regs16"]
		
		# PSH & POP are special. Here, the XZ prefix means that Rd is a cross-reg,
		# and the XY prefix means that Regs16 are all cross-regs.
		for r in self.regset:
			if r.is_cross:
				self.xy_prefix = True
				break
		
		if "Rd" in self.operands:
			self.rd = self.operands["Rd"]
			self.rd_prefix = True
			if self.rd.is_cross:
				self.xz_prefix = True
	
	def get_regs16(self) -> int:
		regs16 = 0
		for r in self.regset:
			regs16 |= 1 << r.num
		return regs16
	
	def __len__(self):
		# Instruction prefixes, opcode byte, Regs16
		return len(self.get_prefix_bytes()) + 1 + 2
	
	def assemble(self, context: 'Context') -> bytes:
		# Encode prefixes
		ret = self.get_prefix_bytes()
		
		# Encode opcode byte
		ret.append(self.make_opbyte())
		
		# Encode Regs16
		ret.extend(p16(self.get_regs16()))
		return bytes(ret)

class InsnPSH(InsnRegset):
	OPCODE = 0x1A

class InsnPOP(InsnRegset):
	OPCODE = 0x1B

class InsnINC(Instruction):
	OPCODE = 0x1C
	
	def got_operands(self):
		self.rx: RegExpr = self.operands["Rx"]
		if self.rx.is_cross:
			self.xx_prefix = True
		self.simm4: Expr = self.operands.get("SImm4", NumExpr(1))
		
		if "Rd" in self.operands:
			self.rd: RegExpr = self.operands["Rd"]
			
			# DR prefix byte needed?
			if self.rd != self.rx:
				self.rd_prefix = True
				if self.rd.is_cross:
					self.xz_prefix = True
		else:
			self.rd = None
	
	def __len__(self):
		# Instruction prefixes, opcode byte, Rx:SImm4 byte
		return len(self.get_prefix_bytes()) + 1 + 1
	
	def assemble(self, context: 'Context') -> bytes:
		# Encode prefixes
		ret = self.get_prefix_bytes()
		
		# Encode opcode byte
		ret.append(self.make_opbyte())
		
		# Compute value of SImm4 constexpr
		val = self.simm4.value(context)
		
		# Check range of SImm4
		if val < -8 or val > 8:
			raise ValueError("SImm4 value in INC out of range [-8, 8]: %d" % val)
		
		# INC Rx, 0 cannot be encoded
		if val == 0:
			# Encode "INC [Rd,] Rx, 0" as "ADD [Rd,] Rx, ZERO" (same size)
			add = InsnADD(self.cc, self.tf_prefix)
			operands = {}
			if self.rd is not None:
				operands["Rd"] = self.rd
			operands["Rx"] = self.rx
			operands["Vy"] = RegExpr("ZERO")
			add.set_operands(**operands)
			
			assert len(add) == len(self)
			return add.assemble(context)
		
		# Encode Rx:SImm4 byte
		if val > 0:
			val -= 1
		ret.append((self.rx.num << 4) | (val & 0x0F))
		return bytes(ret)

class InsnBare(Instruction):
	def __len__(self):
		# Instruction prefixes, opcode byte
		return len(self.get_prefix_bytes()) + 1
	
	def assemble(self, context: 'Context') -> bytes:
		# Encode prefixes
		ret = self.get_prefix_bytes()
		
		# Encode opcode byte
		ret.append(self.make_opbyte())
		return bytes(ret)

class InsnBPT(InsnBare):
	OPCODE = 0x1D

class InsnHLT(InsnBare):
	OPCODE = 0x1E

class InsnNOP(InsnBare):
	OPCODE = 0x1F

class PseudoInstruction(Instruction):
	def __init__(
			self,
			cc: str="AL",
			toggle_flags: bool=False,
			write_flags: Optional[bool]=None,
			loc: Optional[Location]=None
	):
		super().__init__(cc, toggle_flags, write_flags, loc=loc)
		self.insns: list[Instruction] = None
	
	def get_instructions(self) -> list[Instruction]:
		if self.insns is None:
			self.insns = self.real_instructions()
		return self.insns
	
	@abstractmethod
	def real_instructions(self) -> list[Instruction]:
		...
	
	def __len__(self):
		return sum(len(insn) for insn in self.get_instructions())
	
	def assemble(self, context: 'Context') -> bytes:
		# Encode each real instruction and concatenate their encoded results
		return b"".join(insn.assemble(context) for insn in self.get_instructions())

class InsnRET(PseudoInstruction):
	def real_instructions(self):
		# Replace "RET.cc" with "BRA.cc RD, RA"
		return [InsnBRA(self.cc).set_operands(Rx=RegExpr("RD"), Vy=RegExpr("RA"))]

class InsnDEC(PseudoInstruction):
	def got_operands(self):
		# Replace "DEC.cc Rx, value" with "INC.cc Rx, -value"
		self.operands["SImm4"] = NegExpr(self.operands.get("SImm4", NumExpr(1)))
	
	def real_instructions(self) -> list[Instruction]:
		return [InsnINC(self.cc, self.tf_prefix).set_operands(**self.operands)]

class InsnNEG(PseudoInstruction):
	def got_operands(self):
		self.rx = self.operands["Rx"]
	
	def real_instructions(self) -> list[Instruction]:
		# Replace "NEG.cc Rx" with "SUB.cc Rx, ZERO, Rx"
		return [
			InsnSUB(self.cc, self.tf_prefix)
			.set_operands(Rd=self.rx, Rx=RegExpr("ZERO"), Vy=self.rx)
		]

class InsnINV(PseudoInstruction):
	def got_operands(self):
		self.rx = self.operands["Rx"]
	
	def real_instructions(self) -> list[Instruction]:
		# Replace "INV.cc Rx" with "XOR.cc Rx, -1"
		return [InsnXOR(self.cc, self.tf_prefix).set_operands(Rx=self.rx, Vy=NumExpr(-1))]

class InsnADR(PseudoInstruction):
	def got_operands(self):
		self.rx: RegExpr = self.operands["Rx"]
		self.target: Expr = self.operands["Label"]
		
		assert self.rx.num != REGISTER_NUMBERS["PC"]
	
	def __len__(self):
		# Length of this instruction: optional XC byte, DR byte, opcode byte, regpair byte, Imm16
		return len(self.get_prefix_bytes()) + 1 + 1 + 1 + 2
	
	def real_instructions(self) -> list[Instruction]:
		# Replace "ADR.cc Rx, target" with "ADD.cc Rx, PC, target - @PC@"
		pc = SymbolExpr("@PC@")
		vy = SubExpr(self.target, pc)
		return [
			InsnADD(self.cc, self.tf_prefix)
			.set_operands(Rd=self.rx, Rx=RegExpr("PC"), Vy=vy)
		]

class InsnSWP(PseudoInstruction):
	def got_operands(self):
		self.ra: RegExpr = self.operands["Ra"]
		self.rb: RegExpr = self.operands["Rb"]
	
	def real_instructions(self) -> list[Instruction]:
		# Replace "SWP.cc Ra, Rb" with "XOR.cc Ra, Rb; XOR.cc Rb, Ra; XOR.cc Ra, Rb"
		kwargs = {}
		if self.cc != "AL":
			kwargs["write_flags"] = False
		
		xor_ab1 = InsnXOR(self.cc, **kwargs).set_operands(Rx=self.ra, Vy=self.rb)
		xor_ba = InsnXOR(self.cc, **kwargs).set_operands(Rx=self.rb, Vy=self.ra)
		xor_ab2 = InsnXOR(self.cc, self.tf_prefix).set_operands(Rx=self.ra, Vy=self.rb)
		return [xor_ab1, xor_ba, xor_ab2]

class PseudoAddOrSubWithCarry(PseudoInstruction):
	IS_ADD: bool
	
	def __init__(
			self,
			cc="AL",
			toggle_flags: bool=False,
			write_flags: Optional[bool]=None,
			loc: Optional[Location]=None
	):
		super().__init__(cc, toggle_flags=toggle_flags, write_flags=write_flags, loc=loc)
	
	def got_operands(self):
		self.ra: RegExpr = self.operands["Rx"]
		self.vb = self.operands["Vy"]
		self.rd: RegExpr = self.operands.get("Rd", self.ra)
	
	def real_instructions(self):
		incdec = InsnINC if self.IS_ADD else InsnDEC
		addsub = InsnADD if self.IS_ADD else InsnSUB
		
		if self.ra == self.rd:
			"""
			ADC Ra, Vb:
				INC.CS Ra
				ADD Ra, Vb
			
			ADC.cc Ra, Vb:
				BRR.!cc @.after
				ADC Ra, Vb
				
			@.after:
			"""
			incdec_cs = incdec("CS").set_operands(Rx=self.ra)
			addsub_ab = addsub().set_operands(Rx=self.ra, Vy=self.vb)
			
			ret = []
			if self.cc != "AL":
				jump_amount = len(incdec_cs) + len(addsub_ab)
				brr_ncc = InsnBRR(CONDITION_INVERSES[self.cc]).set_operands(Imm16=jump_amount)
				ret.append(brr_ncc)
			
			ret.append(incdec_cs)
			ret.append(addsub_ab)
			return ret
		else:
			"""
			ADC Rd, Ra, Vb:
				MOV     Rd, ZERO
				INC.CS  Rd
				ADD     Rd, Ra
				ADD     Rd, Vb
			
			SBC Rd, Ra, Vb:
				MOV     Rd, ZERO
				DEC.CS  Rd
				ADD     Rd, Ra
				SUB     Rd, Vb
			
			ADC.cc Rd, Ra, Vb:
				BRR.!cc PC, @.after - @PC@
				ADC     Rd, Ra, Vb
			
			@.after:
			"""
			zero_rd = InsnMOV().set_operands(Rx=self.rd, Vy=RegExpr("ZERO"))
			incdec_cs = incdec("CS").set_operands(Rx=self.rd)
			add_da = InsnADD().set_operands(Rx=self.rd, Vy=self.ra)
			addsub_db = addsub().set_operands(Rx=self.rd, Vy=self.vb)
			
			ret = []
			if self.cc != "AL":
				jump_amount = len(zero_rd) + len(incdec_cs) + len(add_da) + len(addsub_db)
				brr_after = InsnBRR(CONDITION_INVERSES[self.cc]).set_operands(Imm16=jump_amount)
				ret.append(brr_after)
			
			ret.append(zero_rd)
			ret.append(incdec_cs)
			ret.append(add_da)
			ret.append(addsub_db)
			return ret

class InsnADC(PseudoAddOrSubWithCarry):
	IS_ADD = True

class InsnSBC(PseudoAddOrSubWithCarry):
	IS_ADD = False


INSNS: list[Instruction] = [
	# Real instructions
	InsnADD, InsnSUB, InsnMLU, InsnMLS, InsnDVU, InsnDVS, InsnXOR, InsnAND,
	InsnORR, InsnSHL, InsnSRU, InsnSRS, InsnMOV, InsnCMP, InsnRDC, InsnWRC,
	InsnLDW, InsnSTW, InsnLDB, InsnSTB, InsnBRA, InsnBRR, InsnFCA, InsnFCR,
	InsnRDB, InsnWRB, InsnPSH, InsnPOP, InsnINC, InsnBPT, InsnHLT, InsnNOP,
	
	# Pseudo instructions
	InsnRET, InsnDEC, InsnNEG, InsnINV, InsnADR, InsnSWP, InsnADC, InsnSBC
]

INSN_MAP: dict[str, Instruction] = {
	x.__name__[4:]: x for x in INSNS
}
