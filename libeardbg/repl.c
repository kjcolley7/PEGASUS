#include "repl.h"
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h>
#include "common/dynamic_array.h"
#include "linenoise/linenoise.h"
#include "libear/ear.h"
#include "debugger.h"
#include "ansi_colors.h"
#include "utils.h"


#define LINE_HINT_MAX 500


static uint8_t parseHexDigit(char c, bool* bad) {
	c = toupper(c);
	if(c >= '0' && c <= '9') {
		return c - '0' + 0x0;
	}
	else if(c >= 'A' && c <= 'F') {
		return c - 'A' + 0xA;
	}
	
	*bad = true;
	return 0;
}

static uint8_t str_parseHexByte(const char* str, bool* bad) {
	uint8_t h = parseHexDigit(str[0], bad) << 4;
	if(*bad) {
		return 0;
	}
	
	return h | parseHexDigit(str[1], bad);
}

static EAR_FullAddr strtofull(const char* str, char** end, bool* isLong) {
	EAR_FullAddr full = 0;
	const char* p = str;
	
	if(isLong) {
		*isLong = false;
	}
	
	// If starting with "0x" prefix, fall back to strtoul
	if(!strncmp(p, "0x", 2)) {
		goto fallback;
	}
	
	bool bad = false;
	EAR_FullAddr first = str_parseHexByte(p, &bad);
	if(bad) {
		goto fallback;
	}
	p += 2;
	*end = (char*)p;
	
	bool phys = false;
	if(*p == ':') {
		p++;
		phys = true;
	}
	
	EAR_FullAddr second = str_parseHexByte(p, &bad);
	if(bad) {
		goto fallback;
	}
	p += 2;
	
	EAR_FullAddr third = str_parseHexByte(p, &bad);
	if(bad) {
		if(phys) {
			goto fallback;
		}
		
		*end = (char*)p;
		full = (first << EAR_PAGE_SHIFT) | second;
		return full;
	}
	p += 2;
	
	*end = (char*)p;
	if(isLong) {
		*isLong = phys;
	}
	full = (first << EAR_REGION_SHIFT) | (second << EAR_PAGE_SHIFT) | third;
	return full;
	
fallback:
	full = (EAR_FullAddr)strtoul(str, end, 16);
	if(isLong && full >= EAR_VIRTUAL_ADDRESS_SPACE_SIZE) {
		*isLong = true;
	}
	return full;
}


static bool Debugger_parseRegisterName(const char* name, EAR_Register* out_reg) {
	struct {
		const char* name;
		EAR_Register num;
	} regmap[] = {
		{"R0", R0}, {"ZERO", ZERO},
		{"R1", R1}, {"A0", A0},
		{"R2", R2}, {"A1", A1},
		{"R3", R3}, {"A2", A2},
		{"R4", R4}, {"A3", A3},
		{"R5", R5}, {"A4", A4},
		{"R6", R6}, {"A5", A5},
		{"R7", R7}, {"S0", S0},
		{"R8", R8}, {"S1", S1},
		{"R9", R9}, {"S2", S2},
		{"R10", R10}, {"FP", FP},
		{"R11", R11}, {"SP", SP},
		{"R12", R12}, {"RA", RA},
		{"R13", R13}, {"RD", RD},
		{"R14", R14}, {"PC", PC},
		{"R15", R15}, {"DPC", DPC}
	};
	
	size_t i;
	for(i = 0; i < ARRAY_COUNT(regmap); ++i) {
		if(!strcasecmp(name, regmap[i].name)) {
			*out_reg = regmap[i].num;
			return true;
		}
	}
	
	return false;
}


static bool Debugger_parseAddress(
	Debugger* dbg, const char* str, EAR_FullAddr* out_addr, bool* isLong
) { //Debugger_parseAddress
	// Not a physical address unless explicitly parsed as one
	if(isLong) {
		*isLong = false;
	}
	
	// First, try to parse as a register name
	EAR_Register reg;
	if(Debugger_parseRegisterName(str, &reg)) {
		*out_addr = CTX(*dbg->cpu)->r[reg];
		return true;
	}
	
	// Second, try to look up as a symbol name
	Pegasus* peg = dbg->pegs[dbg->cpu->ctx.active];
	if(peg) {
		Pegasus_Symbol* sym = Pegasus_findSymbolByName(peg, str);
		if(sym) {
			*out_addr = sym->value;
			return true;
		}
	}
	
	// Third, try to parse as a literal address
	char* end;
	*out_addr = strtofull(str, &end, isLong);
	return *end == '\0';
}


typedef enum CMD_TYPE {
	CMD_INVALID = 0,
	CMD_BACKTRACE,
	CMD_BREAKPOINT,
	CMD_CONTEXT,
	CMD_CONTINUE,
	CMD_CONTROL_REGISTERS,
	CMD_DISASSEMBLE,
	CMD_EXCEPTION,
	CMD_HELP,
	CMD_HEXDUMP,
	CMD_PMAP,
	CMD_QUIT,
	CMD_REGISTERS,
	CMD_STEP,
	CMD_VMMAP,
	
	CMD_COUNT //!< Number of command types
} CMDTYPE;

#define CMD_KERNEL 0x1000

typedef struct CommandArgsHints CommandArgsHints;
struct CommandArgsHints {
	const char* arghint;
	bool optional;
	CommandArgsHints* nextarg;
};

typedef struct CommandMapEntry {
	const char* name;
	CMDTYPE type;
	CommandArgsHints* hints;
} CommandMapEntry;

// <vaddr(XXXX)>
static CommandArgsHints hnt_vaddr = {
	"vaddr(XXXX)", false, NULL
};

// <addr(XXXX or XX:XXXX)>
static CommandArgsHints hnt_either_addr = {
	"addr(XXXX or XX:XXXX)", false, NULL
};

// <mode([RWX]+)> <addr(XXXX or XX:XXXX)>
static CommandArgsHints hnt_mode_addr = {
	"mode([RWX]+)", false, &hnt_either_addr
};

// <breakpoint id>
static CommandArgsHints hnt_bpid = {
	"breakpoint id", false, NULL
};

// <subcommand or vaddr>
static CommandArgsHints hnt_subcommand_or_addr = {
	"subcommand or vaddr", false, NULL
};

// [<dpc=DPC>]
static CommandArgsHints hnt_dpc = {
	"dpc=DPC", true, NULL
};

// [<addr(XXXX or XX:XXXX)=PC> [<dpc=DPC>]]
static CommandArgsHints hnt_addr_dpc = {
	"addr(XXXX or XX:XXXX)=PC", true, &hnt_dpc
};

