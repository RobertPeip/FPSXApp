#include "CPU.h"
#include "Memory.h"
#include "PSXRegs.h"

void Cpu::stage1()
{
    bool request = false;
    bool anyIRQ = (((cop0_SR & 0x700) & (cop0_CAUSE & 0x700)) > 0);
    //if (blockirq == 0 && (cop0_SR & 1) && (cop0_SR & 0x400) && (cop0_CAUSE & 0x400))
    if (blockirq == 0 && (cop0_SR & 1) && anyIRQ)
    {
#ifdef COMPATIRQ
        if (!stall || (stall == 1 && fetchWaitRead))
        {
            blockirq = 10;
            newException |= 0b10000;
            lastExcIRQ = true;
            doException(0, pcOld[2], opcode[2], writebackBranchTaken, writebackBranchdelaySlot);
            Memory.busy = 0;
        }
        else if (Memory.done && memoryMuxStage == 1)
        {
            newStall &= 0b11110;
        }
#else
        //if (!((newStall & 0b01000) > 0)) // seems to be not required
        if (stall == 0)
        {
            blockirq = 10;
            newException |= 0b10000;
            lastExcIRQ = true;
            doException(0, pcOld[2], opcode[2], writebackBranchTaken, writebackBranchdelaySlot);
        }
        else
        {
            if (Memory.done && memoryMuxStage == 4)
            {
                newStall &= 0b11110;
            }
            if (Memory.done && memoryMuxStage == 1)
            {
                newStall &= 0b11110;
                switch (pc >> 29)
                {
                case 0x00: // KUSEG - cached
                case 0x04: // KSEG0 - cached
                    opcode[0] = Memory.cacheLine[(pc & 0xF) >> 2];
                    {
                        byte index = (pc >> 4) & 0xFF;
                        for (int i = 0; i < 4; i++) cacheData[index][i] = Memory.cacheLine[i];
                        cacheTags[index] = ((pc & 0xFFFFFFFF) >> 12);
                        cacheValid[index] = true;
                    }
                    break;
                }
            }
        }
#endif
    }
    else if (!stall)
    {
        if (exception)
        {
            request = true;
            if ((cop0_SR >> 22) & 1) pc = 0xBFC00180;
            else pc = 0x80000080;
        }
        else
        {
            if (branch)
            {
                pc = newPc;
            }
            fetchReady = false;
            if (!Memory.requestData && !Memory.requestWrite)
            {
                request = true;
            }
            else
            {
                fetchWaitRead = true;
                newStall |= 1;
            }
            if (blockirq > 0) blockirq--;
        }
    }
    else
    {
        if (Memory.done && memoryMuxStage == 1)
        {
            switch (pc >> 29)
            {
            case 0x00: // KUSEG - cached
            case 0x04: // KSEG0 - cached
                opcode[0] = Memory.cacheLine[(pc & 0xF) >> 2];
                {
                    byte index = (pc >> 4) & 0xFF;
                    for (int i = 0; i < 4; i++) cacheData[index][i] = Memory.cacheLine[i];
                    cacheTags[index] = ((pc & 0xFFFFFFFF) >> 12);
                    cacheValid[index] = true;
                }
                break;

            case 0x05: // KSEG1 - uncached
                opcode[0] = Memory.dataRead;
                break;
            }

            newStall &= 0b11110;
            pcOld[0] = pc;
            pc += 4;
            fetchReady = true;
        }

        if (Memory.done && memoryMuxStage == 4 && !fetchReady && ((exception >> 4) == 0))
        {
            request = true;
            if (exception != 0)
            {
                int a = 5;
            }
        }
    }

    if (request)
    {
        fetchWaitRead = false;
        switch (pc >> 29)
        {
        case 0x00: // KUSEG - cached
        case 0x04: // KSEG0 - cached
            {
#ifdef NOMEMTIME
            Memory.requestCode = true;
            Memory.address = pc;
            memoryMuxStage = 1;
            Memory.work();
            Memory.requestCode = false;
            opcode[0] = Memory.cacheLine[(pc & 0xF) >> 2];
            pcOld[0] = pc;
            pc += 4;
#else
                byte index = (pc >> 4) & 0xFF;
#ifndef DISABLEINSTRUCTIONCACHE
                //if (((PSXRegs.CACHECONTROL >> 11) & 1) && cacheValid[index] && cacheTags[index] == (pc >> 12))
                if (cacheValid[index] && cacheTags[index] == ((pc & 0xFFFFFFFF) >> 12))
                {
                    opcode[0] = cacheData[index][(pc & 0xF) >> 2];
                    pcOld[0] = pc;
                    pc += 4;
                    fetchReady = true;
                    newStall &= 0b11110;
                }
                else
#endif
                {
                    Memory.requestCode = true;
                    Memory.address = pc;
                    memoryMuxStage = 1;
                    newStall |= 1;
                }
#endif
            }
            break;

        case 0x05: // KSEG1 - uncached
            Memory.requestCode = true;
            Memory.address = pc;
            memoryMuxStage = 1;
#ifdef NOMEMTIME
            Memory.work();
            opcode[0] = Memory.dataRead;
            pcOld[0] = pc;
            pc += 4;
            fetchReady = true;
#else
            newStall |= 1;
#endif // NOMEMTIME
            break;

        default:
            newException |= 0b00001;
            opcode[0] = 0;
            doException(6, pc, opcode[2], executeBranchTaken, executeBranchdelaySlot);
        }
    }
}
