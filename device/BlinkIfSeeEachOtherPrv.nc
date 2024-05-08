/*
 * whip6: Warsaw High-performance IPv6.
 *
 * Copyright (c) 2012-2017 Przemyslaw Horban
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE     
 * files.
 */

extern void DebugMonitorISR();
extern void EndOfTrace();

module BlinkIfSeeEachOtherPrv {
    uses interface Boot;

    uses interface Led as Led0;
    uses interface Led as Led1;

    uses interface Init as LowInit;
    uses interface RawFrame;
    uses interface RawFrameSender as LowFrameSender;
    uses interface RawFrameReceiver as LowFrameReceiver;

    uses interface Timer<TMilli, uint32_t> as Timer;

    uses interface InterruptSource as DebugFault @exactlyonce();
}


implementation {
    platform_frame_t txFr, rxFr;

    event void Boot.booted() {
        call LowInit.init();
        call Timer.startWithTimeoutFromNow(4);
        call LowFrameReceiver.startReceiving(&rxFr);
    }

    event void LowFrameReceiver.receivingFinished(platform_frame_t *fp,
      error_t status) {
        uint8_t_xdata *data;

        if (120 == call RawFrame.getLength(&rxFr)) {
            data = call RawFrame.getData(&rxFr);
            if(data[0] == 42 && data[1] == 44) {
                call Led0.toggle();
            }
        }

        call LowFrameReceiver.startReceiving(&rxFr);
    }

    event void Timer.fired() {
        uint8_t_xdata *data;

        call RawFrame.setLength(&txFr, 120);

        data = call RawFrame.getData(&txFr);
        data[0] = 42;
        data[1] = 44;

        call LowFrameSender.startSending(&txFr);
    }

    event void LowFrameSender.sendingFinished(platform_frame_t * framePtr, error_t status) {
        if (status == SUCCESS) {
            call Led1.toggle();
        }
        call Timer.startWithTimeoutFromLastTrigger(4);

#if PLATFORM_CC26XX_STARTUP_HAS_RESET_HOOK
      EndOfTrace();
#endif
    }

    async void event DebugFault.interruptFired() {
#if PLATFORM_CC26XX_STARTUP_HAS_RESET_HOOK
        DebugMonitorISR();
#endif
    }
}