// [<count=5> [<addr(XXXX or XX:XXXX)=PC> [<dpc=DPC>]]]
static CommandArgsHints hnt_count_addr_dpc = {
	"count=5", true, &hnt_addr_dpc
};

// [<command or category>]
static CommandArgsHints hnt_command_or_category = {
	"command or category", true, NULL
};

// <mode([RWX]+)>
static CommandArgsHints hnt_mode = {
	"mode([RWX]+)", true, NULL
};

// <size> <mode([RWX]+)>
static CommandArgsHints hnt_size_mode = {
	"size", false, &hnt_mode
};

// <addr(XXXX or XX:XXXX)> <size> [<mode([RWX]+)>]
static CommandArgsHints hnt_addr_size_mode = {
	"addr(XXXX or XX:XXXX)", false, &hnt_size_mode
};

// <exception subcommand>
static CommandArgsHints hnt_exception_subcmd = {
	"exception subcommand", false, NULL
};

// This must remain sorted (according to strncasecmp)
static CommandMapEntry cmd_map[] = {
	{"altbacktrace",    CMD_BACKTRACE | CMD_KERNEL, NULL},
	{"altbt",           CMD_BACKTRACE | CMD_KERNEL, NULL},
	{"altcontext",      CMD_CONTEXT | CMD_KERNEL, NULL},
	{"altcregs",        CMD_CONTROL_REGISTERS | CMD_KERNEL, NULL},
	{"altctx",          CMD_CONTEXT | CMD_KERNEL, NULL},
	{"altregs",         CMD_REGISTERS | CMD_KERNEL, NULL},
	{"b",               CMD_BREAKPOINT, &hnt_vaddr},
	{"ba",              CMD_BREAKPOINT, &hnt_mode_addr},
	{"bd",              CMD_BREAKPOINT, &hnt_bpid},
	{"be",              CMD_BREAKPOINT, &hnt_bpid},
	{"bp",              CMD_BREAKPOINT, &hnt_subcommand_or_addr},
	{"break",           CMD_BREAKPOINT, &hnt_subcommand_or_addr},
	{"breakpoint",      CMD_BREAKPOINT, &hnt_subcommand_or_addr},
	{"bt",              CMD_BACKTRACE, NULL},
	{"c",               CMD_CONTINUE, NULL},
	{"cont",            CMD_CONTINUE, NULL},
	{"context",         CMD_CONTEXT, NULL},
	{"continue",        CMD_CONTINUE, NULL},
	{"cr",              CMD_CONTROL_REGISTERS | CMD_KERNEL, NULL},
	{"cregs",           CMD_CONTROL_REGISTERS | CMD_KERNEL, NULL},
	{"ctx",             CMD_CONTEXT, NULL},
	{"dis",             CMD_DISASSEMBLE, &hnt_count_addr_dpc},
	{"disasm",          CMD_DISASSEMBLE, &hnt_count_addr_dpc},
	{"disass",          CMD_DISASSEMBLE, &hnt_count_addr_dpc},
	{"disassemble",     CMD_DISASSEMBLE, &hnt_count_addr_dpc},
	{"exc",             CMD_EXCEPTION | CMD_KERNEL, &hnt_exception_subcmd},
	{"exception",       CMD_EXCEPTION | CMD_KERNEL, &hnt_exception_subcmd},
	{"exit",            CMD_QUIT, NULL},
	{"h",               CMD_HELP, &hnt_command_or_category},
	{"hd",              CMD_HEXDUMP, &hnt_addr_size_mode},
	{"help",            CMD_HELP, NULL},
	{"hexdump",         CMD_HEXDUMP, &hnt_addr_size_mode},
	{"hlt",             CMD_EXCEPTION | CMD_KERNEL, NULL},
	{"pmap",            CMD_PMAP | CMD_KERNEL, NULL},
	{"q",               CMD_QUIT, NULL},
	{"quit",            CMD_QUIT, NULL},
	{"reg",             CMD_REGISTERS, NULL},
	{"registers",       CMD_REGISTERS, NULL},
	{"regs",            CMD_REGISTERS, NULL},
	{"s",               CMD_STEP, NULL},
	{"si",              CMD_STEP, NULL},
	{"step",            CMD_STEP, NULL},
	{"vmmap",           CMD_VMMAP, NULL},
	{"xxd",             CMD_HEXDUMP, &hnt_addr_size_mode},
};


typedef struct Command {
	//! Command type
	CMDTYPE type;
	
	//! Array of arguments
	dynamic_array(char*) args;
} Command;

static Command* cmd_parse(const char* line);
static char* cmd_getNextArgument(const char** line);
static CMDTYPE cmd_getType(const char* cmdstr);


static Command* cmd_parse(const char* line) {
	Command* cmd = calloc(1, sizeof(*cmd));
	if(!cmd) {
		return NULL;
	}
	
	char* cmdstr;
	while((cmdstr = cmd_getNextArgument(&line))) {
		array_append(&cmd->args, cmdstr);
	}
	
	// Need at least the command name
	if(cmd->args.count == 0) {
		array_clear(&cmd->args);
		free(cmd);
		return NULL;
	}
	
	cmd->type = cmd_getType(cmd->args.elems[0]);
	
	// Invalid command?
	if(cmd->type == CMD_INVALID) {
		fprintf(stderr, "Invalid command\n");
		array_destroy(&cmd->args);
		free(cmd);
		return NULL;
	}
	
	return cmd;
}


static char* cmd_getNextArgument(const char** line) {
	// Skip leading spaces
	while(isspace(**line)) {
		++*line;
	}
	
	// Check for end of string
	if(**line == '\0') {
		return NULL;
	}
	
	// Found start of argument
	const char* arg_begin = *line;
	
	// Find end of argument
	while(!isspace(**line) && **line != '\0') {
		++*line;
	}
	
	// Compute argument string length using current (end) pointer
	size_t arg_length = *line - arg_begin;
	
	// Allocate a copy of the argument
	return strndup(arg_begin, arg_length);
}


static CMDTYPE cmd_getType(const char* cmdstr) {
	// Lookup the command string in the command map
	size_t i;
	for(i = 0; i < ARRAY_COUNT(cmd_map); i++) {
		if(strcasecmp(cmdstr, cmd_map[i].name) == 0) {
			return cmd_map[i].type;
		}
	}
	
	// Didn't find the command
	return CMD_INVALID;
}


