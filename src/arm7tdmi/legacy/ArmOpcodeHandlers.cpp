#include <bitset>

#include "../ARM7TDMI.h"
#include "../../memory/Bus.h"
#include "assert.h"

// TODO: after an I cycle, the next data access cycle is always non sequential (unique to GBA)
inline
ARM7TDMI::FetchPCMemoryAccess ARM7TDMI::multiplyHandler(uint32_t instruction) {
    uint8_t opcode = getOpcode(instruction);
    // rd is different for multiply
    uint8_t rd = (instruction & 0x000F0000) >> 16;
    uint8_t rm = getRm(instruction);
    uint8_t rs = getRs(instruction);
    assert(rd != rm && (rd != PC_REGISTER && rm != PC_REGISTER && rs != PC_REGISTER));
    assert((instruction & 0x000000F0) == 0x00000090);
    uint64_t result;
    BitPreservedInt64 longResult;
    int internalCycles;

    switch (opcode) {
        case 0b0000: {  // MUL
            uint32_t rsVal = getRegister(rs);
            result = (uint64_t)getRegister(rm) * (uint64_t)rsVal;
            setRegister(rd, (uint32_t)result);
            internalCycles = mulGetExecutionTimeMVal(rsVal);
            break;
        }
        case 0b0001: {  // MLA
            // rn is different for multiply
            uint8_t rn = (instruction & 0x0000F000) >> 12;
            uint32_t rsVal = getRegister(rs);
            assert(rn != PC_REGISTER);
            result = (uint64_t)getRegister(rm) *
                     (uint64_t)rsVal +
                     (uint64_t)getRegister(rn);
            setRegister(rd, (uint32_t)result);
            internalCycles = mulGetExecutionTimeMVal(rsVal) + 1;
            break;
        }
        case 0b0100: {  // UMULL{cond}{S} RdLo,RdHi,Rm,Rs ;RdHiLo=Rm*Rs
            uint32_t rsVal = getRegister(rs);
            uint8_t rdhi = rd;
            uint8_t rdlo = (instruction & 0x0000F000) >> 12;
            longResult._unsigned = (uint64_t)getRegister(rm) * (uint64_t)rsVal;
            // high destination reg
            setRegister(rdhi, (uint32_t)(longResult._unsigned >> 32));
            setRegister(rdlo, (uint32_t)longResult._unsigned);
            internalCycles = umullGetExecutionTimeMVal(rsVal) + 1;
            break;
        }
        case 0b0101: {  // UMLAL {cond}{S} RdLo,RdHi,Rm,Rs ;RdHiLo=Rm*Rs+RdHiLo
            uint32_t rsVal = getRegister(rs);
            uint8_t rdhi = rd;
            uint8_t rdlo = (instruction & 0x0000F000) >> 12;
            longResult._unsigned =
                (uint64_t)getRegister(rm) *
                (uint64_t)getRegister(rs) +
                ((((uint64_t)(getRegister(rdhi))) << 32) | ((uint64_t)(getRegister(rdlo))));
            // high destination reg
            setRegister(rdhi, (uint32_t)(longResult._unsigned >> 32));
            setRegister(rdlo, (uint32_t)longResult._unsigned);
            internalCycles = umullGetExecutionTimeMVal(rsVal) + 2;
            break;
        }
        case 0b0110: {  // SMULL {cond}{S} RdLo,RdHi,Rm,Rs ;RdHiLo=Rm*Rs
            uint32_t rsValRaw = getRegister(rs);
            uint8_t rdhi = rd;
            uint8_t rdlo = (instruction & 0x0000F000) >> 12;
            BitPreservedInt32 rmVal;
            BitPreservedInt32 rsVal;
            rmVal._unsigned = getRegister(rm);
            rsVal._unsigned = rsValRaw;
            longResult._signed =
                (int64_t)rmVal._signed * (int64_t)rsVal._signed;
            // high destination reg
            setRegister(rdhi, (uint32_t)(longResult._unsigned >> 32));
            setRegister(rdlo, (uint32_t)longResult._unsigned);
            internalCycles = mulGetExecutionTimeMVal(rsValRaw) + 1;
            break;
        }
        case 0b0111: {  // SMLAL{cond}{S} RdLo,RdHi,Rm,Rs ;RdHiLo=Rm*Rs+RdHiLo
            uint32_t rsValRaw = getRegister(rs);
            uint8_t rdhi = rd;
            uint8_t rdlo = (instruction & 0x0000F000) >> 12;
            BitPreservedInt32 rmVal;
            BitPreservedInt32 rsVal;
            rmVal._unsigned = getRegister(rm);
            rsVal._unsigned = rsValRaw;
            BitPreservedInt64 accum;
            accum._unsigned = ((((uint64_t)(getRegister(rdhi))) << 32) |
                                ((uint64_t)(getRegister(rdlo))));
            longResult._signed = (int64_t)rmVal._signed * (int64_t)rsVal._signed + accum._signed;
            // high destination reg
            setRegister(rdhi, (uint32_t)(longResult._unsigned >> 32));
            setRegister(rdlo, (uint32_t)longResult._unsigned);
            internalCycles = mulGetExecutionTimeMVal(rsValRaw) + 2;
            break;
        }
    }

    if (sFlagSet(instruction)) {
        if (!(opcode & 0b0100)) {  // regular mult opcode,
            cpsr.Z = aluSetsZeroBit((uint32_t)result);
            cpsr.N = aluSetsSignBit((uint32_t)result);
        } else {
            cpsr.Z = (longResult._unsigned == 0);
            cpsr.N = (longResult._unsigned >> 63);
        }
        // cpsr.C = 0;
        // cpsr.V = 0;
    }
    // TODO: can probably optimize this greatly
    for(int i = 0; i < internalCycles; i++) {
        bus->addCycleToExecutionTimeline(Bus::CycleType::INTERNAL, 0, 0);
    }
    if(internalCycles == 0) {
        return SEQUENTIAL;
    } else {
        return NONSEQUENTIAL;
    }
}

