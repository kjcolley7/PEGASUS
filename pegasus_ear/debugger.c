//
//  debugger.c
//  PegasusEar
//
//  Created by Kevin Colley on 10/31/20.
//

#include "debugger.h"
#include <ctype.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include "utils.h"
#include "common/dynamic_array.h"
#include "common/dynamic_string.h"
#include "linenoise/linenoise.h"
#include "ansi_colors.h"


#define LINE_HINT_MAX 500


typedef enum BreakpointState {
	BP_UNUSED = 0,
	BP_ENABLED,
	BP_DISABLED,
} BreakpointState;

typedef struct Breakpoint {
	EAR_Size addr;
	EAR_Protection prot;
	BreakpointState state;
} Breakpoint;

typedef struct Debugger {
	EAR* cpu;
	EAR_HaltReason r;
	dynamic_array(Breakpoint) breakpoints;
} Debugger;


static EAR_HaltReason Debugger_cbAttach(void* cookie) {
	return Debugger_run(cookie);
}


/*!
 * @brief Initializes a debugger that will control the provided EAR processor.
 * 
 * @param cpu EAR processor to debug
 * @return Newly initialized debugger object
 */
Debugger* Debugger_init(EAR* cpu) {
	Debugger* dbg = calloc(1, sizeof(*dbg));
	if(!dbg) {
		return NULL;
	}
	
	dbg->cpu = cpu;
	dbg->r = HALT_NONE;
	
	EAR_attachDebugger(dbg->cpu, &Debugger_cbAttach, dbg);
	return dbg;
}


/*! Destroys a debugger object that was previously created using `Debugger_init`. */
void Debugger_destroy(Debugger* dbg) {
	array_clear(&dbg->breakpoints);
	free(dbg);
}


/*!
 * @brief Registers a hardware breakpoint.
 * 
 * @param addr Virtual address to place a breakpoint
 * @param prot Mask of memory access modes to break on
 * @return Registered breakpoint ID
 */
BreakpointID Debugger_addBreakpoint(Debugger* dbg, EAR_Size addr, EAR_Protection prot) {
	Breakpoint new_bp = {
		.addr = addr,
		.prot = prot,
		.state = BP_ENABLED,
	};
	
	enumerate(&dbg->breakpoints, i, bp) {
		if(bp->state == BP_UNUSED) {
			*bp = new_bp;
			return (BreakpointID)i;
		}
	}
	
	array_append(&dbg->breakpoints, new_bp);
	return (BreakpointID)(dbg->breakpoints.count - 1);
}


/*!
 * @brief Checks whether a breakpoint with the provided ID exists.
 * 
 * @param bpid Breakpoint ID to check for existence
 * @return True if a breakpoint with that ID exists, false otherwise
 */
static bool Debugger_breakpointExists(Debugger* dbg, BreakpointID bpid) {
	if(bpid >= dbg->breakpoints.count) {
		return false;
	}
	
	return dbg->breakpoints.elems[bpid].state != BP_UNUSED;
}


/*!
 * @brief Temporarily disables a registered breakpoint.
 * 
 * @param bpid Breakpoint ID to disable
 */
void Debugger_disableBreakpoint(Debugger* dbg, BreakpointID bpid) {
	if(!Debugger_breakpointExists(dbg, bpid)) {
		return;
	}
	
	dbg->breakpoints.elems[bpid].state = BP_DISABLED;
}


/*!
 * @brief Enables a previously disabled breakpoint.
 * 
 * @param bpid Breakpoint ID to enable
 */
void Debugger_enableBreakpoint(Debugger* dbg, BreakpointID bpid) {
	if(!Debugger_breakpointExists(dbg, bpid)) {
		return;
	}
	
	dbg->breakpoints.elems[bpid].state = BP_ENABLED;
}


/*!
 * @brief Toggles whether the target breakpoint is enabled or disabled.
 * 
 * @param bpid Breakpoint ID to toggle
 * @return Whether the breakpoint is now enabled or disabled
 */