static int Debugger_perform(Debugger* dbg, Command* cmd);
static void Debugger_doBacktrace(Debugger* dbg, Command* cmd);
static void Debugger_doBreakpoint(Debugger* dbg, Command* cmd);
static void Debugger_doContext(Debugger* dbg, Command* cmd);
static void Debugger_doContinue(Debugger* dbg, Command* cmd);
static void Debugger_doControlRegisters(Debugger* dbg, Command* cmd);
static void Debugger_doDisassemble(Debugger* dbg, Command* cmd);
static void Debugger_doException(Debugger* dbg, Command* cmd);
static void Debugger_doHelp(Command* cmd);
static void Debugger_doHexdump(Debugger* dbg, Command* cmd);
static void Debugger_doPMap(Debugger* dbg, Command* cmd);
static void Debugger_doRegisters(Debugger* dbg, Command* cmd);
static void Debugger_doStep(Debugger* dbg, Command* cmd);
static void Debugger_doVMMap(Debugger* dbg, Command* cmd);


static bool prompt_line(const char* prompt, char** out_line) {
	char* line = linenoise(prompt);
	if(line == NULL) {
		return false;
	}
	
	// Track this line in linenoise's history so up arrow works
	linenoiseHistoryAdd(line);
	
	// Strip newline and return success
	*out_line = strsep(&line, "\n");
	return true;
}


typedef bool cmd_foreach_cb(const CommandMapEntry* entry, void* cookie);
static void dbg_foreach_cmd_with_prefix(const char* prefix, cmd_foreach_cb* cb, void* cookie) {
	size_t i;
	for(i = 0; i < ARRAY_COUNT(cmd_map); i++) {
		if(strncasecmp(prefix, cmd_map[i].name, strlen(prefix)) == 0) {
			if(!cb(&cmd_map[i], cookie)) {
				break;
			}
		}
	}
}

static bool dbg_completion_cb(const CommandMapEntry* entry, void* cookie) {
	linenoiseCompletions* lc = cookie;
	linenoiseAddCompletion(lc, entry->name);
	return true;
}

static void dbg_completion(const char* buf, linenoiseCompletions* lc) {
	dbg_foreach_cmd_with_prefix(buf, &dbg_completion_cb, lc);
}

static bool dbg_cmd_hint_prefix_cb(const CommandMapEntry* entry, void* cookie) {
	const CommandMapEntry** out_entry = cookie;
	*out_entry = entry;
	return false;
}

static void dbg_build_hint_string_helper(
	CommandArgsHints* hint, const char* tail, char* dst, size_t dst_size
) { //dbg_build_hint_string_helper
	// Always put a space before the argument
	if(dst_size == 0) {
		return;
	}
	
	*dst++ = ' ';
	dst_size--;
	
	// Start with the argument description in angled brackets
	snprintf(dst, dst_size, "<%s>", hint->arghint);
	if(tail != NULL && tail[0] != '\0') {
		// Append the subsequent argument descriptions after a space
		size_t dst_len = strlen(dst);
		snprintf(&dst[dst_len], dst_size - dst_len, "%s", tail);
	}
	
	// If this part is optional, wrap the whole thing in square brackets
	if(hint->optional) {
		size_t hintLen = strlen(dst);
		if(hintLen + 2 + 1 > dst_size) {
			hintLen = dst_size - 2 - 1;
		}
		memmove(&dst[1], &dst[0], hintLen);
		dst[0] = '[';
		dst[1 + hintLen] = ']';
		dst[1 + hintLen + 1] = '\0';
	}
}

static void dbg_build_hint_string(CommandArgsHints* hints, char dst[LINE_HINT_MAX]) {
	static char tmp[LINE_HINT_MAX];
	
	// Initially clear string buffers
	tmp[0] = '\0';
	dst[0] = '\0';
	
	// Reverse linked list
	CommandArgsHints* cur = hints;
	CommandArgsHints* last = NULL;
	CommandArgsHints* next;
	int hintsLen = 0;
	while(cur != NULL) {
		next = cur->nextarg;
		cur->nextarg = last;
		last = cur;
		cur = next;
		hintsLen++;
	}
	
	// Each processed node will flip s between 0 and 1. We need to ensure
	// that the final write happens in dst, not tmp. As the write target
	// buffer is swapped each time, we only need to consider the odd vs
	// even list length cases. For odd list lengths (such as 1), the first
	// array written into will be the same as the last one. So since s will
	// start at 1 for odd lists, we just need to put dst in index 1 of the
	// ss array.
	int s = hintsLen % 2;
	char* ss[2] = {tmp, dst};
	
	// last is now a pointer to the final arg hint
	cur = last;
	
	// Iterate through the reversed linked list
	while(cur != NULL) {
		dbg_build_hint_string_helper(cur, ss[!s], ss[s], LINE_HINT_MAX);
		s = !s;
		cur = cur->nextarg;
	}
	
	// Reverse linked list (again to undo)
	cur = last;
	last = NULL;
	while(cur != NULL) {
		next = cur->nextarg;
		cur->nextarg = last;
		last = cur;
		cur = next;
	}
	
	// The last write was into ss[!s] (using the current value of s), which
	// is guaranteed to be dst.
}

static char* dbg_hints(const char* buf, int* color, int* bold) {
	(void)bold;
	static char s_line[LINE_HINT_MAX];
	static char s_hint[LINE_HINT_MAX];
	
	// Make a mutable copy of the line buffer
	strncpy(s_line, buf, sizeof(s_line) - 1);
	
	// Split the line buffer by (one or more) whitespace characters
	char* save = NULL;
	const char* argv0 = strtok_r(s_line, " \t", &save);
	if(argv0 == NULL) {
		return NULL;
	}
	
	// Just find and return the first (lexicographically) command w/ matching prefix
	const CommandMapEntry* entry = NULL;
	dbg_foreach_cmd_with_prefix(argv0, &dbg_cmd_hint_prefix_cb, &entry);
	if(entry == NULL) {
		*color = ANSI_COLOR_BRIGHT_RED;
		return " <-- INVALID COMMAND";
	}
	
	// If the user is still editing the command name (argv[0]), hint the
	// first (lexicographically) command with a matching name prefix
	size_t cmd_len = strlen(argv0);
	if(cmd_len < strlen(entry->name)) {
		*color = ANSI_COLOR_MAGENTA;
		strncpy(s_hint, entry->name + cmd_len, sizeof(s_hint) - 1);
		return s_hint;
	}
	
	// Need to figure out how many arguments have already been typed out
	// to know where in the hints list to start
	int argc;
	for(argc = 1; ; argc++) {
		const char* arg = strtok_r(NULL, " \t", &save);
		if(arg == NULL) {
			// Not another argument
			break;
		}
	}
	
	// Seek in the hints list to the argument currently being edited
	CommandArgsHints* hintStart = entry->hints;
	int hintidx;
	for(hintidx = 1; hintidx < argc; hintidx++) {
		if(hintStart == NULL) {
			break;
		}
		hintStart = hintStart->nextarg;
	}
	
	// Too many arguments?
	if(argc > hintidx) {
		*color = ANSI_COLOR_BRIGHT_RED;
		return " <-- TOO MANY ARGUMENTS";
	}
	
	// Dynamically build the hint string into the static buffer
	dbg_build_hint_string(hintStart, s_hint);
	*color = ANSI_COLOR_MAGENTA;
	return s_hint;
}