/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ PSR Transfer (MRS, MSR) Operations
 * ~~~~~~~~~~~~~~~~~~~~*/
inline
ARM7TDMI::FetchPCMemoryAccess ARM7TDMI::psrHandler(uint32_t instruction) {
    assert(!(instruction & 0x0C000000));
    assert(!sFlagSet(instruction));
    // bit 25: I - Immediate Operand Flag
    // (0=Register, 1=Immediate) (Zero for MRS)
    bool immediate = (instruction & 0x02000000);
    // bit 22: Psr - Source/Destination PSR
    // (0=CPSR, 1=SPSR_<current mode>)
    bool psrSource = (instruction & 0x00400000);

    // bit 21: special opcode for PSR
    switch ((instruction & 0x00200000) >> 21) {
        case 0: {  // MRS{cond} Rd,Psr ; Rd = Psr

            assert(!immediate);
            assert(getRn(instruction) == 0xF);
            assert(!(instruction & 0x00000FFF));
            uint8_t rd = getRd(instruction);
            if (!psrSource) {

                setRegister(rd, psrToInt(cpsr));
            } else {

                setRegister(rd, psrToInt(*(getCurrentModeSpsr())));
            }
            break;
        }
        case 1: {  // MSR{cond} Psr{_field},Op  ;Psr[field] = Op=
            assert((instruction & 0x0000F000) == 0x0000F000);
            uint8_t fscx = (instruction & 0x000F0000) >> 16;
            ProgramStatusRegister *psr = (!psrSource ? &(cpsr) : getCurrentModeSpsr());

            if (immediate) {

                uint32_t immValue = (uint32_t)(instruction & 0x000000FF);
                uint8_t shift = (instruction & 0x00000F00) >> 7;
                transferToPsr(aluShiftRor(immValue, shift), fscx, psr);
            } else {  // register

                assert(!(instruction & 0x00000FF0));
                assert(getRm(instruction) != PC_REGISTER);
                // TODO: refactor this, don't have to pass in a pointer to the psr
                transferToPsr(getRegister(getRm(instruction)), fscx, psr);
            }
            break;
        }
    }

    return SEQUENTIAL;
}

