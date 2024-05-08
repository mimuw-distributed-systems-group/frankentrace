![frankentrace_logo_finaler_fixed_512](https://user-images.githubusercontent.com/1452106/225130445-baa8693d-df0d-41a2-802f-4f04ff659413.png)
# FrankenTrace
Low-Cost, Cycle-Level, Widely Applicable Program Execution Tracing for ARM Cortex-M SoC

This repository contains supplementary materials to the published research paper available at https://doi.org/10.1145/3576914.3587521.

## Citing
```bibtex
@inproceedings{10.1145/3576914.3587521,
author = {Matraszek, Maciej and Banaszek, Mateusz and Ciszewski, Wojciech and Iwanicki, Konrad},
title = {FrankenTrace: Low-Cost, Cycle-Level, Widely Applicable Program Execution Tracing for ARM Cortex-M SoC},
year = {2023},
isbn = {9798400700491},
publisher = {Association for Computing Machinery},
address = {New York, NY, USA},
url = {https://doi.org/10.1145/3576914.3587521},
doi = {10.1145/3576914.3587521},
abstract = {Program execution tracing is an important technique in software development and analysis. However, noninvasively obtaining cycle-level traces for modern low-power ARMv7-M-based SoCs is challenging, because convenient off-the-shelf high-speed tracing probes are expensive and cannot be applied to SoCs that lack high-speed debug components, notably Embedded Trace Macrocell (ETM) and parallel tracing port (PTP). To address this issue, in this work, we present FrankenTrace, a technique for generating full, noninvasive, cycle-level program counter traces and full, cycle-level data transfer traces of varying invasiveness on SoCs with only low-speed debug components, namely Debug Watchpoint and Trace unit (DWT), Instrumentation Trace Macrocell (ITM), Single Wire Output (SWO), and an inexpensive probe. We demonstrate the technique by tracing software running on a node of the 1KT testbed.},
booktitle = {Proceedings of Cyber-Physical Systems and Internet of Things Week 2023},
pages = {72–77},
numpages = {6},
keywords = {ARM Cortex-M, DWT, ETM, ITM, cycle-level trace, embedded debugging, execution trace, low-cost tracing probe, memory trace},
location = {San Antonio, TX, USA},
series = {CPS-IoT Week '23}
}
```