static void dbg_free_hint(void* hint) {
	// Don't need to free as we use a static buffer
	(void)hint;
}


EAR_HaltReason Debugger_run(Debugger* dbg) {
	// Should we even run the debugger REPL?
	if(dbg->debug_flags & DEBUG_DETACHED) {
		Debugger_doContinue(dbg, NULL);
		return dbg->r;
	}
	
	// When not kernel debugging, skip to the first instruction in user mode
	if(!(dbg->debug_flags & DEBUG_KERNEL)) {
		Debugger_stepInstruction(dbg);
		if(dbg->r != HALT_NONE && dbg->r != HALT_EXCEPTION) {
			return dbg->r;
		}
	}
	
	char* line = NULL;
	const char* prompt = "(dbg) ";
	
	linenoiseHistorySetMaxLen(500);
	linenoiseSetHintsCallback(&dbg_hints);
	linenoiseSetFreeHintsCallback(&dbg_free_hint);
	linenoiseSetCompletionCallback(&dbg_completion);
	
	fprintf(stderr, "\nEAR debugger\n");
	
	Command* lastCmd = NULL;
	while(prompt_line(prompt, &line)) {
		Command* cmd;
		
		// If the line is empty, run the previous command again
		if(*line == '\0') {
			cmd = lastCmd;
		}
		else {
			cmd = cmd_parse(line);
			lastCmd = cmd;
		}
		
		if(cmd->type & CMD_KERNEL && !(dbg->debug_flags & DEBUG_KERNEL)) {
			fprintf(stderr, "Command is only available in kernel debug mode.\n");
			array_destroy(&cmd->args);
			free(cmd);
			continue;
		}
		cmd->type &= ~CMD_KERNEL;
		
		if(!cmd) {
			fprintf(stderr, "%s", prompt);
			continue;
		}
		
#define MARK_SEEN(hr) ((hr) < 0 ? ((hr) & ~0x1000) : ((hr) | 0x1000))
#define CLEAR_SEEN(hr) ((hr) < 0 ? ((hr) | 0x1000) : ((hr) & ~0x1000))
#define IS_SEEN(hr) (!!((hr) & 0x1000) != ((hr) < 0))
		
		// Mark this halt reason as seen
		dbg->r = MARK_SEEN(dbg->r);
		
		// Run the command
		int quit = Debugger_perform(dbg, cmd);
		if(quit != 0) {
			break;
		}
		
		// Don't print the halt reason if it hasn't changed since the last time it was printed
		if(IS_SEEN(dbg->r)) {
			continue;
		}
		
		// Check on the status of the CPU
		switch(dbg->r) {
			case HALT_DEBUGGER:
				fprintf(stderr, "Halted by debugger!\n");
				break;
			
			case HALT_EXCEPTION:
				fprintf(stderr, "\nException!\n");
				//TODO exc_info
				break;
			
			case HALT_NONE:
				break;
			
			default:
				fprintf(stderr, "%s\n", EAR_haltReasonToString(dbg->r));
				break;
		}
		
		bool alt = !(dbg->debug_flags & DEBUG_KERNEL) && Debugger_isKernelMode(CTX(*dbg->cpu));
		Debugger_showContext(dbg, alt, stderr);
	}
	
	EAR_HaltReason r = CLEAR_SEEN(dbg->r);
	if(r == HALT_NONE) {
		r = HALT_DEBUGGER;
	}
	return r;
	
#undef MARK_SEEN
#undef CLEAR_SEEN
#undef IS_SEEN
}


static int Debugger_perform(Debugger* dbg, Command* cmd) {
	switch(cmd->type) {
		case CMD_BACKTRACE:
			Debugger_doBacktrace(dbg, cmd);
			break;
		
		case CMD_BREAKPOINT:
			Debugger_doBreakpoint(dbg, cmd);
			break;
		
		case CMD_CONTEXT:
			Debugger_doContext(dbg, cmd);
			break;
		
		case CMD_CONTINUE:
			Debugger_doContinue(dbg, cmd);
			break;
		
		case CMD_CONTROL_REGISTERS:
			Debugger_doControlRegisters(dbg, cmd);
			break;
		
		case CMD_DISASSEMBLE:
			Debugger_doDisassemble(dbg, cmd);
			break;
		
		case CMD_EXCEPTION:
			Debugger_doException(dbg, cmd);
			break;
		
		case CMD_HELP:
			Debugger_doHelp(cmd);
			break;
		
		case CMD_HEXDUMP:
			Debugger_doHexdump(dbg, cmd);
			break;
		
		case CMD_QUIT:
			return 1;
		
		case CMD_REGISTERS:
			Debugger_doRegisters(dbg, cmd);
			break;
		
		case CMD_STEP:
			Debugger_doStep(dbg, cmd);
			break;
		
		case CMD_VMMAP:
			Debugger_doVMMap(dbg, cmd);
			break;
		
		case CMD_PMAP:
			Debugger_doPMap(dbg, cmd);
			break;
		
		case CMD_INVALID:
		case CMD_COUNT:
			fprintf(stderr, "Unexpected CMD type %d\n", cmd->type);
			break;
	}
	
	return 0;
}


static bool Debugger_parseMode(const char* s, BreakpointFlags* out_flags) {
	BreakpointFlags flags = 0;
	
	while(*s != '\0') {
		switch(toupper(*s)) {
			case 'R':
				flags |= BP_READ;
				break;
			
			case 'W':
				flags |= BP_WRITE;
				break;
			
			case 'X':
				flags |= BP_EXECUTE;
				break;
			
			default:
				fprintf(stderr, "Invalid memory access type '%c'\n", *s);
				return false;
		}
		++s;
	}
	
	*out_flags = flags;
	return true;
}