bool Debugger_toggleBreakpoint(Debugger* dbg, BreakpointID bpid) {
	if(!Debugger_breakpointExists(dbg, bpid)) {
		return false;
	}
	
	Breakpoint* bp = &dbg->breakpoints.elems[bpid];
	if(bp->state == BP_ENABLED) {
		bp->state = BP_DISABLED;
		return false;
	}
	
	bp->state = BP_ENABLED;
	return true;
}


/*!
 * @brief Removes a breakpoint so that its ID may be reused.
 * 
 * @param bpid Breakpoint ID to remove
 */
void Debugger_removeBreakpoint(Debugger* dbg, BreakpointID bpid) {
	if(!Debugger_breakpointExists(dbg, bpid)) {
		return;
	}
	
	dbg->breakpoints.elems[bpid].state = BP_UNUSED;
}


/*! Clear all registered breakpoints. */
void Debugger_clearBreakpoints(Debugger* dbg) {
	array_clear(&dbg->breakpoints);
}


static EAR_HaltReason Debugger_cbUnhandledFault(
	void* cookie,
	EAR_Size vmaddr,
	EAR_Protection prot,
	EAR_TTE* tte,
	EAR_HaltReason faultReason,
	EAR_Size* out_paddr
) {
	Debugger* dbg = cookie;
	(void)dbg;
	(void)tte;
	(void)out_paddr;
	
	const char* accessMode;
	switch(prot) {
		case EAR_PROT_READ:
			accessMode = "Read";
			break;
		
		case EAR_PROT_WRITE:
			accessMode = "Write";
			break;
		
		case EAR_PROT_EXECUTE:
			accessMode = "Execute";
			break;
		
		default:
			accessMode = "Access";
			break;
	}
	fprintf(stderr, "%s violation at 0x%04X\n", accessMode, vmaddr);
	return faultReason;
}


static EAR_HaltReason Debugger_cbMemAccess(void* cookie, EAR_Size vmaddr, EAR_Protection prot, EAR_Size size, void* data) {
	Debugger* dbg = cookie;
	(void)data;
	
	// When resuming from a breakpoint or trying to disassemble code, don't halt
	if(dbg->cpu->debug_flags & (DEBUG_RESUMING | DEBUG_NOFAULT)) {
		return HALT_NONE;
	}
	
	enumerate(&dbg->breakpoints, i, bp) {
		if(bp->state != BP_ENABLED) {
			continue;
		}
		
		if((bp->prot & prot) == 0) {
			continue;
		}
		
		if(vmaddr <= bp->addr && bp->addr < vmaddr + size) {
			const char* accessMode;
			switch(prot) {
				case EAR_PROT_READ:
					accessMode = "read";
					break;
				
				case EAR_PROT_WRITE:
					accessMode = "write";
					break;
				
				case EAR_PROT_EXECUTE:
					accessMode = "execute";
					break;
				
				default:
					accessMode = "access";
					break;
			}
			fprintf(stderr, "HW breakpoint #%u hit trying to %s %u bytes at address %04X\n", (BreakpointID)(i + 1), accessMode, size, vmaddr);
			return HALT_HW_BREAKPOINT;
		}
	}
	
	return HALT_NONE;
}

static void Debugger_register(Debugger* dbg) {
	dbg->cpu->debug_flags |= DEBUG_ACTIVE;
	
	EAR_setFaultHandler(dbg->cpu, &Debugger_cbUnhandledFault, dbg);
	EAR_setMemoryHook(dbg->cpu, &Debugger_cbMemAccess, dbg);
}


static void Debugger_unregister(Debugger* dbg) {
	dbg->cpu->debug_flags &= ~DEBUG_ACTIVE;
	
	EAR_setFaultHandler(dbg->cpu, NULL, NULL);
	EAR_setMemoryHook(dbg->cpu, NULL, NULL);
}


