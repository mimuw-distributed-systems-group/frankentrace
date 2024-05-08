include ../../../platforms/parts/mcu/cortex-m3/Makefile.common

OBJS := itm.o
CFLAGS += -I../../../platforms/parts/mcu/cc26xx/native/cc26xxware
CFLAGS += -DBENCHMARK_VIMS_LB_DIS=${BENCHMARK_VIMS_LB_DIS} -DBENCHMARK_WRITE_BUFFER_DIS=${BENCHMARK_WRITE_BUFFER_DIS}

all: $(OBJS)

# Recompile on flags change
$(OBJS): build.spec ../build.spec Makefile