static void Debugger_helpBreakpoint(void) {
	fprintf(stderr,
		"Available breakpoint commands:\n"
		"ba <access mode ([RWX]+)> <addr(XXXX or XX:XXXX)>\n"
		"                -- Add a memory access breakpoint on an address with some combination of access modes\n"
		"bp add <vaddr>  -- Add a breakpoint at code address <vaddr>\n"
		"b <vaddr>       -- Short mode for `bp add <vaddr>`\n"
		"bp list         -- List all breakpoints and their enabled status\n"
		"bp disable <id> -- Disable the breakpoint with ID <id>\n"
		"bp enable <id>  -- Enable the breakpoint with ID <id>\n"
		"bp toggle <id>  -- Toggle the enabled state of breakpoint with ID <id>\n"
		"bp remove <id>  -- Remove the breakpoint with ID <id>\n"
		"bp clear        -- Clear all breakpoints\n"
	);
}


#define CHECK_ARG_COUNT(argc) do { \
	if(cmd->args.count != (argc)) { \
		fprintf(stderr, "Wrong argument count for %s %s\n", cmd->args.elems[0], cmd->args.elems[1]); \
		Debugger_helpBreakpoint(); \
		return; \
	} \
} while(0)


static void Debugger_doBreakpoint(Debugger* dbg, Command* cmd) {
	char* end;
	const char* subcmd;
	const char* str;
	EAR_FullAddr addr;
	BreakpointID bpid;
	bool enabled;
	unsigned pos = 0;
	
	if(cmd->args.count < 2) {
		Debugger_helpBreakpoint();
		return;
	}
	
	// Handle "ba" command separately from the others
	BreakpointFlags mode = BP_EXECUTE;
	const char* first = cmd->args.elems[pos++];
	if(strcasecmp(first, "ba") == 0) {
		if(!Debugger_parseMode(cmd->args.elems[pos++], &mode)) {
			fprintf(stderr, "The `ba` command expects memory access type (RWX) as the first argument\n");
			Debugger_helpBreakpoint();
			return;
		}
		
		subcmd = "add";
	}
	else if(strcasecmp(first, "bd") == 0) {
		subcmd = "disable";
	}
	else if(strcasecmp(first, "be") == 0) {
		subcmd = "enable";
	}
	else {
		subcmd = cmd->args.elems[pos++];
	}
	
	if(strcasecmp(subcmd, "list") == 0) {
		CHECK_ARG_COUNT(pos);
		Pegasus* peg = dbg->pegs[dbg->cpu->ctx.active];
		
		fprintf(stderr, "Breakpoints:\n");
		enumerate(&dbg->breakpoints, i, bp) {
			if(!(bp->flags & BP_IN_USE)) {
				continue;
			}
			
			if(bp->flags & BP_PHYSICAL) {
				fprintf(
					stderr,
					"Breakpoint #%u at physical address %02X:%04X (%s%s) is %sabled\n",
					(unsigned)i + 1,
					EAR_FULL_REGION(bp->addr), EAR_FULL_NOTREGION(bp->addr),
					(bp->flags & BP_READ) ? "R" : "",
					(bp->flags & BP_WRITE) ? "W" : "",
					(bp->flags & BP_ENABLED) ? "en" : "dis"
				);
			}
			else {
				fprintf(
					stderr, "Breakpoint #%u at address %04X",
					(unsigned)i + 1, bp->addr
				);
				
				if(peg) {
					Pegasus_Symbol* sym = Pegasus_findSymbolByAddress(peg, bp->addr);
					if(sym) {
						unsigned sym_offset = bp->addr - sym->value;
						fprintf(stderr, " %s+%u", sym->name, sym_offset);
					}
				}
				
				fprintf(
					stderr, " (%s%s%s) is %sabled\n",
					(bp->flags & BP_READ) ? "R" : "",
					(bp->flags & BP_WRITE) ? "W" : "",
					(bp->flags & BP_EXECUTE) ? "X" : "",
					(bp->flags & BP_ENABLED) ? "en" : "dis"
				);
			}
		}
	}
	else if(strcasecmp(subcmd, "disable") == 0) {
		CHECK_ARG_COUNT(pos + 1);
		
		// Parse breakpoint ID
		bpid = (BreakpointID)strtoul(cmd->args.elems[pos++], &end, 0);
		if(*end != '\0') {
			fprintf(stderr, "Invalid breakpoint ID given to `breakpoint disable`\n");
			Debugger_helpBreakpoint();
			return;
		}
		--bpid;
		
		Debugger_disableBreakpoint(dbg, bpid);
		fprintf(stderr, "Disabled breakpoint #%u\n", bpid + 1);
	}
	else if(strcasecmp(subcmd, "enable") == 0) {
		CHECK_ARG_COUNT(pos + 1);
		
		// Parse breakpoint ID
		bpid = (BreakpointID)strtoul(cmd->args.elems[pos++], &end, 0);
		if(*end != '\0') {
			fprintf(stderr, "Invalid breakpoint ID given to `breakpoint enable`\n");
			Debugger_helpBreakpoint();
			return;
		}
		--bpid;
		
		Debugger_enableBreakpoint(dbg, bpid);
		fprintf(stderr, "Enabled breakpoint #%u\n", bpid + 1);
	}
	else if(strcasecmp(subcmd, "remove") == 0) {
		CHECK_ARG_COUNT(pos + 1);
		
		// Parse breakpoint ID
		bpid = (BreakpointID)strtoul(cmd->args.elems[pos++], &end, 0);
		if(*end != '\0') {
			fprintf(stderr, "Invalid breakpoint ID given to `breakpoint remove`\n");
			Debugger_helpBreakpoint();
			return;
		}
		--bpid;
		
		Debugger_removeBreakpoint(dbg, bpid);
		fprintf(stderr, "Removed breakpoint #%u\n", bpid + 1);
	}
	else if(strcasecmp(subcmd, "toggle") == 0) {
		CHECK_ARG_COUNT(pos + 1);
		
		// Parse breakpoint ID
		bpid = (BreakpointID)strtoul(cmd->args.elems[pos++], &end, 0);
		if(*end != '\0') {
			fprintf(stderr, "Invalid breakpoint ID given to `breakpoint toggle`\n");
			Debugger_helpBreakpoint();
			return;
		}
		--bpid;
		
		enabled = Debugger_toggleBreakpoint(dbg, bpid);
		fprintf(stderr, "Toggled breakpoint #%u %s\n", bpid + 1, enabled ? "on" : "off");
	}
	else if(strcasecmp(subcmd, "clear") == 0) {
		CHECK_ARG_COUNT(pos);
		
		Debugger_clearBreakpoints(dbg);
		fprintf(stderr, "Cleared all breakpoints\n");
	}
	else {
		// Allow a short form like "b <vaddr>"
		if(!strcasecmp(subcmd, "add")) {
			CHECK_ARG_COUNT(pos + 1);
			str = cmd->args.elems[pos++];
		}
		else {
			CHECK_ARG_COUNT(pos);
			str = subcmd;
		}
		
		bool do_phys = false;
		if(!Debugger_parseAddress(dbg, str, &addr, &do_phys)) {
			fprintf(stderr, "Invalid address given to `breakpoint add`\n");
			Debugger_helpBreakpoint();
			return;
		}
		
		if(do_phys) {
			if(mode & BP_EXECUTE) {
				fprintf(stderr, "Physical breakpoints can only use read/write mode, not execute\n");
				Debugger_helpBreakpoint();
				return;
			}
			
			bpid = Debugger_addBreakpoint(dbg, addr, mode);
			fprintf(
				stderr,
				"Created breakpoint #%u at physical address %02X:%04X (%s%s)\n",
				bpid + 1,
				EAR_FULL_REGION(addr), EAR_FULL_NOTREGION(addr),
				(mode & BP_READ) ? "R" : "",
				(mode & BP_WRITE) ? "W" : ""
			);
		}
		else {
			bpid = Debugger_addBreakpoint(dbg, addr, mode);
			fprintf(
				stderr,
				"Created breakpoint #%u at address %04X (%s%s%s)\n",
				bpid + 1,
				addr,
				(mode & BP_READ) ? "R" : "",
				(mode & BP_WRITE) ? "W" : "",
				(mode & BP_EXECUTE) ? "X" : ""
			);
		}
	}
}