typedef enum CMD_TYPE {
	CMD_INVALID = 0,
	CMD_BREAKPOINT,
	CMD_CONTEXT,
	CMD_CONTINUE,
	CMD_DISASSEMBLE,
	CMD_HELP,
	CMD_HEXDUMP,
	CMD_QUIT,
	CMD_REGISTERS,
	CMD_STEP,
	CMD_VMMAP,
	
	CMD_COUNT //!< Number of command types
} CMDTYPE;

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

// <addr>
static CommandArgsHints hnt_addr = {
	"addr", false, NULL
};

// <r/w/x>
static CommandArgsHints hnt_prot_addr = {
	"r/w/x", false, &hnt_addr
};

// <breakpoint id>
static CommandArgsHints hnt_bpid = {
	"breakpoint id", false, NULL
};

// <subcommand or addr>
static CommandArgsHints hnt_subcommand_or_addr = {
	"subcommand or addr", false, NULL
};

// [<dpc=DPC>]
static CommandArgsHints hnt_dpc = {
	"dpc=DPC", true, NULL
};

// [<addr=PC> [<dpc=DPC>]]
static CommandArgsHints hnt_addr_dpc = {
	"addr=PC", true, &hnt_dpc
};

// [<count=5> [<addr=PC> [<dpc=DPC>]]]
static CommandArgsHints hnt_count_addr_dpc = {
	"count=5", true, &hnt_addr_dpc
};

// [<command or category>]
static CommandArgsHints hnt_command_or_category = {
	"command or category", true, NULL
};

// <size>
static CommandArgsHints hnt_size = {
	"size", false, NULL
};

// <addr> <size>
static CommandArgsHints hnt_addr_size = {
	"addr", false, &hnt_size
};

// <r/w/x/p> <addr> <size>
static CommandArgsHints hnt_prot_addr_size = {
	"r/w/x/p", false, &hnt_addr_size
};