/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ ALU OPERATIONS
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
// TODO: put assertions for every unexpected or unallowed circumstance (for
// deubgging)
// TODO: cycle calculation
/*
    0: AND{cond}{S} Rd,Rn,Op2    ;AND logical       Rd = Rn AND Op2
    1: EOR{cond}{S} Rd,Rn,Op2    ;XOR logical       Rd = Rn XOR Op2
    2: SUB{cond}{S} Rd,Rn,Op2 ;* ;subtract          Rd = Rn-Op2
    3: RSB{cond}{S} Rd,Rn,Op2 ;* ;subtract reversed Rd = Op2-Rn
    4: ADD{cond}{S} Rd,Rn,Op2 ;* ;add               Rd = Rn+Op2
    5: ADC{cond}{S} Rd,Rn,Op2 ;* ;add with carry    Rd = Rn+Op2+Cy
    6: SBC{cond}{S} Rd,Rn,Op2 ;* ;sub with carry    Rd = Rn-Op2+Cy-1
    7: RSC{cond}{S} Rd,Rn,Op2 ;* ;sub cy. reversed  Rd = Op2-Rn+Cy-1
    8: TST{cond}{P}    Rn,Op2    ;test            Void = Rn AND Op2
    9: TEQ{cond}{P}    Rn,Op2    ;test exclusive  Void = Rn XOR Op2
    A: CMP{cond}{P}    Rn,Op2 ;* ;compare         Void = Rn-Op2
    B: CMN{cond}{P}    Rn,Op2 ;* ;compare neg.    Void = Rn+Op2
    C: ORR{cond}{S} Rd,Rn,Op2    ;OR logical        Rd = Rn OR Op2
    D: MOV{cond}{S} Rd,Op2       ;move              Rd = Op2
    E: BIC{cond}{S} Rd,Rn,Op2    ;bit clear         Rd = Rn AND NOT Op2
    F: MVN{cond}{S} Rd,Op2       ;not               Rd = NOT Op2
*/
inline
ARM7TDMI::FetchPCMemoryAccess ARM7TDMI::dataProcHandler(uint32_t instruction) {
    // shift op2
    bool i = (instruction & 0x02000000);
    bool r = (instruction & 0x00000010);

    AluShiftResult shiftResult = ARM7TDMI::aluShift(instruction, i, r);
    uint8_t rd = getRd(instruction);
    uint8_t rn = getRn(instruction);
    uint8_t opcode = getOpcode(instruction);
    bool carryBit = (cpsr).C;
    bool overflowBit = (cpsr).V;
    bool signBit = (cpsr).N;
    bool zeroBit = (cpsr).Z;

    uint32_t rnVal;
    // if rn == pc regiser, have to add to it to account for pipelining /
    // prefetching
    // TODO probably dont need this logic if pipelining is emulated
    if (rn != PC_REGISTER) {
        rnVal = getRegister(rn);
    } else if (!(instruction & 0x02000000) && (instruction & 0x00000010)) {
        rnVal = getRegister(rn) + 8;
    } else {
        rnVal = getRegister(rn) + 4;
    }
    uint32_t op2 = shiftResult.op2;

    switch (opcode) {
        case AND: {  // AND
            uint32_t result = rnVal & op2;
            setRegister(rd, result);
            zeroBit = aluSetsZeroBit(result);
            signBit = aluSetsSignBit(result);
            carryBit = shiftResult.carry;
            break;
        }
        case EOR: {  // EOR
            uint32_t result = rnVal ^ op2;
            setRegister(rd, result);
            zeroBit = aluSetsZeroBit(result);
            signBit = aluSetsSignBit(result);
            carryBit = shiftResult.carry;
            break;
        }
        case SUB: {  // SUB
            uint32_t result = rnVal - op2;
            setRegister(rd, result);
            zeroBit = aluSetsZeroBit(result);
            signBit = aluSetsSignBit(result);
            carryBit = aluSubtractSetsCarryBit(rnVal, op2);
            overflowBit = aluSubtractSetsOverflowBit(rnVal, op2, result);
            break;
        }
        case RSB: {  // RSB
            uint32_t result = op2 - rnVal;
            setRegister(rd, result);
            zeroBit = aluSetsZeroBit(result);
            signBit = aluSetsSignBit(result);
            carryBit = aluSubtractSetsCarryBit(op2, rnVal);
            overflowBit = aluSubtractSetsOverflowBit(op2, rnVal, result);
            break;
        }
        case ADD: {  // ADD
            uint32_t result = rnVal + op2;
            setRegister(rd, result);
            zeroBit = aluSetsZeroBit(result);
            signBit = aluSetsSignBit(result);
            carryBit = aluAddSetsCarryBit(rnVal, op2);
            overflowBit = aluAddSetsOverflowBit(rnVal, op2, result);
            break;
        }
        case ADC: {  // ADC
            uint64_t result = (uint64_t)rnVal + (uint64_t)op2 + (uint64_t)(cpsr).C;
            setRegister(rd, (uint32_t)result);
            zeroBit = aluSetsZeroBit((uint32_t)result);
            signBit = aluSetsSignBit((uint32_t)result);
            carryBit = aluAddWithCarrySetsCarryBit(result);
            overflowBit = aluAddWithCarrySetsOverflowBit(rnVal, op2, (uint32_t)result, this);
            break;
        }
        case SBC: {  // SBC
            uint64_t result = (uint64_t)rnVal + ~((uint64_t)op2) + (uint64_t)(cpsr).C;
            setRegister(rd, (uint32_t)result);
            zeroBit = aluSetsZeroBit((uint32_t)result);
            signBit = aluSetsSignBit((uint32_t)result);
            carryBit = aluSubWithCarrySetsCarryBit(result);
            overflowBit = aluSubWithCarrySetsOverflowBit(rnVal, op2, (uint32_t)result, this);
            break;
        }
        case RSC: {  // RSC
            uint64_t result = (uint64_t)op2 + ~((uint64_t)rnVal) + (uint64_t)(cpsr).C;
            setRegister(rd, (uint32_t)result);
            zeroBit = aluSetsZeroBit((uint32_t)result);
            signBit = aluSetsSignBit((uint32_t)result);
            carryBit = aluSubWithCarrySetsCarryBit(result);
            overflowBit = aluSubWithCarrySetsOverflowBit(op2, rnVal, (uint32_t)result, this);
            break;
        }
        case TST: {  // TST
            uint32_t result = rnVal & op2;
            carryBit = shiftResult.carry;
            zeroBit = aluSetsZeroBit(result);
            signBit = aluSetsSignBit(result);
            break;
        }
        case TEQ: {  // TEQ
            uint32_t result = rnVal ^ op2;
            carryBit = shiftResult.carry;
            zeroBit = aluSetsZeroBit(result);
            signBit = aluSetsSignBit(result);
            break;
        }
        case CMP: {  // CMP
            uint32_t result = rnVal - op2;
            zeroBit = aluSetsZeroBit(result);
            signBit = aluSetsSignBit(result);
            carryBit = aluSubtractSetsCarryBit(rnVal, op2);
            overflowBit = aluSubtractSetsOverflowBit(rnVal, op2, result);
            break;
        }
        case CMN: {  // CMN
            uint32_t result = rnVal + op2;
            zeroBit = aluSetsZeroBit(result);
            signBit = aluSetsSignBit(result);
            carryBit = aluAddSetsCarryBit(rnVal, op2);
            overflowBit = aluAddSetsOverflowBit(rnVal, op2, result);
            break;
        }
        case ORR: {  // ORR
            uint32_t result = rnVal | op2;
            setRegister(rd, result);
            zeroBit = aluSetsZeroBit(result);
            signBit = aluSetsSignBit(result);
            carryBit = shiftResult.carry;
            break;
        }
        case MOV: {  // MOV
            uint32_t result = op2;

            setRegister(rd, result);
            zeroBit = aluSetsZeroBit(result);
            carryBit = shiftResult.carry;
            signBit = aluSetsSignBit(result);
            break;
        }
        case BIC: {  // BIC
            uint32_t result = rnVal & (~op2);
            setRegister(rd, result);
            zeroBit = aluSetsZeroBit(result);
            signBit = aluSetsSignBit(result);
            carryBit = shiftResult.carry;
            break;
        }
        case MVN: {  // MVN
            uint32_t result = ~op2;
            setRegister(rd, result);
            zeroBit = aluSetsZeroBit(result);
            signBit = aluSetsSignBit(result);
            carryBit = shiftResult.carry;
            break;
        }
    }

    if (rd != PC_REGISTER && sFlagSet(instruction)) {
        cpsr.C = carryBit;
        cpsr.Z = zeroBit;
        cpsr.N = signBit;
        cpsr.V = overflowBit;
    } else if (rd == PC_REGISTER && sFlagSet(instruction)) {
        cpsr = *(getCurrentModeSpsr());
        switchToMode(ARM7TDMI::Mode((*(getCurrentModeSpsr())).Mode));
    } else {
    }  // flags not affected, not allowed in CMP

    Cycles cycles = {.nonSequentialCycles = 0,
                     .sequentialCycles = 1,
                     .internalCycles = 0,
                     .waitState = 0};
    // TODO: can potentially optimize a bit by using already existing condition checks earlier in code
    if(!i && r) {
        bus->addCycleToExecutionTimeline(Bus::CycleType::INTERNAL, 0, 0);
        if(rd == PC_REGISTER) {
            return BRANCH;
        } else {
            return NONSEQUENTIAL;
        }
    }
    if(rd == PC_REGISTER) {
        return BRANCH;
    } else {
        return SEQUENTIAL;
    }
}

