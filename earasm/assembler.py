from .parse import parser
from .ast import *
from .insns import *
from .geometry import *
from .layout import LAYOUT


class Context(object):
	def __init__(self, name, prot="", vmaddr=None, vmsize=0):
		assert(name.startswith("@"))
		self.name = name
		self.prot = prot
		self.vmaddr = vmaddr
		self.vmsize = vmsize
		self.asmlist = []
		self.auto_labels = {}
		self.global_labels = {}
		self.scopes = []
		self.exports = []
		self.resolver = None
	
	def prepare(self, nextaddr):
		self.vmaddr = self.vmaddr or page_ceil(nextaddr)
		self.new_scope()
		self.here_label = Label("")
		self.pc_label = Label("PC@")
		self.dpc_label = Label("DPC@")
		self.after_label = Label("AFTER@")
		self.set_loc(self.vmaddr, 0)
		self.after_label.value = self.here
		self.after_label.calldpc = 0
		self.add_auto_label(self.here_label)
		self.add_auto_label(self.pc_label)
		self.add_auto_label(self.dpc_label)
		self.add_auto_label(self.after_label)
	
	def add_asm_item(self, asmitem):
		self.asmlist.append(asmitem)
	
	def new_scope(self):
		self.local_labels = {}
		self.scopes.append(self.local_labels)
		self.scope_number = len(self.scopes) - 1
	
	def next_scope(self):
		self.scope_number += 1
		self.local_labels = self.scopes[self.scope_number]
	
	def set_loc(self, here, dpc=None):
		self.here = here
		if dpc is not None:
			self.dpc = dpc
		self.here_label.value = self.here
		self.here_label.calldpc = self.dpc
		self.pc_label.value = None
		self.pc_label.calldpc = self.dpc
		self.dpc_label.value = self.dpc
		self.dpc_label.calldpc = self.dpc
	
	def set_item_len(self, curlen):
		self.pc = self.here + curlen * (1 + self.dpc)
		self.pc_label.value = self.pc
		if curlen > 0:
			bytes_till_end = 1 + (curlen - 1) * (1 + self.dpc)
			newend = self.here - self.vmaddr + bytes_till_end
			self.vmsize = max((self.vmsize, newend))
			self.after_label.value = self.here + bytes_till_end
	
	def advance(self, numbytes=None):
		if numbytes is None:
			# PC is the address of the next instruction
			self.set_loc(self.pc, self.dpc)
		else:
			self.set_loc(self.here + numbytes * (self.dpc + 1))
	
	def is_auto_name(self, name):
		return name in ["", "@", "PC@", "DPC@", "AFTER@"]
	
	def is_special_name(self, name):
		return name == "" or name.endswith("@")
	
	def is_local_name(self, name):
		return name.startswith(".")
	
	def get_label(self, name):
		if self.is_auto_name(name):
			label = self.auto_labels[name]
		elif self.is_local_name(name):
			# Skip the dot
			label = self.local_labels[name[1:]]
		else:
			if name in self.global_labels:
				label = self.global_labels[name]
			elif self.resolver is not None:
				label = self.resolver.resolve(name)
			else:
				raise NameError("No label found with name: %r" % name)
		return label
	
	def add_auto_label(self, label):
		self.add_label_internal(self.auto_labels, label.name, label)
	
	def add_label_internal(self, labelmap, name, label):
		labelmap[name] = label
	
	def add_label(self, label):
		# Assign the label its address and call DPC
		label.value = self.here
		label.calldpc = self.dpc
		
		# Store the label into its relevant dict
		if self.is_auto_name(label.name) or self.is_special_name(label.name):
			raise TypeError("Cannot redefine special label: %r" % label)
		elif self.is_local_name(label.name):
			labelmap = self.local_labels
			
			# Skip the dot
			name = label.name[1:]
		else:
			labelmap = self.global_labels
			name = label.name
		
		# Ensure this won't overwrite an existing label
		if name in labelmap:
			raise NameError("Cannot redefine existing label: %r" % label)
		
		# Actually insert the label into its corresponding dict
		self.add_label_internal(labelmap, name, label)
	
	def add_export(self, name, label):
		self.exports.append((name, label))
	
	def get_exports(self):
		return ((name, label.value(self)) for name, label in self.exports)
	
	def rewind(self):
		self.scope_number = -1
		self.next_scope()
		self.set_loc(self.vmaddr)
		self.after_label.value = self.here
	
	def compute_internal_labels(self, nextaddr):
		outlist = []
		
		# This assigns self.vmaddr a concrete value
		self.prepare(nextaddr)
		
		# Add segment start label and its alias
		lseg = Label("@")
		lseg.value = self.vmaddr
		lseg.calldpc = 0
		self.add_auto_label(lseg)
		
		# Segment start label (full name). Example: "@TEXT@"
		lseg = Label(self.name[1:] + "@")
		lseg.value = self.vmaddr
		lseg.calldpc = 0
		self.add_label_internal(self.global_labels, lseg.name, lseg)
		
		# Process any assembly items that affect labels or the current location
		for item in self.asmlist:
			if isinstance(item, DotScope):
				# Handle .scope directives
				self.new_scope()
				outlist.append(item)
			elif isinstance(item, DotExport):
				# Handle .export directives
				if item.symbol is None:
					if self.is_local_name(item.label.name):
						raise TypeError("Cannot export a local label: %r" % item)
					elif self.is_special_name(item.label.name):
						raise TypeError("Cannot export special label: %r" % item)
				self.add_export(item.symbol or item.label.name, item.label)
			elif isinstance(item, DotLoc):
				# Handle .loc directives
				new_pc = item.pc.value(self)
				new_dpc = None
				if item.dpc is not None:
					new_dpc = item.dpc.value(self)
				self.set_loc(new_pc, new_dpc)
				outlist.append(item)
			elif isinstance(item, DotSegment):
				raise RuntimeError(".segment directives should have already been handled!")
			elif isinstance(item, Label):
				# Handle label definitions
				self.add_label(item)
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
		
		# Remove the fully processed directives
		self.asmlist = outlist
		self.rewind()
		return nextaddr
	
	def set_label_resolver(self, resolver):
		self.resolver = resolver
	
	def assemble(self):
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
			else:
				# Instruction or data directive like .db/.dw/.lestring
				curlen = len(item)
				self.set_item_len(curlen)
				assembled = item.assemble(self)
				start = self.here - self.vmaddr
				step = 1 + self.dpc
				end = self.here + curlen * step - self.vmaddr
				data[start:end:step] = assembled
				self.advance()
		
		return list2bytes(data)


