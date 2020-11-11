REGISTER_NUMBERS = {
	"R0": 0, "ZERO": 0,
	"R1": 1, "TMP": 1,
	"R2": 2, "RV": 2,
	"R3": 3,
	"R4": 4,
	"R5": 5,
	"R6": 6,
	"R7": 7,
	"R8": 8,
	"R9": 9,
	"R10": 10, "FP": 10,
	"R11": 11, "SP": 11,
	"R12": 12, "RA": 12,
	"R13": 13, "RD": 13,
	"R14": 14, "PC": 14,
	"R15": 15, "DPC": 15
}

REGISTER_NAMES = [
	"ZERO", "TMP", "RV", "R3", "R4", "R5", "R6", "R7",
	"R8", "R9", "FP", "SP", "RA", "RD", "PC", "DPC"
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

def PREFIX_DR(rd):
	return 0xD0 + rd.num
