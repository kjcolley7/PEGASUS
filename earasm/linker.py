from typing import Any
from .pegasus import *

class Linker(object):
	def __init__(self, layout: dict[str, Any]):
		self.layout: dict[str, Any] = layout
		self.symtab: SymbolTable = SymbolTable()
		self.reltab: RelocTable = RelocTable()
		self.segments: list[Segment] = []
		self.segmap: dict[str, Segment] = {}
		self.entrypoints: list[Entrypoint] = []
		
		for desc in layout["segments"]:
			seg = Segment(**desc)
			self.segments.append(seg)
			self.segmap[seg.name] = seg
	
	def precompute_header_size(self) -> int:
		fakepeg = Pegasus()
		
		for seg in self.segments:
			seg.emit(fakepeg)
		
		self.symtab.emit(fakepeg)
		
		if "entrypoints" in self.layout:
			fakeentry = Entrypoint()
			fakeentry.emit(fakepeg)
		
		return fakepeg.size_of_header_and_cmds
	
	def add_segment(self, name: str, vmaddr: int, vmsize: int, data: bytes):
		assert vmaddr & PAGE_MASK == 0, "vmaddr must be page-aligned"
		
		seg = self.segmap[name]
		seg.vpage = vmaddr // PAGE_SIZE
		seg.contents = data
		seg.vmsize = vmsize
	
	def add_entrypoint(self, **regs: dict[str, int]):
		try:
			if "SP" not in regs:
				stack = self.segmap["@STACK"]
				assert stack.vmsize is not None, "Stack segment must have a fixed size"
				sp = stack.vpage * PAGE_SIZE + stack.vmsize
				sp -= 2
				regs["SP"] = sp
				if "FP" not in regs:
					regs["FP"] = sp
		except Exception:
			pass
		entry = Entrypoint(**regs)
		self.entrypoints.append(entry)
	
	def add_symbol(self, name, value):
		sym = Pegasus_Symbol(name, value)
		self.symtab.add(sym)
	
	def add_relocation(self, name, fileobj, offset=0):
		sym = [x for x in self.symtab.syms if x.name == name][0]
		reloc = Pegasus_Relocation(sym, fileobj, offset)
		self.reltab.add(reloc)
	
	def link_binary(self) -> bytes:
		peg = Pegasus()
		
		for seg in self.segments:
			seg.emit(peg)
		
		self.symtab.emit(peg)
		
		for entry in self.entrypoints:
			entry.emit(peg)
		
		return peg.data()
