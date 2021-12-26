#pragma once


#include <array>
#include <vector>
#include <string>
#include <memory>
#include "EEPROM.h"
#include "Flash.h"

//#define LARGE_CARTRIDGE 1;
#define FLASH_CART 1;

class PPU;
class Timer;
class ARM7TDMI;
class DMA;

class Bus {
    // TODO: implement an OPEN BUS (ie if retreiving invalid mem location, return value last on bus)
    
    // TODO: NOTES: The GBA forcefully uses non-sequential timing at the beginning of each 128K-block of gamepak ROM, 
    // eg. "LDMIA [801fff8h],r0-r7" will have non-sequential timing at 8020000h.

   public:
    Bus();
    ~Bus();

    void connectTimer(std::shared_ptr<Timer> timer);
    void connectDma(std::shared_ptr<DMA> dma);
    void connectPpu(std::shared_ptr<PPU> ppu);

    enum CycleType {
        SEQUENTIAL,
        NONSEQUENTIAL,
        INTERNAL
    };

    enum IORegister {
        IE = 0x04000200 - 0x04000000, // Interrupt Enable Register
        IF = 0x04000202 - 0x04000000, // Interrupt Request Flags / IRQ Acknowledge
        IME = 0x04000208 - 0x04000000, // Interrupt Master Enable Register
        DISPCNT = 0x04000000 - 0x04000000, // LCD control
        DISPSTAT = 0x04000004 - 0x04000000,  // General LCD Status (STAT,LYC)
        VCOUNT = 0x04000006 - 0x04000000, // Vertical Counter (LY)
        KEYINPUT = 0x04000130 - 0x04000000, // KEYINPUT - Key Status (R)
        KEYCNT = 0x04000132 - 0x04000000, // R/W  KEYCNT    Key Interrupt Control

        DMA0SAD = 0x040000B0 - 0x04000000, // DMA 0 Source Address
        DMA0DAD = 0x040000B4 - 0x04000000, // DMA 0 Destination Address
        DMA0CNT_L = 0x040000B8 - 0x04000000, // DMA 0 Word Count
        DMA0CNT_H = 0x040000BA - 0x04000000, // DMA 0 Control
        DMA1SAD = 0x040000BC - 0x04000000, // DMA 1 Source Address
        DMA1DAD = 0x040000C0 - 0x04000000, // DMA 1 Destination Address
        DMA1CNT_L = 0x040000C4 - 0x04000000, // DMA 1 Word Count
        DMA1CNT_H = 0x040000C6 - 0x04000000, // DMA 1 Control
        DMA2SAD = 0x040000C8 - 0x04000000, // DMA 2 Source Address
        DMA2DAD = 0x040000CC - 0x04000000, // DMA 2 Destination Address
        DMA2CNT_L = 0x040000D0 - 0x04000000, // DMA 2 Word Count
        DMA2CNT_H = 0x040000D2 - 0x04000000, // DMA 2 Control
        DMA3SAD = 0x040000D4 - 0x04000000, // DMA 3 Source Address
        DMA3DAD = 0x040000D8 - 0x04000000, // DMA 3 Destination Address
        DMA3CNT_L = 0x040000DC - 0x04000000, // DMA 3 Word Count
        DMA3CNT_H = 0x040000DE - 0x04000000, // DMA 3 Control

        WIN0H = 0x04000040 - 0x04000000, // Window 0 Horizontal Dimensions (W)
        WIN1H = 0x04000042 - 0x04000000, // Window 1 Horizontal Dimensions (W)
        WIN0V = 0x04000044 - 0x04000000, // Window 0 Vertical Dimensions (W)
        WIN1V = 0x04000046 - 0x04000000, // Window 1 Vertical Dimensions (W)
        WININ = 0x04000048 - 0x04000000, // WININ - Control of Inside of Window(s) (R/W)
        WINOUT = 0x0400004A - 0x04000000, // Control of Outside of Windows & Inside of OBJ Window (R/W)

        TM0CNT_L = 0x04000100 - 0x04000000, // TM0CNT_L  Timer 0 Counter/Reload
        TM1CNT_L = 0x04000104 - 0x04000000, // TM0CNT_L  Timer 0 Counter/Reload
        TM2CNT_L = 0x04000108 - 0x04000000, // TM0CNT_L  Timer 0 Counter/Reload
        TM3CNT_L = 0x0400010C - 0x04000000, // TM0CNT_L  Timer 0 Counter/Reload

        /*
                4000800h - 32bit - Undocumented - Internal Memory Control (R/W)
                Supported by GBA and GBA SP only - NOT supported by DS (even in GBA mode).
                Also supported by GBA Micro - but crashes on "overclocked" WRAM setting.
                Initialized to 0D000020h (by hardware). Unlike all other I/O registers, this register is mirrored across the whole I/O area (in increments of 64K, ie. at 4000800h, 4010800h, 4020800h, ..., 4FF0800h)
                TODO: implement more logic for this
        */
        INTERNAL_MEM_CNT = 0x04000800 - 0x04000000,

