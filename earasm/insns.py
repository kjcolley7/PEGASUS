from .geometry import *
from .isa import *
from .ast import *


class Instruction(object):
	def __init__(self, cc="AL", toggle_flags=False, write_flags=None):
		self.cc = cc
		self.operands = dict()
		self.pc = None
		self.prefixes = []
		self.xc_prefix = [p8(PREFIX_XC)] if CC_MAP[cc] >= 8 else []
		
		if write_flags is not None:
			assert(toggle_flags == False)
			
			will_write_flags = True
			if cc not in ("AL", "SP"):
				will_write_flags = False
			
			if write_flags != will_write_flags:
				toggle_flags = True
		
		self.toggle_flags = toggle_flags
		if toggle_flags:
			self.prefixes.append(p8(PREFIX_TF))
	
	def get_prefix_bytes(self):
		return self.prefixes + self.xc_prefix
	
	def got_operands(self):
		pass
	
	def set_operands(self, **kwargs):
		self.operands = kwargs
		self.got_operands()
		return self
	
	def get_mnemonic(self):
		mnem = self.__class__.__name__
		if mnem.startswith("Insn"):
			mnem = mnem[4:]
		if self.toggle_flags:
			mnem += "F"
		return mnem
	
	def get_opcode(self):
		return self.OPCODE
	
	def make_opbyte(self):
		return p8(((CC_MAP[self.cc] & 7) << 5) | (self.get_opcode() & 0x1F))
	
	def __repr__(self):
		argstr = repr(self.cc)
		if self.operands:
			argstr += ", " + ", ".join("%s=%r" % (k, v) for k, v in self.operands.items())
		return "%s(%s)" % (self.get_mnemonic(), argstr)

class InsnRxy(Instruction):
	def got_operands(self):
		self.rx = self.operands["Rx"]
		self.vy = self.operands["Vy"]
		
		if "Rd" in self.operands:
			self.rd = self.operands["Rd"]
			
			# DR prefix byte needed?
			if self.rd != self.rx:
				self.prefixes.append(p8(PREFIX_DR(self.rd)))
		else:
			self.rd = None
	
	def has_imm(self):
		return not isinstance(self.vy, RegExpr)
	
	def __len__(self):
		# Register prefixes, opcode byte, regpair byte
		size = len(self.get_prefix_bytes()) + 1 + 1
		if self.has_imm():
			# Imm16
			size += 2
		return size
	
	def assemble(self, context):
		# Encode prefixes
		ret = self.get_prefix_bytes()
		
		# Encode opcode byte
		ret.append(self.make_opbyte())
		
		# Encode regpair byte
		if self.has_imm():
			ry = RegExpr("DPC")
		else:
			ry = self.vy
		ret.append(p8((self.rx.num << 4) | ry.num))
		
		# Encode Imm16 (if present)
		if self.has_imm():
			ret.append(p16(self.vy.value(context)))
		return b"".join(ret)

class InsnAddOrSub(InsnRxy):
	def __init__(self, cc="AL", toggle_flags=False, write_flags=None):
		InsnRxy.__init__(self, cc, toggle_flags, write_flags)
		self._inc = None
		self._is_inc = None
	
	def _make_inc(self):
		operands = {}
		if self.rd is not None:
			operands["Rd"] = self.rd
		operands["Rx"] = self.rx
		operands["SImm4"] = self.vy if self.OPCODE == 0x00 else NegExpr(self.vy)
		self._inc = InsnINC(self.cc, self.toggle_flags).set_operands(**operands)
		self._is_inc = True
	
	def is_inc(self):
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
		def _proxy_inc(self, *args, **kwargs):
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

class InsnMLS(InsnRxy):
	OPCODE = 0x03

class InsnDVU(InsnRxy):
	OPCODE = 0x04

class InsnDVS(InsnRxy):
	OPCODE = 0x05

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

class InsnLDW(InsnRxy):
	OPCODE = 0x10

class InsnSTW(InsnRxy):
	OPCODE = 0x11

class InsnLDB(InsnRxy):
	OPCODE = 0x12

class InsnSTB(InsnRxy):
	OPCODE = 0x13
	
	def got_operands(self):
		self.rx = self.operands["Rx"]
		self.v8 = self.operands["V8"]
	
	def has_imm(self):
		return not isinstance(self.v8, RegExpr)
	
	def __len__(self):
		# Register prefixes, opcode byte, regpair byte
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
		
		# Encode Rx:V8 byte
		if self.has_imm():
			ry = RegExpr("DPC")
		else:
			ry = self.v8
		ret.append(p8((self.rx.num << 4) | ry.num))
		
		# Encode Imm8 byte (if present)
		if self.has_imm():
			# Truncate value if too large
			immval = self.v8.value(context) & 0xFF
			ret.append(p8(immval))
		return b"".join(ret)

