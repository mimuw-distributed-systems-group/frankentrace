#include <stdio.h>
#include <inc/hw_nvic.h>
#include <driverlib/cpu.h>
#include <driverlib/osc.h>
#include <driverlib/ioc.h>
#include <driverlib/vims.h>
#include <driverlib/prcm.h>
#include <driverlib/sys_ctrl.h>

#include "../debug.h"

/*
 * TPIU is clocked with the main 48MHz clock.
 * The clock is divided by PRESCALER + 1.
 */
enum TPIU_PRESCALER_FOR_BAUDRATE {
    BAUD_PRESCALER_460800 = 103,
    BAUD_PRESCALER_1M = 47,
    BAUD_PRESCALER_8M = 5,
    BAUD_PRESCALER_8M_FOR_CPUDIV2 = 2, // when CPU is slowed down
    BAUD_PRESCALER_24M = 1,
    BAUD_PRESCALER_48M = 0,
};

// This should not be shifted and already has CYCCNTNA bit set
enum FASTEST_PC_SAMPLING_FOR_BAUDRATE {
    TAP_POST_192_FOR_24M = 0x005,
    TAP_POST_512_FOR_8M = 0x00f,
    TAP_POST_1024_FOR_4M = 0x201,
    TAP_POST_5120_FOR_1M = 0x209,
};
const uint32_t BAUD_PRESCALER = BAUD_PRESCALER_8M;
//const uint32_t BAUD_PRESCALER = BAUD_PRESCALER_8M_FOR_CPUDIV2;
const uint32_t TAP_POST = TAP_POST_512_FOR_8M;
const uint32_t PC_SAMPLE_EVERY = 512;

const uint32_t PC_TRACE_BASIC_CONF = DWT_CTRL_PCSAMPLENA_Msk | (0 << DWT_CTRL_SYNCTAP_Pos) | DWT_CTRL_CYCCNTENA_Msk;


#define ITM_FUNC_ATTR  __attribute__((used,noinline))

void ITM_FUNC_ATTR enable_xtal(){
    // This should be achievable be seeting CC26XX_XOSC_HF_ALWAYS_ON, but smake disallows duplicate definition
    OSCInterfaceEnable();
    OSCHF_TurnOnXosc();
    // Actually switch to XOSC_HF.
    while (!OSCHF_AttemptToSwitchToXosc()) /* nop */;
}

void ITM_FUNC_ATTR setup_cpu_clk_div(){
    HWREG(PRCM_NONBUF_BASE + PRCM_O_CPUCLKDIV) |= PRCM_CPUCLKDIV_RATIO_DIV2;

    // Wait for switch
    HWREG(PRCM_NONBUF_BASE + PRCM_O_CLKLOADCTL) |= PRCM_CLKLOADCTL_LOAD;
    while (HWREG(PRCM_NONBUF_BASE + PRCM_O_CLKLOADCTL) != PRCM_CLKLOADCTL_LOAD_DONE) /* nop */;
}

void ITM_FUNC_ATTR configure_itm() {
    IOCPortConfigureSet(IOID_21, IOC_PORT_MCU_SWV, IOC_STD_OUTPUT);
    ITM->LAR = 0xC5ACCE55;                           // Instruction Trace Macrocell, enable tracing control registers

    /* Configure TPIU */
    TPI->FFCR &= ~TPI_FFCR_EnFCont_Msk; // Disable formatter - we don't have ETM Anyway
    TPI->SPPR = 2; // Change mode to UART (no parity bit, 1 stop bit)
    TPI->ACPR = BAUD_PRESCALER;

    /* Configure ITM */
    ITM->TER = ~0; // Enable all stimuli ports
    ITM->TCR = ITM_TCR_ITMENA_Msk | ITM_TCR_DWTENA_Msk /*| ITM_TCR_TSENA_Msk*/; // Enable only ITM and DWT stimuli with timestamps

    /* Configure DWT */
    DWT->CTRL = DWT_CTRL_CYCCNTENA_Msk; /* disable all events and just run the counter */
}