// This must remain sorted (according to strncasecmp)
static CommandMapEntry cmd_map[] = {
	{"b",           CMD_BREAKPOINT, &hnt_addr},
	{"ba",          CMD_BREAKPOINT, &hnt_prot_addr},
	{"bd",          CMD_BREAKPOINT, &hnt_bpid},
	{"be",          CMD_BREAKPOINT, &hnt_bpid},
	{"bp",          CMD_BREAKPOINT, &hnt_subcommand_or_addr},
	{"break",       CMD_BREAKPOINT, &hnt_subcommand_or_addr},
	{"breakpoint",  CMD_BREAKPOINT, &hnt_subcommand_or_addr},
	{"c",           CMD_CONTINUE, NULL},
	{"cont",        CMD_CONTINUE, NULL},
	{"context",     CMD_CONTEXT, NULL},
	{"continue",    CMD_CONTINUE, NULL},
	{"ctx",         CMD_CONTEXT, NULL},
	{"dis",         CMD_DISASSEMBLE, &hnt_count_addr_dpc},
	{"disasm",      CMD_DISASSEMBLE, &hnt_count_addr_dpc},
	{"disass",      CMD_DISASSEMBLE, &hnt_count_addr_dpc},
	{"disassemble", CMD_DISASSEMBLE, &hnt_count_addr_dpc},
	{"exit",        CMD_QUIT, NULL},
	{"h",           CMD_HELP, &hnt_command_or_category},
	{"hd",          CMD_HEXDUMP, &hnt_prot_addr_size},
	{"help",        CMD_HELP, NULL},
	{"hexdump",     CMD_HEXDUMP, &hnt_prot_addr_size},
	{"q",           CMD_QUIT, NULL},
	{"quit",        CMD_QUIT, NULL},
	{"r",           CMD_REGISTERS, NULL},
	{"reg",         CMD_REGISTERS, NULL},
	{"registers",   CMD_REGISTERS, NULL},
	{"regs",        CMD_REGISTERS, NULL},
	{"s",           CMD_STEP, NULL},
	{"si",          CMD_STEP, NULL},
	{"step",        CMD_STEP, NULL},
	{"vmmap",       CMD_VMMAP, NULL},
	{"xxd",         CMD_HEXDUMP, &hnt_prot_addr_size},
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
static void Debugger_helpBreakpoint(void);
static void Debugger_doBreakpoint(Debugger* dbg, Command* cmd);
static void Debugger_helpRunning(void);
static void Debugger_doContinue(Debugger* dbg, Command* cmd);
static void Debugger_doStep(Debugger* dbg, Command* cmd);
static void Debugger_doDisassemble(Debugger* dbg, Command* cmd);
static void Debugger_doHexdump(Debugger* dbg, Command* cmd);
static void Debugger_doRegisters(Debugger* dbg, Command* cmd);
static void Debugger_doVMMap(Debugger* dbg, Command* cmd);
static void showContext(EAR* ear);
static void Debugger_doContext(Debugger* dbg, Command* cmd);
static void Debugger_doHelp(Command* cmd);



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

static void dbg_build_hint_string_helper(CommandArgsHints* hint, const char* tail, char* dst, size_t dst_size) {
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
	char* line = NULL;
	bool keepGoing = true;
	const char* prompt = "(dbg) ";
	
	linenoiseHistorySetMaxLen(500);
	linenoiseSetHintsCallback(&dbg_hints);
	linenoiseSetFreeHintsCallback(&dbg_free_hint);
	linenoiseSetCompletionCallback(&dbg_completion);
	
	Debugger_register(dbg);
	
	fprintf(stderr, "\nEAR debugger\n");
	
	Command* lastCmd = NULL;
	while(keepGoing && prompt_line(prompt, &line)) {
		Command* cmd;
		
		// If the line is empty, run the previous command again
		if(*line == '\0') {
			cmd = lastCmd;
		}
		else {
			cmd = cmd_parse(line);
			lastCmd = cmd;
		}
		
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
			case HALT_SW_BREAKPOINT:
				fprintf(stderr, "Hit a breakpoint instruction at %04X!\n", dbg->cpu->active->r[PC]);
				break;
			
			case HALT_DEBUGGER:
				fprintf(stderr, "Received keyboard interrupt!\n");
				break;
			
			case HALT_HW_BREAKPOINT: // Already printed in the memory access callback
			case HALT_NONE:
				break;
			
			case HALT_INSTRUCTION:
				fprintf(stderr, "\nProgram execution halted successfully\n");
				break;
			
			default:
				fprintf(stderr, "%s\n", EAR_haltReasonToString(dbg->r));
				break;
		}
		
		showContext(dbg->cpu);
	}
	
	Debugger_unregister(dbg);
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
		case CMD_BREAKPOINT:
			Debugger_doBreakpoint(dbg, cmd);
			break;
		
		case CMD_CONTEXT:
			Debugger_doContext(dbg, cmd);
			break;
		
		case CMD_CONTINUE:
			Debugger_doContinue(dbg, cmd);
			break;
		
		case CMD_DISASSEMBLE:
			Debugger_doDisassemble(dbg, cmd);
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
		
		default:
			fprintf(stderr, "Unexpected CMD type %d\n", cmd->type);
			break;
	}
	
	return 0;
}


static bool Debugger_parseProtection(const char* s, EAR_Protection* out_prot) {
	EAR_Protection prot = EAR_PROT_NONE;
	
	while(*s != '\0') {
		switch(toupper(*s++)) {
			case 'R':
				prot |= EAR_PROT_READ;
				break;
			
			case 'W':
				prot |= EAR_PROT_WRITE;
				break;
			
			case 'X':
			case 'E':
				prot |= EAR_PROT_EXECUTE;
				break;
			
			case 'P':
				prot |= EAR_PROT_PHYSICAL;
				break;
			
			default:
				fprintf(stderr, "Invalid memory access type '%c'\n", *s);
				return false;
		}
	}
	
	if((prot & EAR_PROT_PHYSICAL) && prot != EAR_PROT_PHYSICAL) {
		fprintf(stderr, "Cannot combine 'P' with any of 'RWX' for memory access mode\n");
		return false;
	}
	
	*out_prot = prot;
	return true;
}


