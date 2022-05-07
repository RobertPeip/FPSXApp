#include "CPU.h"
#include "GTE.h"
#include "Memory.h"
#include "PSXRegs.h"

void Cpu::stage4()
{
    if (!stall)
    {
        if (exception & 0b11000)
        {
            writebackException = true;

            newStall &= 0b10111; // ? cannot be stalled here

            writebackMemWriteEnable = false;
            writebackWriteEnable = false;
            writebackCOP0WriteEnable = false;
        }
        else
        {
            pcOld[3] = pcOld[2];
            opcode[3] = opcode[2];
            writebackWriteEnable = false;
            writebackTarget = resultTarget;

            writebackMemWriteData = executeMemWriteData;
            writebackMemWriteAddress = executeMemWriteAddress;
            writebackMemWriteEnable = executeMemWriteEnable;
            writebackMemWriteMask = executeMemWriteMask;

            writebackException = executeException;

            writebackCOP0WriteEnable = executeCOP0WriteEnable;
            writebackCOP0WriteDestination = executeCOP0WriteDestination;
            writebackCOP0WriteValue = executeCOP0WriteValue;

            writebackBranchTaken = executeBranchTaken;
            writebackBranchdelaySlot = executeBranchdelaySlot;

            writebackGTEReadEnable = executeGTEReadEnable;
            writebackGTETarget = executeGTETarget;

            writebackData = resultData;

            if (executeCOP0WriteEnable)
            {
                switch (executeCOP0WriteDestination)
                {
                case 0xC: cop0_SR = executeCOP0WriteValue & 0xF27FFF3F; break; // only this is done in stage 4, todo: what is correct stage4 or stage5?
                }
            }

#ifndef STAGE5WRITE
            if (executeMemWriteEnable)
            {
                bool skipmem = false;
                switch (executeMemWriteAddress >> 29)
                {
                case 0x00: // KUSEG - cached
                case 0x04: // KSEG0 - cached
                {
                    if ((cop0_SR >> 16) & 1) // cache isolation
                    {
                        skipmem = true;
                        //updateCache(executeMemWriteAddress, executeMemWriteData, executeMemWriteMask);
                        byte index = (executeMemWriteAddress >> 4) & 0xFF;
                        cacheValid[index] = false;
                    }

                    UInt32 addr = executeMemWriteAddress & 0x1FFFFFFF;
                    if ((addr & 0xFFFFFC00) == 0x1F800000) // scratchpad
                    {
                        skipmem = true;
                        addr &= 0x3FC;
                        if (executeMemWriteMask & 0b0001) scratchpad[addr] = executeMemWriteData;
                        if (executeMemWriteMask & 0b0010) scratchpad[addr + 1] = executeMemWriteData >> 8;
                        if (executeMemWriteMask & 0b0100) scratchpad[addr + 2] = executeMemWriteData >> 16;
                        if (executeMemWriteMask & 0b1000) scratchpad[addr + 3] = executeMemWriteData >> 24;
                    }
                }
                break;

                case 06: // KSEG2
                case 07: // KSEG2
                    skipmem = true;
                    if (executeMemWriteAddress == 0xFFFE0130)
                    {
                        PSXRegs.CACHECONTROL = executeMemWriteData;
                    }
                    break;
                }

                if (!skipmem)
                {
                    Memory.requestWrite = true;
                    Memory.addressWrite = executeMemWriteAddress;
                    Memory.dataWrite = executeMemWriteData;
                    Memory.writeMask = executeMemWriteMask;
#if defined(NOMEMTIME) || defined(NOMEMREADTIME)
                    Memory.work();
                    Memory.requestWrite = false;
                    Memory.busy = 0;
#else
                    memoryMuxStage = 4;
                    newStall |= 0b01000;
                    writebackMemOPisRead = false;
#endif
                }
            }
#endif

            if (executeReadEnable)
            {
                writebackLoadType = executeLoadType;
                writebackReadAddress = executeReadAddress;

                switch (executeLoadType)
                {
                case CPU_LOADTYPE::SBYTE: Memory.requestSize = 1; break;
                case CPU_LOADTYPE::SWORD: Memory.requestSize = 2; break;
                case CPU_LOADTYPE::LEFT: Memory.requestSize = 4; break;
                case CPU_LOADTYPE::DWORD: Memory.requestSize = 4; break;
                case CPU_LOADTYPE::BYTE: Memory.requestSize = 1; break;
                case CPU_LOADTYPE::WORD: Memory.requestSize = 2; break;
                case CPU_LOADTYPE::RIGHT: Memory.requestSize = 4; break;
                }
                byte area = executeReadAddress >> 29;
                if ((area == 0 || area == 4) && (executeReadAddress & 0x1FFFFC00) == 0x1F800000) // scratchpad
                {
                    UInt32 data;
                    if (executeLoadType == CPU_LOADTYPE::LEFT || executeLoadType == CPU_LOADTYPE::RIGHT) data = *(UInt32*)&scratchpad[executeReadAddress & 0x3FC];
                    else data = *(UInt32*)&scratchpad[executeReadAddress & 0x3FF];
                    stage4DataRead(data, executeReadAddress & 3);
                }
                else
                {
                    Memory.requestData = true;
                    Memory.address = executeReadAddress;
                    if (executeLoadType == CPU_LOADTYPE::LEFT || executeLoadType == CPU_LOADTYPE::RIGHT) Memory.address &= 0xFFFFFFC;
#if defined(NOMEMTIME) || defined(NOMEMREADTIME)
                    Memory.work();
                    Memory.requestData = false;
                    Memory.busy = 0;
                    stage4DataRead(Memory.dataRead, executeReadAddress & 3);
#else
                    memoryMuxStage = 4;
                    newStall |= 0b01000;
                    writebackMemOPisRead = true;
#endif // NOMEMTIME
                }
            }
            else
            {
                writebackWriteEnable = resultWriteEnable;
            }

#ifdef FPGACOMPATIBLE
            if (execute_gte_writeEna)
            {
                Gte.write_reg(execute_gte_writeAddr, execute_gte_writeData, 2, false);
            }

            if (execute_gte_cmdEna)
            {
                gtecalctime = Gte.command(execute_gte_cmdData, 2);
            }
#endif
        }
    }
    else
    {
        if (Memory.done && memoryMuxStage == 4)
        {
            newStall &= 0b10111;
            if (writebackMemOPisRead) stage4DataRead(Memory.dataRead, writebackReadAddress & 3);
        }
    }
}

