#include "debug.h"
extern void DebugMonitorISR();
extern void EndOfTrace();

module MinimalForCmemuPrv {
    uses interface Boot;
    //uses interface OnOffSwitch as SleepSwitch @exactlyonce();
    uses interface InterruptSource as DebugFault @exactlyonce();
}

implementation {
    event void Boot.booted() {
	  printf("This is all I can emulate.\n");

      // call SleepSwitch.off();
#if PLATFORM_CC26XX_STARTUP_HAS_RESET_HOOK
      EndOfTrace();
#endif
	  for(;;) {}
    }

    async void event DebugFault.interruptFired() {
#if PLATFORM_CC26XX_STARTUP_HAS_RESET_HOOK
        DebugMonitorISR();
#endif
    }
}
