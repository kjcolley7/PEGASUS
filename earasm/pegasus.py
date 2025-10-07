from abc import ABC, abstractmethod
from .geometry import *
from .isa import *
from .debug_print import debug_print

from typing import Optional, TypeAlias, Union

RegValue: TypeAlias = Union[int, 'Pegasus_Symbol']

EAR_PROT_READ = 1 << 0
EAR_PROT_WRITE = 1 << 1
EAR_PROT_EXECUTE = 1 << 2

def decode_prot(prot: str | int) -> int:
	if not isinstance(prot, str):
		return prot
	
	ret = 0
	for c in prot:
		ret |= 1 << "rwx".index(c.lower())
	return ret


class Pegasus_CmdHeader(object):
	"""
	struct Pegasus_CmdHeader {
		uint16_t cmd_size;
		uint16_t cmd_type;
	};
	"""
	
	def __init__(self, cmdtype: int, cmddata: bytes):
		self.cmdsize = 2 + 2 + len(cmddata)
		if self.cmdsize & 1:
			self.cmdsize += 1
		self.cmdtype = cmdtype
		self.cmddata = cmddata
	
	def data(self) -> bytes:
		x = p16(self.cmdsize) + p16(self.cmdtype) + self.cmddata
		if len(x) & 1:
			x += b"\xEA"
		return x
	
	def __len__(self):
		return self.cmdsize


class Pegasus_Cmd(ABC):
	@abstractmethod
	def cmd(self) -> Pegasus_CmdHeader:
		...
	
	def fixup(self):
		pass
	
	def set_foff(self, foff: int) -> None:
		pass
	
	def data(self) -> bytes:
		self.fixup()
		return self.cmd().data()
	
	def __len__(self):
		return len(self.data())


class Pegasus_Segment(Pegasus_Cmd):
	"""
	// cmd_type = 1
	struct Pegasus_Segment: Pegasus_Cmd {
		uint8_t virtual_page;           // Virtual page number where the segment should be mapped
		uint8_t file_page;              // Page index within the file where the segment data is found
		uint8_t present_page_count;     // Number of pages to map from the file
		uint8_t absent_page_count;      // Number of extra RAM pages to map
		EAR_Protection prot;            // Bitmask of memory protections to apply
		lestring name;                  // Name of the segment such as @TEXT
	};
	"""
	
	def __init__(
			self,
			virtual_page: int,
			file_page: int,
			present_page_count: int,
			absent_page_count: int,
			prot: str | int,
			name: str
	):
		self.virtual_page: int = virtual_page
		self.file_page: Optional[int] = file_page
		self.present_page_count: int = present_page_count
		self.absent_page_count: int = absent_page_count
		self.prot: int = decode_prot(prot)
		self.name: str = name
	
	@staticmethod
	def cmdlen(name: str) -> int:
		x = 1 + 1 + 1 + 1 + 1 + min(len(name), 1)
		if x & 1:
			x += 1
		return x
	
	def cmd(self) -> Pegasus_CmdHeader:
		data = p8(self.virtual_page)
		data += p8(self.file_page)
		data += p8(self.present_page_count)
		data += p8(self.absent_page_count)
		data += p8(self.prot)
		data += pack_lestring(self.name)
		return Pegasus_CmdHeader(1, data)


class Pegasus_Entrypoint(Pegasus_Cmd):
	"""
	// cmd_type = 2
	struct Pegasus_Entrypoint: Pegasus_Cmd {
		uint16_t A0, A1, A2, A3, A4, A5, S0, S1, S2, FP, SP, RA, RD, PC, DPC;
	};
	"""
	
	REGISTERS = [
		# Exclude "ZERO"
		"A0", "A1", "A2", "A3", "A4", "A5",
		"S0", "S1", "S2",
		"FP", "SP",
		"RA", "RD",
		"PC", "DPC"
	]
	REGMAP = {}
	for name, num in REGISTER_NUMBERS.items():
		reg = REGISTER_NAMES[num]
		try:
			REGMAP[name] = REGISTERS.index(reg)
		except ValueError:
			pass
	
	def __init__(self, **kwargs: dict[str, RegValue]):
		self.registers: list[RegValue] = [0] * len(self.REGISTERS)
		self.set_regs(A5=0xEA23)
		self.set_regs(RA=0xFF00)
		self.set_regs(**kwargs)
		self.foff: Optional[int] = None
	
	def set_regs(self, **kwargs: dict[str, RegValue]):
		for k, v in kwargs.items():
			self.registers[self.REGMAP[k]] = v
	
	def set_foff(self, foff: int) -> None:
		# Skip cmd header
		self.foff = foff + 2 + 2
	
	def cmd(self):
		regvals: list[int] = []
		for x in self.registers:
			if isinstance(x, Pegasus_Symbol):
				x.fixup()
				x = x.value
			regvals.append(x)
		
		return Pegasus_CmdHeader(2, b"".join(map(p16, regvals)))


