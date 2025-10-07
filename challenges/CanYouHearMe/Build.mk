PEG := CanYouHearMe.peg

PRODUCT := $(DIR)/$(PEG)
PUBLISH := $(PEG)

HELLO_DIR := $(DIR)
HELLO_BUILD := $(BUILD_DIR)
HELLO_PEG := $(PRODUCT)

$(HELLO_PEG): $(HELLO_BUILD)/flag.ear

$(HELLO_BUILD)/flag.ear: $(HELLO_DIR)/flag.txt | $(HELLO_BUILD)/.dir
	$(_v)echo "@flag: .lestring \"$$(cat $<)\"" > $@