class InsnBRA(InsnRxy):
	OPCODE = 0x14
	
	def got_operands(self):
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
			curpc = LabelExpr("PC@")
			self.imm16 = SubExpr(label, curpc)
		
		Instruction.got_operands(self)
	
	def __len__(self):
		# Register prefixes, opcode byte, Imm16
		return len(self.get_prefix_bytes()) + 1 + 2
	
	def assemble(self, context):
		# Encode prefixes
		ret = self.get_prefix_bytes()
		
		# Encode opcode byte
		ret.append(self.make_opbyte())
		
		# Encode Imm16
		ret.append(p16(self.imm16.value(context)))
		return b"".join(ret)

class InsnFCA(InsnBRA):
	OPCODE = 0x16

class InsnFCR(InsnBRR):
	OPCODE = 0x17

class InsnRDB(Instruction):
	OPCODE = 0x18
	
	def got_operands(self):
		self.rx = self.operands["Rx"]
		self.port = self.operands.get("Port", NumExpr(0))
	
	def __len__(self):
		# Register prefixes, opcode byte, Rx:Port byte
		return len(self.get_prefix_bytes()) + 1 + 1
	
	def assemble(self, context):
		# Encode prefixes
		ret = self.get_prefix_bytes()
		
		# Encode opcode byte
		ret.append(self.make_opbyte())
		
		# Encode Rx:Port byte
		port = self.port.value(context)
		if port < 0 or port >= 16:
			raise RangeError("Port number in RDB out of range 0-15: %d" % port)
		ret.append(p8((self.rx.num << 4) | port))
		return b"".join(ret)

class InsnWRB(Instruction):
	OPCODE = 0x19
	
	def got_operands(self):
		self.port = self.operands.get("Port", NumExpr(0))
		self.v8 = self.operands["V8"]
	
	def has_imm(self):
		return not isinstance(self.v8, RegExpr)
	
	def __len__(self):
		# Register prefixes, opcode byte, Port:V8 byte
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
			raise RangeError("Port number in RDB out of range [0, 15]: %d" % port)
		if self.has_imm():
			ry = RegExpr("DPC")
		else:
			ry = self.v8
		ret.append(p8((port << 4) | ry.num))
		
		# Encode Imm8 (if present)
		if self.has_imm():
			ret.append(p8(self.v8.value(context)))
		return b"".join(ret)

class InsnRegset(Instruction):
	def got_operands(self):
		self.regset = self.operands["Regs16"]
		
		if "Rd" in self.operands:
			self.rd = self.operands["Rd"]
			self.prefixes.append(p8(PREFIX_DR(self.rd)))
	
	def get_regs16(self):
		regs16 = 0
		for r in self.regset:
			regs16 |= 1 << r.num
		return regs16
	
	def __len__(self):
		# Register prefixes, opcode byte, Regs16
		return len(self.get_prefix_bytes()) + 1 + 2
	
	def assemble(self, context):
		# Encode prefixes
		ret = self.get_prefix_bytes()
		
		# Encode opcode byte
		ret.append(self.make_opbyte())
		
		# Encode Regs16
		ret.append(p16(self.get_regs16()))
		return b"".join(ret)

class InsnPSH(InsnRegset):
	OPCODE = 0x1A

class InsnPOP(InsnRegset):
	OPCODE = 0x1B

class InsnINC(Instruction):
	OPCODE = 0x1C
	
	def got_operands(self):
		self.rx = self.operands["Rx"]
		self.simm4 = self.operands.get("SImm4", NumExpr(1))
		
		if "Rd" in self.operands:
			self.rd = self.operands["Rd"]
			
			# DR prefix byte needed?
			if self.rd != self.rx:
				self.prefixes.append(p8(PREFIX_DR(self.rd)))
		else:
			self.rd = None
	
	def __len__(self):
		# Register prefixes, opcode byte, Rx:SImm4 byte
		return len(self.get_prefix_bytes()) + 1 + 1
	
	def assemble(self, context):
		# Encode prefixes
		ret = self.get_prefix_bytes()
		
		# Encode opcode byte
		ret.append(self.make_opbyte())
		
		# Compute value of SImm4 constexpr
		val = self.simm4.value(context)
		
		# Check range of SImm4
		if val < -8 or val > 8:
			raise RangeError("SImm4 value in INC out of range [-8, 8]: %d" % val)
		
		# INC Rx, 0 cannot be encoded
		if val == 0:
			nop = InsnNOP().assemble(context)
			assert(len(nop) == 1)
			
			if self.rd is None:
				# Encode "INC Rx, 0" as NOPs
				return nop * len(self)
			else:
				# Encode "INC Rd, Rx, 0" as "MOV Rd, Rx" padded with trailing NOPs as necessary
				mov = InsnMOV(self.cc)
				mov.set_operands(Rx=self.rd, Vy=self.rx)
				return mov.assemble(context).ljust(len(self), nop)
		
		# Encode Rx:SImm4 byte
		if val > 0:
			val -= 1
		ret.append(p8((self.rx.num << 4) | (val & 0x0F)))
		return b"".join(ret)