class Pegasus_Symbol(object):
	"""
	struct Pegasus_Symbol {
		uint16_t value;
		lestring name;
	};
	"""
	
	def __init__(self, name: str, value: int=0xFFFF, segment: Optional['Segment']=None, segoffset: Optional[int]=None):
		self.name = name
		self.value = value
		self.segment = segment
		self.segoffset = segoffset
		self.index: Optional[int] = None
	
	def __len__(self):
		return 2 + max((len(self.name), 1))
	
	def fixup(self):
		if self.segment is not None:
			self.value = self.segment.vpage * PAGE_SIZE + self.segoffset
	
	def data(self) -> bytes:
		data = p16(self.value)
		data += pack_lestring(self.name)
		if len(data) & 1:
			data += b"\xE3"
		return data


class Pegasus_SymbolTable(Pegasus_Cmd):
	"""
	// cmd_type = 3
	struct Pegasus_SymbolTable: Pegasus_Cmd {
		uint16_t sym_count;
		Pegasus_Symbol syms[sym_count];
	};
	"""
	
	def __init__(self, syms: Optional[list[Pegasus_Symbol]]=None):
		self.syms: list[Pegasus_Symbol] = syms or []
	
	def add(self, sym: Pegasus_Symbol):
		sym.index = len(self.syms)
		self.syms.append(sym)
	
	def fixup(self):
		for sym in self.syms:
			sym.fixup()
	
	def cmd(self) -> Pegasus_CmdHeader:
		data = p16(len(self.syms))
		for x in self.syms:
			data += x.data()
		return Pegasus_CmdHeader(3, data)


class Pegasus_Relocation(object):
	"""
	struct Pegasus_Relocation {
		uint16_t symbol_index;
		uint16_t fileoff;
	};
	"""
	
	def __init__(self, sym: Pegasus_Symbol, fileobj, offset: int=0):
		self.sym: Pegasus_Symbol = sym
		self.fileobj = fileobj
		self.offset: int = offset
	
	def __len__(self):
		return 2 + 2
	
	def data(self) -> bytes:
		if self.sym.index == -1:
			raise ValueError("Symbol %s hasn't been assigned an index" % (self.sym.name,))
		
		data = p16(self.sym.index)
		data += p16(self.fileobj.foff + offset)
		return data

class Pegasus_RelocTable(Pegasus_Cmd):
	"""
	// cmd_type = 4
	struct Pegasus_RelocTable: PegasusCmd {
		uint16_t reloc_count;
		Pegasus_Relocation relocs[reloc_count];
	};
	"""
	
	def __init__(self, relocs: Optional[list[Pegasus_Relocation]]=None):
		self.relocs: list[Pegasus_Relocation] = relocs or []
	
	def add(self, reloc: Pegasus_Relocation):
		self.relocs.append(reloc)
	
	def cmd(self) -> Pegasus_CmdHeader:
		data = p16(len(self.relocs))
		for reloc in self.relocs:
			data += reloc.data()
		return Pegasus_CmdHeader(4, data)


