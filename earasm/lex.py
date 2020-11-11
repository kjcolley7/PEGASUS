import ply.lex as lex

# List of token names
tokens = (
	# Condition codes
	"EQ", "NE", "GT", "LE", "LT", "GE", "AL",
	"NG", "PS", "BG", "SE", "SM", "BE", "OD", "EV",
	
	# Register names
	"ZERO", "TMP", "RV", "R3", "R4", "R5", "R6", "R7",
	"R8", "R9", "FP", "SP", "RA", "RD", "PC", "DPC",
	
	# Real instructions
	"ADD", "SUB", "MLU", "MLS", "DVU", "DVS", "XOR", "AND",
	"ORR", "SHL", "SRU", "SRS", "MOV", "CMP",
	"LDW", "STW", "LDB", "STB", "BRA", "BRR", "FCA", "FCR",
	"RDB", "WRB", "PSH", "POP", "INC", "BPT", "HLT", "NOP",
	
	# Pseudo instructions
	"RET", "DEC", "NEG", "INV", "ADR", "SWP", "ADC", "SBC",
	
	# Syntax
	"LPAREN", "RPAREN", "LBRACKET", "RBRACKET", "DASH",
	"LBRACE", "RBRACE", "PLUS", "STAR", "COLON",
	"SLASH", "PERCENT", "CARAT", "AMPERSAND", "PIPE",
	"LSHIFT", "RSHIFT", "TILDE",
	"COMMA", "DOT",
	
	# Values
	"LABEL", "NUMBER", "STRING",
	
	# Assembler directives
	"DOTLESTRING", "DOTDB", "DOTDW", "DOTLOC", "DOTSEGMENT",
	"DOTSCOPE", "DOTEXPORT"
)

words = {
	# Condition codes
	"EQ": "EQ", "ZR": "EQ",
	"NE": "NE", "NZ": "NE",
	"GT": "GT",
	"LE": "LE",
	"LT": "LT", "CC": "LT",
	"GE": "GE", "CS": "GE",
	"AL": "AL",
	"NG": "NG",
	"PS": "PS",
	"BG": "BG",
	"SE": "SE",
	"SM": "SM",
	"BE": "BE",
	"OD": "OD",
	"EV": "EV",
	
	# Register names and their aliases
	"R0": "ZERO", "ZERO": "ZERO",
	"R1": "TMP", "TMP": "TMP",
	"R2": "RV", "RV": "RV",
	"R3": "R3",
	"R4": "R4",
	"R5": "R5",
	"R6": "R6",
	"R7": "R7",
	"R8": "R8",
	"R9": "R9",
	"R10": "FP", "FP": "FP",
	"R11": "SP", "SP": "SP",
	"R12": "RA", "RA": "RA",
	"R13": "RD", "RD": "RD",
	"R14": "PC", "PC": "PC",
	"R15": "DPC", "DPC": "DPC",
	
	# Real instructions
	"ADD": "ADD",
	"SUB": "SUB",
	"MLU": "MLU",
	"MLS": "MLS",
	"DVU": "DVU",
	"DVS": "DVS",
	"XOR": "XOR",
	"AND": "AND",
	"ORR": "ORR",
	"SHL": "SHL",
	"SRU": "SRU",
	"SRS": "SRS",
	"MOV": "MOV",
	"CMP": "CMP",
	"LDW": "LDW",
	"STW": "STW",
	"LDB": "LDB",
	"STB": "STB",
	"BRA": "BRA",
	"BRR": "BRR",
	"FCA": "FCA",
	"FCR": "FCR",
	"RDB": "RDB",
	"WRB": "WRB",
	"PSH": "PSH",
	"POP": "POP",
	"INC": "INC",
	"BPT": "BPT",
	"HLT": "HLT",
	"NOP": "NOP",
	
	# Pseudo instructions
	"RET": "RET",
	"DEC": "DEC",
	"NEG": "NEG",
	"INV": "INV",
	"ADR": "ADR",
	"SWP": "SWP",
	"ADC": "ADC",
	"SBC": "SBC"
}

# Syntax
t_LPAREN = r'\('
t_RPAREN = r'\)'
t_LBRACKET = r'\['
t_RBRACKET = r'\]'
t_DASH = r'-'
t_LBRACE = r'\{'
t_RBRACE = r'\}'
t_PLUS = r'\+'
t_STAR = r'\*'
t_COLON = r':'
t_SLASH = r'/'
t_PERCENT = r'%'
t_CARAT = r'\^'
t_AMPERSAND = r'&'
t_PIPE = r'\|'
t_LSHIFT = r'<<'
t_RSHIFT = r'>>'
t_TILDE = r'~'
t_COMMA = r','
t_DOT = r'\.'

# Words like register names, condition codes, instructions
def t_word(t):
	r'[a-zA-Z_][a-zA-Z0-9_]*'
	try:
		t.type = words[t.value]
	except KeyError:
		# Handle cases like "MOVF", which includes the TF
		# (toggle flags) instruction modifier.
		if any(t.value.endswith(c) for c in "FNY"):
			t.type = words[t.value[:-1]]
		else:
			raise
	return t

# Labels all start with an at sign. They may contain alphanumeric
# characters, underscores, dollar signs, at signs, and dots, but
# they may not end with a dot or have multiple consecutive dots.
def t_LABEL(t):
	r'\@(\.?[a-zA-Z0-9_$@])*'
	t.value = t.value[1:]
	return t

# Literal numbers
def t_NUMBER(t):
	r'''([1-9]\d*|0(x[a-fA-F0-9]+|b[01]+|o[0-7]+)?|'[^']'|'\\['0afvtrn]')'''
	
	sign = 1
	s = t.value
	if s.startswith("-"):
		sign = -1
		s = s[1:]
	
	if s.startswith("0x"):
		t.value = int(s[2:], 16)
	elif s.startswith("0b"):
		t.value = int(s[2:], 2)
	elif s.startswith("0o"):
		t.value = int(s[2:], 8)
	elif s.startswith("'"):
		if s[1] == '\\':
			t.value = ord("'\0\a\f\v\t\r\n"["'0afvtrn".index(s[2])])
		else:
			t.value = ord(s[1])
	else:
		t.value = int(s, 10)
	
	t.value *= sign
	return t

# String literals
def t_STRING(t):
	r'".*?(?<!\\)"'
	
	# This might be unsafe, not entirely sure, but it doesn't matter
	# because I'm not expecting to assemble untrusted input. This makes
	# handling escaped values like \n or \x41 so easy that it's worth it.
	t.value = eval("b" + t.value)
	if isinstance(t.value, str):
		t.value = [ord(x) for x in t.value]
	return t

# Comments
def t_comment(t):
	r'(/\*(.|\n)*?\*/)|(//.*)'
	t.lexer.lineno += t.value.count("\n")
	pass

# Assembler directives
t_DOTLESTRING = r'\.lestring'
t_DOTDB = r'\.db'
t_DOTDW = r'\.dw'
t_DOTLOC = r'\.loc'
t_DOTSEGMENT = r'\.segment'
t_DOTSCOPE = r'\.scope'
t_DOTEXPORT = r'\.export'

# Define a rule so we can track line numbers
def t_newline(t):
	r'\n+'
	t.lexer.lineno += len(t.value)

# A string containing ignored characters (spaces, tabs, and carriage returns)
t_ignore = " \t\r"

# Error handling rule
def t_error(t):
	print("Illegal character: '%s'" % t.value[0])
	t.lexer.skip(1)

# Build the lexer
lexer = lex.lex()
