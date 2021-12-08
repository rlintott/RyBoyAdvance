#include "GameBoyAdvance.h"

#include <fstream>
#include <iostream>
#include <unistd.h>
#include <iterator>
#include <chrono>
#include <algorithm> 
#include <future>
#include <thread>


#include "ARM7TDMI.h"
#include "Bus.h"
#include "LCD.h"
#include "PPU.h"
#include "Gamepad.h"
#include "DMA.h"
#include "Timer.h"
#include "Debugger.h"
#include "Scheduler.h"

using milliseconds = std::chrono::milliseconds;


static std::string getAnswer() {    
    std::string answer;
    std::cin >> answer;
    return answer;
}

std::future<std::string> asyncGetInput() {
    printf("can enter memory address and width to watch, (ex. 0xFFFFFFFF 32):");
    std::string input = "0";
    std::future<std::string> future = std::async(getAnswer);

    return future;
}

GameBoyAdvance::GameBoyAdvance(ARM7TDMI* _arm7tdmi, Bus* _bus, LCD* _screen, PPU* _ppu, DMA* _dma, Timer* _timer) {
    this->arm7tdmi = _arm7tdmi;
    this->bus = _bus;
    this->screen = _screen;
    arm7tdmi->connectBus(bus);
    this->ppu = _ppu;
    ppu->connectBus(bus);
    this->dma = _dma;
    dma->connectBus(bus);
    dma->connectCpu(arm7tdmi);
    this->timer = _timer;
    this->timer->connectBus(bus);
    this->timer->connectCpu(arm7tdmi);

    this->debugger = new Debugger();
}

GameBoyAdvance::GameBoyAdvance(ARM7TDMI* _arm7tdmi, Bus* _bus, Timer* _timer) {
    this->arm7tdmi = _arm7tdmi;
    this->bus = _bus;
    arm7tdmi->connectBus(bus);
    this->timer = _timer;
    this->timer->connectBus(bus);
    this->timer->connectCpu(arm7tdmi);
}

GameBoyAdvance::GameBoyAdvance(ARM7TDMI* _arm7tdmi, Bus* _bus) {
    DEBUG("initializing GBA\n");
    this->arm7tdmi = _arm7tdmi;
    this->bus = _bus;
    arm7tdmi->connectBus(bus);
}

GameBoyAdvance::~GameBoyAdvance() {}

// if rom loading successful return true, else return false
bool GameBoyAdvance::loadRom(std::string path) { 
    std::ifstream binFile(path, std::ios::binary);
    if(binFile.fail()) {
        std::cerr << "could not find file" << std::endl;
        return false;
    }
    std::vector<uint8_t> buffer(std::istreambuf_iterator<char>(binFile), {});

    bus->loadRom(buffer); 
    arm7tdmi->initializeWithRom();
    return true;
}

void GameBoyAdvance::testDisplay() {
    screen->initWindow();
}