class Pegasus(object):
	"""
	struct Pegasus {
		char magic[8] = "\xe4PEGASUS";
		uint32_t arch = 'EAR3';
		uint16_t cmd_count;
		cmds...
		segments...
	};
	"""
	
	def __init__(self, arch=b"EAR3"):
		assert len(arch) == 4
		self.magic: bytes = b"\xe4PEGASUS"
		self.arch: bytes = arch
		self.dirty: bool = True
		self.header: Optional['Segment'] = None
		self.cmds: list[Pegasus_Cmd] = []
		self.segments: list['Segment'] = []
		self.size_of_header_and_cmds: int = 8 + 4 + 2
	
	def add_cmd(self, cmd: Pegasus_Cmd):
		self.dirty = True
		self.cmds.append(cmd)
		self.size_of_header_and_cmds += len(cmd)
	
	def add_segment(self, seg: 'Segment'):
		if seg.is_header:
			assert self.header is None, "Only one header segment allowed"
			assert len(self.segments) == 0, "Header segment must be first"
			
			if seg.should_emit:
				assert seg.vpage is not None, "Header segment must have a virtual page"
			self.header = seg
		
		self.dirty = True
		self.segments.append(seg)
		
		if seg.should_emit:
			self.size_of_header_and_cmds += Pegasus_Segment.cmdlen(seg.name)
	
	def layout(self):
		if not self.dirty:
			return
		self.dirty = False
		
		assert self.header is not None, "Header segment must be defined"
		
		# Build PEGASUS header
		self.header.contents = b""
		self.header.vmsize = self.size_of_header_and_cmds
		debug_print(f"layout: Header segment vmsize set to {hex(self.header.vmsize)}")
		
		# Emit magic and architecture
		self.header.add(self.magic)
		self.header.add(self.arch)
		
		# Emit segment count, adding room for the header's segment command
		cmd_count = len(self.cmds) + sum(seg.should_emit for seg in self.segments)
		self.header.add(p16(cmd_count))
		
		segcmds: list[Pegasus_Segment] = []
		foff = 0
		
		# Assign each segment object a file offset
		for seg in self.segments:
			# Segments must be page-aligned
			foff = page_ceil(foff)
			seg.file_page = foff // PAGE_SIZE
			foff += len(seg)
			
			if not seg.should_emit:
				debug_print(f"layout: Skipping segment {seg.name} in header")
				continue
			
			# Trailing zero-filled pages can be omitted
			total_pages = page_ceil(len(seg)) // PAGE_SIZE
			trimmed_len = len(seg.contents.rstrip(b"\x00"))
			file_pages = page_ceil(trimmed_len) // PAGE_SIZE
			absent_pages = total_pages - file_pages
			
			# Header segment will be filled in later
			if seg.is_header:
				file_pages += absent_pages
				absent_pages = 0
			
			# Build segment command
			debug_print(f"layout: Emitting segment {seg.name} with len {len(seg)}, trimmed_len {trimmed_len}, present {file_pages}, absent {absent_pages}, prot {bin(seg.prot)}")
			segcmd = Pegasus_Segment(
				virtual_page=seg.vpage,
				file_page=seg.file_page,
				present_page_count=file_pages,
				absent_page_count=absent_pages,
				prot=seg.prot,
				name=seg.name
			)
			segcmds.append(segcmd)
		
		# Emit all load commands
		for cmd in segcmds + self.cmds:
			cmd.set_foff(len(self.header))
			self.header.add(cmd.data())
	
	def data(self) -> bytes:
		self.layout()
		
		data: bytes = b""
		
		# Serialize segments, including the header
		for seg in self.segments:
			if not seg.is_header and not seg.should_emit:
				debug_print(f"data: Skipping segment {seg.name}")
				continue
			
			segdata = seg.contents.rstrip(b"\x00")
			if len(segdata) == 0:
				debug_print(f"data: Skipping empty segment {seg.name}")
				continue
			
			debug_print(f"data: Emitting segment {seg.name} with vmsize {hex(seg.vmsize) if seg.vmsize is not None else "None"}, fsize {hex(len(segdata))}")
			pegsize = len(data)
			data += b"\x00" * (page_ceil(pegsize) - pegsize)
			data += segdata
		
		return data


class Segment(object):
	def __init__(
			self,
			name: str,
			prot: str | int,
			vmaddr: Optional[int | str]=None,
			vmsize: Optional[int]=None,
			emit: bool=True,
			header: bool=False,
			sections: list[str]=[]
	):
		self.name: str = name
		self.prot: int = decode_prot(prot)
		self.vpage: Optional[int] = None
		if vmaddr is not None:
			if isinstance(vmaddr, str):
				vmaddr = int(vmaddr, 0)
			assert vmaddr & PAGE_MASK == 0, "vmaddr must be page-aligned"
			self.vpage = vmaddr // PAGE_SIZE
		self.file_page: Optional[int] = None
		self.contents: bytes = b""
		self.vmsize: Optional[int] = vmsize
		self.should_emit: bool = emit
		self.is_header: bool = header
		self.sections: list[str] = sections
	
	def add(self, data: bytes, name: Optional[str]=None) -> Optional[Pegasus_Symbol]:
		segoffset = len(self.contents)
		self.contents += data
		
		if name:
			return Pegasus_Symbol(name, segment=self, segoffset=segoffset)
	
	def emit(self, pegasus: Pegasus):
		if not self.is_header:
			if not self.should_emit:
				return
		
		pegasus.add_segment(self)
	
	def __len__(self):
		if self.vmsize is not None:
			return self.vmsize
		
		return len(self.contents.rstrip(b"\x00"))
	
	def data(self) -> bytes:
		return self.contents


class SymbolTable(object):
	def __init__(self, syms: Optional[list[Pegasus_Symbol]]=None):
		self.syms: list[Pegasus_Symbol] = syms or []
	
	def add(self, sym: Pegasus_Symbol):
		self.syms.append(sym)
	
	def emit(self, pegasus: Pegasus):
		# Don't add an empty symbol table
		if not self.syms:
			return
		
		symtab = Pegasus_SymbolTable(self.syms)
		pegasus.add_cmd(symtab)


class Entrypoint(object):
	def __init__(self, **kwargs):
		self.entry: Pegasus_Entrypoint = Pegasus_Entrypoint(**kwargs)
	
	def emit(self, pegasus: Pegasus):
		pegasus.add_cmd(self.entry)


class RelocTable(object):
	def __init__(self, relocs: Optional[list[Pegasus_Relocation]]=None):
		self.reltab = Pegasus_RelocTable(relocs)
	
	def add(self, reloc: Pegasus_Relocation):
		self.reltab.add(reloc)
	
	def emit(self, pegasus: Pegasus):
		pegasus.add_cmd(self.reltab)
