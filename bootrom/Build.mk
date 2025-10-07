PRODUCTS := $(BOOTROM)

BOOTROM_SRCS := $(wildcard $(BOOTROM_DIR)/*.ear)

$(BOOTROM): $(BOOTROM_SRCS)
	$(_V)echo 'Assembling $@'
	@mkdir -p $(@D)
	$(_v)$(EARASM) $(EARASMFLAGS) --layout $(BOOTROM_DIR)/rom_layout.json --dump-symbols $(@D)/bootrom.syms -o $@ $(<D)/bootrom.ear