/* ~~~~~~~~ SINGLE DATA TRANSFER ~~~~~~~~~*/
inline
ARM7TDMI::FetchPCMemoryAccess ARM7TDMI::singleDataTransHandler(uint32_t instruction) {
    // TODO:  implement the following restriction <expression>  ;an immediate
    // used as address
    // ;*** restriction: must be located in range PC+/-4095+8, if so,
    // ;*** assembler will calculate offset and use PC (R15) as base.
    assert((instruction & 0x0C000000) == (instruction & 0x04000000));
    uint8_t rd = getRd(instruction);
    uint32_t rdVal = (rd == 15) ? getRegister(rd) + 8 : getRegister(rd);
    uint8_t rn = getRn(instruction);
    uint32_t rnVal = (rn == 15) ? getRegister(rn) + 4 : getRegister(rn);

    uint32_t offset;
    // I - Immediate Offset Flag (0=Immediate, 1=Shifted Register)
    if ((instruction & 0x02000000)) {
        // Register shifted by Immediate as Offset
        assert(!(instruction & 0x00000010));  // bit 4 Must be 0 (Reserved, see
                                              // The Undefined Instruction)
        uint8_t rm = getRm(instruction);
        assert(rm != 15);
        uint8_t shiftAmount = (instruction & 0x00000F80) >> 7;
        switch ((instruction & 0x00000060) >> 5 /*shift type*/) {
            case 0: {  // LSL
                offset = shiftAmount != 0
                             ? aluShiftLsl(getRegister(rm), shiftAmount)
                             : getRegister(rm);
                break;
            }
            case 1: {  // LSR
                offset = shiftAmount != 0
                             ? aluShiftLsr(getRegister(rm), shiftAmount)
                             : 0;
                break;
            }
            case 2: {  // ASR
                offset = shiftAmount != 0
                             ? aluShiftAsr(getRegister(rm), shiftAmount)
                             : aluShiftAsr(getRegister(rm), 32);
            
                break;
            }
            case 3: {  // ROR
                offset =
                    shiftAmount != 0
                        ? aluShiftRor(getRegister(rm), shiftAmount % 32)
                        : aluShiftRrx(getRegister(rm), 1, this);
                break;
            }
        }
    } else {  // immediate as offset (12 bit offset)
        offset = instruction & 0x00000FFF;
    }
    uint32_t address = rnVal;

    // U - Up/Down Bit (0=down; subtract offset
    // from base, 1=up; add to base)
    bool u = dataTransGetU(instruction);

    // P - Pre/Post (0=post; add offset after
    // transfer, 1=pre; before trans.)
    bool p = dataTransGetP(instruction);

    if (p) {  // add offset before transfer
        address = u ? address + offset : address - offset;
        if (dataTransGetW(instruction)) {
            // write address back into base register
            setRegister(rn, address);
        }
    } else {
        // add offset after transfer and always write back
        setRegister(rn, u ? address + offset : address - offset);
    }

    bool b = dataTransGetB(instruction);  // B - Byte/Word bit (0=transfer
                                          // 32bit/word, 1=transfer 8bit/byte)
    // TODO implement t bit, force non-privilege access
    // L - Load/Store bit (0=Store to memory, 1=Load from memory)
    if (dataTransGetL(instruction)) {
        // LDR{cond}{B}{T} Rd,<Address> ;Rd=[Rn+/-<offset>]
        if (b) {  // transfer 8 bits
            setRegister(rd, (uint32_t)(bus->read8(address, Bus::CycleType::NONSEQUENTIAL)));
        } else {  // transfer 32 bits
            if ((address & 0x00000003) == 2) {
                // aligned to half-word but not word
                uint32_t low = (uint32_t)(bus->read16(address, Bus::CycleType::NONSEQUENTIAL));
                uint32_t hi = (uint32_t)(bus->read16((address - 2), Bus::CycleType::NONSEQUENTIAL));
                uint32_t full = ((hi << 16) | low);
                setRegister(rd, full);
            } else {
                // aligned to word
                // Reads from forcibly aligned address "addr AND (NOT 3)",
                // and does then rotate the data as "ROR (addr AND 3)*8". T
                // TODO: move the masking and shifting into the read/write functions
                setRegister(rd, 
                                 aluShiftRor(bus->read32(address,
                                 Bus::CycleType::NONSEQUENTIAL),
                                 (address & 3) * 8));
            }
        }
        bus->addCycleToExecutionTimeline(Bus::CycleType::INTERNAL, 0, 0);
        if(rd == PC_REGISTER) {
            return BRANCH;
        } else {
            return NONSEQUENTIAL;
        }
    } else {
        // STR{cond}{B}{T} Rd,<Address>   ;[Rn+/-<offset>]=Rd
        if (b) {  // transfer 8 bits
            bus->write8(address, (uint8_t)(rdVal), 
                             Bus::CycleType::NONSEQUENTIAL);
        } else {  // transfer 32 bits
            bus->write32(address, (rdVal), 
                              Bus::CycleType::NONSEQUENTIAL);
        }
        
        return NONSEQUENTIAL;
    }
}

