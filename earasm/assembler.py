import os
import sys
from typing import Any, Optional, TypeAlias, Generator, TextIO
from .parse import parser
from .lex import lexer
from .ast import *
from .insns import *
from .geometry import *
from .layout import DEFAULT_LAYOUT


AsmItem: TypeAlias = Instruction | Directive

class Context(object):
	def __init__(
			self,
			name: str,
			prot="",
			vmaddr: Optional[int | str]=None,
			vmsize: int=0,
			emit: bool=True,
			header: bool=False,
			sections: list[str]=[]
	):
		assert(name.startswith("@"))
		self.name: str = name
		self.prot: str = prot
		self.vmaddr: Optional[int]
		if isinstance(vmaddr, str):
			self.vmaddr = int(vmaddr, 0)
		else:
			self.vmaddr = vmaddr
		self.vmsize: int = vmsize
		self.should_emit: bool = emit
		self.is_header: bool = header
		self.sections: list[str] = sections
		
		self.asmlist: list[AsmItem] = []
		self.auto_symbols: dict[str, Symbol] = {}
		self.global_symbols: dict[str, Symbol] = {}
		self.scopes: list[dict[str, Label]] = []
		self.exports: list[tuple[str, SymbolExpr]] = []
		self.resolver = None
	
	def prepare(self, nextaddr):
		self.vmaddr = self.vmaddr or page_ceil(nextaddr)
		self.max_addr = self.vmaddr
		self.new_scope()
		self.here_label = Label("@")
		self.pc_label = Label("@PC@")
		self.dpc_label = Label("@DPC@")
		self.after_label = Label("@AFTER@")
		self.end_label = Label("@END@")
		self.seg_end_label = Label(self.name + ".END@")
		self.set_loc(self.vmaddr, 0)
		self.after_label.value = self.here
		self.after_label.calldpc = 0
		self.add_auto_label(self.here_label)
		self.add_auto_label(self.pc_label)
		self.add_auto_label(self.dpc_label)
		self.add_auto_label(self.after_label)
		self.add_auto_label(self.end_label)
		self.add_global_label(self.seg_end_label)
	
	def add_asm_item(self, asmitem: AsmItem):
		if isinstance(asmitem, DotExport):
			# Handle .export directives immediately
			if asmitem.exported_name is None:
				if self.is_local_name(asmitem.label.name):
					raise TypeError("Cannot export a local label: %r" % asmitem)
				elif self.is_special_name(asmitem.label.name):
					raise TypeError("Cannot export special label: %r" % asmitem)
			self.add_export(asmitem.exported_name or asmitem.label.name, asmitem.label)
		else:
			self.asmlist.append(asmitem)
	
	def new_scope(self):
		self.local_symbols = {}
		self.scopes.append(self.local_symbols)
		self.scope_number = len(self.scopes) - 1
	
	def next_scope(self):
		self.scope_number += 1
		self.local_symbols = self.scopes[self.scope_number]
	
	def set_loc(self, here: int, dpc: Optional[int]=None):
		self.here = here
		if dpc is not None:
			self.dpc = dpc
		self.here_label.value = self.here
		self.here_label.calldpc = self.dpc
		self.pc_label.value = None
		self.pc_label.calldpc = self.dpc
		self.dpc_label.value = self.dpc
		self.dpc_label.calldpc = self.dpc
		if here > self.max_addr:
			self.max_addr = here
	
	def set_item_len(self, curlen: int):
		self.pc = self.here + curlen * (1 + self.dpc)
		self.pc_label.value = self.pc
		if curlen > 0:
			bytes_till_end = 1 + (curlen - 1) * (1 + self.dpc)
			newend = self.here - self.vmaddr + bytes_till_end
			self.vmsize = max((self.vmsize, newend))
			self.after_label.value = self.here + bytes_till_end
	
	def advance(self, numbytes: Optional[int]=None):
		if numbytes is None:
			# PC is the address of the next instruction
			self.set_loc(self.pc, self.dpc)
		else:
			self.set_loc(self.here + numbytes * (self.dpc + 1))
	
	def is_auto_name(self, name: str) -> bool:
		return name in ["@", "@@", "@PC@", "@DPC@", "@AFTER@", "@END@"]
	
	def is_special_name(self, name: str) -> bool:
		return name.endswith("@") or name.endswith("$")
	
	def is_local_name(self, name: str) -> bool:
		return name.startswith("@.") or name.startswith("$.")
	
	def get_symbol(self, name: str) -> Symbol:
		if self.is_auto_name(name):
			sym = self.auto_symbols[name]
		elif self.is_local_name(name):
			sym = self.local_symbols[name]
		else:
			if name in self.global_symbols:
				sym = self.global_symbols[name]
			elif self.resolver is not None:
				sym = self.resolver.resolve(name)
			else:
				raise NameError("No symbol found with name: %r" % name)
		return sym
	
	def get_value(self, name: str) -> int:
		if name.endswith(".DPC@"):
			return self.get_dpc_value(name[:-5])
		
		sym = self.get_symbol(name)
		if sym.value is None and isinstance(sym, Equate):
			sym.value = sym.expr.value(self)
		
		assert sym.value is not None
		return sym.value
	
	def get_dpc_value(self, name: str) -> int:
		lbl = self.get_symbol(name)
		
		assert isinstance(lbl, Label)
		assert lbl.calldpc is not None
		return lbl.calldpc
	
	def add_auto_label(self, label: Label):
		self.add_symbol_internal(self.auto_symbols, label.name, label)
	
	def add_global_label(self, label: Label):
		self.add_symbol_internal(self.global_symbols, label.name, label)
		if self.resolver is not None:
			self.resolver.add_global_symbol(label)
	
	def add_symbol_internal(self, symbolmap: dict[str, Symbol], name: str, symbol: Symbol):
		symbolmap[name] = symbol
	
	def add_symbol(self, symbol: Symbol, internal: bool = False):
		# Assign the symbol its value (and call DPC, for labels)
		if isinstance(symbol, Label):
			symbol.value = self.here
			symbol.calldpc = self.dpc
		else:
			assert isinstance(symbol, Equate)
			# Leave value unset, will be evaluated lazily
		
		# Store the symbol into its relevant dict
		if (self.is_auto_name(symbol.name) or self.is_special_name(symbol.name)) and not internal:
			raise TypeError("Cannot redefine special label: %r" % symbol)
		elif self.is_auto_name(symbol.name):
			symbolmap = self.auto_symbols
		elif self.is_local_name(symbol.name):
			symbolmap = self.local_symbols
		else:
			symbolmap = self.global_symbols
			if self.resolver is not None:
				self.resolver.add_global_symbol(symbol)
		
		# Ensure this won't overwrite an existing symbol
		if symbol.name in symbolmap:
			raise NameError("Cannot redefine existing symbol: %r" % symbol)
		
		# Actually insert the symbol into its corresponding dict
		self.add_symbol_internal(symbolmap, symbol.name, symbol)
	
	def add_export(self, name: str, label: SymbolExpr):
		self.exports.append((name, label))
	
	def get_exports(self) -> Generator[tuple[str, int], None, None]:
		return ((name, label.value(self)) for name, label in self.exports)
	
	def rewind(self):
		self.scope_number = -1
		self.next_scope()
		self.set_loc(self.vmaddr)
		self.after_label.value = self.here
	
	def compute_internal_labels(self, nextaddr: int):
		outlist: list[AsmItem] = []
		
		# This assigns self.vmaddr a concrete value
		self.prepare(nextaddr)
		
		# Add segment start label and its alias
		lseg = Label("@@")
		lseg.value = self.vmaddr
		lseg.calldpc = 0
		self.add_auto_label(lseg)
		
		# Segment start label (full name). Example: "@TEXT@"
		lseg = Label(self.name + "@")
		lseg.value = self.vmaddr
		lseg.calldpc = 0
		self.add_global_label(lseg)
		
		# Process any assembly items that affect labels or the current location
		for item in self.asmlist:
			try:
				if isinstance(item, DotScope):
					# Handle .scope directives
					self.new_scope()
					outlist.append(item)
				elif isinstance(item, DotLoc):
					# Handle .loc directives
					new_pc = item.pc.value(self)
					new_dpc = None
					if item.dpc is not None:
						new_dpc = item.dpc.value(self)
					self.set_loc(new_pc, new_dpc)
					outlist.append(item)
				elif isinstance(item, DotAlign):
					# Handle .align directives
					align = item.align.value(self)
					new_pc = (self.here + align - 1) // align * align
					self.set_loc(new_pc)
					outlist.append(item)
				elif isinstance(item, DotAssert):
					# Will be handled during assemble()
					outlist.append(item)
				elif isinstance(item, DotSegment):
					raise RuntimeError(".segment directives should have already been handled!")
				elif isinstance(item, Symbol):
					# Handle symbol definitions
					self.add_symbol(item)
				else:
					# Instruction or data directive like .db/.dw/.lestring
					curlen = len(item)
					
					# Use this to compute the value of the @PC@ label
					self.set_item_len(curlen)
					
					# Compute address directly after where the last byte will be emitted
					nextaddr = self.pc + 1 + (curlen - 1) * (1 + self.dpc)
					
					# Advance current address to next PC
					self.advance()
					
					# These items are still needed in a later pass
					outlist.append(item)
			except Exception as e:
				sys.stderr.write(f"Assembler error: {e}\n")
				item.loc.show(sys.stderr)
				if os.environ.get("PYTEST", "0") == "1":
					raise
				sys.exit(1)
		
		# Add @END@ and @<segname>.END@ labels
		self.end_label.value = self.max_addr
		self.seg_end_label.value = self.max_addr
		
		# Remove the fully processed directives
		self.asmlist = outlist
		self.rewind()
		
		# Some segments (like @PEG) have a precomputed vmsize
		return max(nextaddr, self.vmaddr + self.vmsize)
	
	def set_symbol_resolver(self, resolver):
		self.resolver = resolver
	
	def assemble(self) -> bytes:
		data = [0] * self.vmsize
		for item in self.asmlist:
			if isinstance(item, DotScope):
				# Handle .scope directives
				self.next_scope()
			elif isinstance(item, DotLoc):
				# Handle .loc directives
				new_pc = item.pc.value(self)
				new_dpc = None
				if item.dpc is not None:
					new_dpc = item.dpc.value(self)
				self.set_loc(new_pc, new_dpc)
			elif isinstance(item, DotAlign):
				# Handle .align directives
				align = item.align.value(self)
				new_pc = (self.here + align - 1) // align * align
				self.set_loc(new_pc)
			elif isinstance(item, DotAssert):
				lhs = item.lhs.value(self)
				rhs = item.rhs.value(self)
				if item.cmp_op == "==":
					assert lhs == rhs, f"Assertion failed: {lhs} != {rhs}"
				elif item.cmp_op == "!=":
					assert lhs != rhs, f"Assertion failed: {lhs} == {rhs}"
				elif item.cmp_op == "<":
					assert lhs < rhs, f"Assertion failed: {lhs} >= {rhs}"
				elif item.cmp_op == "<=":
					assert lhs <= rhs, f"Assertion failed: {lhs} > {rhs}"
				elif item.cmp_op == ">":
					assert lhs > rhs, f"Assertion failed: {lhs} <= {rhs}"
				elif item.cmp_op == ">=":
					assert lhs >= rhs, f"Assertion failed: {lhs} < {rhs}"
				else:
					raise ValueError(f"Unknown comparison operator: {item.cmp_op}")
			else:
				# Instruction or data directive like .db/.dw/.lestring
				curlen = len(item)
				self.set_item_len(curlen)
				try:
					assembled = item.assemble(self)
					assert len(assembled) == curlen
				except Exception as e:
					sys.stderr.write(f"Assembler error: {e}\n")
					item.loc.show(sys.stderr)
					if os.environ.get("PYTEST", "0") == "1":
						raise
					sys.exit(1)
				start = self.here - self.vmaddr
				step = 1 + self.dpc
				end = self.here + curlen * step - self.vmaddr
				data[start:end:step] = assembled
				self.advance()
		
		return list2bytes(data).rstrip(b"\x00")


