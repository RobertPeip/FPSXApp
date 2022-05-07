#include <algorithm>
#include <bitset>
using namespace std;

#include "CPU.h"
#include "Memory.h"
#include "GPU_Timing.h"
#include "GPU.h"
#include "PSXRegs.h"
#include "CDRom.h"
#include "Sound.h"
#include "MDEC.h"
#include "DMA.h"
#include "psx.h"

Cpu CPU;

#if DEBUG
void cpustate::update()
{
    for (int i = 0; i < 32; i++)
    {
        regs[i] = CPU.regs[i];
    }
    this->regs_hi = CPU.hi;
    this->regs_lo = CPU.lo;

    this->sr = CPU.cop0_SR_next;
    CPU.cop0_SR_next = CPU.cop0_SR;
    this->cause = CPU.cop0_CAUSE;

	this->opcode = CPU.opcode[4];
    this->pc = CPU.pcOld[4];

	this->ticks = CPU.totalticks;

    this->irq = PSXRegs.I_STATUS;

    this->gpu_time = GPU_Timing.nextHCount;
    this->gpu_line = GPU_Timing.vpos;
    this->gpu_stat = PSXRegs.GPUSTAT;
    this->fifocount = GPU.fifo.size();
    this->gpu_ticks = GPU.cmdTicksLast;
    this->gpu_obj = GPU.objCount;

    this->mdec_stat = PSXRegs.MDECStatus;

    this->cd_status = PSXRegs.CDROM_STATUS | (CDRom.internalStatus << 8);

    for (int i = 0; i < 3; i++)
    {
        this->timer[i] = PSXRegs.T_CURRENT[i];
    }

	//this->debug8 = Memory.RAM[0x001ffe98];
    //this->debug8 = GPU_Timing.interlacedField;
    //this->debug8 = DMA.requests[4];
    //this->debug8 = Sound.fifo.size();
    this->debug8 = 0;
    //this->debug16 = *(UInt16*)&GPU.VRAM[0x7FFFC * 2];
    //this->debug16 = PSXRegs.SPU_STAT;
    this->debug16 = 0;
	//this->debug32 = *(UInt32*)&Memory.RAM[0x1ffc64];
	//this->debug32 = *(UInt32*)&CPU.scratchpad[0x08];
	//this->debug32 = CDRom.delay + 1;
	//this->debug32 = GPU.cmdTicks / 2;
	//this->debug32 = Sound.cmdTicks;
	//this->debug32 = MDEC.cmdTicks - 1;
	//this->debug32 = GPU_Timing.vDisplayEnd;
    //this->debug32 = PSXRegs.GPUREAD;
    this->debug32 = 0;
}

inline void printsingle(FILE* file, uint32_t value, const char* name, int size)
{
    char buf[10];
    fputs(name, file);
    fputc(' ', file);
    _itoa(value, buf, 16);
    for (int c = strlen(buf); c < size; c++) fputc('0', file);
    fputs(buf, file);
    //fputc('\n', file);
    fputc(' ', file);
}

inline void printchange(FILE* file, uint32_t oldvalue, uint32_t newvalue, const char* name, int size)
{
    char buf[10];
    fputs(name, file);
    fputc(' ', file);
    //_itoa(oldvalue, buf, 16);
    //for (int c = strlen(buf); c < size; c++) fputc('0', file);
    //fputs(buf, file);
    //fputc(' ', file);
    _itoa(newvalue, buf, 16);
    for (int c = strlen(buf); c < size; c++) fputc('0', file);
    fputs(buf, file);
    //fputc('\n', file);
    fputc(' ', file);
}