inline
ARM7TDMI::FetchPCMemoryAccess ARM7TDMI::halfWordDataTransHandler(uint32_t instruction) {
    assert((instruction & 0x0E000000) == 0);

    uint8_t rd = getRd(instruction);
    uint32_t rdVal =
        (rd == 15) ? getRegister(rd) + 8 : getRegister(rd);
    uint8_t rn = getRn(instruction);
    uint32_t rnVal =
        (rn == 15) ? getRegister(rn) + 4 : getRegister(rn);

    uint32_t offset = 0;
    bool l = dataTransGetL(instruction);

    if (instruction & 0x00400000) {
        // immediate as offset
        offset = (((instruction & 0x00000F00) >> 4) | (instruction & 0x0000000F));
    } else {
        // register as offset
        assert(!(instruction & 0x00000F00));
        assert(getRm(instruction) != 15);
        offset = getRegister(getRm(instruction));
    }
    assert(instruction & 0x00000080);
    assert(instruction & 0x00000010);

    uint32_t address = rnVal;
    uint8_t p = dataTransGetP(instruction);
    if (p) {
        // pre-indexing offset
        address =
            dataTransGetU(instruction) ? address + offset : address - offset;
        if (dataTransGetW(instruction)) {
            // write address back into base register
            setRegister(rn, address);
        }
    } else {
        // post-indexing offset
        assert(dataTransGetW(instruction) == 0);
        // add offset after transfer and always write back
        setRegister(rn, dataTransGetU(instruction) ? address + offset : address - offset);

    }

    uint8_t opcode = (instruction & 0x00000060) >> 5;
    switch (opcode) {
        case 0: {
            // Reserved for SWP instruction
            assert(false);
            break;
        }
        case 1: {     // STRH or LDRH (depending on l)
            if (l) {  // LDR{cond}H  Rd,<Address>  ;Load Unsigned halfword
                      // (zero-extended)         
                setRegister(
                    rd, aluShiftRor(
                            (uint32_t)(bus->read16(address, Bus::CycleType::NONSEQUENTIAL)),
                            (address & 1) * 8));
                bus->addCycleToExecutionTimeline(Bus::CycleType::INTERNAL, 0, 0);
            } else {  // STR{cond}H  Rd,<Address>  ;Store halfword   [a]=Rd
                bus->write16(address, (uint16_t)rdVal, Bus::CycleType::NONSEQUENTIAL);
            }
            break;
        }
        case 2: {  // LDR{cond}SB Rd,<Address>  ;Load Signed byte (sign
                   // extended)
            // TODO: better way to do this?
            assert(l);
            uint32_t val = (uint32_t)(bus->read8(address, Bus::CycleType::NONSEQUENTIAL));
            if (val & 0x00000080) {
                setRegister(rd, 0xFFFFFF00 | val);
            } else {
                setRegister(rd, val);
            }
            bus->addCycleToExecutionTimeline(Bus::CycleType::INTERNAL, 0, 0);
            break;
        }
        case 3: {  // LDR{cond}SH Rd,<Address>  ;Load Signed halfword (sign
                   // extended)
            if(address & 0x00000001) {
                // TODO refactor this, reusing the same code as case 2
                // strange case: LDRSH Rd,[odd]  -->  LDRSB Rd,[odd] ;sign-expand BYTE value
                assert(l);
                uint32_t val = (uint32_t)(bus->read8(address, Bus::CycleType::NONSEQUENTIAL));
                if (val & 0x00000080) {
                    setRegister(rd, 0xFFFFFF00 | val);
                } else {
                    setRegister(rd, val);
                }
                break;
            }

            assert(l);
            uint32_t val = (uint32_t)(bus->read16(address, Bus::CycleType::NONSEQUENTIAL));
            if (val & 0x00008000) {
                setRegister(rd, 0xFFFF0000 | val);
            } else {
                setRegister(rd, val);
            }
            bus->addCycleToExecutionTimeline(Bus::CycleType::INTERNAL, 0, 0);
            break;
        }
    }

    if(rd == PC_REGISTER && l) {
        // LDR PC
        return BRANCH;
    } else {
        return NONSEQUENTIAL;
    }
}

