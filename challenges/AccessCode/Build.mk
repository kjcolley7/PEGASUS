PEG := AccessCode.peg
PUBLISH := $(PEG)

REV_PEG := $(DIR)/$(PEG)
PRODUCT := $(REV_PEG)

REV_DIR := $(DIR)

check: check-rev

.PHONY: check-rev
check-rev: $(REV_PEG) $(REV_DIR)/flag.txt $(PEG_BIN)/runpeg
	$(_v)$(PEG_BIN)/runpeg --timeout=1 $< < $(REV_DIR)/flag.txt >/dev/null && echo "PASS $(<F)" || echo "FAIL $(<F)"