void Cpu::trace_file_last()
{
    FILE* file;
    if (CPU.debug_outdiv == 1) file = fopen("R:\\debug.txt", "w");
    else file = fopen("R:\\debug_n.txt", "w");

    for (int i = 0; i < traclist_ptr; i++)
    {
        cpustate laststate = Tracelist[i];
        cpustate state = Tracelist[i];
        UInt32 newticks = 0;
        if (i > 0)
        {
            laststate = Tracelist[i - 1];
            newticks = state.ticks - laststate.ticks;
        }
        else
        {
            state.opcode = Tracelist[1].opcode;
        }

        printsingle(file, state.ticks, "#", 8);
        printsingle(file, newticks, "#", 3);
        printsingle(file, state.pc, "PC", 8);
        printsingle(file, state.opcode, "OP", 8);

        for (int j = 0; j < 32; j++)
        {
            if (i == 0 || state.regs[j] != laststate.regs[j])
            {
                fputc('R', file);
                char buf[10];
                _itoa(j, buf, 10);
                if (j < 10) fputc('0', file);
                printchange(file, laststate.regs[j], state.regs[j], buf, 8);
            }
        }

        //if (i == 0 || state.regs_hi != laststate.regs_hi) printchange(file, laststate.regs_hi, state.regs_hi, "HI", 8);
        //if (i == 0 || state.regs_lo != laststate.regs_lo) printchange(file, laststate.regs_lo, state.regs_lo, "LO", 8);

        //if (i == 0 || state.sr != laststate.sr) printchange(file, laststate.sr, state.sr, "SR", 8);
        if (i == 0 || state.cause != laststate.cause) printchange(file, laststate.cause, state.cause, "CAUSE", 8);

        if (i == 0 || state.irq != laststate.irq) printchange(file, laststate.irq, state.irq, "IRQ", 4);

        if (i == 0 || state.gpu_time != laststate.gpu_time) printchange(file, laststate.gpu_time, state.gpu_time, "GTM", 3);
        if (i == 0 || state.gpu_line != laststate.gpu_line) printchange(file, laststate.gpu_line, state.gpu_line, "LINE", 3);
        if (i == 0 || state.gpu_stat != laststate.gpu_stat) printchange(file, laststate.gpu_stat, state.gpu_stat, "GPUS", 8);
        //if (i == 0 || state.fifocount != laststate.fifocount) printchange(file, laststate.fifocount, state.fifocount, "FIFO", 4);
        if (i == 0 || state.gpu_ticks > 0) printchange(file, laststate.gpu_ticks, state.gpu_ticks, "GTCK", 4);
        if (i == 0 || state.gpu_obj != laststate.gpu_obj) printchange(file, laststate.gpu_obj, state.gpu_obj, "GOBJ", 4);

        if (i == 0 || state.mdec_stat != laststate.mdec_stat) printchange(file, laststate.mdec_stat, state.mdec_stat, "MDEC", 8);

        //if (i == 0 || state.cd_status != laststate.cd_status) printchange(file, laststate.cd_status, state.cd_status, "CDS", 4);

        if (i == 0 || state.timer[0] != laststate.timer[0]) printchange(file, laststate.timer[0], state.timer[0], "T0", 4);
        if (i == 0 || state.timer[1] != laststate.timer[1]) printchange(file, laststate.timer[1], state.timer[1], "T1", 4);
        if (i == 0 || state.timer[2] != laststate.timer[2]) printchange(file, laststate.timer[2], state.timer[2], "T2", 4);

        if (i == 0 || state.debug8 != laststate.debug8) printchange(file, laststate.debug8, state.debug8, "D8", 2);
        if (i == 0 || state.debug16 != laststate.debug16) printchange(file, laststate.debug16, state.debug16, "D16", 4);
        if (i == 0 || state.debug32 != laststate.debug32) printchange(file, laststate.debug32, state.debug32, "D32", 8);

        fputc('\n', file);
    }
    fclose(file);
}

#endif

