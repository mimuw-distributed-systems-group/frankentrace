#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <stddef.h>

typedef struct r {
    uint8_t cycles;
    uint8_t aspr;
    uint8_t tag;
} __attribute__((packed)) result_t;

extern uint32_t do_test(uint8_t *res);

#ifndef BENCHMARK_WRITE_BUFFER_DIS
#define BENCHMARK_WRITE_BUFFER_DIS 1
#endif

#ifndef BENCHMARK_VIMS_LB_DIS
#define BENCHMARK_VIMS_LB_DIS 0
#endif


extern volatile uint8_t thunk_start_;
extern volatile uint8_t thunk_end_;
extern uint8_t* flash_mem_start;
extern uint8_t* gpram_mem_start;
extern uint8_t* sram_mem_start;


static volatile uint8_t* thunk_start = &thunk_start_;
static volatile uint8_t* thunk_end = &thunk_end_;

#ifndef HWREG
#define HWREG(addr) (*(volatile uint32_t*)(addr))
#endif

#if !CC26XX_VIMS_GPRAM_MODE
#define VIMS_CACHE 1
#else
#define VIMS_CACHE (HWREG(0x40034004)&1)
#endif
/*
 * Bits:
 * 0x31 - thunk_addr (code loc)
 * 0x4 - write_buffer (1=disabled)
 * 0x8 - vims line bufer (1=disabled)
 * 0x2 - vims mode (1=cache, 0=gpram)
 */
#define CONFIGURATION ((((uint32_t)thunk_start)>>24) | (BENCHMARK_VIMS_LB_DIS << 3) | (BENCHMARK_WRITE_BUFFER_DIS << 2) | (VIMS_CACHE<<1))
#endif
