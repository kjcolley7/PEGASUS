REGISTER_NUMBERS = {
	"R0": 0, "ZERO": 0,
	"R1": 1, "A0": 1,
	"R2": 2, "A1": 2,
	"R3": 3, "A2": 3,
	"R4": 4, "A3": 4,
	"R5": 5, "A4": 5,
	"R6": 6, "A5": 6,
	"R7": 7, "S0": 7,
	"R8": 8, "S1": 8,
	"R9": 9, "S2": 9,
	"R10": 10, "FP": 10,
	"R11": 11, "SP": 11,
	"R12": 12, "RA": 12,
	"R13": 13, "RD": 13,
	"R14": 14, "PC": 14,
	"R15": 15, "DPC": 15
}

REGISTER_NAMES = [
	"ZERO", "A0", "A1", "A2", "A3", "A4", "A5", "S0",
	"S1", "S2", "FP", "SP", "RA", "RD", "PC", "DPC"
]

CONTROL_REGISTER_NUMBERS = {}

for i in range(16):
	CONTROL_REGISTER_NUMBERS["CR%d" % i] = i

for i, name in enumerate([
	"CREG_DENY_R",
	"CREG_DENY_W",
	"INSN_DENY_0",
	"INSN_DENY_1",
	"INSN_COUNT_LO",
	"INSN_COUNT_HI",
	"EXEC_STATE_0",
	"EXEC_STATE_1",
	"MEMBASE_R",
	"MEMBASE_W",
	"MEMBASE_X",
	"EXC_INFO",
	"EXC_ADDR",
	"TIMER",
	"INSN_ADDR",
	"FLAGS"
]):
	if name:
		CONTROL_REGISTER_NUMBERS[name] = i

CONTROL_REGISTER_NAMES = [
	"CR0", "CR1", "CR2", "CR3", "CR4", "CR5", "CR6", "CR7",
	"CR8", "CR9", "CR10", "CR11", "CR12", "CR13", "CR14", "CR15"
]

CONDITION_CODES = [
	"EQ", "NE", "GT", "LE", "LT", "GE", "SP", "AL",
	"NG", "PS", "BG", "SE", "SM", "BE", "OD", "EV"
]

CC_MAP = {cc: i for i, cc in enumerate(CONDITION_CODES)}
for a, b in [
	("ZR", "EQ"),
	("NZ", "NE"),
	("CC", "LT"),
	("CS", "GE"),
	("", "AL")
]:
	CC_MAP[a] = CC_MAP[b]

CONDITION_INVERSES = {}
for a, b in [
	("EQ", "NE"),
	("GT", "LE"),
	("LT", "GE"),
	("NG", "PS"),
	("BG", "SE"),
	("SM", "BE"),
	("OD", "EV")
]:
	CONDITION_INVERSES[a] = b
	CONDITION_INVERSES[b] = a

PREFIX_XC = 0xC0
PREFIX_TF = 0xC1
PREFIX_XX = 0xC2
PREFIX_XY = 0xC3
PREFIX_XZ = 0xC4

def PREFIX_DR(rd: int) -> int:
	return 0xD0 + rd