void ITM_FUNC_ATTR start_trace(const uint16_t offset) {
    const uint32_t beacon = 0xCACA0000 | offset;
    const uint32_t cyc_tap = (TAP_POST & 0x200) ? 10 : 6;
    const uint32_t cyc_mask = (1<<cyc_tap)-1;
    const uint32_t post_init = (offset - 1) >> cyc_tap;
    const uint32_t cyc_init = ((1<<cyc_tap) - offset)&cyc_mask;
    const uint32_t dwt_ctrl = PC_TRACE_BASIC_CONF | TAP_POST | ((post_init << DWT_CTRL_POSTINIT_Pos)&DWT_CTRL_POSTINIT_Msk);

    uint32_t scratch = 1;
    ITM->TCR = ITM_TCR_ITMENA_Msk | ITM_TCR_DWTENA_Msk | ITM_TCR_TSENA_Msk; // Enable only ITM and DWT stimuli with timestamps

    // Software must write to DWT_CTRL.POSTINIT to define the required initial value of the POSTCNT counter, and
    //then perform a second write to DWT_CTRL to set either the PCSAMPLENA...
    __asm volatile (
        // and.w can have a shifted byte
            "and.w %[scratch], %[dwt_ctrl], 0x1fe \n" // First set POSTINIT and disable other bits
            ".align 3 \n"
            "str.w %[scratch], [%[dwt_base], 0] \n"      // prevent accidential triggers be writing 1
            "isb.w \n"
            "str.n %[beacon], [%[itm_stim_base], %[itm_port]*4] \n" // use port as below
            "str.n %[cyc_init], [%[dwt_base], 4] \n" // Set starting point
            "str.n %[dwt_ctrl], [%[dwt_base], 0] \n" // DWT_CTRL - start ticking from postinit

            ".rept 128; add.n %[scratch], %[scratch]; .endr \n" // spin a while for a predictable position
            : [scratch] "+l" (scratch)
    : [beacon] "l" (beacon), [itm_stim_base] "r" (ITM_BASE), [itm_port] "I" (1),
    [dwt_base] "l" (DWT_BASE), [cyc_init] "l" (cyc_init), [dwt_ctrl] "l" (dwt_ctrl)
    );
}

#define LSU_TRACE_DISABLED 0
#define LSU_TRACE_RW_EMIT_PC 0x01
#define LSU_TRACE_RW_EMIT_ADDR 0x21
#define LSU_TRACE_RW_EMIT_DATA 0x02
#define LSU_TRACE_RW_EMIT_ADDR_AND_DATA 0x22
#define LSU_TRACE_RW_EMIT_PC_AND_DATA 0x03

#define LSU_TRACE_RO_EMIT_ADDR 0x2c
#define LSU_TRACE_RO_EMIT_DATA 0x0c
#define LSU_TRACE_RO_EMIT_ADDR_AND_DATA 0x2e
#define LSU_TRACE_RO_EMIT_PC_AND_DATA 0x0e

#define LSU_TRACE_WO_EMIT_ADDR                1|LSU_TRACE_RO_EMIT_ADDR
#define LSU_TRACE_WO_EMIT_DATA                1|LSU_TRACE_RO_EMIT_DATA
#define LSU_TRACE_WO_EMIT_ADDR_AND_DATA       1|LSU_TRACE_RO_EMIT_ADDR_AND_DATA
#define LSU_TRACE_WO_EMIT_PC_AND_DATA         1|LSU_TRACE_RO_EMIT_PC_AND_DATA

#define LSU_TRACE_RO_DEBUG_EVENT 0x05
#define LSU_TRACE_WO_DEBUG_EVENT 0x06
#define LSU_TRACE_RW_DEBUG_EVENT 0x07