void Cpu::reset()
{
	for (int i = 0; i < 32; i++) regs[i] = 0;
    hi = 0;
    lo = 0;
    for (int i = 0; i < 5; i++)  opcode[i] = 0;

    cop0_BPC = 0;
    cop0_BDA = 0;
    cop0_JUMPDEST = 0;
    cop0_DCIC = 0;
    cop0_BADVADDR = 0;
    cop0_BDAM = 0;
    cop0_BPCM = 0;
    cop0_SR = 0;
    cop0_CAUSE = 0;
    cop0_EPC = 0;
    cop0_PRID = 2;

    stall = 0;
    newStall = 0;
    exception = 0;
    newException = 0;
    blockLoadforward = false;

    irqNext3 = false;
    irqNext2 = false;
    irqNext1 = false;

    // stage1
    fetchReady = false;
    fetchWaitRead = false;

    // stage2
    decodeException = false;
    decodeImmData = 0;
    decodeSource1 = 0;
    decodeSource2 = 0;
    decodeValue1 = 0;
    decodeValue2 = 0;
    decodeOP = 0;
    decodeFunct = 0;
    decodeShamt = 0;
    decodeRD = 0;
    decodeTarget = 0;

    //stage 3
    executeException = false;
    resultWriteEnable = false;
    branch = false;
    executeReadEnable = false;
    executeMemWriteEnable = false;
    executeGTEReadEnable = false;
    executeCOP0WriteEnable = false;
    executelastWasLoad = false;
    executeBranchTaken = false;
    executeBranchdelaySlot = false;
    gtecalctime = 0;
    stallFromHILO = false;
    stallFromGTE = false;

    //stage 4
    writebackException = false;
    writebackMemWriteEnable = false;
    writebackWriteEnable = false;
    writebackCOP0WriteEnable = false;
    writebackBranchTaken = false;
    writebackBranchdelaySlot = false;
    writebackGTEReadEnable = false;

    // stage 5
    writeDoneWriteEnable = false;
    writeDoneException = false;
    writeDoneBranchTaken = false;
    writeDoneBranchdelaySlot = false;

	pc = 0xbfc00000;

    hilo_wait = 0;

    totalticks = 0;

    blockirq = 0;
    lastExcIRQ = false;
    
    for (int i = 0; i < 256; i++) cacheValid[i] = false;
    for (int i = 0; i < 1024; i++) scratchpad[i] = 0;

    runmoretrace = -1;
    commands = 0;
#if DEBUG
    debug_outdivcnt = 0;
    debug_outdiv = 1;
#endif

    paused = false;

#ifdef FPGACOMPATIBLE
    emptyPipe = 0;
    totalticks = 0;
#else
    emptyPipe = 4;
#endif
}

void Cpu::work()
{
    newStall = stall;
    if (!stall) newException = 0;
    Memory.requestData = false;
    Memory.requestWrite = false;
    Memory.requestCode = false;

    value1 = decodeValue1;
    if (decodeSource1 > 0 && resultTarget == decodeSource1 && resultWriteEnable && !lastReadCOP) value1 = resultData;
    else if (decodeSource1 > 0 && writebackTarget == decodeSource1 && writebackWriteEnable) value1 = writebackData;
    else if (decodeSource1 > 0 && writeDoneTarget == decodeSource1 && writeDoneWriteEnable) value1 = writeDoneData;

    value2 = decodeValue2;
    if (decodeSource2 > 0 && resultTarget == decodeSource2 && resultWriteEnable && !lastReadCOP) value2 = resultData;
    else if (decodeSource2 > 0 && writebackTarget == decodeSource2 && writebackWriteEnable && !blockLoadforward) value2 = writebackData;
    else if (decodeSource2 > 0 && writeDoneTarget == decodeSource2 && writeDoneWriteEnable) value2 = writeDoneData;

    stage5();
    stage4();
    stage3();
    stage2();
    stage1();

    blockLoadforward = false;
    if (executelastWasLoad && executeReadEnable && executelastLoadTarget == resultTarget)
    {
        blockLoadforward = true;
    }

    stall = newStall;
    exception = newException;

#ifdef FPGACOMPATIBLE
    if (irqNext1) CPU.cop0_CAUSE |= 0x400; else CPU.cop0_CAUSE &= 0xFFFFFBFF;
    irqNext1 = irqNext2;
    irqNext2 = irqNext3;
#endif
}