class Assembler(object):
	def __init__(
			self,
			layout: dict[str, Any]=DEFAULT_LAYOUT,
			search_paths: list[str]=[],
			dump_symbols: Optional[TextIO]=None):
		self.global_symbols: dict[str, Symbol] = {}
		self.segments: list[Context] = []
		self.segmap: dict[str, Context] = {}
		self.imported: set[str] = set()
		self.dump_symbols: Optional[TextIO] = dump_symbols
		self.search_paths: list[str] = search_paths
		
		for i, desc in enumerate(layout["segments"]):
			seg = Context(**desc)
			seg.set_symbol_resolver(self)
			self.segments.append(seg)
			self.segmap[seg.name] = seg
		
		self.default_segname: str = layout.get("default", self.segments[0].name)
	
	def search(self, filename: str, cwd: Optional[str]=None) -> str:
		relpath = filename
		if cwd is not None:
			relpath = os.path.join(cwd, filename)
		if os.path.isfile(relpath):
			return os.path.normpath(relpath)
		
		for path in self.search_paths:
			fullpath = os.path.join(path, filename)
			if os.path.isfile(fullpath):
				return os.path.normpath(fullpath)
		
		raise FileNotFoundError(f"Could not find import file in search path: {filename} (cwd={cwd}, search_paths={self.search_paths})")
	
	def add_input(self, asmstr: str, filename: Optional[str]=None):
		asmdir: Optional[str] = None
		if filename is not None:
			asmdir = os.path.dirname(filename)
			
			import_filename = os.path.abspath(filename)
			if import_filename in self.imported:
				return
			
			self.imported.add(import_filename)
		
		# Parse the input text into AST objects
		saved_line = lexer.lineno
		try:
			saved_filename = lexer.filename
		except AttributeError:
			saved_filename = None
		lexer.had_error = False
		lexer.lineno = 1
		lexer.filename = filename
		parser.filename = filename
		# if filename is not None:
		# 	print("Parsing %s" % filename)
		asmlist: list[AsmItem] = parser.parse(asmstr, tracking=True)
		if lexer.had_error:
			raise SyntaxError
		lexer.lineno = saved_line
		lexer.filename = saved_filename
		parser.filename = saved_filename
		
		# All imported assembly files start assembling into the default segment
		curseg = self.default_segname
		
		# Reset DPC to zero for each segment
		for seg in self.segments:
			# Add a fake ".loc @AFTER@, 0" line
			seg.add_asm_item(DotLoc(SymbolExpr("@AFTER@"), NumExpr(0)))
		
		# Add each assembly item like directives, instructions, symbols, and labels to
		# their corresponding segment
		for item in asmlist:
			if isinstance(item, DotSegment):
				# Switching to a different segment
				curseg = item.name
				if curseg not in self.segmap:
					raise KeyError("Segment %r not defined in LAYOUT" % curseg)
			elif isinstance(item, DotImport):
				# Importing another assembly file
				import_filename = self.search(item.filename, cwd=asmdir)
				if import_filename in self.imported:
					continue
				
				with open(import_filename, "r") as import_fp:
					new_asmstr = import_fp.read()
				
				self.add_input(new_asmstr, import_filename)
			else:
				# Adding another instruction or directive to an existing segment
				self.segmap[curseg].add_asm_item(item)
	
	def add_global_symbol(self, sym: Symbol):
		if sym.name in self.global_symbols:
			raise NameError("Cannot redefine symbol: %r" % sym)
		self.global_symbols[sym.name] = sym
		
		if (
			not self.dump_symbols
			or sym.name.endswith("@")
			or not isinstance(sym, Label)
		):
			return
		
		print(sym, file=self.dump_symbols)
	
	def resolve(self, name: str) -> Symbol:
		if name not in self.global_symbols:
			raise NameError("No symbol found with name %r" % name)
		return self.global_symbols[name]
	
	def get_export_names(self) -> list[str]:
		names: list[str] = []
		for seg in self.segments:
			names.extend([name for (name, _) in seg.exports])
		return names
	
	def get_exports(self) -> list[tuple[str, int]]:
		exports: list[tuple[str, int]] = []
		for seg in self.segments:
			exports.extend(seg.get_exports())
		return exports
	
	def assemble(self) -> list[tuple[str, int, int, bytes]]:
		nextaddr = 0
		segdata: list[tuple[str, int, int, bytes]] = []
		
		# Pass 1: Compute addresses, collect symbols, decide which labels should be exported
		for seg in self.segments:
			# Respect the segment's vmaddr if set
			if seg.vmaddr is not None:
				nextaddr = seg.vmaddr
			
			# Decide segment base, compute symbol values, and get address just after end of segment
			nextaddr = seg.compute_internal_labels(nextaddr)
			
			# Round up to address of start of next page
			nextaddr = page_ceil(nextaddr)
		
		# Pass 2: Compute values for constexprs, resolve symbol references, assemble instructions into bytes
		for seg in self.segments:
			segdata.append((seg.name, seg.vmaddr, seg.vmsize, seg.assemble()))
		
		return segdata
