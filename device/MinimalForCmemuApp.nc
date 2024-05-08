configuration MinimalForCmemuApp {
}

implementation {
    components BoardStartupPub, MinimalForCmemuPrv as AppPrv;
    AppPrv.Boot -> BoardStartupPub;

    //components SleepDisablePub;
    //AppPrv.SleepSwitch -> SleepDisablePub;

    components HplCC26xxIntSrcPub as INTS;
    AppPrv.DebugFault -> INTS.DebugFault;
}