void Cpu::doException(byte code, UInt32 pcSave, UInt32 instr, bool branchtaken, bool isDelaySlot)
{
    exception_SR = (cop0_SR & 0xFFFFFFC0) | ((cop0_SR & 0x0F) << 2);
    exception_CAUSE = cop0_CAUSE & 0x0FFFFFC3;
    exception_CAUSE |= code << 2;
    exception_CAUSE |= ((instr >> 26) & 3) << 28;
    if (branchtaken) exception_CAUSE |= 1 << 30;
    if (isDelaySlot)
    {
        exception_CAUSE |= 1 << 31;
        exception_EPC = pcSave - 4;
        exception_JMP = pcOld[0];
    }
    else
    {
        exception_EPC = pcSave;
    }
}

void Cpu::updateCache(UInt32 addr, UInt32 data, byte writeMask)
{
    byte index = (addr >> 4) & 0xFF;
    if (cacheValid[index] && cacheTags[index] == (addr >> 12))
    {
        std::string debugstring = "Cache update: ";
        debugstring.append(std::to_string(addr));
        debugstring.append(" ");
        debugstring.append(std::to_string(data));
        psx.log(debugstring);
        if (writeMask & 0b0001) cacheData[index][((addr & 0xF) >> 2) + 0] = data;
        if (writeMask & 0b0010) cacheData[index][((addr & 0xF) >> 2) + 1] = data >> 8;
        if (writeMask & 0b0100) cacheData[index][((addr & 0xF) >> 2) + 2] = data >> 16;
        if (writeMask & 0b1000) cacheData[index][((addr & 0xF) >> 2) + 3] = data >> 24;
    }
}

