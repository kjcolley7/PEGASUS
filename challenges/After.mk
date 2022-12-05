PEG_CHALS_DIR := $(DIR)

ifndef SERVER
SERVER :=
endif

.PHONY: solve

#####
# solve_rule($1: challenge name)
#####
define _solve_rule

$1+PEG_FILES := $$(patsubst %.ear,%.peg,$$(wildcard $(PEG_CHALS_DIR)/$1/*.ear))

ifdef $(PEG_CHALS_DIR)/$1+DOCKER_PORTS
$1+PORT_ARG := PORT="$$($(PEG_CHALS_DIR)/$1+DOCKER_PORTS)"
else
$1+PORT_ARG :=
endif

.PHONY: solve[$1]
solve[$1]: $$($1+PEG_FILES)
	$$(_v)cd $$(PEG_CHALS_DIR)/$1 && PATH="$$(PEG_PATH)" $$($1+PORT_ARG) SERVER="$$(SERVER)" ./solve | diff -w flag.txt - && echo 'SOLVE $1 PASS' || (echo 'SOLVE $1 FAIL'; cat solve.out)

solve: solve[$1]

.PHONY: clean[$1]
clean[$1]:
	$(_v)rm -f $$($1+PEG_FILES)

clean:: clean[$1]

endef #_solve_rule
solve_rule = $(eval $(call _solve_rule,$1))

PEG_CHALS := $(patsubst $(PEG_CHALS_DIR)/%/solve,%,$(wildcard $(PEG_CHALS_DIR)/*/solve))
$(foreach chal,$(PEG_CHALS),$(call solve_rule,$(chal)))