void ITM_FUNC_ATTR start_lsu_trace(uint32_t addr, uint32_t masked_bits, uint32_t function, uint8_t comp) {
#define set_comp(i) do {\
    DWT->FUNCTION##i = LSU_TRACE_DISABLED; \
    DWT->COMP##i = addr & ((~0)<<masked_bits); \
    DWT->MASK##i = masked_bits << DWT_MASK_MASK_Pos; \
    DWT->FUNCTION##i = function;} while(0)
   switch(comp) {
       case 0: set_comp(0);
       case 1: set_comp(1);
       case 2: set_comp(2);
       case 3: set_comp(3);
   }
#undef set_comp
}

void ITM_FUNC_ATTR end_lsu_trace() {
    start_lsu_trace(0, 0, LSU_TRACE_DISABLED, 0);
    start_lsu_trace(0, 0, LSU_TRACE_DISABLED, 1);
    start_lsu_trace(0, 0, LSU_TRACE_DISABLED, 2);
    start_lsu_trace(0, 0, LSU_TRACE_DISABLED, 3);
}

void ITM_FUNC_ATTR pause_trace() {
    DWT->CTRL = DWT_CTRL_CYCCNTENA_Msk; /* disable all events and just run the counter */
    ITM->PORT[1].u32 = 0xB0BAFEED;
    ITM->TCR = 0; // Disable timestamping every 2M cycles (makes easier triggers)
}

void ITM_FUNC_ATTR prepare_for_tracing() {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;  // Debug Exception and Monitor Control, enable DWT/ITM/ETM/TPIU access
    ITM->LAR = 0xC5ACCE55;                           // Instruction Trace Macrocell, enable tracing control registers

    /* Configure CYCCNT */
    DWT->CYCCNT = 0;                                 // Reset counter
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;             // Enable counter

    /* Configure CPICNT */
    DWT->CPICNT &= 0xFFFFFF00;                       // Reset counter (high 24 bits are reserved)
    /* Configure LSUCNT */
    DWT->LSUCNT &= 0xFFFFFF00;                       // Reset counter (high 24 bits are reserved)
    /* Configure FOLDCNT */
    DWT->FOLDCNT &= 0xFFFFFF00;                      // Reset counter (high 24 bits are reserved)
    /* Configure SLEEPCNT */
    DWT->SLEEPCNT &= 0xFFFFFF00;                     // Reset counter (high 24 bits are reserved)
    /* Configure EXCCNT */
    DWT->EXCCNT &= 0xFFFFFF00;                       // Reset counter (high 24 bits are reserved)
    // DWT counters will generate events on their own

//    enable_xtal();
    if (BAUD_PRESCALER == BAUD_PRESCALER_8M_FOR_CPUDIV2) {
        // FIXME: this is just value of 2, it may be used for other prescalers as well!
        setup_cpu_clk_div();
    }
    configure_itm();
}

void ITM_FUNC_ATTR DebugMonitorISR() {
    CPUdelay(200);
}

#include "inc/hw_memmap.h"
#include "inc/hw_cpu_dwt.h"

void setup_lsu_trace(uint32_t high_16b_a, uint32_t high_16b_b, uint32_t emission) {
    CPUdelay(128);
    ITM->PORT[4].u16 = high_16b_a; // chan 0
    ITM->PORT[5].u16 = high_16b_b; // chan 2
    ITM->PORT[6].u8 = emission;
    CPUdelay(128);
    ITM->PORT[7].u32 = 0xD00DBACA;
    CPUdelay(128);
    if (emission == LSU_TRACE_DISABLED) {
        DWT->CTRL |= DWT_CTRL_EXCTRCENA_Msk;
    }
    start_lsu_trace(high_16b_a<<16, 15, emission, 0);
    start_lsu_trace(high_16b_a<<16, 15, LSU_TRACE_RW_DEBUG_EVENT, 1);
    start_lsu_trace(high_16b_b<<16, 15, emission, 2);
    start_lsu_trace(high_16b_b<<16, 15, LSU_TRACE_RW_DEBUG_EVENT, 3);
}