static void Debugger_helpRunning(void) {
	fprintf(
		stderr,
		"Available running commands:\n"
		"continue/c      -- Run until a breakpoint is encountered or the program halts\n"
		"step/s          -- Runs a single instruction and returns to the debugger\n"
	);
}


static void Debugger_doContinue(Debugger* dbg, Command* cmd) {
	if(cmd && cmd->args.count != 1) {
		fprintf(stderr, "Wrong argument count for continue\n");
		Debugger_helpRunning();
		return;
	}
	
	bool enabledInterruptHandler = enable_interrupt_handler();
	
	dbg->debug_flags |= DEBUG_RESUMING;
	dbg->r = EAR_continue(dbg->cpu);
	
	if(enabledInterruptHandler) {
		disable_interrupt_handler();
	}
}


static void Debugger_doStep(Debugger* dbg, Command* cmd) {
	if(cmd->args.count != 1) {
		fprintf(stderr, "Wrong argument count for step\n");
		Debugger_helpRunning();
		return;
	}
	
	Debugger_stepInstruction(dbg);
}


static void Debugger_helpInspecting(void) {
	fprintf(
		stderr,
		"Available inspecting commands:\n"
		"disassemble/disasm [<count=5> [<addr(XXXX or XX:XXXX)=PC> [<dpc=DPC>]]]\n"
		"                -- Disassembles `count` instructions at the given address and DPC value\n"
		"hexdump/xxd <addr(XXXX or XX:XXXX)> <count> [<mode([RWX]+)]\n"
		"                -- Dumps a region of physical or virtual memory in a hexdump format\n"
		"vmmap           -- Shows virtual memory regions\n"
		"context/ctx     -- Shows register, control registers, and the next few instructions\n"
		"backtrace/bt    -- Shows the current call stack (backtrace)\n"
		"registers/regs  -- Shows register values\n"
		"cregs           -- Shows control register values\n"
		"altctx          -- Like `context` but for the alternate thread state\n"
		"altbt           -- Like `backtrace` but for the alternate thread state\n"
		"altregs         -- Like `regs` but for the alternate thread state\n"
		"altcregs        -- Like `cregs` but for the alternate thread state\n"
	);
}


static void Debugger_doBacktrace(Debugger* dbg, Command* cmd) {
	if(cmd->args.count != 1) {
		fprintf(stderr, "Wrong argument count for backtrace\n");
		Debugger_helpInspecting();
		return;
	}
	
	bool alt = !strncasecmp(cmd->args.elems[0], "alt", 3);
	return Debugger_showBacktrace(dbg, alt, stderr);
}


static void Debugger_doDisassemble(Debugger* dbg, Command* cmd) {
	EAR_ThreadState* ctx = CTX(*dbg->cpu);
	EAR_FullAddr disasmAddr = ctx->r[PC];
	EAR_UWord disasmDPC = ctx->r[DPC];
	unsigned disasmCount = 5;
	bool disasmPhys = false;
	
	size_t pos = 1;
	if(cmd->args.count - pos >= 1) {
		char* end = NULL;
		unsigned long num = 0;
		const char* arg = cmd->args.elems[pos++];
		num = strtoul(arg, &end, 0);
		if(*end != '\0' || num > 500) {
			fprintf(stderr, "Invalid count argument to `disassemble`: %s\n", arg);
			Debugger_helpInspecting();
			return;
		}
		disasmCount = (unsigned)num;
		
		if(cmd->args.count - pos >= 1) {
			arg = cmd->args.elems[pos++];
			if(!Debugger_parseAddress(dbg, arg, &disasmAddr, &disasmPhys)) {
				fprintf(stderr, "Invalid addr argument to `disassemble`: %s\n", arg);
				Debugger_helpInspecting();
				return;
			}
			
			if(cmd->args.count - pos >= 1) {
				arg = cmd->args.elems[pos++];
				num = strtoul(arg, &end, 0);
				if(*end != '\0' || num > EAR_UWORD_MAX) {
					fprintf(stderr, "Invalid dpc argument to `disassemble`: %s\n", arg);
					Debugger_helpInspecting();
					return;
				}
				disasmDPC = (EAR_UWord)num;
				
				if(cmd->args.count - pos >= 1) {
					fprintf(stderr, "Too many arguments for `disassemble`!\n");
					Debugger_helpInspecting();
					return;
				}
			}
		}
	}
	
	EAR_MemoryHandler* mem_fn;
	void* mem_cookie;
	if(disasmPhys) {
		mem_fn = Debugger_memoryHandler_physical;
		mem_cookie = dbg;
	}
	else {
		mem_fn = dbg->mem_fn;
		mem_cookie = dbg->mem_cookie;
	}
	
	dbg->debug_flags |= DEBUG_NOBREAK;
	Debugger_showDisassembly(
		dbg, mem_fn, mem_cookie,
		disasmAddr, disasmDPC, disasmCount,
		disasmPhys, stderr
	);
	dbg->debug_flags &= ~DEBUG_NOBREAK;
}


