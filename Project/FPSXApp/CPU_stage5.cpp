#include "CPU.h"
#include "Memory.h"
#include "PSXRegs.h"

void Cpu::stage5()
{
	// stage 5
    if (!stall)
    {
        pcOld[4] = pcOld[3];
        opcode[4] = opcode[3];
        writeDoneData = writebackData;
        writeDoneTarget = writebackTarget;
        writeDoneWriteEnable = writebackWriteEnable;

        writeDoneException = writebackException;

        writeDoneBranchTaken = writebackBranchTaken;
        writeDoneBranchdelaySlot = writebackBranchdelaySlot;

        if (emptyPipe > 0)
        {
            emptyPipe--;
#if (FIXEDMEMTIME == 2)
            totalticks = -11 + (FIXEDMEMTIME * 4);
#elif (FIXEDMEMTIME == 3)
            totalticks = -16 + (FIXEDMEMTIME * 4);
#elif defined(NOMEMTIME)
            totalticks = -1;
#else
            totalticks = 47;
#endif // NOMEMTIME
        }
        if (writebackWriteEnable && !writebackException)
        {
            regs[writebackTarget] = writebackData;
            regs[0] = 0;
        }
#ifdef STAGE5WRITE
        if (writebackMemWriteEnable)
        {
            bool skipmem = false;
            switch (writebackMemWriteAddress >> 29)
            {
            case 0x00: // KUSEG - cached
            case 0x04: // KSEG0 - cached
            {
                if ((cop0_SR >> 16) & 1) // cache isolation
                {
                    skipmem = true;
                    byte index = (writebackMemWriteAddress >> 4) & 0xFF;
                    cacheData[index][(writebackMemWriteAddress & 0xF) >> 2] = writebackMemWriteData;
                    cacheTags[index] = (writebackMemWriteAddress >> 12);
                    cacheValid[index] = false;
                }

                UInt32 addr = writebackMemWriteAddress & 0x1FFFFFFF;
                if ((addr & 0xFFFFFC00) == 0x1F800000) // scratchpad
                {
                    skipmem = true;
                    addr &= 0x3FC;
                    if (writebackMemWriteMask & 0b0001) scratchpad[addr] = writebackMemWriteData;
                    if (writebackMemWriteMask & 0b0010) scratchpad[addr + 1] = writebackMemWriteData >> 8;
                    if (writebackMemWriteMask & 0b0100) scratchpad[addr + 2] = writebackMemWriteData >> 16;
                    if (writebackMemWriteMask & 0b1000) scratchpad[addr + 3] = writebackMemWriteData >> 24;
                }
            }
                break;

            case 06: // KSEG2
            case 07: // KSEG2
                skipmem = true;
                if (writebackMemWriteAddress == 0xFFFE0130)
                {
                    PSXRegs.CACHECONTROL = writebackMemWriteData;
                }
                break;
            }

            if (!skipmem)
            {
                Memory.requestWrite = true;
                Memory.addressWrite = writebackMemWriteAddress;
                Memory.dataWrite = writebackMemWriteData;
                Memory.writeMask = writebackMemWriteMask;
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
        if (writebackException)
        {
            cop0_SR = exception_SR;
            cop0_CAUSE = exception_CAUSE;
            cop0_EPC = exception_EPC;
            cop0_SR_next = exception_SR;
            cop0_JUMPDEST = exception_JMP;
        }
        if (writebackCOP0WriteEnable)
        {
            switch (writebackCOP0WriteDestination)
            {
            case 0x3: cop0_BPC = writebackCOP0WriteValue; break;
            case 0x5: cop0_BDA = writebackCOP0WriteValue; break;
            case 0x7: cop0_DCIC = writebackCOP0WriteValue & 0xFF80F03F; break;
            case 0x9: cop0_BDAM = writebackCOP0WriteValue; break;
            case 0xB: cop0_BPCM = writebackCOP0WriteValue; break;
            //case 0xC: cop0_SR = writebackCOP0WriteValue & 0xF27FFF3F; break; // done in stage 4
            case 0xD: cop0_CAUSE = writebackCOP0WriteValue & 0x00000300; break;
            }
        }
    }
}