class InsnBare(Instruction):
	def __len__(self):
		# Register prefixes, opcode byte
		return len(self.get_prefix_bytes()) + 1
	
	def assemble(self, context):
		# Encode prefixes
		ret = self.get_prefix_bytes()
		
		# Encode opcode byte
		ret.append(self.make_opbyte())
		return b"".join(ret)

class InsnBPT(InsnBare):
	OPCODE = 0x1D

class InsnHLT(InsnBare):
	OPCODE = 0x1E

class InsnNOP(InsnBare):
	OPCODE = 0x1F

class PseudoInstruction(Instruction):
	def __init__(self, cc="AL", toggle_flags=False, write_flags=None):
		Instruction.__init__(self, cc, toggle_flags, write_flags)
		self.insns = None
	
	def get_instructions(self):
		if self.insns is None:
			self.insns = self.real_instructions()
		return self.insns
	
	def __len__(self):
		return sum(len(insn) for insn in self.get_instructions())
	
	def assemble(self, context):
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
	
	def real_instructions(self):
		return [InsnINC(self.cc, self.toggle_flags).set_operands(**self.operands)]

class InsnNEG(PseudoInstruction):
	def got_operands(self):
		self.rx = self.operands["Rx"]
	
	def real_instructions(self):
		# Replace "NEG.cc Rx" with "SUB.cc Rx, ZERO, Rx"
		return [
			InsnSUB(self.cc, self.toggle_flags)
			.set_operands(Rd=self.rx, Rx=RegExpr("ZERO"), Vy=self.rx)
		]

class InsnINV(PseudoInstruction):
	def got_operands(self):
		self.rx = self.operands["Rx"]
	
	def real_instructions(self):
		# Replace "INV.cc Rx" with "XOR.cc Rx, -1"
		return [InsnXOR(self.cc, self.toggle_flags).set_operands(Rx=self.rx, Vy=NumExpr(-1))]

class InsnADR(PseudoInstruction):
	def got_operands(self):
		self.rx = self.operands["Rx"]
		self.target = self.operands["Label"]
	
	def __len__(self):
		# Length of this instruction: optional XC byte, DR byte, opcode byte, regpair byte, Imm16
		return len(self.get_prefix_bytes()) + 1 + 1 + 1 + 2
	
	def real_instructions(self):
		# Length of this instruction: optional XC byte, DR byte, opcode byte, regpair byte, Imm16
		curlen = NumExpr(len(self.get_prefix_bytes()) + 1 + 1 + 1 + 2)
		
		# Replace "ADR.cc Rx, target" with "ADD.cc Rx, PC, target - @PC@"
		curpc = LabelExpr("PC@")
		vy = SubExpr(self.target, curpc)
		return [
			InsnADD(self.cc, self.toggle_flags)
			.set_operands(Rd=self.rx, Rx=RegExpr("PC"), Vy=vy)
		]

class InsnSWP(PseudoInstruction):
	def got_operands(self):
		self.ra = self.operands["Ra"]
		self.rb = self.operands["Rb"]
	
	def real_instructions(self):
		# Replace "SWP.cc Ra, Rb" with "XOR.cc Ra, Rb; XOR.cc Rb, Ra; XOR.cc Ra, Rb"
		kwargs = {}
		if self.cc != "AL":
			kwargs["write_flags"] = False
		
		xor_ab1 = InsnXOR(self.cc, **kwargs).set_operands(Rx=self.ra, Vy=self.rb)
		xor_ba = InsnXOR(self.cc, **kwargs).set_operands(Rx=self.rb, Vy=self.ra)
		xor_ab2 = InsnXOR(self.cc, self.toggle_flags).set_operands(Rx=self.ra, Vy=self.rb)
		return [xor_ab1, xor_ba, xor_ab2]

class PseudoAddOrSubWithCarry(PseudoInstruction):
	def __init__(self, cc="AL", toggle_flags=False, is_add=None):
		PseudoInstruction.__init__(self, cc, toggle_flags)
		self.is_add = is_add
	
	def got_operands(self):
		self.ra = self.operands["Rx"]
		self.vb = self.operands["Vy"]
		self.rd = self.operands.get("Rd", self.ra)
	
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


INSNS = [
	# Real instructions
	InsnADD, InsnSUB, InsnMLU, InsnMLS, InsnDVU, InsnDVS, InsnXOR, InsnAND,
	InsnORR, InsnSHL, InsnSRU, InsnSRS, InsnMOV, InsnCMP,
	InsnLDW, InsnSTW, InsnLDB, InsnSTB, InsnBRA, InsnBRR, InsnFCA, InsnFCR,
	InsnRDB, InsnWRB, InsnPSH, InsnPOP, InsnINC, InsnBPT, InsnHLT, InsnNOP,
	
	# Pseudo instructions
	InsnRET, InsnDEC, InsnNEG, InsnINV, InsnADR, InsnSWP, InsnADC, InsnSBC
]

INSN_MAP = {
	x.__name__[4:]: x for x in INSNS
}