long getCurrentTime() {
    return  std::chrono::duration_cast<milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

uint64_t GameBoyAdvance::getTotalCyclesElapsed() {
    return totalCycles;
}

void GameBoyAdvance::loop() {
    screen->initWindow();
    uint32_t cyclesThisStep = 0;
    uint64_t nextHBlank = PPU::H_VISIBLE_CYCLES;
    uint64_t nextVBlank = PPU::V_VISIBLE_CYCLES;
    uint64_t nextHBlankEnd = 0;
    uint64_t nextVBlankEnd = 227 * PPU::H_TOTAL;
    bus->iORegisters[Bus::IORegister::DISPSTAT] &= (~0x1);
    bus->iORegisters[Bus::IORegister::DISPSTAT] &= (~0x2);

    uint16_t currentScanline = -1;
    uint16_t nextScanline = 0;
    previousTime = getCurrentTime();
    double previous60Frame = getCurrentTime();
    startTimeSeconds = getCurrentTime() / 1000.0;

    // TODO: initialize this somewhere else
    bus->iORegisters[Bus::IORegister::KEYINPUT] = 0xFF;
    bus->iORegisters[Bus::IORegister::KEYINPUT + 1] = 0x03;
    double fps = 60.0;

    // STARTING MAIN EMULATION LOOP!
    while(true) {
        uint32_t dmaCycles = dma->step(hBlank, vBlank, currentScanline);
        cyclesThisStep += dmaCycles;
    
        timer->step(cyclesThisStep);

        totalCycles += cyclesThisStep;
        cyclesThisStep = 0;

        vBlank = false;
        hBlank = false;
        if(debugMode) {
            debugger->step(arm7tdmi, bus);
            if(debugger->stepMode) {
                while(sf::Keyboard::isKeyPressed(sf::Keyboard::LShift));
                while(!sf::Keyboard::isKeyPressed(sf::Keyboard::LShift));
                debugger->printState();
            }
        }
        cyclesThisStep += arm7tdmi->step();
        totalCycles += cyclesThisStep;

        Scheduler::EventType nextEvent = scheduler->getNextTemporalEvent(totalCycles);
        uint64_t eventCycles = 0;
        while(nextEvent != Scheduler::EventType::NONE) {
            switch(nextEvent) {
                case Scheduler::EventType::DMA0: {
                    eventCycles += dma->dmaX(x);
                    break;
                }
                case Scheduler::EventType::DMA1: {
                    break;
                }
                case Scheduler::EventType::DMA2: {
                    break;
                }
                case Scheduler::EventType::DMA3: {
                    break;
                }
                case Scheduler::EventType::TIMER0: {
                    break;
                }
                case Scheduler::EventType::TIMER1: {
                    break;
                }
                case Scheduler::EventType::TIMER2: {
                    break;
                }
                case Scheduler::EventType::TIMER3: {
                    break;
                }
                case Scheduler::EventType::VBLANK: {
                    // vblank time!
                    




                    break;
                }
                case Scheduler::EventType::HBLANK: {
                    // hblank time!
                    break;
                }
                default: {
                    break;
                    //assert(false);
                }
            }

        }
        




    
        if(totalCycles >= nextHBlank) { 
            hBlank = true;    

            if(bus->iORegisters[Bus::IORegister::DISPSTAT] & 0x10) {
                    arm7tdmi->queueInterrupt(ARM7TDMI::Interrupt::HBlank);
            }
            // setting hblank flag to 1
            bus->iORegisters[Bus::IORegister::DISPSTAT] |= 0x2;

            nextHBlank += PPU::H_TOTAL;
        }
    
        if(totalCycles >= nextHBlankEnd) {
            ppu->renderScanline(nextScanline);
            // setting hblank flag to 0
            currentScanline += 1;
            currentScanline %= 228;
            nextScanline = (currentScanline + 1) % 228;
            
            nextHBlankEnd += PPU::H_TOTAL;
            bus->iORegisters[Bus::IORegister::DISPSTAT] &= (~0x2);
            if(currentScanline == ((uint16_t)(bus->iORegisters[Bus::IORegister::DISPSTAT + 1]))) {
                // current scanline == vcount bits in DISPSTAT
                // set vcounter flag
                bus->iORegisters[Bus::IORegister::DISPSTAT] |= 0x04;
                if(bus->iORegisters[Bus::IORegister::DISPSTAT] & 0x20) {
                    // if vcount irq enabled, queue the interrupt!
                    //DEBUGWARN("VCOUNTER INTERRUPT at scanline " << currentScanline << "\n");
                    arm7tdmi->queueInterrupt(ARM7TDMI::Interrupt::VCounterMatch);
                }
            } else {
                // toggle vcounter flag off
                bus->iORegisters[Bus::IORegister::DISPSTAT] &= (~0x04);

            }

            bus->iORegisters[Bus::IORegister::VCOUNT] = currentScanline;
        }
        if(totalCycles >= nextVBlankEnd) {
            // setting vblank flag to 0
            nextVBlankEnd += PPU::V_TOTAL;
            bus->iORegisters[Bus::IORegister::DISPSTAT] &= (~0x1);
        }

        if(totalCycles >= nextVBlank) {

            vBlank = true;
            // TODO: v blank interrupt if enabled
            if(bus->iORegisters[Bus::IORegister::DISPSTAT] & 0x8) {
                arm7tdmi->queueInterrupt(ARM7TDMI::Interrupt::VBlank);
            }
            nextVBlank += PPU::V_TOTAL; 
            // TODO: clean this up
            // DEBUGWARN("frame!\n");
            // force a draw every frame
            if(sf::Keyboard::isKeyPressed(sf::Keyboard::Z)) {
                debugMode = true;
                debugger->stepMode = true;
            }
            screen->drawWindow(ppu->renderCurrentScreen());  
            Gamepad::getInput(bus);            

            // setting vblank flag to 1
            bus->iORegisters[Bus::IORegister::DISPSTAT] |= 0x1;

            // while(getCurrentTime() - previousTime < 17) {
            //     usleep(500);
            // }
            
            frames++;
            
            if((frames % 60) == 0) {
                double smoothing = 0.2;
                fps = fps * smoothing + ((double)60 / ((getCurrentTime() / 1000.0 - previous60Frame / 1000.0))) * (1.0 - smoothing);
        
                DEBUGWARN("fps: " << fps << "\n");
                //DEBUGWARN("fps: " << ((double)frames / ((getCurrentTime() / 1000.0) - startTimeSeconds)) << "\n");
                previous60Frame = previousTime;
                
            }
            previousTime = getCurrentTime();
        }


    }
}


