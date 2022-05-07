#include "CPU.h"
#include "GTE.h"
#include "Memory.h"
#include "psx.h"

void Cpu::stage3()
{
    if (hilo_wait > 0)
    {
        hilo_wait--;
        if (hilo_wait == 0)
        {
            hi = hi_next;
            lo = lo_next;
            if (stallFromHILO)
            {
                newStall &= 0xb11011;
                stallFromHILO = false;
            }
        }
    }

    if (gtecalctime > 0)
    {
        gtecalctime--;
    }
    if (stallFromGTE && gtecalctime == 0)
    {
        newStall &= 0xb11011;
        stallFromGTE = false;
    }

    if (!stall)
    {
        if (exception & 0b11100)
        {
            executeException = true;

            newStall &= 0xb11011;

            resultWriteEnable = false;
            branch = false;
            executeReadEnable = false;
            executeMemWriteEnable = false;
            executeGTEReadEnable = false;
            executeCOP0WriteEnable = false;
            executelastWasLoad = false;
            //execute_gte_writeEna = false; // most likely wrong
            //execute_gte_cmdEna = false; // most likely wrong
        }
        else
        {
            executeException = decodeException;

            executelastWasLoad = executeReadEnable;
            executelastLoadTarget = resultTarget;

            pcOld[2] = pcOld[1];
            opcode[2] = opcode[1];

            resultTarget = decodeTarget;

            resultWriteEnable = false;
            branch = false;
            executeReadEnable = false;
            executeGTEReadEnable = false;
            executeMemWriteEnable = false;
            executeCOP0WriteEnable = false;
            executeBranchTaken = false;
            executeBranchdelaySlot = false;
            execute_gte_writeEna = false;
            execute_gte_cmdEna = false;

            lastReadCOP = false;

            switch (decodeOP)
            {
            case 0x00: // SPECIAL
                switch (decodeFunct)
                {
                case 0x00: // SLL
                    resultWriteEnable = true;
                    resultData = value2 << decodeShamt;
                    break;

                case 0x02: // SRL
                    resultWriteEnable = true;
                    resultData = value2 >> decodeShamt;
                    break;

                case 0x03: // SRA
                    resultWriteEnable = true;
                    resultData = ((Int32)value2) >> decodeShamt;
                    break;

                case 0x04: // SLLV
                    resultWriteEnable = true;
                    resultData = value2 << (value1 & 0x1F);
                    break;

                case 0x06: // SRLV
                    resultWriteEnable = true;
                    resultData = value2 >> (value1 & 0x1F);
                    break;

                case 0x07: // SRAV
                    resultWriteEnable = true;
                    resultData = ((Int32)value2) >> (value1 & 0x1F);
                    break;

                case 0x08: // JR
                    executeBranchdelaySlot = true;
                    executeBranchTaken = true;
                    newPc = value1;
                    if ((newPc & 3) > 0)
                    {
                        psx.log("JR unaligned exception");
                        doException(0x4, value1, opcode[1], false, false);
                        newException |= 0b00100;
                        lastExcIRQ = false;
                        cop0_BADVADDR = value1;
                    }
                    else
                    {
                        branch = true;
                    }
                    break;

                case 0x09: // JALR
                    executeBranchdelaySlot = true;
                    executeBranchTaken = true;
                    resultWriteEnable = true; // write back even when exception comes up!
                    resultData = pc;
                    resultTarget = decodeRD;
                    newPc = value1;
                    if ((newPc & 3) > 0)
                    {
                        psx.log("JALR unaligned exception");
                        doException(0x4, value1, opcode[1], false, false);
                        newException |= 0b00100;
                        lastExcIRQ = false;
                        cop0_BADVADDR = value1;
                    }
                    else
                    {
                        branch = true;
                    }
                    break;

                case 0x0C: // SYSCALL
                    doException(0x8, pcOld[1], opcode[1], executeBranchTaken, executeBranchdelaySlot);
                    newException |= 0b00100;
                    lastExcIRQ = false;
                    break;

                case 0x0D: // BREAK
                    doException(0x9, pcOld[1], opcode[1], executeBranchTaken, executeBranchdelaySlot);
                    newException |= 0b00100;
                    lastExcIRQ = false;
                    break;

                case 0x10: // MFHI
                    resultWriteEnable = true;
                    if (hilo_wait > 0)
                    {
                        resultData = hi_next;
                        newStall |= 0b00100;
                        stallFromHILO = true;
                    }
                    else
                    {
                        resultData = hi;
                    }
                    break;

                case 0x11: // MTHI
                    hi_next = value1;
                    hilo_wait = HILOTIME;
                    break;

                case 0x12: // MFLO
                    resultWriteEnable = true;
                    if (hilo_wait > 0)
                    {
                        resultData = lo_next;
                        newStall |= 0b00100;
                        stallFromHILO = true;
                    }
                    else
                    {
                        resultData = lo;
                    }
                    break;

                case 0x13: // MTLO
                    lo_next = value1;
                    hilo_wait = HILOTIME;
                    break;

                case 0x18: // MULT
                {
                    UInt64 result64 = ((Int64)(Int32)value1) * ((Int64)(Int32)value2);
                    hi_next = result64 >> 32;
                    lo_next = result64;
                    hilo_wait = HILOTIME;
#ifdef FPGACOMPATIBLE
                    if ((value1 >> 11) == 0 || (value1 >> 11) == 0x1FFFFF) hilo_wait = 5;
                    else if ((value1 >> 20) == 0 || (value1 >> 20) == 0xFFF) hilo_wait = 8;
                    else hilo_wait = 12;
#endif
                }
                break;

                case 0x19: // MULTU
                {
                    UInt64 result64 = ((UInt64)value1) * ((UInt64)value2);
                    hi_next = result64 >> 32;
                    lo_next = result64;
                    hilo_wait = HILOTIME;
#ifdef FPGACOMPATIBLE
                    if ((value1 >> 11) == 0) hilo_wait = 5;
                    else if ((value1 >> 20) == 0) hilo_wait = 8;
                    else hilo_wait = 12;
#endif
                }
                break;

                case 0x1A: // DIV
                    hilo_wait = HILOTIME;
#ifdef FPGACOMPATIBLE
                    hilo_wait = 38;
#endif
                    if (value2 == 0)
                    {
                        if (((Int32)value1) >= 0) lo_next = 0xFFFFFFFF;
                        else lo_next = 1;
                        hi_next = value1;

                    }
                    else if (value1 == 0x80000000 && value2 == 0xFFFFFFFF)
                    {
                        lo_next = 0x80000000;
                        hi_next = 0;
                    }
                    else
                    {
                        lo_next = ((Int32)value1) / ((Int32)value2);
                        hi_next = ((Int32)value1) % ((Int32)value2);
                    }
                    break;

                case 0x1B: // DIVU
                    hilo_wait = HILOTIME;
#ifdef FPGACOMPATIBLE
                    hilo_wait = 38;
#endif
                    if (value2 == 0)
                    {
                        lo_next = 0xFFFFFFFF;
                        hi_next = value1;
                    }
                    else
                    {
                        lo_next = value1 / value2;
                        hi_next = value1 % value2;
                    }
                    break;

                case 0x20: // ADD
                    resultData = value1 + value2;
                    if (((resultData ^ value1) & (resultData ^ value2) & 0x80000000) != 0)
                    {
                        psx.log("Add overflow exception");
                        doException(0xC, pcOld[1], opcode[1], executeBranchTaken, executeBranchdelaySlot);
                        newException |= 0b00100;
                        lastExcIRQ = false;
                    }
                    else
                    {
                        resultWriteEnable = true;
                    }
                    break;

                case 0x21: // ADDU
                    resultWriteEnable = true;
                    resultData = value1 + value2;
                    break;

                case 0x22: // SUB
                    resultData = value1 - value2;
                    if (((resultData ^ value1) & (value1 ^ value2) & 0x80000000) != 0)
                    {
                        psx.log("Sub overflow exception");
                        doException(0xC, pcOld[1], opcode[1], executeBranchTaken, executeBranchdelaySlot);
                        newException |= 0b00100;
                        lastExcIRQ = false;
                    }
                    else
                    {
                        resultWriteEnable = true;
                    }
                    break;

                case 0x23: // SUBU
                    resultWriteEnable = true;
                    resultData = value1 - value2;
                    break;

                case 0x24: // AND
                    resultWriteEnable = true;
                    resultData = value1 & value2;
                    break;

                case 0x25: // OR
                    resultWriteEnable = true;
                    resultData = value1 | value2;
                    break;

                case 0x26: // XOR
                    resultWriteEnable = true;
                    resultData = value1 ^ value2;
                    break;

                case 0x27: // NOR
                    resultWriteEnable = true;
                    resultData = ~(value1 | value2);
                    break;

                case 0x2A: // SLT
                    resultWriteEnable = true;
                    if ((Int32)value1 < (Int32)value2) resultData = 1; else resultData = 0;
                    break;

                case 0x2B: // SLTU
                    resultWriteEnable = true;
                    if (value1 < value2) resultData = 1; else resultData = 0;
                    break;

                default:
                    psx.log("Reserved Opcode exception");
                    doException(0xA, pcOld[1], opcode[1], executeBranchTaken, executeBranchdelaySlot);
                    newException |= 0b00100;
                    lastExcIRQ = false;
                }
                break;

            case 0x01: // B: BLTZ, BGEZ, BLTZAL, BGEZAL
                executeBranchdelaySlot = true;
                if (decodeSource2 & 1)
                {
                    if (((Int32)value1) >= 0)
                    {
                        branch = true;
                        executeBranchTaken = true;
                    }
                }
                else
                {
                    if (((Int32)value1) < 0)
                    {
                        branch = true;
                        executeBranchTaken = true;
                    }
                }
                if ((decodeSource2 & 0x1E) == 0x10)
                {
                    resultWriteEnable = true;
                    resultData = pc;
                    resultTarget = 31;
                }
                newPc = pcOld[0] + (((Int32)((Int16)decodeImmData)) << 2);
                break;

            case 0x02: // J
                executeBranchdelaySlot = true;
                executeBranchTaken = true;
                branch = true;
                newPc = (pcOld[0] & 0xF0000000) | (decodeJumpTarget << 2);
                break;

            case 0x03: // JAL
                executeBranchdelaySlot = true;
                executeBranchTaken = true;
                resultWriteEnable = true;
                resultData = pc;
                resultTarget = 31;
                branch = true;
                newPc = (pcOld[0] & 0xF0000000) | (decodeJumpTarget << 2);
                break;

            case 0x04: // BEQ
                executeBranchdelaySlot = true;
                if (value1 == value2)
                {
                    executeBranchTaken = true;
                    branch = true;
                    newPc = pcOld[0] + (((Int32)((Int16)decodeImmData)) << 2);
                }
                break;

            case 0x05: // BNE
                executeBranchdelaySlot = true;
                if (value1 != value2)
                {
                    executeBranchTaken = true;
                    branch = true;
                    newPc = pcOld[0] + (((Int32)((Int16)decodeImmData)) << 2);
                }
                break;

            case 0x06: // BLEZ
                executeBranchdelaySlot = true;
                if (((Int32)value1) <= 0)
                {
                    executeBranchTaken = true;
                    branch = true;
                    newPc = pcOld[0] + (((Int32)((Int16)decodeImmData)) << 2);
                }
                break;

            case 0x07: // BGTZ
                executeBranchdelaySlot = true;
                if (((Int32)value1) > 0)
                {
                    executeBranchTaken = true;
                    branch = true;
                    newPc = pcOld[0] + (((Int32)((Int16)decodeImmData)) << 2);
                }
                break;

            case 0x08: // ADDI
                resultData = value1 + (Int16)decodeImmData;
                if (((resultData ^ value1) & (resultData ^ (decodeImmData << 16)) & 0x80000000) != 0)
                {
                    psx.log("Add overflow exception");
                    doException(0xC, pcOld[1], opcode[1], executeBranchTaken, executeBranchdelaySlot);
                    newException |= 0b00100;
                    lastExcIRQ = false;
                }
                else
                {
                    resultWriteEnable = true;
                }
                break;

            case 0x09: // ADDIU
                resultWriteEnable = true;
                resultData = value1 + (Int16)decodeImmData;
                break;

            case 0x0A: // SLTI
                resultWriteEnable = true;
                if ((Int32)value1 < (Int32)((Int16)decodeImmData)) resultData = 1; else resultData = 0;
                break;

            case 0x0B: // SLTIU
                resultWriteEnable = true;
                if (value1 < (UInt32)((Int32)((Int16)decodeImmData))) resultData = 1; else resultData = 0;
                break;

            case 0x0C: // ANDI
                resultWriteEnable = true;
                resultData = value1 & decodeImmData;
                break;

            case 0x0D: // ORI
                resultWriteEnable = true;
                resultData = value1 | decodeImmData;
                break;

            case 0x0E: // XORI
                resultWriteEnable = true;
                resultData = value1 ^ decodeImmData;
                break;

            case 0x0F: // LUI
                resultWriteEnable = true;
                resultData = decodeImmData << 16;
                break;

            case 0x10: // COP0
                if (((cop0_SR >> 1) & 1) && (((cop0_SR >> 28) & 1) == 0))
                {
                    psx.log("COP0 usermode exception");
                    doException(0xB, pcOld[1], opcode[1], executeBranchTaken, executeBranchdelaySlot);
                    newException |= 0b00100;
                    lastExcIRQ = false;
                }
                else
                {
                    if (decodeSource1 & 0b10000)
                    {
                        switch ((decodeImmData & 0x3F))
                        {
                        case 0x01: 
                        case 0x02: 
                        case 0x04: 
                        case 0x08: 
                            psx.log("COP0 unknown code exception");
                            doException(0xA, pcOld[1], opcode[1], executeBranchTaken, executeBranchdelaySlot);
                            newException |= 0b00100;
                            lastExcIRQ = false;
                            break;

                        case 0x10: // Cop0Op - rfe
                            executeCOP0WriteEnable = true;
                            executeCOP0WriteDestination = 0xC;
                            executeCOP0WriteValue = (cop0_SR & 0xFFFFFFF0) | ((cop0_SR & 0x3F) >> 2);
                            break;
                        }
                    }
                    else
                    {
                        switch (decodeSource1 & 0xF)
                        {
                        case 0: // mfcn
                            resultWriteEnable = true;
                            lastReadCOP = true;
                            switch (decodeRD & 0x1F)
                            {
                            case 0x3: resultData = cop0_BPC; break;
                            case 0x5: resultData = cop0_BDA; break;
                            case 0x6: resultData = cop0_JUMPDEST; break;
                            case 0x7: resultData = cop0_DCIC; break;
                            case 0x8: resultData = cop0_BADVADDR; break;
                            case 0x9: resultData = cop0_BDAM; break;
                            case 0xB: resultData = cop0_BPCM; break;
                            case 0xC: resultData = cop0_SR; break;
                            case 0xD: resultData = cop0_CAUSE; break;
                            case 0xE: resultData = cop0_EPC; break;
                            case 0xF: resultData = cop0_PRID; break;
                            default: resultData = 0;
                            }
                            break;

                        case 4: // mtcn
                            executeCOP0WriteEnable = true;
                            executeCOP0WriteDestination = decodeRD & 0x1F;
                            executeCOP0WriteValue = value2;
                            break;
                        }
                    }
                }
                break;

            case 0x11:  break; // COP1 -> NOP

            case 0x12: // COP2
                if (((cop0_SR >> 30) & 1) == 0)
                {
                    psx.log("COP2 off exception");
                    doException(0xB, pcOld[1], opcode[1], executeBranchTaken, executeBranchdelaySlot);
                    newException |= 0b00100;
                    lastExcIRQ = false;
                }
                else
                {
                    if (decodeSource1 & 0b10000)
                    {
#ifdef FPGACOMPATIBLE
                        execute_gte_cmdEna = true;
                        execute_gte_cmdData = opcode[1];
                        if (gtecalctime > 0)
                        {
                            newStall |= 0b00100;
                            stallFromGTE = true;
                        }
#else
                        Gte.command(opcode[1], 2);
#endif
                    }
                    else
                    {
                        switch (decodeSource1 & 0xF)
                        {
                        case 0x0: //mfcn
                            resultWriteEnable = true;
                            resultData = Gte.read_reg(decodeRD);
                            lastReadCOP = true;
                            if (gtecalctime > 0)
                            {
                                newStall |= 0b00100;
                                stallFromGTE = true;
                            }
                            break;

                        case 0x2: //cfcn
                            resultWriteEnable = true;
                            resultData = Gte.read_reg(decodeRD + 32);
                            lastReadCOP = true;
                            if (gtecalctime > 0)
                            {
                                newStall |= 0b00100;
                                stallFromGTE = true;
                            }
                            break;

                        case 0x4: //mtcn
#ifdef FPGACOMPATIBLE
                            execute_gte_writeEna = true;
                            execute_gte_writeAddr = decodeRD;
                            execute_gte_writeData = value2;
                            if (gtecalctime > 0)
                            {
                                newStall |= 0b00100;
                                stallFromGTE = true;
                        }
#else
                            Gte.write_reg(decodeRD, value2, 2, false);
#endif
                            break;

                        case 0x6: //cfcn
#ifdef FPGACOMPATIBLE
                            execute_gte_writeEna = true;
                            execute_gte_writeAddr = decodeRD + 32;
                            execute_gte_writeData = value2;
                            if (gtecalctime > 0)
                            {
                                newStall |= 0b00100;
                                stallFromGTE = true;
                            }
#else
                            Gte.write_reg(decodeRD + 32, value2, 2, false);
#endif
                            break;

                        }
                    }
                }
                break;

            case 0x13:  break; // COP3 -> NOP

            case 0x20: // LB
                executeLoadType = CPU_LOADTYPE::SBYTE;
                executeReadEnable = true;
                executeReadAddress = value1 + (Int16)decodeImmData;
                break;

            case 0x21: // LH
                executeLoadType = CPU_LOADTYPE::SWORD;
                executeReadAddress = value1 + (Int16)decodeImmData;
                if ((executeReadAddress & 1) == 1)
                {
                    psx.log("LH unaligned exception");
                    doException(0x4, pcOld[1], opcode[1], executeBranchTaken, executeBranchdelaySlot);
                    newException |= 0b00100;
                    lastExcIRQ = false;
                    cop0_BADVADDR = executeReadAddress;
                }
                else
                {
                    executeReadEnable = true;
                }
                break;

            case 0x22: // LWL
                executeLoadType = CPU_LOADTYPE::LEFT;
                executeReadEnable = true;
                executeReadAddress = value1 + (Int16)decodeImmData;
                resultData = value2;
                break;

            case 0x23: // LW
                executeLoadType = CPU_LOADTYPE::DWORD;
                executeReadAddress = value1 + (Int16)decodeImmData;
                if ((executeReadAddress & 3) > 0)
                {
                    psx.log("LW unaligned exception");
                    doException(0x4, pcOld[1], opcode[1], executeBranchTaken, executeBranchdelaySlot);
                    newException |= 0b00100;
                    lastExcIRQ = false;
                    cop0_BADVADDR = executeReadAddress;
                }
                else
                {
                    executeReadEnable = true;
                }
                break;

            case 0x24: // LBU
                executeLoadType = CPU_LOADTYPE::BYTE;
                executeReadEnable = true;
                executeReadAddress = value1 + (Int16)decodeImmData;
                break;

            case 0x25: // LHU
                executeLoadType = CPU_LOADTYPE::WORD;
                executeReadAddress = value1 + (Int16)decodeImmData;
                if ((executeReadAddress & 1) == 1)
                {
                    psx.log("LHU unaligned exception");
                    doException(0x4, pcOld[1], opcode[1], executeBranchTaken, executeBranchdelaySlot);
                    newException |= 0b00100;
                    lastExcIRQ = false;
                    cop0_BADVADDR = executeReadAddress;
                }
                else
                {
                    executeReadEnable = true;
                }
                break;

            case 0x26: // LWR
                executeLoadType = CPU_LOADTYPE::RIGHT;
                executeReadEnable = true;
                executeReadAddress = value1 + (Int16)decodeImmData;
                resultData = value2;
                break;

            case 0x28: // SB
                executeMemWriteEnable = true;
                executeMemWriteAddress = value1 + (Int16)decodeImmData;
                executeMemWriteData = value2;
                switch (executeMemWriteAddress & 3)
                {
                case 0: executeMemWriteMask = 0b0001; break;
                case 1: executeMemWriteMask = 0b0010; executeMemWriteData <<= 8; break;
                case 2: executeMemWriteMask = 0b0100; executeMemWriteData <<= 16; break;
                case 3: executeMemWriteMask = 0b1000; executeMemWriteData <<= 24; break;
                }
                executeMemWriteAddress &= 0xFFFFFFFC;
                break;

            case 0x29: // SH
                executeMemWriteAddress = value1 + (Int16)decodeImmData;
                executeMemWriteData = value2;
                if (executeMemWriteAddress & 2)
                {
                    executeMemWriteData <<= 16;
                    executeMemWriteMask = 0b1100;
                }
                else
                {
                    executeMemWriteMask = 0b0011;
                }
                if ((executeMemWriteAddress & 1) == 1)
                {
                    psx.log("SH unaligned exception");
                    doException(0x5, pcOld[1], opcode[1], executeBranchTaken, executeBranchdelaySlot);
                    newException |= 0b00100;
                    lastExcIRQ = false;
                    cop0_BADVADDR = executeMemWriteAddress;
                }
                else
                {
                    executeMemWriteEnable = true;
                    executeMemWriteAddress &= 0xFFFFFFFC;
                }
                break;

            case 0x2A: // SWL
                executeMemWriteEnable = true;
                executeMemWriteAddress = value1 + (Int16)decodeImmData;
                switch (executeMemWriteAddress & 3)
                {
                case 0: executeMemWriteMask = 0b0001; executeMemWriteData = value2 >> 24; break;
                case 1: executeMemWriteMask = 0b0011; executeMemWriteData = value2 >> 16; break;
                case 2: executeMemWriteMask = 0b0111; executeMemWriteData = value2 >> 8;  break;
                case 3: executeMemWriteMask = 0b1111; executeMemWriteData = value2; break;
                }
                executeMemWriteAddress &= 0xFFFFFFFC;
                break;

            case 0x2B: // SW
                executeMemWriteAddress = value1 + (Int16)decodeImmData;
                executeMemWriteData = value2;
                executeMemWriteMask = 0b1111;
                if ((executeMemWriteAddress & 3) > 0)
                {
                    psx.log("SW unaligned exception");
                    doException(0x5, pcOld[1], opcode[1], executeBranchTaken, executeBranchdelaySlot);
                    newException |= 0b00100;
                    lastExcIRQ = false;
                    cop0_BADVADDR = executeMemWriteAddress;
                }
                else
                {
                    executeMemWriteEnable = true;
                    executeMemWriteAddress &= 0xFFFFFFFC;
                }
                break;

            case 0x2E: // SWR
                executeMemWriteEnable = true;
                executeMemWriteAddress = value1 + (Int16)decodeImmData;
                switch (executeMemWriteAddress & 3)
                {
                case 0: executeMemWriteMask = 0b1111; executeMemWriteData = value2; break;
                case 1: executeMemWriteMask = 0b1110; executeMemWriteData = value2 << 8; break;
                case 2: executeMemWriteMask = 0b1100; executeMemWriteData = value2 << 16; break;
                case 3: executeMemWriteMask = 0b1000; executeMemWriteData = value2 << 24; break;
                }
                executeMemWriteAddress &= 0xFFFFFFFC;
                break;

            case 0x30:  break; // LWC0 -> NOP

            case 0x31:  // LWC1 -> NOP 
                break;

            case 0x32:  // LWC2
                if (((cop0_SR >> 30) & 1) == 0)
                {
                    psx.log("COP2 off exception");
                    doException(0xB, pcOld[1], opcode[1], executeBranchTaken, executeBranchdelaySlot);
                    newException |= 0b00100;
                    lastExcIRQ = false;
                }
                else
                {
                    executeGTEReadEnable = true;
                    executeLoadType = CPU_LOADTYPE::DWORD;
                    executeReadEnable = true;
                    executeReadAddress = value1 + (Int16)decodeImmData;
                    executeGTETarget = decodeSource2;
#ifdef FPGACOMPATIBLE
                    if (gtecalctime > 0)
                    {
                        newStall |= 0b00100;
                        stallFromGTE = true;
                    }
#endif
                }
                break;

            case 0x33:  break; // LWC3 -> NOP

            case 0x38:  break; // SWC0 -> NOP

            case 0x39: break;  // SWC1 -> NOP

            case 0x3A:  // SWC2
                if (((cop0_SR >> 30) & 1) == 0)
                {
                    psx.log("COP2 off exception");
                    doException(0xB, pcOld[1], opcode[1], executeBranchTaken, executeBranchdelaySlot);
                    newException |= 0b00100;
                    lastExcIRQ = false;
                }
                else
                {
                    executeMemWriteEnable = true;
                    executeMemWriteAddress = value1 + (Int16)decodeImmData;
                    executeMemWriteData = Gte.read_reg(decodeSource2);
                    executeMemWriteMask = 0b1111;
                    executeMemWriteAddress &= 0xFFFFFFFC;
                    if (gtecalctime > 0)
                    {
                        newStall |= 0b00100;
                        stallFromGTE = true;
                    }
                }
                break;


            case 0x3B:  break; // SWC3 -> NOP

            default:
                psx.log("Unknown instruction OPCode");
                doException(0xA, pcOld[1], opcode[1], executeBranchTaken, executeBranchdelaySlot);
                newException |= 0b00100;
                lastExcIRQ = false;
            }

            if (executeMemWriteEnable || executeReadEnable)
            {
                UInt32 address = executeMemWriteAddress;
                if (executeReadEnable) address = executeReadAddress;
                UInt32 exc_addr = address;

                bool nocheck = false;
                byte area = address >> 29;
                switch (area)
                {
                case 0x00: // KUSEG - cached
                case 0x04: // KSEG0 - cached
                    if ((cop0_SR >> 16) & 1) nocheck = true; // cache isolation
                    address &= 0x1FFFFFFF;
                    if ((address & 0xFFFFFC00) == 0x1F800000)  nocheck = true; // scratchpad 
                    break;

                case 0x05: // KSEG1 - uncached
                    address &= 0x1FFFFFFF;
                    break;

                case 0x01: // KUSEG 512M-1024M
                case 0x02: // KUSEG 1024M-1536M
                case 0x03: // KUSEG 1536M-2048M
                    nocheck = true;
                    psx.log("Area 1-3 access exception");
                    doException(0x7, pcOld[1], opcode[1], executeBranchTaken, executeBranchdelaySlot);
                    newException |= 0b00100;
                    lastExcIRQ = false;
                    cop0_BADVADDR = address;
                    executeMemWriteEnable = false;
                    executeReadAddress = false;
                    break;

                case 06: // KSEG2
                case 07: // KSEG2
                    nocheck = true;
                    if (address != 0xFFFE0130)
                    {
                        psx.log("Area 6-7 access exception");
                        doException(0x7, pcOld[1], opcode[1], executeBranchTaken, executeBranchdelaySlot);
                        newException |= 0b00100;
                        lastExcIRQ = false;
                        cop0_BADVADDR = address;
                        executeMemWriteEnable = false;
                        executeReadAddress = false;
                    }
                    break;
                }

                if (!nocheck)
                {

                    if (address < 0x800000) {} // RAM
                    else if (address >= 0x1FC00000 && address < 0x1FC80000) {}// BIOS
                    else if (address >= 0x1F000000 && address < 0x1F800000) {}// EXP1
                    else if (address >= 0x1F801000 && address < 0x1F801040) {}// MEMCTRL
                    else if (address >= 0x1F801040 && address < 0x1F801050) {}// PAD
                    else if (address >= 0x1F801050 && address < 0x1F801060) {}// SIO
                    else if (address >= 0x1F801060 && address < 0x1F801070) {}// MEMCTRL2
                    else if (address >= 0x1F801070 && address < 0x1F801080) {}// IRQ
                    else if (address >= 0x1F801080 && address < 0x1F801100) {}// DMA
                    else if (address >= 0x1F801100 && address < 0x1F801140) {}// TIMER
                    else if (address >= 0x1F801800 && address < 0x1F801810) {}// CDROM
                    else if (address >= 0x1F801810 && address < 0x1F801820) {}// GPU
                    else if (address >= 0x1F801820 && address < 0x1F801830) {}// MDEC
                    else if (address >= 0x1F801C00 && address < 0x1F802000) {}// SPU
                    else if (address >= 0x1F802000 && address < 0x1F804000) {}// EXP2
                    else if (address >= 0x1FA00000 && address < 0x1FA00001) {}// EXP3
                    else
                    {
                        psx.log("Undefined register area exception");
                        doException(0x7, pcOld[1], opcode[1], executeBranchTaken, executeBranchdelaySlot);
                        newException |= 0b00100;
                        lastExcIRQ = false;
                        cop0_BADVADDR = exc_addr;
                        executeMemWriteEnable = false;
                        executeReadAddress = false;
                    }
                }
            }
        }
    }
}