const uint32_t TRACE_RANGES[] = {
        /*
//        0x08000000 - 0x08020000
        0x2100,

        0x4000,
        0x4001,
        0x4002,
        0x4003,
        0x4004,
        0x4008,
        0x4009,
        0x400C,
        0x400E,

//        0x42440000 - 0x42460000
//        0x42600000 - 0x42620000
//        0x42640000 - 0x42660000
//        0x42680000 - 0x426a0000
//        0x42800000 - 0x428e0000
//        0x42900000 - 0x42920000
//        0x43000000 - 0x430d0000
//        0x43200000 - 0x43400000
//        0x43820000 - 0x43860000
//        0x43880000 - 0x43980000
//        0x43c00000 - 0x43c40000

        0x5000,

        0x6000,
        0x6001,
        0x6002,
        0x6003,
        0x6004,
        0x6008,
        0x6009,
        0x600C,
        0x600E,

//      Radio bitband
//        0x42800000 - 0x428e0000
         */
        // Bitband
        0x4244,
        0x4245,
        0x4260,
        0x4261,
        0x4264,
        0x4265,
        0x4268,
        0x4269,
        0x4280,
        0x4281,
        0x4282,
        0x4283,
        0x4284,
        0x4285,
        0x4286,
        0x4287,
        0x4288,
        0x4289,
        0x428a,
        0x428b,
        0x428c,
        0x428d,
        0x4290,
        0x4291,
        0x4300,
        0x4301,
        0x4302,
        0x4303,
        0x4304,
        0x4305,
        0x4306,
        0x4307,
        0x4308,
        0x4309,
        0x430a,
        0x430b,
        0x430c,
        0x4320,
        0x4321,
        0x4322,
        0x4323,
        0x4324,
        0x4325,
        0x4326,
        0x4327,
        0x4328,
        0x4329,
        0x432a,
        0x432b,
        0x432c,
        0x432d,
        0x432e,
        0x432f,
        0x4330,
        0x4331,
        0x4332,
        0x4333,
        0x4334,
        0x4335,
        0x4336,
        0x4337,
        0x4338,
        0x4339,
        0x433a,
        0x433b,
        0x433c,
        0x433d,
        0x433e,
        0x433f,
        0x4382,
        0x4383,
        0x4384,
        0x4385,
        0x4388,
        0x4389,
        0x438a,
        0x438b,
        0x438c,
        0x438d,
        0x438e,
        0x438f,
        0x4390,
        0x4391,
        0x4392,
        0x4393,
        0x4394,
        0x4395,
        0x4396,
        0x4397,
        0x43c0,
        0x43c1,
        0x43c2,
        0x43c3,
};

int ITM_FUNC_ATTR setup_next_lsu_trace_phase(int phase_no) {
    const uint32_t modes[4] = {
            // emit a single value, and one run with no emission for reference.
            LSU_TRACE_DISABLED, LSU_TRACE_RW_EMIT_ADDR, LSU_TRACE_RW_EMIT_DATA, LSU_TRACE_RW_EMIT_PC
    };
    const size_t ranges = sizeof(TRACE_RANGES)/sizeof(TRACE_RANGES[0]);

    phase_no -= 1; // we count from 1
    uint32_t pos = phase_no / 4;
    uint32_t a_pos = pos / ranges;
    uint32_t b_pos = pos - a_pos*ranges;

    if (a_pos < ranges) {
        setup_lsu_trace(TRACE_RANGES[a_pos], TRACE_RANGES[b_pos], modes[phase_no&3]);
    } else {
        return 0;
    }

    // 1 deduced at start
    return phase_no+2;
}

int ITM_FUNC_ATTR setup_next_pc_trace_phase(int phase_no) {
    start_trace(phase_no);
    return phase_no + 1;
}