inline
ARM7TDMI::FetchPCMemoryAccess ARM7TDMI::singleDataSwapHandler(uint32_t instruction) {
    // TODO: figure out memory alignment logic (for all data transfer ops)
    // (verify against existing CPU implementations
    assert((instruction & 0x0F800000) == 0x01000000);
    assert(!(instruction & 0x00300000));
    assert((instruction & 0x00000FF0) == 0x00000090);
    bool b = dataTransGetB(instruction);
    uint8_t rn = getRn(instruction);
    uint8_t rd = getRd(instruction);
    uint8_t rm = getRm(instruction);
    assert((rn != 15) && (rd != 15) && (rm != 15));

    // SWP{cond}{B} Rd,Rm,[Rn]     ;Rd=[Rn], [Rn]=Rm
    if (b) {
        // SWPB swap byte
        uint32_t rnVal = getRegister(rn);
        uint8_t rmVal = (uint8_t)(getRegister(rm));
        uint8_t data = bus->read8(rnVal, Bus::CycleType::NONSEQUENTIAL);
        setRegister(rd, (uint32_t)data);
        // TODO: check, is it a zero-extended 32-bit write or an 8-bit write?
        bus->write8(rnVal, rmVal, Bus::CycleType::NONSEQUENTIAL);
    } else {
        // SWPB swap word
        // The SWP opcode works like a combination of LDR and STR, that means, 
        // it does read-rotated, but does write-unrotated.
        uint32_t rnVal = getRegister(rn);
        uint32_t rmVal = getRegister(rm);
        // uint32_t data = bus->read32(aluShiftRor(rnVal, (rnVal & 3) * 8));
        
        uint32_t data = aluShiftRor(bus->read32(rnVal, Bus::CycleType::NONSEQUENTIAL), (rnVal & 3) * 8);
        setRegister(rd, data);
        bus->write32(rnVal, rmVal, Bus::CycleType::NONSEQUENTIAL);
    }
    bus->addCycleToExecutionTimeline(Bus::CycleType::INTERNAL, 0, 0);
    return NONSEQUENTIAL;
}