static void Debugger_helpException(void) {
	fprintf(
		stderr,
		"Available exception commands:\n"
		"exc catch <exception type>\n"
		"                -- Break into the debugger when the CPU raises the provided exception type\n"
		"exc ignore <exception type>\n"
		"                -- Don't break into the debugger for the provided exception type\n"
		"exc show        -- Show the list of exceptions the debugger will catch\n"
		"\n"
		"Invasive mode commands:\n"
		"exc clear       -- Clear the current exception\n"
		"hlt             -- Swap the current CPU thread context (like the `HLT` instruction)\n"
		"\n"
		"Exception types (case-insensitive):\n"
		" * HLT\n"
		" * MMU\n"
		" * BUS\n"
		" * DECODE\n"
		" * ARITHMETIC\n"
		" * DENIED_CREG\n"
		" * DENIED_INSN\n"
		" * TIMER\n"
	);
}


struct exc_map {
	const char* name;
	EAR_ExceptionMask type;
};


static EAR_ExceptionMask Debugger_exceptionKindFromString(const char* str) {
	static const struct exc_map exc_types[] = {
		{"HLT", EXC_MASK_HLT},
		{"MMU", EXC_MASK_MMU},
		{"BUS", EXC_MASK_BUS},
		{"DECODE", EXC_MASK_DECODE},
		{"ARITHMETIC", EXC_MASK_ARITHMETIC},
		{"DENIED_CREG", EXC_MASK_DENIED_CREG},
		{"DENIED_INSN", EXC_MASK_DENIED_INSN},
		{"TIMER", EXC_MASK_TIMER},
		{"ALL", EXC_MASK_ALL},
		{NULL, EXC_MASK_NONE}
	};
	
	const struct exc_map* cur = exc_types;
	while(cur->name != NULL) {
		if(!strcasecmp(str, cur->name)) {
			return cur->type;
		}
		cur++;
	}
	
	return 0;
}


static void Debugger_doException(Debugger* dbg, Command* cmd) {
	// Syntax: exc <subcmd> [<args>]
	//         exc catch <exception type>
	//         exc ignore <exception type>
	//         exc show
	//         exc clear (INVASIVE)
	//         hlt (INVASIVE)
	bool invasive = !!(dbg->debug_flags & DEBUG_INVASIVE);
	if(!strcasecmp(cmd->args.elems[0], "hlt")) {
		if(!invasive) {
			fprintf(stderr, "The `hlt` command is only available in invasive mode\n");
			Debugger_helpException();
			return;
		}
		
		dbg->cpu->ctx.active ^= 1;
		fprintf(stderr, "Swapped CPU thread context\n");
		return;
	}
	
	if(cmd->args.count < 2) {
		fprintf(stderr, "Wrong argument count for `%s`\n", cmd->args.elems[0]);
		Debugger_helpException();
		return;
	}
	
	const char* subcmd = cmd->args.elems[1];
	bool is_catch = !strcasecmp(subcmd, "catch");
	if(is_catch || !strcasecmp(subcmd, "ignore")) {
		if(cmd->args.count != 3) {
			fprintf(stderr, "Wrong argument count for `%s`\n", cmd->args.elems[0]);
			Debugger_helpException();
			return;
		}
		
		EAR_ExceptionInfo mask = Debugger_exceptionKindFromString(cmd->args.elems[2]);
		if(!mask) {
			fprintf(stderr, "Invalid exception type given to `%s`\n", cmd->args.elems[0]);
			Debugger_helpException();
			return;
		}
		
		if(is_catch) {
			dbg->cpu->exc_catch |= mask;
		}
		else {
			dbg->cpu->exc_catch &= ~mask;
		}
	}
	else if(!strcasecmp(subcmd, "show")) {
		if(dbg->cpu->exc_catch == EXC_MASK_NONE) {
			fprintf(stderr, "Debugger will not catch any exceptions\n");
		}
		else {
			fprintf(stderr, "Debugger will catch the following exceptions:\n");
			if(dbg->cpu->exc_catch & EXC_MASK_HLT) {
				fprintf(stderr, " * HLT\n");
			}
			if(dbg->cpu->exc_catch & EXC_MASK_MMU) {
				fprintf(stderr, " * MMU\n");
			}
			if(dbg->cpu->exc_catch & EXC_MASK_BUS) {
				fprintf(stderr, " * BUS\n");
			}
			if(dbg->cpu->exc_catch & EXC_MASK_DECODE) {
				fprintf(stderr, " * DECODE\n");
			}
			if(dbg->cpu->exc_catch & EXC_MASK_ARITHMETIC) {
				fprintf(stderr, " * ARITHMETIC\n");
			}
			if(dbg->cpu->exc_catch & EXC_MASK_DENIED_CREG) {
				fprintf(stderr, " * DENIED_CREG\n");
			}
			if(dbg->cpu->exc_catch & EXC_MASK_DENIED_INSN) {
				fprintf(stderr, " * DENIED_INSN\n");
			}
			if(dbg->cpu->exc_catch & EXC_MASK_TIMER) {
				fprintf(stderr, " * TIMER\n");
			}
		}
	}
	else if(!strcasecmp(subcmd, "clear")) {
		if(!invasive) {
			fprintf(stderr, "The `exc clear` command is only available in invasive mode\n");
			Debugger_helpException();
			return;
		}
		
		CTX(*dbg->cpu)->cr[CR_EXC_INFO] = 0;
		fprintf(stderr, "Cleared active thread state's EXC_INFO\n");
	}
	else {
		fprintf(stderr, "Invalid exception command!\n");
		Debugger_helpException();
	}
}