static void Debugger_helpBreakpoint(void) {
	fprintf(stderr,
		"Available breakpoint commands:\n"
		"ba <access mode (R|W|X)> <addr>\n"
		"                -- Add a memory access breakpoint on an address with some combination of access modes\n"
		"bp add <addr>   -- Add a breakpoint at code address <addr>\n"
		"b <addr>        -- Short mode for `bp add <addr>`\n"
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
	EAR_Size addr;
	BreakpointID bpid;
	bool enabled;
	unsigned pos = 0;
	
	if(cmd->args.count < 2) {
		Debugger_helpBreakpoint();
		return;
	}
	
	// Handle "ba" command separately from the others
	EAR_Protection prot = EAR_PROT_EXECUTE;
	const char* first = cmd->args.elems[pos++];
	if(strcasecmp(first, "ba") == 0) {
		if(!Debugger_parseProtection(cmd->args.elems[pos++], &prot) || (prot & EAR_PROT_PHYSICAL)) {
			fprintf(stderr, "The `ba` command expects memory access type (RWX) as the first argument\n");
			Debugger_helpRunning();
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
	
	if(strcasecmp(subcmd, "add") == 0 || isdigit(subcmd[0])) {
		// Allow a short form like "b <addr>"
		if(isdigit(subcmd[0])) {
			CHECK_ARG_COUNT(pos);
			str = subcmd;
		}
		else {
			CHECK_ARG_COUNT(pos + 1);
			str = cmd->args.elems[pos++];
		}
		
		addr = (EAR_Size)strtoul(str, &end, 0);
		if(*end != '\0') {
			fprintf(stderr, "Invalid address given to `breakpoint add`\n");
			Debugger_helpBreakpoint();
			return;
		}
		
		bpid = Debugger_addBreakpoint(dbg, addr, prot);
		fprintf(
			stderr,
			"Created breakpoint #%u at address %04X (%s%s%s)\n",
			bpid + 1,
			addr,
			(prot & EAR_PROT_READ) ? "R" : "",
			(prot & EAR_PROT_WRITE) ? "W" : "",
			(prot & EAR_PROT_EXECUTE) ? "X" : ""
		);
	}
	else if(strcasecmp(subcmd, "list") == 0) {
		CHECK_ARG_COUNT(pos);
		
		fprintf(stderr, "Breakpoints:\n");
		enumerate(&dbg->breakpoints, i, bp) {
			if(bp->state == BP_UNUSED) {
				continue;
			}
			
			fprintf(
				stderr,
				"Breakpoint #%u at address %04X (%s%s%s) is %sabled\n",
				(EAR_Size)i + 1,
				bp->addr,
				(bp->prot & EAR_PROT_READ) ? "R" : "",
				(bp->prot & EAR_PROT_WRITE) ? "W" : "",
				(bp->prot & EAR_PROT_EXECUTE) ? "X" : "",
				bp->state == BP_ENABLED ? "en" : "dis"
			);
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
		fprintf(stderr, "Invalid breakpoint command!\n");
		Debugger_helpBreakpoint();
	}
}


static void Debugger_helpRunning(void) {
	fprintf(
		stderr,
		"Available running commands:\n"
		"continue        -- Run until a breakpoint is encountered or the program halts\n"
		"step            -- Runs a single instruction and returns to the debugger\n"
		"disassemble [<count=5> [<addr=PC> [<dpc=DPC>]]]\n"
		"                -- Disassembles `count` instructions at the given address and DPC value\n"
		"hexdump <access mode (R|W|X|P)> <addr> <count>\n"
		"                -- Dumps a region of physical or virtual memory in a hexdump format\n"
		//"reg [name [value]]\n"
		"registers       -- Shows register values\n"
		"vmmap           -- Shows virtual memory regions\n"
		"context         -- Shows register values and the next few instructions\n"
	);
}


static void Debugger_doContinue(Debugger* dbg, Command* cmd) {
	if(cmd->args.count != 1) {
		fprintf(stderr, "Wrong argument count for continue\n");
		Debugger_helpRunning();
		return;
	}
	
	dbg->cpu->debug_flags |= DEBUG_RESUMING;
	dbg->r = EAR_continue(dbg->cpu);
}


static void Debugger_doStep(Debugger* dbg, Command* cmd) {
	if(cmd->args.count != 1) {
		fprintf(stderr, "Wrong argument count for step\n");
		Debugger_helpRunning();
		return;
	}
	
	dbg->cpu->debug_flags |= DEBUG_RESUMING;
	dbg->r = EAR_stepInstruction(dbg->cpu);
}


static void Debugger_doDisassemble(Debugger* dbg, Command* cmd) {
	EAR_Size disasmAddr = dbg->cpu->active->r[PC];
	EAR_Size disasmDPC = dbg->cpu->active->r[DPC];
	unsigned disasmCount = 5;
	
	size_t pos = 1;
	if(cmd->args.count - pos >= 1) {
		char* end = NULL;
		unsigned long num = 0;
		const char* arg = cmd->args.elems[pos++];
		num = strtoul(arg, &end, 0);
		if(*end != '\0' || num > 500) {
			fprintf(stderr, "Invalid count argument to `disassemble`: %s\n", arg);
			Debugger_helpRunning();
			return;
		}
		disasmCount = (unsigned)num;
		
		if(cmd->args.count - pos >= 1) {
			arg = cmd->args.elems[pos++];
			num = strtoul(arg, &end, 0);
			if(*end != '\0' || num >= EAR_ADDRESS_SPACE_SIZE) {
				fprintf(stderr, "Invalid addr argument to `disassemble`: %s\n", arg);
				Debugger_helpRunning();
				return;
			}
			disasmAddr = (EAR_Size)num;
			
			if(cmd->args.count - pos >= 1) {
				arg = cmd->args.elems[pos++];
				num = strtoul(arg, &end, 0);
				if(*end != '\0' || num > EAR_SIZE_MAX) {
					fprintf(stderr, "Invalid dpc argument to `disassemble`: %s\n", arg);
					Debugger_helpRunning();
					return;
				}
				disasmDPC = (EAR_Size)num;
				
				if(cmd->args.count - pos >= 1) {
					fprintf(stderr, "Too many arguments for `disassemble`!\n");
					Debugger_helpRunning();
					return;
				}
			}
		}
	}
	
	dbg->cpu->debug_flags |= DEBUG_NOFAULT;
	EAR_writeDisassembly(dbg->cpu, disasmAddr, disasmDPC, disasmCount, stderr);
	dbg->cpu->debug_flags &= ~DEBUG_NOFAULT;
}


static void Debugger_doHexdump(Debugger* dbg, Command* cmd) {
	// Syntax: hexdump <access mode (R|W|X|P)> <addr> <size>
	size_t pos = 1;
	if(cmd->args.count != 4) {
		fprintf(stderr, "Wrong argument count for hexdump\n");
		Debugger_helpRunning();
		return;
	}
	
	// Parse memory access mode
	EAR_Protection prot = EAR_PROT_NONE;
	
	bool okay = true;
	if(!Debugger_parseProtection(cmd->args.elems[pos++], &prot)) {
		okay = false;
	}
	
	if(okay) {
		switch(prot) {
			case EAR_PROT_READ:
			case EAR_PROT_WRITE:
			case EAR_PROT_EXECUTE:
			case EAR_PROT_PHYSICAL:
				break;
			
			default:
				okay = false;
		}
	}
	
	if(!okay) {
		fprintf(stderr, "The `hexdump` command expects memory access type as the final argument\n");
		Debugger_helpRunning();
		return;
	}
	
	char* end = NULL;
	EAR_Size addr = (EAR_Size)strtoul(cmd->args.elems[pos++], &end, 0);
	if(*end != '\0') {
		fprintf(stderr, "Invalid address given to `hexdump`\n");
		Debugger_helpRunning();
		return;
	}
	
	EAR_Size size = (EAR_Size)strtoul(cmd->args.elems[pos++], &end, 0);
	if(*end != '\0') {
		fprintf(stderr, "Invalid size given to `hexdump`\n");
		Debugger_helpRunning();
		return;
	}
	
	// Bounds checking
	if(EAR_SIZE_MAX - size < addr) {
		size = EAR_SIZE_MAX - addr + 1;
	}
	
	// Dump either physical or virtual memory, depending on the memory access mode requested
	void* dump = NULL;
	bool shouldFree = false;
	if(prot == EAR_PROT_PHYSICAL) {
		// Dump physical memory directly
		dump = EAR_getPhys(dbg->cpu, addr, &size);
	}
	else {
		// Dump virtual memory after copying it into a buffer
		dump = malloc(size);
		if(!dump) {
			fprintf(stderr, "Out of memory\n");
			return;
		}
		shouldFree = true;
		
		// If an exception is raised partway through this copyout, it will return a smaller size.
		// We don't care what the exception is as it will be printed by the exception handler, so
		// just update the size to dump to the number of bytes that were actually copied.
		bool didSetDebugNoFault = dbg->cpu->debug_flags & DEBUG_NOFAULT;
		dbg->cpu->debug_flags |= DEBUG_NOFAULT;
		
		size = EAR_copyout(dbg->cpu, dump, addr, size, prot, NULL);
		
		if(didSetDebugNoFault) {
			dbg->cpu->debug_flags &= ~DEBUG_NOFAULT;
		}
	}
	
	ear_xxd(dump, size, &addr, stderr);
	
	if(shouldFree) {
		free(dump);
	}
}


static void Debugger_doRegisters(Debugger* dbg, Command* cmd) {
	if(cmd->args.count != 1) {
		fprintf(stderr, "Wrong argument count for registers\n");
		Debugger_helpRunning();
		return;
	}
	
	EAR_writeRegs(dbg->cpu, stderr);
}


static void Debugger_doVMMap(Debugger* dbg, Command* cmd) {
	if(cmd->args.count != 1) {
		fprintf(stderr, "Wrong argument count for vmmap\n");
		Debugger_helpRunning();
		return;
	}
	
	EAR_writeVMMap(dbg->cpu, stderr);
}


static void showContext(EAR* ear) {
	EAR_writeRegs(ear, stderr);
	
	fprintf(stderr, "\nNext instructions:\n");
	
	ear->debug_flags |= DEBUG_NOFAULT;
	EAR_writeDisassembly(ear, ear->active->r[PC], ear->active->r[DPC], 5, stderr);
	ear->debug_flags &= ~DEBUG_NOFAULT;
}


static void Debugger_doContext(Debugger* dbg, Command* cmd) {
	if(cmd->args.count != 1) {
		fprintf(stderr, "Wrong argument count for context\n");
		Debugger_helpRunning();
		return;
	}
	
	showContext(dbg->cpu);
}


static void Debugger_doHelp(Command* cmd) {
	if(cmd->args.count >= 2) {
		// Specific help for commands
		CMDTYPE type = cmd_getType(cmd->args.elems[1]);
		if(type == 0) {
			if(strcasecmp(cmd->args.elems[1], "running") == 0) {
				// Set the type to some random running command
				type = CMD_CONTINUE;
			}
		}
		
		switch(type) {
			case CMD_BREAKPOINT:
				Debugger_helpBreakpoint();
				return;
			
			case CMD_CONTINUE:
			case CMD_STEP:
			case CMD_REGISTERS:
			case CMD_VMMAP:
				Debugger_helpRunning();
				return;
			
			default:
				// Fall back to general help
				break;
		}
	}
	
	// General help
	fprintf(
		stderr,
		"Available topics (type help <topic> to learn more):\n"
		"breakpoint      -- Setting and modifying breakpoints\n"
		"running         -- Controlling how a program runs and getting info\n"
		"quit            -- Exit the debugger and stop execution\n"
	);
}