inline
ARM7TDMI::FetchPCMemoryAccess ARM7TDMI::blockDataTransHandler(uint32_t instruction) {
    // TODO: data aborts (if even applicable to GBA?)
    assert((instruction & 0x0E000000) == 0x08000000);
    // base register
    uint8_t rn = getRn(instruction);
    // align memory address;
    uint32_t rnVal = getRegister(rn);
    assert(rn != 15);

    bool p = dataTransGetP(instruction);
    bool u = dataTransGetU(instruction);
    bool l = dataTransGetL(instruction);
    bool w = dataTransGetW(instruction);

    if(!(instruction & 0x0000FFFF)) {
        // Empty Rlist: R15 loaded/stored (ARMv4 only), and Rb=Rb+/-40h (ARMv4-v5).
        instruction |= 0x00008000;
        // manually setting write bit to false so wont overwrite rn
        w = false;
        setRegister(rn, u ? rnVal + 0x40 : rnVal - 0x40);
    }

    // special case for block transfer, s = what is usually b
    bool s = dataTransGetB(instruction);
    if (s) assert(cpsr.Mode != USER);
    uint16_t regList = (uint16_t)instruction;
    uint32_t addressRnStoredAt = 0;  // see below
    bool firstAccess = true;

    if (u) {
        // up, add offset to base
        if (p) {
            // pre-increment addressing
            rnVal += 4;
        }
        for (int reg = 0; reg < 16; reg++) {
            if (regList & 1) {
                if (l) {
                    // LDM{cond}{amod} Rn{!},<Rlist>{^}  ;Load  (Pop)
                    uint32_t data;
                    if(firstAccess) {
                        data = bus->read32(rnVal, Bus::CycleType::NONSEQUENTIAL);
                        firstAccess = false;
                    } else {
                        data = bus->read32(rnVal, Bus::CycleType::SEQUENTIAL);
                    }
                    (!s) ? setRegister(reg, data)
                         : setUserRegister(reg, data);
                    if(reg == rn) {
                        // when base is in rlist no writeback (LDM/ARMv4),
                        w = false;
                    }
                } else {
                    // STM{cond}{amod} Rn{!},<Rlist>{^}  ;Store (Push)
                    if (reg == rn) {
                        // hacky!
                        addressRnStoredAt = rnVal;
                    }
                    uint32_t data = (!s) ? getRegister(reg)
                                         : getUserRegister(reg);
                    // TODO: take this out when implemeinting pipelining
                    if (reg == 15) data += 8;
                    if(firstAccess) {
                        bus->write32(rnVal, data, Bus::CycleType::NONSEQUENTIAL);
                        firstAccess = false;
                    } else {
                        bus->write32(rnVal, data, Bus::CycleType::SEQUENTIAL);
                    }
                }
                rnVal += 4;
            }
            regList >>= 1;
        }
        if (p) {
            // adjust the final rnVal so it is correct
            rnVal -= 4;
        }

    } else {
        // down, subtract offset from base
        if (p) {
            // pre-increment addressing
            rnVal -= 4;
        }
        for (int reg = 15; reg >= 0; reg--) {
            if (regList & 0x8000) {
                if (l) {
                    // LDM{cond}{amod} Rn{!},<Rlist>{^}  ;Load  (Pop)
                    uint32_t data;
                    if(firstAccess) {
                        data = bus->read32(rnVal, Bus::CycleType::NONSEQUENTIAL);
                        firstAccess = false;
                    } else {
                        data = bus->read32(rnVal, Bus::CycleType::SEQUENTIAL);            
                    }
                    (!s) ? setRegister(reg, data)
                         : setUserRegister(reg, data);
                    if(reg == rn) {
                        // when base is in rlist no writeback (LDM/ARMv4),
                        w = false;
                    }
                } else {
                    // STM{cond}{amod} Rn{!},<Rlist>{^}  ;Store (Push)
                    if (reg == rn) {
                        // hacky!
                        addressRnStoredAt = rnVal;
                    }

                    uint32_t data = (!s) ? getRegister(reg)
                                         : getUserRegister(reg);
                    if (reg == 15) data += 8;
                    if(firstAccess) {
                        bus->write32(rnVal, data, Bus::CycleType::NONSEQUENTIAL);
                        firstAccess = false;
                    } else {
                        bus->write32(rnVal, data, Bus::CycleType::SEQUENTIAL);
                    }
                }
                rnVal -= 4;
            }
            regList <<= 1;
        }
        if (p) {
            // adjust the final rnVal so it is correct
            rnVal += 4;
        }
    }

    if (w) {
        if (((uint16_t)(instruction << (15 - rn)) > 0x8000) && !l) {
            // check if base is not first reg to be stored

            // A STM which includes storing the base, with the base
            // as the first register to be stored, will therefore
            // store the unchanged value, whereas with the base second
            // or later in the transfer order, will store the modified value.
            assert(addressRnStoredAt != 0);
            // TODO: how to tell of sequential or nonsequential
            bus->write32(addressRnStoredAt, rnVal, Bus::CycleType::SEQUENTIAL);
        }
        setRegister(rn, rnVal);
    }

    if (s && l && (instruction & 0x00008000)) {
        // f instruction is LDM and R15 is in the list: (Mode Changes)
        // While R15 loaded, additionally: CPSR=SPSR_<current mode>
        // TODO make sure to switch mode ANYWHERE where cpsr is set
        cpsr = *(getCurrentModeSpsr());
        switchToMode(ARM7TDMI::Mode(cpsr.Mode));
    }

    bus->addCycleToExecutionTimeline(Bus::CycleType::INTERNAL, 0, 0);
    if(l && (instruction & 0x00008000)) {
        // LDM with PC in list
        return BRANCH;
    } else {
        return NONSEQUENTIAL;
    }
}