class Assembler(object):
	def __init__(self, layout=LAYOUT):
		self.global_labels = {}
		self.exports = []
		self.segments = []
		self.segmap = {}
		
		for desc in layout:
			seg = Context(**desc)
			self.segments.append(seg)
			self.segmap[seg.name] = seg
	
	def add_input(self, asmstr):
		# Parse the input text into AST objects
		asmlist = parser.parse(asmstr)
		
		# Assembling starts in the @TEXT segment by default
		curseg = "@TEXT"
		
		# Reset DPC to zero for each segment
		for seg in self.segments:
			# Add a fake ".loc @AFTER@, 0" line
			seg.add_asm_item(DotLoc(LabelExpr("AFTER@"), NumExpr(0)))
		
		# Add each assembly item like directives, instructions, and labels to their corresponding segment
		for item in asmlist:
			if isinstance(item, DotSegment):
				# Switching to a different segment
				curseg = item.name
				if curseg not in self.segmap:
					raise KeyError("Segment %r not defined in LAYOUT" % curseg)
			else:
				# Adding another instruction or directive to an existing segment
				self.segmap[curseg].add_asm_item(item)
	
	def resolve(self, name):
		if name not in self.global_labels:
			raise NameError("No label found with name: %r" % name)
		return self.global_labels[name]
	
	def get_exports(self):
		return self.exports
	
	def assemble(self):
		# First address after the NULL page
		nextaddr = PAGE_SIZE
		segdata = []
		
		# Pass 1: Compute addresses, collect labels, decide which labels should be exported
		for seg in self.segments:
			# Decide segment base, compute label addresses, and get address just after end of segment
			nextaddr = seg.compute_internal_labels(nextaddr)
			
			# Collect global labels from segment
			for label in seg.global_labels.values():
				if label.name in self.global_labels:
					raise NameError("Label cannot be defined in more than one segment: %r" % l)
				self.global_labels[label.name] = label
		
		# Pass 2: Compute values for constexprs, resolve label references, assemble instructions into bytes
		for seg in self.segments:
			seg.set_label_resolver(self)
			segdata.append((seg.name, seg.vmaddr, seg.assemble()))
			
			# Collect lists of labels to be exported as symbols
			self.exports.extend(seg.get_exports())
		
		return segdata
