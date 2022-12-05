PRODUCTS := $(DIR)/example_asm.peg

EARASM_FILES := \
	$(PEG_DIR)/earasm/README.md \
	$(filter-out %/earparsetab.py,$(wildcard $(PEG_DIR)/earasm/*.py))

$(BUILD_DIR)/earasm.tar.gz: $(EARASM_FILES) | $(BUILD_DIR)/.dir
	$(_V)echo 'Archiving $@'
	$(_v)tar czf $@ -C $(PEG_DIR)/earasm $(^F)

PUBLISH_BUILD := earasm.tar.gz
PUBLISH := example_asm.ear
