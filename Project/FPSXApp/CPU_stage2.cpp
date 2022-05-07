#include "CPU.h"
#include "Memory.h"

void Cpu::stage2()
{
    if (!stall)
    {
        if (exception & 0b11110)
        {
            if (exception & 0b10000) decodeException = true;

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
        }
        else
        {
            decodeException = false;
            if (exception & 0b00001) decodeException = true;

            pcOld[1] = pcOld[0];
            opcode[1] = opcode[0];

            decodeImmData = (opcode[0] & 0xFFFF);
            decodeJumpTarget = (opcode[0] & 0x3FFFFFF);
            decodeSource1 = (opcode[0] >> 21) & 0x1F;
            decodeSource2 = (opcode[0] >> 16) & 0x1F;
            decodeValue1 = regs[decodeSource1];
            decodeValue2 = regs[decodeSource2];
            decodeOP = opcode[0] >> 26;
            decodeFunct = opcode[0] & 0x3F;
            decodeShamt = (opcode[0] >> 6) & 0x1F;
            decodeRD = (opcode[0] >> 11) & 0x1F;
            if (decodeOP > 0)
            {
                decodeTarget = (opcode[0] >> 16) & 0x1F;
            }
            else
            {
                decodeTarget = (opcode[0] >> 11) & 0x1F;
            }
        }
    }
}
