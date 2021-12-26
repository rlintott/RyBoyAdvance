
#pragma once
#include "../ARM7TDMI.h"
#include "../../memory/Bus.h"


template<uint16_t op>
ARM7TDMI::FetchPCMemoryAccess ARM7TDMI::armSwiHandler(uint32_t instruction, ARM7TDMI* cpu) {
    constexpr uint16_t opcode = (op & 0xF00);
    if constexpr(opcode == 0xF00) {
        // 1111b: SWI{cond} nn   ;software interrupt
        cpu->switchToMode(Mode::SUPERVISOR);
        // switch to ARM mode, interrupts disabled
        *(cpu->currentSpsr) = cpu->cpsr;
        cpu->cpsr.Mode = Mode::SUPERVISOR;
        cpu->cpsr.T = 0;
        cpu->cpsr.I = 1; 
        cpu->setRegister(LINK_REGISTER, cpu->getRegister(PC_REGISTER));
        cpu->setRegister(PC_REGISTER, 0x8);
    } else if constexpr(opcode == 0x100) {
        // 0001b: BKPT      nn   ;breakpoint (ARMv5 and up)
        DEBUGWARN("BKPT instruction not implemented!\n");
    } else {
        assert(false);
    }

    return BRANCH;

}