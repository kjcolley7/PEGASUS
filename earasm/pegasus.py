from .geometry import *
from .isa import *

EAR_PROT_READ = 1 << 0
EAR_PROT_WRITE = 1 << 1
EAR_PROT_EXECUTE = 1 << 2

def decode_prot(prot):
	if not isinstance(prot, str):
		return prot
	
	ret = 0
	for c in prot:
		ret |= 1 << "rwx".index(c.lower())
	return ret


class Pegasus_Filepart(object):
	def __init__(self, foff, obj):
		self.foff = foff
		self.obj = obj
		try:
			self.obj.got_filepart_foff(self.foff)
		except AttributeError:
			pass
	
	def __len__(self):
		return len(self.data())
	
	def data(self):
		return self.obj.data()

class Pegasus_CmdHeader(object):
	"""
	struct Pegasus_CmdHeader {
		uint16_t cmd_type;
		uint16_t cmd_size;
	};
	"""
	
	def __init__(self, cmdtype, cmddata):
		self.cmdtype = cmdtype
		self.cmdsize = 2 + 2 + len(cmddata)
		self.cmddata = cmddata
	
	def data(self):
		return p16(self.cmdtype) + p16(self.cmdsize) + self.cmddata
	
	def __len__(self):
		return self.cmdsize


class Pegasus_Cmd(object):
	def cmd(self):
		raise NotImplemented("This method must be overridden")
	
	def fixup(self):
		pass
	
	def data(self):
		self.fixup()
		return self.cmd().data()
	
	def __len__(self):
		return len(self.data())


class Pegasus_Segment(Pegasus_Cmd):
	"""
	// cmd_type = 1
	struct Pegasus_Segment: Pegasus_Cmd {
		lestring name;
		uint8_t mem_vppn;
		uint8_t mem_vpage_count;
		uint16_t mem_foff;
		uint16_t mem_fsize;
		uint8_t mem_prot;
	};
	"""
	
	def __init__(self, name, vmaddr, vmsize, prot, foff=None, fsize=None, segment=None):
		assert (vmaddr & 0xFF) == 0
		assert (vmsize & 0xFF) == 0
		
		self.name = name
		self.vppn = vmaddr // 0x100
		self.vpage_count = vmsize // 0x100
		self.prot = decode_prot(prot)
		self.foff = foff
		self.fsize = fsize
		self.segment = segment
	
	def fixup(self):
		self.vmsize = self.segment.vmsize
		self.foff = self.segment.foff
		self.fsize = len(self.segment.contents)
	
	def cmd(self):
		data = []
		data.append(pack_lestring(self.name))
		data.append(p8(self.vppn))
		data.append(p8(self.vpage_count))
		data.append(p16(self.foff))
		data.append(p16(self.fsize))
		data.append(p8(self.prot))
		return Pegasus_CmdHeader(1, b"".join(data))


class Pegasus_Entrypoint(Pegasus_Cmd):
	"""
	// cmd_type = 2
	struct Pegasus_Entrypoint: Pegasus_Cmd {
		uint16_t RV, R3, R4, R5, R6, R7, PC, DPC;
	};
	"""
	
	REGISTERS = ["R2", "R3", "R4", "R5", "R6", "R7", "R14", "R15"]
	REGMAP = {name: index for index, name in enumerate(REGISTERS)}
	
	# Add aliases for RV, PC, DPC
	REGMAP["RV"] = REGMAP["R2"]
	REGMAP["PC"] = REGMAP["R14"]
	REGMAP["DPC"] = REGMAP["R15"]
	
	def __init__(self, **kwargs):
		self.registers = [0] * len(self.REGISTERS)
		self.set_regs(R7=0xEA2A)
		self.set_regs(**kwargs)
		self.foff = None
	
	def set_regs(self, **kwargs):
		for k, v in kwargs.items():
			self.registers[self.REGMAP[k]] = v
	
	def got_filepart_foff(self, foff):
		# Skip cmd header
		self.foff = foff + 2 + 2
	
	def cmd(self):
		regvals = []
		for x in self.registers:
			if isinstance(x, Symbol):
				x.fixup()
				x = x.value
			regvals.append(x)
		
		return Pegasus_CmdHeader(2, b"".join(map(p16, regvals)))


class Symbol(object):
	"""
	struct Symbol {
		lestring name;
		uint16_t value;
	};
	"""
	
	def __init__(self, name, value=0xFFFF, segment=None, segoffset=None):
		self.name = name
		self.value = value
		self.segment = segment
		self.segoffset = segoffset
		self.index = None
	
	def __len__(self):
		return 2 + max((len(self.name), 1))
	
	def fixup(self):
		if self.segment is not None:
			self.value = self.segment.vmaddr + self.segoffset
	
	def data(self):
		data = []
		data.append(pack_lestring(self.name))
		data.append(p16(self.value))
		return b"".join(data)


