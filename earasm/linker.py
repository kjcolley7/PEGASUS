from .pegasus import *

class Linker(object):
	def __init__(self, layout):
		self.symtab = SymbolTable()
		self.reltab = RelocTable()
		self.segments = []
		self.segmap = {}
		self.entrypoints = []
		
		for desc in layout:
			seg = Segment(**desc)
			self.segments.append(seg)
			self.segmap[seg.name] = seg
	
	def add_segment(self, name, vmaddr, data):
		seg = self.segmap[name]
		seg.vmaddr = vmaddr
		seg.add(data)
	
	def add_entrypoint(self, entrypoint):
		self.entrypoints.append(Entrypoint(PC=entrypoint))
	
	def add_symbol(self, name, value):
		sym = Symbol(name, value)
		self.symtab.add(sym)
	
	def add_relocation(self, name, fileobj, offset=0):
		sym = [x for x in self.symtab.syms if x.name == name][0]
		reloc = Relocation(sym, fileobj, offset)
		self.reltab.add(reloc)
	
	def link_binary(self):
		peg = Pegasus()
		
		for seg in self.segments:
			if seg.vmsize:
				seg.emit(peg)
		
		self.symtab.emit(peg)
		
		for entry in self.entrypoints:
			entry.emit(peg)
		
		return peg.data()