void Cpu::saveState(UInt32 offset)
{
    psx.savestate_addvalue(offset +  0, 31, 0, pc);
    psx.savestate_addvalue(offset +  1, 31, 0, hi);
    psx.savestate_addvalue(offset +  2, 31, 0, lo);
                                     
    psx.savestate_addvalue(offset +  3, 31, 0, cop0_BPC);
    psx.savestate_addvalue(offset +  4, 31, 0, cop0_BDA);
    psx.savestate_addvalue(offset +  5, 31, 0, cop0_JUMPDEST);
    psx.savestate_addvalue(offset +  6, 31, 0, cop0_DCIC);
    psx.savestate_addvalue(offset +  7, 31, 0, cop0_BADVADDR);
    psx.savestate_addvalue(offset +  8, 31, 0, cop0_BDAM);
    psx.savestate_addvalue(offset +  9, 31, 0, cop0_BPCM);
    psx.savestate_addvalue(offset + 10, 31, 0, cop0_SR);
    psx.savestate_addvalue(offset + 11, 31, 0, cop0_CAUSE);
    psx.savestate_addvalue(offset + 12, 31, 0, cop0_EPC);
    psx.savestate_addvalue(offset + 13, 31, 0, cop0_PRID);

    for (int i = 0; i < 5; i++) psx.savestate_addvalue(offset + i + 14, 31, 0, opcode[i]);
    for (int i = 0; i < 5; i++) psx.savestate_addvalue(offset + i + 19, 31, 0, pcOld[i]);

    psx.savestate_addvalue(offset + 24, 4, 0, stall);
    psx.savestate_addvalue(offset + 24, 9, 5, exception);
    psx.savestate_addvalue(offset + 24, 12, 10, memoryMuxStage);
    psx.savestate_addvalue(offset + 24, 13, 13, blockLoadforward);

    //// stage 1
    psx.savestate_addvalue(offset + 25, 0, 0, fetchReady);
    psx.savestate_addvalue(offset + 25, 1, 1, fetchWaitRead); // not required for FPGA

    //// stage 2
    psx.savestate_addvalue(offset + 26, 31, 0, decodeImmData);
    psx.savestate_addvalue(offset + 27, 31, 0, decodeValue1);
    psx.savestate_addvalue(offset + 28, 31, 0, decodeValue2);
    psx.savestate_addvalue(offset + 29, 31, 0, decodeJumpTarget);
    psx.savestate_addvalue(offset + 30, 31, 0, newPc); // not required for FPGA
    psx.savestate_addvalue(offset + 31, 7,  0, decodeSource1);
    psx.savestate_addvalue(offset + 31, 15, 8, decodeSource2);
    psx.savestate_addvalue(offset + 31, 23,16, decodeTarget);
    psx.savestate_addvalue(offset + 31, 31,24, decodeOP);
    psx.savestate_addvalue(offset + 32,  7, 0, decodeRD);
    psx.savestate_addvalue(offset + 32, 15, 8, decodeFunct);
    psx.savestate_addvalue(offset + 32, 23, 16, decodeShamt);
    psx.savestate_addvalue(offset + 32, 24, 24, branch); // not required for FPGA
    psx.savestate_addvalue(offset + 32, 25, 25, decodeException);

    //// stage 3
    psx.savestate_addvalue(offset + 33, 31, 0, resultData);
    psx.savestate_addvalue(offset + 34, 31, 0, executeReadAddress);
    psx.savestate_addvalue(offset + 35, 31, 0, executeMemWriteData);
    psx.savestate_addvalue(offset + 36, 31, 0, executeMemWriteAddress);
    psx.savestate_addvalue(offset + 37, 31, 0, executeCOP0WriteValue);
    psx.savestate_addvalue(offset + 38, 31, 0, hi_next); // not required for FPGA
    psx.savestate_addvalue(offset + 39, 31, 0, lo_next); // not required for FPGA
    psx.savestate_addvalue(offset + 40,  7,  0, resultTarget);
    psx.savestate_addvalue(offset + 40, 15,  8, executeGTETarget);
    psx.savestate_addvalue(offset + 40, 23, 16, executeMemWriteMask);
    psx.savestate_addvalue(offset + 40, 31, 24, executeCOP0WriteDestination);
    psx.savestate_addvalue(offset + 41,  7,  0, executelastLoadTarget); // not required for FPGA
    psx.savestate_addvalue(offset + 41, 15,  8, hilo_wait); // todo -> wait for idle?
    psx.savestate_addvalue(offset + 41, 19, 16, (byte)executeLoadType);
    psx.savestate_addvalue(offset + 41, 20, 20, resultWriteEnable);
    psx.savestate_addvalue(offset + 41, 21, 21, executeReadEnable);
    psx.savestate_addvalue(offset + 41, 22, 22, executeGTEReadEnable);
    psx.savestate_addvalue(offset + 41, 23, 23, executeMemWriteEnable);
    psx.savestate_addvalue(offset + 41, 24, 24, executeException);
    psx.savestate_addvalue(offset + 41, 25, 25, executeCOP0WriteEnable);
    psx.savestate_addvalue(offset + 41, 26, 26, executeBranchTaken);
    psx.savestate_addvalue(offset + 41, 27, 27, executeBranchdelaySlot);
    psx.savestate_addvalue(offset + 41, 28, 28, executelastWasLoad); // not required for FPGA

    psx.savestate_addvalue(offset + 57, 31, 0, execute_gte_writeData);
    psx.savestate_addvalue(offset + 58, 31, 0, execute_gte_cmdData);
    psx.savestate_addvalue(offset + 59, 7, 0, execute_gte_writeAddr);
    psx.savestate_addvalue(offset + 59, 8, 8, execute_gte_writeAddr);
    psx.savestate_addvalue(offset + 59, 9, 9, execute_gte_cmdEna);
    psx.savestate_addvalue(offset + 59, 10, 10, lastReadCOP);

    //// stage 4
    psx.savestate_addvalue(offset + 42, 31, 0, writebackData);
    psx.savestate_addvalue(offset + 43, 31, 0, writebackMemWriteData); // not required for FPGA
    psx.savestate_addvalue(offset + 44, 31, 0, writebackMemWriteAddress); // not required for FPGA
    psx.savestate_addvalue(offset + 45, 31, 0, writebackCOP0WriteValue); // not required for FPGA
    psx.savestate_addvalue(offset + 46, 31, 0, writebackReadAddress); // not required for FPGA
    psx.savestate_addvalue(offset + 47,  7,  0, writebackTarget);
    psx.savestate_addvalue(offset + 47, 15,  8, writebackCOP0WriteDestination); // not required for FPGA
    psx.savestate_addvalue(offset + 47, 19, 16, writebackMemWriteMask); // not required for FPGA
    psx.savestate_addvalue(offset + 47, 23, 20, (byte)writebackLoadType); // not required for FPGA
    psx.savestate_addvalue(offset + 47, 24, 24, writebackWriteEnable); 
    psx.savestate_addvalue(offset + 47, 25, 25, writebackMemWriteEnable); // not required for FPGA
    psx.savestate_addvalue(offset + 47, 26, 26, writebackException);
    psx.savestate_addvalue(offset + 47, 27, 27, writebackCOP0WriteEnable); // not required for FPGA
    psx.savestate_addvalue(offset + 47, 28, 28, writebackBranchTaken); // not required for FPGA
    psx.savestate_addvalue(offset + 47, 29, 29, writebackBranchdelaySlot); // not required for FPGA
    psx.savestate_addvalue(offset + 47, 30, 30, writebackGTEReadEnable);
    psx.savestate_addvalue(offset + 48,  7,  0, writebackGTETarget); // not required for FPGA

    //// stage 5
    psx.savestate_addvalue(offset + 49, 31, 0, writeDoneData);
    psx.savestate_addvalue(offset + 50,  7,  0, emptyPipe); // not required for FPGA
    psx.savestate_addvalue(offset + 50, 15,  8, writeDoneTarget);
    psx.savestate_addvalue(offset + 50, 16, 16, writeDoneWriteEnable);
    psx.savestate_addvalue(offset + 50, 17, 17, writeDoneException); // not required for FPGA
    psx.savestate_addvalue(offset + 50, 18, 18, writeDoneBranchTaken); // not required for FPGA
    psx.savestate_addvalue(offset + 50, 19, 19, writeDoneBranchdelaySlot); // not required for FPGA

    //// exception
    psx.savestate_addvalue(offset + 51, 31, 0, exception_SR);
    psx.savestate_addvalue(offset + 52, 31, 0, exception_CAUSE);
    psx.savestate_addvalue(offset + 53, 31, 0, exception_EPC);
    psx.savestate_addvalue(offset + 54, 31, 0, exception_JMP);

    psx.savestate_addvalue(offset + 55, 7, 0, blockirq); // todo
    psx.savestate_addvalue(offset + 55, 8, 8, lastExcIRQ);

    psx.savestate_addvalue(offset + 56, 8, 8, PSXRegs.CACHECONTROL);

    for (int i = 0; i < 32; i++) psx.savestate_addvalue(offset + i + 96, 31, 0, regs[i]);
}

