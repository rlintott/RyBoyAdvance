#include <string>
#include "ARM7TDMI.h"
#include "Bus.h"
#include "GameBoyAdvance.h"
#include "PPU.h"


void runCpuWithState() {
    PPU ppu;
    Bus bus{&ppu};
    ARM7TDMI cpu;
    GameBoyAdvance gba(&cpu, &bus);

    cpu.setCurrInstruction(0x0000D8FC);

    cpu.cpsr.T = 1;

    cpu.step();
}


int main() {
    runCpuWithState();
    return 0;
}

