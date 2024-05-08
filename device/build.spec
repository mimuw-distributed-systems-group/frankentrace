#app name: MinimalForCmemuApp
app name: BlinkIfSeeEachOtherApp
boards:
 - cherry

build dir: $(SPEC_DIR)/build/$(BOARD)

dependencies:
  - $(SPEC_DIR)/runner

define: &DEFINES
 - BENCHMARK_WRITE_BUFFER_DIS=0
 - BENCHMARK_VIMS_LB_DIS=0
# enable cache mode with 0
 - CC26XX_VIMS_GPRAM_MODE=1
# disable cache at startup
 - SET_CCFG_SIZE_AND_DIS_FLAGS_DIS_GPRAM=1
 # always use XOSC
 # Cannot override this flag?
 # - CC26XX_XOSC_HF_ALWAYS_ON=1
 # Set to 1 to enable ITM tracing (use heni-whip6 tip)
 - PLATFORM_CC26XX_STARTUP_HAS_RESET_HOOK=1

make options: *DEFINES