inline
ARM7TDMI::FetchPCMemoryAccess ARM7TDMI::branchHandler(uint32_t instruction) {
    assert((instruction & 0x0E000000) == 0x0A000000);
    int32_t offset = ((instruction & 0x00FFFFFF) << 2);
    if(offset & 0x02000000) {
        // negative? Then must sign extend
        offset |= 0xFE000000;
    }

    BitPreservedInt32 pcVal;
    pcVal._unsigned = getRegister(PC_REGISTER);
    BitPreservedInt32 branchAddr;

    // TODO might be able to remove +4 when after implekemting pipelining
    branchAddr._signed = pcVal._signed + offset + 4;
    branchAddr._unsigned &= 0xFFFFFFFC;
    switch ((instruction & 0x01000000) >> 24) {
        case 0: {
            // B
            break;
        }
        case 1: {
            // BL LR=PC+4
            setRegister(14, getRegister(PC_REGISTER));
            break;
        }
    }

    setRegister(PC_REGISTER, branchAddr._unsigned);

    return BRANCH;
}

ARM7TDMI::FetchPCMemoryAccess ARM7TDMI::branchAndExchangeHandler(uint32_t instruction) {
    assert(((instruction & 0x0FFFFF00) >> 8) == 0b00010010111111111111);

    // for this op rn is where rm usually is
    uint8_t rn = getRm(instruction);
    assert(rn != PC_REGISTER);
    uint32_t rnVal = getRegister(rn);

    switch ((instruction & 0x000000F0) >> 4) {
        case 0x1: {
            // BX PC=Rn, T=Rn.0
            break;
        }
        case 0x3: {
            // BLX PC=Rn, T=Rn.0, LR=PC+4
            setRegister(14, getRegister(PC_REGISTER));
            break;
        }
    }

    /*
        For ARM code, the low bits of the target address should be usually zero,
        otherwise, R15 is forcibly aligned by clearing the lower two bits.
        For THUMB code, the low bit of the target address may/should/must be
        set, the bit is (or is not) interpreted as thumb-bit (depending on the
        opcode), and R15 is then forcibly aligned by clearing the lower bit. In
        short, R15 will be always forcibly aligned, so mis-aligned branches won't
        have effect on subsequent opcodes that use R15, or [R15+disp] as operand.
    */
    bool t = rnVal & 0x1;
    cpsr.T = t;
    if (t) {
        rnVal &= 0xFFFFFFFE;
    } else {
        rnVal &= 0xFFFFFFFC;
    }
    setRegister(PC_REGISTER, rnVal);

    return BRANCH;
}

inline
ARM7TDMI::FetchPCMemoryAccess ARM7TDMI::swiHandler(uint32_t instruction) {
    uint8_t opcode = (instruction & 0x0F000000);

    switch(opcode) {
        case 0x0F000000: {
            // 1111b: SWI{cond} nn   ;software interrupt
            switchToMode(Mode::SUPERVISOR);
            // switch to ARM mode, interrupts disabled
            cpsr.T = 0;
            cpsr.I = 1; 
            setRegister(LINK_REGISTER, getRegister(PC_REGISTER));
            setRegister(PC_REGISTER, 0x18);
            break;
        } 
        case 0x01000000: {
            // 0001b: BKPT      nn   ;breakpoint (ARMv5 and up)
            DEBUGWARN("BKPT instruction not implemented!\n");
            break;
        }
        default: {
            assert(false);
            break;
        }
    }
    return BRANCH;
}

/* ~~~~~~~~~~~~~~~ Undefined Operation ~~~~~~~~~~~~~~~~~~~~*/
inline
ARM7TDMI::FetchPCMemoryAccess ARM7TDMI::undefinedOpHandler(uint32_t instruction) {
    DEBUGWARN("UNDEFINED ARM OPCODE! " << std::bitset<32>(instruction).to_string() << std::endl);
    switchToMode(ARM7TDMI::Mode::UNDEFINED);
    bus->addCycleToExecutionTimeline(Bus::CycleType::INTERNAL, 0, 0);
    return BRANCH;
}