        HALTCNT = 0x04000301 - 0x04000000   // HALTCNT - BYTE - Undocumented - Low Power Mode Control (W)

    };

    enum CartSaveType {
        FLASH512_TYPE,
        FLASH1024_TYPE,
        SRAM_TYPE,
        EEPROM_TYPE
    };

    CartSaveType cartSaveType;

    bool haltMode = false;

    /* General Internal Memory */

    // 00000000-00003FFF   BIOS - System ROM (16 KBytes) 16448
    std::vector<uint8_t> bios;
    // work ram! 02000000-0203FFFF (256kB) 263168
    std::vector<uint8_t> wRamBoard;
    // 03000000-03007FFF (32 kB) 32896
    std::vector<uint8_t> wRamChip;
    // 04000000-040003FE   I/O Registers 1028
    std::vector<uint8_t> iORegisters;

    /* Internal Display Memory */

    // 05000000-050003FF   BG/OBJ Palette RAM        (1 Kbyte) 1028
    std::vector<uint8_t> paletteRam;
    // 06000000-06017FFF   VRAM - Video RAM          (96 KBytes) 98688
    std::vector<uint8_t> vRam;
    // 07000000-070003FF   OAM - OBJ Attributes      (1 Kbyte) 1028
    // TODO: VRAM and Palette RAM may be accessed during H-Blanking. 
    // OAM can accessed only if "H-Blank Interval Free" bit in DISPCNT register is set.
    std::vector<uint8_t> objAttributes;

    /* External Memory (Game Pak) */

    // 08000000-09FFFFFF   Game Pak ROM/FlashROM (max 32MB) - Wait State 0
    // 0A000000-0BFFFFFF   Game Pak ROM/FlashROM (max 32MB) - Wait State 1
    // 0C000000-0DFFFFFF   Game Pak ROM/FlashROM (max 32MB) - Wait State 2
    std::vector<uint8_t> gamePakRom;
    // 0E000000-0E00FFFF   Game Pak SRAM    (max 64 KBytes) - 8bit Bus width (65792)
    std::vector<uint8_t> gamePakSram;


    uint32_t read32(uint32_t address, CycleType accessType);
    uint16_t read16(uint32_t address, CycleType accessType);
    uint8_t read8(uint32_t address, CycleType accessType);

    uint32_t view32(uint32_t address);

    void write32(uint32_t address, uint32_t word, CycleType accessType);
    void write16(uint32_t address, uint16_t halfWord, CycleType accessType);
    void write8(uint32_t address, uint8_t byte, CycleType accessType);

    void loadRom(std::vector<uint8_t> &buffer);

    uint8_t getCurrentNWaitstate();
    uint8_t getCurrentSWaitstate();

    // resets the execution step timeline. 
    void resetCycleCountTimeline();

    // adds eexecution step to timeline 
    // use if need to emualte extra exection steps besides the memory access
    void addCycleToExecutionTimeline(CycleType cycleType, uint32_t shift, uint8_t width);
    void printCurrentExecutionTimeline();

    void enterVBlank();
    void enterHBlank();
    void leaveVBlank();
    void leaveHBlank();

    bool ppuMemDirty = false;

    uint32_t getMemoryAccessCycles();

    bool isAddressInEeprom(uint32_t address);

    void setEepromBusWidth(uint32_t width);

   private:
    uint8_t currentNWaitstate;
    uint8_t currentSWaitstate;

    uint32_t view(uint32_t address, uint8_t width);

    uint32_t read(uint32_t address, uint8_t width, CycleType accessType);
    void write(uint32_t address, uint32_t value, uint8_t width, CycleType accessType);

    static constexpr uint32_t waitcntOffset = 0x204;

    uint8_t getWaitState0NCycles();
    uint8_t getWaitState1NCycles();
    uint8_t getWaitState2NCycles();

    uint8_t getWaitState0SCycles();
    uint8_t getWaitState1SCycles();
    uint8_t getWaitState2SCycles();

    uint8_t waitStateNVals[4] = {4,3,2,8};

    uint8_t waitState0SVals[2] = {2,1};
    uint8_t waitState1SVals[2] = {4,1};
    uint8_t waitState2SVals[2] = {8,1};

    std::array<std::string, 3> cycleTypesSerialized = {"S", "N", "I"};

    uint8_t executionTimelineSize = 0;
    std::array<uint8_t, 32> executionTimelineCycles;
    // TODO: questionable wither this should be owned by bus, could move into a scheduler class?
    std::array<CycleType, 32> executionTimelineCycleType;

    uint32_t memAccessCycles = 0;

    std::shared_ptr<PPU> ppu;
    std::shared_ptr<Timer> timer; 
    std::shared_ptr<DMA> dma;
    EEPROM eeprom;
    Flash flash;

    static uint32_t align32(uint32_t address);
    static uint32_t align16(uint32_t address);

    bool largeCart = false;

 };

 inline
 uint32_t Bus::align32(uint32_t address) {
     return address & 0xFFFFFFFC;
 }

 inline
 uint32_t Bus::align16(uint32_t address) {
     return address & 0xFFFFFFFE;
 } 