void Cpu::stage4DataRead(UInt32 data, byte AddrLow)
{
    if (writebackGTEReadEnable)
    {
        Gte.write_reg(writebackGTETarget, data, 1, false);
    }
    else
    {
        writebackWriteEnable = true;
        switch (writebackLoadType)
        {
        case CPU_LOADTYPE::SBYTE: writebackData = (SByte)data; break;
        case CPU_LOADTYPE::SWORD: writebackData = (Int16)data; break;
        case CPU_LOADTYPE::LEFT:
        {
            UInt32 oldData;
            if (writeDoneTarget == writebackTarget && writeDoneWriteEnable) oldData = writeDoneData; // using writeDoneTarget/Data as writeback values are already changed!
            else oldData = writebackData;
            switch (AddrLow)
            {
            case 0: writebackData = (oldData & 0x00FFFFFF) | (data << 24); break;
            case 1: writebackData = (oldData & 0x0000FFFF) | (data << 16); break;
            case 2: writebackData = (oldData & 0x000000FF) | (data << 8); break;
            case 3: writebackData = data; break;
            }
        }
            break;
        case CPU_LOADTYPE::DWORD: writebackData = data; break;
        case CPU_LOADTYPE::BYTE:  writebackData = data & 0xFF; break;
        case CPU_LOADTYPE::WORD:  writebackData = data & 0xFFFF; break;
        case CPU_LOADTYPE::RIGHT:
        {
            UInt32 oldData;
            if (writeDoneTarget == writebackTarget && writeDoneWriteEnable)
                oldData = writeDoneData; // using writeDoneTarget/Data as writeback values are already changed!
            else oldData = writebackData;
            switch (AddrLow)
            {
            case 0: writebackData = data; break;
            case 1: writebackData = (oldData & 0xFF000000) | (data >> 8); break;
            case 2: writebackData = (oldData & 0xFFFF0000) | (data >> 16); break;
            case 3: writebackData = (oldData & 0xFFFFFF00) | (data >> 24); break;
            }
            break;
        }
        }
    }
}