class Pegasus_SymbolTable(Pegasus_Cmd):
	"""
	// cmd_type = 3
	struct Pegasus_SymbolTable: Pegasus_Cmd {
		uint16_t sym_count;
		Symbol syms[sym_count];
	};
	"""
	
	def __init__(self, syms=None):
		self.syms = syms or []
	
	def add(self, sym):
		sym.index = len(self.syms)
		self.syms.append(sym)
	
	def fixup(self):
		for sym in self.syms:
			sym.fixup()
	
	def cmd(self):
		data = []
		data.append(p16(len(self.syms)))
		data.extend(x.data() for x in self.syms)
		return Pegasus_CmdHeader(3, b"".join(data))


class Relocation(object):
	"""
	struct Relocation {
		uint16_t symbol_index;
		uint16_t fileoff;
	};
	"""
	
	def __init__(self, sym, fileobj, offset=0):
		self.sym = sym
		self.fileobj = fileobj
		self.offset = offset
	
	def __len__(self):
		return 2 + 2
	
	def data(self):
		if self.sym.index == -1:
			raise ValueError("Symbol %s hasn't been assigned an index" % (self.sym.name,))
		
		data = []
		data.append(p16(self.sym.index))
		data.append(p16(self.fileobj.foff + offset))
		return b"".join(data)

class Pegasus_RelocTable(Pegasus_Cmd):
	"""
	// cmd_type = 4
	struct Pegasus_RelocTable: PegasusCmd {
		uint16_t reloc_count;
		Relocation relocs[reloc_count];
	};
	"""
	
	def __init__(self, relocs=None):
		self.relocs = relocs or []
	
	def add(self, reloc):
		self.relocs.append(reloc)
	
	def cmd(self):
		data = []
		data.append(p16(len(self.relocs)))
		data.append(map(str, self.relocs))
		return Pegasus_CmdHeader(4, b"".join(data))


class Pegasus(object):
	"""
	struct Pegasus {
		char magic[8] = "\x7fPEGASUS";
		uint32_t arch = '_EAR';
		uint16_t cmd_count;
		cmds...
		segments...
	};
	"""
	
	def __init__(self, arch=b"_EAR"):
		self.magic = b"\x7fPEGASUS"
		self.arch = arch
		self.cmds = []
		self.segments = []
		self.size_of_header_and_cmds = 8 + 4 + 2
		self.dirty = True
	
	def add_cmd(self, cmd):
		self.dirty = True
		part = Pegasus_Filepart(self.size_of_header_and_cmds, cmd)
		self.cmds.append(part)
		self.size_of_header_and_cmds += len(part)
	
	def add_segment(self, seg):
		self.dirty = True
		self.segments.append(seg)
	
	def layout(self):
		if not self.dirty:
			return
		
		# Find the file offset after the end of the header and load commands
		foff = self.size_of_header_and_cmds
		
		# Assign each segment object a file offset
		for seg in self.segments:
			seg.foff = foff
			foff += len(seg.contents)
		
		self.dirty = False
	
	def data(self):
		self.layout()
		
		data = []
		
		# Serialize header
		data.append(self.magic)
		data.append(self.arch)
		data.append(p16(len(self.cmds)))
		
		# Serialize load commands
		for cmd in self.cmds:
			data.append(cmd.data())
		
		# Serialize segments
		for seg in self.segments:
			data.append(list2bytes(seg.contents))
		data.append(b"\xEA\x2A")
		
		return b"".join(data)


class Segment(object):
	def __init__(self, name, prot, vmaddr=None, vmsize=0, foff=0):
		self.name = name
		self.prot = decode_prot(prot)
		self.vmaddr = vmaddr
		self.vmsize = vmsize
		self.foff = foff
		self.contents = []
	
	def add(self, data, name=None):
		segoffset = len(self.contents)
		self.contents.extend(data)
		
		self.vmsize = (len(self.contents) + 0x100 - 1) & ~0xff
		
		if name:
			return Symbol(name, segment=self, segoffset=segoffset)
	
	def emit(self, pegasus):
		segcmd = Pegasus_Segment(self.name, self.vmaddr, self.vmsize, self.prot, segment=self)
		pegasus.add_segment(self)
		pegasus.add_cmd(segcmd)
	
	def data(self):
		return list2bytes(self.contents)


class SymbolTable(object):
	def __init__(self, syms=None):
		self.syms = syms or []
	
	def add(self, sym):
		self.syms.append(sym)
	
	def emit(self, pegasus):
		symtab = Pegasus_SymbolTable(self.syms)
		pegasus.add_cmd(symtab)


class Entrypoint(object):
	def __init__(self, **kwargs):
		self.entry = Pegasus_Entrypoint(**kwargs)
	
	def emit(self, pegasus):
		pegasus.add_cmd(self.entry)


class RelocTable(object):
	def __init__(self, relocs=None):
		self.reltab = Pegasus_RelocTable(relocs)
	
	def add(self, reloc):
		self.reltab.add(reloc)
	
	def emit(self, pegasus):
		pegasus.add_cmd(self.reltab)