void Cpu::loadState(UInt32 offset)
{
    pc = psx.savestate_loadvalue(offset + 0, 31, 0);
    hi = psx.savestate_loadvalue(offset + 1, 31, 0);
    lo = psx.savestate_loadvalue(offset + 2, 31, 0);

    cop0_BPC      = psx.savestate_loadvalue(offset + 3, 31, 0); 
    cop0_BDA      = psx.savestate_loadvalue(offset + 4, 31, 0); 
    cop0_JUMPDEST = psx.savestate_loadvalue(offset + 5, 31, 0); 
    cop0_DCIC     = psx.savestate_loadvalue(offset + 6, 31, 0); 
    cop0_BADVADDR = psx.savestate_loadvalue(offset + 7, 31, 0); 
    cop0_BDAM     = psx.savestate_loadvalue(offset + 8, 31, 0); 
    cop0_BPCM     = psx.savestate_loadvalue(offset + 9, 31, 0); 
    cop0_SR       = psx.savestate_loadvalue(offset + 10, 31, 0);
    cop0_CAUSE    = psx.savestate_loadvalue(offset + 11, 31, 0);
    cop0_EPC      = psx.savestate_loadvalue(offset + 12, 31, 0);
    cop0_PRID     = psx.savestate_loadvalue(offset + 13, 31, 0);

    for (int i = 0; i < 5; i++) opcode[i] = psx.savestate_loadvalue(offset + i + 14, 31, 0);
    for (int i = 0; i < 5; i++) pcOld[i] = psx.savestate_loadvalue(offset + i + 19, 31, 0);

    stall            = psx.savestate_loadvalue(offset + 24, 4, 0);   
    exception        = psx.savestate_loadvalue(offset + 24, 9, 5);   
    memoryMuxStage   = psx.savestate_loadvalue(offset + 24, 12, 10); 
    blockLoadforward = psx.savestate_loadvalue(offset + 24, 13, 13); 

    //// stage 1
    fetchReady = psx.savestate_loadvalue(offset + 25, 0, 0);
    fetchWaitRead = psx.savestate_loadvalue(offset + 25, 1, 1);

    //// stage 2
    decodeImmData    = psx.savestate_loadvalue(offset + 26, 31, 0);  
    decodeValue1     = psx.savestate_loadvalue(offset + 27, 31, 0);  
    decodeValue2     = psx.savestate_loadvalue(offset + 28, 31, 0);  
    decodeJumpTarget = psx.savestate_loadvalue(offset + 29, 31, 0);  
    newPc            = psx.savestate_loadvalue(offset + 30, 31, 0);  
    decodeSource1    = psx.savestate_loadvalue(offset + 31, 7, 0);   
    decodeSource2    = psx.savestate_loadvalue(offset + 31, 15, 8);  
    decodeTarget     = psx.savestate_loadvalue(offset + 31, 23, 16); 
    decodeOP         = psx.savestate_loadvalue(offset + 31, 31, 24); 
    decodeRD         = psx.savestate_loadvalue(offset + 32, 7, 0);   
    decodeFunct      = psx.savestate_loadvalue(offset + 32, 15, 8);  
    decodeShamt      = psx.savestate_loadvalue(offset + 32, 23, 16); 
    branch           = psx.savestate_loadvalue(offset + 32, 24, 24); 
    decodeException  = psx.savestate_loadvalue(offset + 32, 25, 25); 

    //// stage 3
    resultData                    = psx.savestate_loadvalue(offset + 33, 31, 0);  
    executeReadAddress            = psx.savestate_loadvalue(offset + 34, 31, 0);  
    executeMemWriteData           = psx.savestate_loadvalue(offset + 35, 31, 0);  
    executeMemWriteAddress        = psx.savestate_loadvalue(offset + 36, 31, 0);  
    executeCOP0WriteValue         = psx.savestate_loadvalue(offset + 37, 31, 0);  
    hi_next                       = psx.savestate_loadvalue(offset + 38, 31, 0);  
    lo_next                       = psx.savestate_loadvalue(offset + 39, 31, 0);  
    resultTarget                  = psx.savestate_loadvalue(offset + 40, 7, 0);   
    executeGTETarget              = psx.savestate_loadvalue(offset + 40, 15, 8);  
    executeMemWriteMask           = psx.savestate_loadvalue(offset + 40, 23, 16); 
    executeCOP0WriteDestination   = psx.savestate_loadvalue(offset + 40, 31, 24); 
    executelastLoadTarget         = psx.savestate_loadvalue(offset + 41, 7, 0);   
    hilo_wait                     = psx.savestate_loadvalue(offset + 41, 15, 8);  
    executeLoadType               = (CPU_LOADTYPE)psx.savestate_loadvalue(offset + 41, 19, 16);
    resultWriteEnable             = psx.savestate_loadvalue(offset + 41, 20, 20); 
    executeReadEnable             = psx.savestate_loadvalue(offset + 41, 21, 21); 
    executeGTEReadEnable          = psx.savestate_loadvalue(offset + 41, 22, 22); 
    executeMemWriteEnable         = psx.savestate_loadvalue(offset + 41, 23, 23); 
    executeException              = psx.savestate_loadvalue(offset + 41, 24, 24); 
    executeCOP0WriteEnable        = psx.savestate_loadvalue(offset + 41, 25, 25); 
    executeBranchTaken            = psx.savestate_loadvalue(offset + 41, 26, 26); 
    executeBranchdelaySlot        = psx.savestate_loadvalue(offset + 41, 27, 27); 
    executelastWasLoad            = psx.savestate_loadvalue(offset + 41, 28, 28); 

    execute_gte_writeData         = psx.savestate_loadvalue(offset + 57, 31, 0);
    execute_gte_cmdData           = psx.savestate_loadvalue(offset + 58, 31, 0);
    execute_gte_writeAddr         = psx.savestate_loadvalue(offset + 59, 7, 0);
    execute_gte_writeAddr         = psx.savestate_loadvalue(offset + 59, 8, 8);
    execute_gte_cmdEna            = psx.savestate_loadvalue(offset + 59, 9, 9);
    lastReadCOP                   = psx.savestate_loadvalue(offset + 59, 10, 10);

    //// stage 4
    writebackData                   = psx.savestate_loadvalue(offset + 42, 31, 0);  
    writebackMemWriteData           = psx.savestate_loadvalue(offset + 43, 31, 0);  
    writebackMemWriteAddress        = psx.savestate_loadvalue(offset + 44, 31, 0);  
    writebackCOP0WriteValue         = psx.savestate_loadvalue(offset + 45, 31, 0);  
    writebackReadAddress            = psx.savestate_loadvalue(offset + 46, 31, 0);  
    writebackTarget                 = psx.savestate_loadvalue(offset + 47, 7, 0);   
    writebackCOP0WriteDestination   = psx.savestate_loadvalue(offset + 47, 15, 8); 
    writebackMemWriteMask           = psx.savestate_loadvalue(offset + 47, 19, 16); 
    writebackLoadType               = (CPU_LOADTYPE)psx.savestate_loadvalue(offset + 47, 23, 20);
    writebackWriteEnable            = psx.savestate_loadvalue(offset + 47, 24, 24); 
    writebackMemWriteEnable         = psx.savestate_loadvalue(offset + 47, 25, 25); 
    writebackException              = psx.savestate_loadvalue(offset + 47, 26, 26); 
    writebackCOP0WriteEnable        = psx.savestate_loadvalue(offset + 47, 27, 27); 
    writebackBranchTaken            = psx.savestate_loadvalue(offset + 47, 28, 28); 
    writebackBranchdelaySlot        = psx.savestate_loadvalue(offset + 47, 29, 29); 
    writebackGTEReadEnable          = psx.savestate_loadvalue(offset + 47, 30, 30);
    writebackGTETarget              = psx.savestate_loadvalue(offset + 48,  7,  0);

    //// stage 5
    writeDoneData            = psx.savestate_loadvalue(offset + 49, 31, 0);  
    emptyPipe                = psx.savestate_loadvalue(offset + 50, 7, 0);   
    writeDoneTarget          = psx.savestate_loadvalue(offset + 50, 15, 8);  
    writeDoneWriteEnable     = psx.savestate_loadvalue(offset + 50, 16, 16); 
    writeDoneException       = psx.savestate_loadvalue(offset + 50, 17, 17); 
    writeDoneBranchTaken     = psx.savestate_loadvalue(offset + 50, 18, 18); 
    writeDoneBranchdelaySlot = psx.savestate_loadvalue(offset + 50, 19, 19); 

    //// exception
    exception_SR    = psx.savestate_loadvalue(offset + 51, 31, 0); 
    exception_CAUSE = psx.savestate_loadvalue(offset + 52, 31, 0); 
    exception_EPC   = psx.savestate_loadvalue(offset + 53, 31, 0); 
    exception_JMP   = psx.savestate_loadvalue(offset + 54, 31, 0); 

    blockirq   = psx.savestate_loadvalue(offset + 55, 7, 0);
    lastExcIRQ = psx.savestate_loadvalue(offset + 55, 8, 8);

    PSXRegs.CACHECONTROL = psx.savestate_loadvalue(offset + 56, 8, 8);

    for (int i = 0; i < 32; i++) regs[i] = psx.savestate_loadvalue(offset + i + 96, 31, 0);
}