// We be called again after restart with the returned value unless it is 0.
int ITM_FUNC_ATTR next_trace_phase(int phase_no) {
    const int pc_reps = PC_SAMPLE_EVERY + 2; // We do extra repetitions to verify repeatability.
    int ret;

    prepare_for_tracing();
    CoreDebug->DEMCR |= CoreDebug_DEMCR_MON_EN_Msk;  // Debug Exception and Monitor Control, enable Debug monitor exception
    ITM->TCR = ITM_TCR_ITMENA_Msk | ITM_TCR_DWTENA_Msk | ITM_TCR_TSENA_Msk; // Enable only ITM and DWT stimuli with timestamps
    // upper addresses:
    // 0x21000000: RFC RAM
    // 0x40000000: UART (SSI0, I2C, SSI1)
    // 0x40010000: GPT
    // 0x40020000: GPIO, UDMA, I2S0, Crypto, TRNG
    // 0x40030000: VIMS/FLASH
    // 0x40040000: Radio regs
    // 0x40080000: perbusull (WDT, IOC, PRCM, EVENT, SMPH)
    // 0x40090000:  AON
    // 0x400C0000: AUX (analog)
    // 0x400E0000: Sensor Controller
    // 0x50000000:  configuration area in flash
    // 0x600X0000: All the same as 400x, but unbuffered alias
    // 0xE0000000:  CPU
    ITM->PORT[1].u32 = 0xBABA0000 | phase_no;

    if (phase_no <= pc_reps) {
        ret = setup_next_pc_trace_phase(phase_no);
    } else {
        ret = setup_next_lsu_trace_phase(phase_no - pc_reps);
        ret = ret > 0 ? ret + pc_reps : 0;
    }
    // prevent code reordering
    // shared point of reference between PC and LSU
    __asm ("isb.w");

    IOCPortConfigureGet(IOID_21);
    return ret;
}

// Pin/system reset doesn't powerdown SRAM, thus its contents is retained.
// We use non-init section to allocate some addresses
#define SURVIVES_RESET __attribute__((section(".heap.itm")))

volatile uint32_t trace_offset SURVIVES_RESET;
volatile uint32_t trace_magic SURVIVES_RESET;
const uint32_t END_MAGIC = 0xfeeddead;
const uint32_t MEM_MAGIC = 0xcacab0b0;

// See heni-whip6/nesc/whip6/platforms/parts/mcu/cc26xx/native/startup/startup.c
void ITM_FUNC_ATTR ResetHook() {
    uint32_t reset_reason = SysCtrlResetSourceGet();

    if (reset_reason == RSTSRC_PWR_ON || reset_reason == RSTSRC_PIN_RESET) {
        // This is a new run for us
        trace_magic = MEM_MAGIC + 1;
        trace_offset = 1;
    }
    // Programming is SYSRESET


    HWREG(NVIC_PRI0) |= 0x88888888;
    HWREG(NVIC_PRI1) |= 0x88888888;
    HWREG(NVIC_PRI2) |= 0x88888888;
    HWREG(NVIC_PRI3) |= 0x88888888;
    HWREG(NVIC_PRI4) |= 0x88888888;
    HWREG(NVIC_PRI5) |= 0x88888888;

    if (trace_magic == END_MAGIC) {
        trace_offset = 0;
    } else {
        if (trace_magic - trace_offset != MEM_MAGIC) {
            // seems like memory reset
            trace_magic = MEM_MAGIC + 1;
            trace_offset = 1;
        }

        trace_offset = next_trace_phase(trace_offset);
        trace_magic = trace_offset + MEM_MAGIC;
    }
}

void ITM_FUNC_ATTR EndOfTrace() {
    pause_trace();

    if (trace_magic != END_MAGIC) {
        // returning 0 marks end of the times -- reset for the last time
        if (trace_offset == 0) {
            trace_magic = END_MAGIC;
            ITM->PORT[8].u32 = 0xBA1BA1AF;
        }
        CPUdelay(1000); // Allow all outstanding transfers to finish
        SysCtrlSystemReset(); // Never returns
    }
}