static void Debugger_doHexdump(Debugger* dbg, Command* cmd) {
	// Syntax: hexdump <addr(XXXX or XX:XXXX)> <size> [<access mode (R|W|X)>]
	size_t pos = 1;
	if(cmd->args.count < 3 || cmd->args.count > 4) {
		fprintf(stderr, "Wrong argument count for `%s`\n", cmd->args.elems[0]);
		Debugger_helpInspecting();
		return;
	}
	
	// Parse memory access mode
	BreakpointFlags mode = BP_READ;
	EAR_Protection prot = EAR_PROT_READ;
	if(cmd->args.count == 4) {
		bool okay = Debugger_parseMode(cmd->args.elems[3], &mode);
		if(okay) {
			switch(mode) {
				case BP_READ:
					prot = EAR_PROT_READ;
					break;
				
				case BP_WRITE:
					prot = EAR_PROT_WRITE;
					break;
				
				case BP_EXECUTE:
					prot = EAR_PROT_EXECUTE;
					break;
				
				default:
					okay = false;
			}
		}
		
		if(!okay) {
			fprintf(
				stderr, "Invalid memory access mode for last argument to `%s`\n",
				cmd->args.elems[0]
			);
			Debugger_helpInspecting();
			return;
		}
	}
	
	bool do_phys = false;
	EAR_FullAddr addr;
	if(!Debugger_parseAddress(dbg, cmd->args.elems[pos++], &addr, &do_phys)) {
		fprintf(stderr, "Invalid address given to `%s`\n", cmd->args.elems[0]);
		Debugger_helpInspecting();
		return;
	}
	
	if(do_phys && cmd->args.count == 4) {
		fprintf(stderr, "Memory access mode shouldn't be provided for physical addresses\n");
		Debugger_helpInspecting();
		return;
	}
	
	const char* size_arg = cmd->args.elems[pos++];
	char* end = NULL;
	EAR_UWord size = (EAR_UWord)strtoul(size_arg, &end, 0);
	if(*end != '\0') {
		EAR_Register reg;
		if(!Debugger_parseRegisterName(size_arg, &reg)) {
			fprintf(stderr, "Invalid size given to `%s`\n", cmd->args.elems[0]);
			Debugger_helpInspecting();
			return;
		}
		
		size = CTX(*dbg->cpu)->r[reg];
	}
	
	// Dump either physical or virtual memory, depending on the memory access mode requested
	void* dump = malloc(size);
	if(!dump) {
		fprintf(stderr, "Out of memory\n");
		return;
	}
	
	EAR_UWord not_copied = 0;
	if(do_phys) {
		// Dump physical memory directly
		not_copied = Debugger_readPhys(dbg, dump, addr, size, NULL);
	}
	else {
		// Dump virtual memory
		not_copied = Debugger_readVirt(dbg, dump, prot, addr, size);
	}
	
	const char* prefix;
	if(do_phys) {
		prefix = NULL;
	}
	else if(mode == BP_READ) {
		prefix = "R::";
	}
	else if(mode == BP_WRITE) {
		prefix = "W::";
	}
	else {
		ASSERT(mode == BP_EXECUTE);
		prefix = "X::";
	}
	
	ear_xxd(
		dump, size - not_copied, &addr,
		prefix, do_phys ? 6 : 4, stderr
	);
	free(dump);
	
	if(not_copied) {
		fprintf(stderr, "Unable to dump remaining %u/%u bytes\n", not_copied, size);
	}
}


static void Debugger_doRegisters(Debugger* dbg, Command* cmd) {
	if(cmd->args.count != 1) {
		fprintf(stderr, "Wrong argument count for `%s`\n", cmd->args.elems[0]);
		Debugger_helpInspecting();
		return;
	}
	
	
	bool alt = !strncasecmp(cmd->args.elems[0], "alt", 3);
	if(alt) {
		fprintf(stderr, "\nAlt thread state:\n");
	}
	else {
		fprintf(stderr, "\nThread state:\n");
	}
	
	Debugger_showRegs(dbg, alt, stderr);
}


static void Debugger_doControlRegisters(Debugger* dbg, Command* cmd) {
	if(cmd->args.count != 1) {
		fprintf(stderr, "Wrong argument count for `%s`\n", cmd->args.elems[0]);
		Debugger_helpInspecting();
		return;
	}
	
	bool alt = !strncasecmp(cmd->args.elems[0], "alt", 3);
	Debugger_showControlRegs(dbg, alt, stderr);
}


static void Debugger_doVMMap(Debugger* dbg, Command* cmd) {
	if(cmd->args.count != 1) {
		fprintf(stderr, "Wrong argument count for `%s`\n", cmd->args.elems[0]);
		Debugger_helpInspecting();
		return;
	}
	
	Debugger_showVMMap(dbg, stderr);
}


static void Debugger_doPMap(Debugger* dbg, Command* cmd) {
	if(cmd->args.count != 1) {
		fprintf(stderr, "Wrong argument count for `%s`\n", cmd->args.elems[0]);
		Debugger_helpInspecting();
		return;
	}
	
	if(!dbg->bus_dump_fn) {
		fprintf(stderr, "Debugger doesn't know how to dump the physical memory layout\n");
		return;
	}
	
	dbg->bus_dump_fn(dbg->bus_cookie, stderr);
}


static void Debugger_doContext(Debugger* dbg, Command* cmd) {
	if(cmd->args.count != 1) {
		fprintf(stderr, "Wrong argument count for `%s`\n", cmd->args.elems[0]);
		Debugger_helpInspecting();
		return;
	}
	
	bool alt = !strncasecmp(cmd->args.elems[0], "alt", 3);
	Debugger_showContext(dbg, alt, stderr);
}


static void Debugger_doHelp(Command* cmd) {
	if(cmd->args.count >= 2) {
		// Specific help for commands
		CMDTYPE type = cmd_getType(cmd->args.elems[1]);
		if(type == 0) {
			if(!strcasecmp(cmd->args.elems[1], "running")) {
				// Set the type to some random running command
				type = CMD_CONTINUE;
			}
			else if(!strcasecmp(cmd->args.elems[1], "inspecting")) {
				// Set the type to some random inspecting command
				type = CMD_HEXDUMP;
			}
		}
		
		switch(type) {
			case CMD_BREAKPOINT:
				Debugger_helpBreakpoint();
				return;
			
			case CMD_CONTINUE:
			case CMD_STEP:
				Debugger_helpRunning();
				return;
			
			case CMD_BACKTRACE:
			case CMD_CONTEXT:
			case CMD_DISASSEMBLE:
			case CMD_HEXDUMP:
			case CMD_REGISTERS:
			case CMD_CONTROL_REGISTERS:
			case CMD_VMMAP:
			case CMD_PMAP:
				Debugger_helpInspecting();
				return;
			
			case CMD_EXCEPTION:
				Debugger_helpException();
				return;
			
			case CMD_QUIT:
				fprintf(stderr, "It just quits the debugger. What did you expect?\n");
				return;
			
			case CMD_HELP:
				break;
			
			case CMD_INVALID:
			case CMD_COUNT:
				fprintf(stderr, "Invalid command type %d\n", type);
				return;
		}
	}
	
	// General help
	fprintf(
		stderr,
		"Available topics (type `help <topic>` to learn more):\n"
		"breakpoint      -- Setting and modifying breakpoints\n"
		"running         -- Controlling how a program runs\n"
		"inspecting      -- Getting runtime info about the program\n"
		"exception       -- Catching and handling exceptions\n"
		"quit            -- Exit the debugger and stop execution\n"
	);
}
