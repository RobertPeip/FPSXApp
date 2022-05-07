#include <algorithm>
#include <bitset>
#undef NDEBUG
#include <assert.h>
#include <fstream>
#include <string>

#include "CPU.h"
#include "GTE.h"
#include "psx.h"
#include "memory.h"

GTE Gte;

void GTE::reset()
{
    for (int i = 0; i < 64; i++)
    {
        regs[i] = 0;
    }
}

void GTE::write_reg(byte adr, UInt32 value, byte cmdOffset, bool loadSS)
{
#ifdef GTEFILEOUT
    if (debug_GTEOutCount < GTEOutCountMax - 100)
    {
        debug_GTEOutTime[debug_GTEOutCount] = CPU.commands + cmdOffset;
        debug_GTEOutAddr[debug_GTEOutCount] = adr;
        debug_GTEOutData[debug_GTEOutCount] = value;
        debug_GTEOutType[debug_GTEOutCount] = 4;
        debug_GTEOutCount++;
    }
#endif

    switch (adr)
    {
    case 8:  // IR0
    case 9:  // IR1
    case 10: // IR2
    case 11: // IR3
    case 58: // H       - sign-extended on read but zext on use
    case 59: // DQA
    case 61: // ZSF3
    case 62: // ZSF4
    {
        regs[adr] = (Int32)(Int16)(value);
    }
    break;

    case V0XY:
        regs[adr] = value;
        vectors[0][0] = (Int16)(value & 0xFFFF);
        vectors[0][1] = (Int16)(value >> 16);
        break;

    case V0Z:
        regs[adr] = (Int32)(Int16)(value);
        vectors[0][2] = (Int16)(value & 0xFFFF);
        break;

    case V1XY:
        regs[adr] = value;
        vectors[1][0] = (Int16)(value & 0xFFFF);
        vectors[1][1] = (Int16)(value >> 16);
        break;

    case V1Z:
        regs[adr] = (Int32)(Int16)(value);
        vectors[1][2] = (Int16)(value & 0xFFFF);
        break;

    case V2XY:
        regs[adr] = value;
        vectors[2][0] = (Int16)(value & 0xFFFF);
        vectors[2][1] = (Int16)(value >> 16);
        break;

    case V2Z:
        regs[adr] = (Int32)(Int16)(value);
        vectors[2][2] = (Int16)(value & 0xFFFF);
        break;

    case 7:  // OTZ
    case 16: // SZ0
    case 17: // SZ1
    case 18: // SZ2
    case 19: // SZ3
    {
        regs[adr] = value & 0xFFFF;
    }
    break;

    case 15: // SXY3 -> // writing to SXYP pushes to the FIFO
    {
        if (loadSS) regs[GTEREGPOS::SXYP] = value;
        else pushSXY(value);      
    }
    break;

    case 28: // IRGB
    {
        // IRGB register, convert 555 to 16-bit
        regs[adr] = value & 0x7FFF;
        if (!loadSS)
        {
            regs[GTEREGPOS::IR1] = (value & 0x1F) * 0x80;
            regs[GTEREGPOS::IR2] = ((value >> 5) & 0x1F) * 0x80;
            regs[GTEREGPOS::IR3] = ((value >> 10) & 0x1F) * 0x80;
        }
    }
    break;

    case 30: // LZCS
    {
        regs[adr] = value;
        byte leadCount = 0;
        if (value & 0x80000000)
        {
            value = ~value;
        }
        for (int i = 0; i < 32; i++)
        {
            if ((value >> i) == 0)
            {
                leadCount = 32 - i;
                break;
            }
        }
        regs[GTEREGPOS::LZCR] = leadCount;
    }
    break;

    case 29: // ORGB
    case 31: // LZCR
    {
        if (loadSS) regs[adr] = value;
        // read-only registers
    }
    break;

    case RT0:
        regs[adr] = value;
        rotMatrix[0][0] = (Int16)(value & 0xFFFF);
        rotMatrix[0][1] = (Int16)(value >> 16);
        break;

    case RT1: 
        regs[adr] = value;
        rotMatrix[0][2] = (Int16)(value & 0xFFFF);
        rotMatrix[1][0] = (Int16)(value >> 16);
        break;
    
    case RT2: 
        regs[adr] = value;
        rotMatrix[1][1] = (Int16)(value & 0xFFFF);
        rotMatrix[1][2] = (Int16)(value >> 16);
        break;
    
    case RT3: 
        regs[adr] = value;
        rotMatrix[2][0] = (Int16)(value & 0xFFFF);
        rotMatrix[2][1] = (Int16)(value >> 16);
        break;
    
    case RT4:
        regs[adr] = (Int32)(Int16)(value);
        rotMatrix[2][2] = (Int16)(value & 0xFFFF);
        break;

    case LLM0:
        regs[adr] = value;
        lightMatrix[0][0] = (Int16)(value & 0xFFFF);
        lightMatrix[0][1] = (Int16)(value >> 16);
        break;

    case LLM1:
        regs[adr] = value;
        lightMatrix[0][2] = (Int16)(value & 0xFFFF);
        lightMatrix[1][0] = (Int16)(value >> 16);
        break;

    case LLM2:
        regs[adr] = value;
        lightMatrix[1][1] = (Int16)(value & 0xFFFF);
        lightMatrix[1][2] = (Int16)(value >> 16);
        break;

    case LLM3:
        regs[adr] = value;
        lightMatrix[2][0] = (Int16)(value & 0xFFFF);
        lightMatrix[2][1] = (Int16)(value >> 16);
        break;

    case LLM4:
        regs[adr] = (Int32)(Int16)(value);
        lightMatrix[2][2] = (Int16)(value & 0xFFFF);
        break;

    case LCM0:
        regs[adr] = value;
        colorMatrix[0][0] = (Int16)(value & 0xFFFF);
        colorMatrix[0][1] = (Int16)(value >> 16);
        break;

    case LCM1:
        regs[adr] = value;
        colorMatrix[0][2] = (Int16)(value & 0xFFFF);
        colorMatrix[1][0] = (Int16)(value >> 16);
        break;

    case LCM2:
        regs[adr] = value;
        colorMatrix[1][1] = (Int16)(value & 0xFFFF);
        colorMatrix[1][2] = (Int16)(value >> 16);
        break;

    case LCM3:
        regs[adr] = value;
        colorMatrix[2][0] = (Int16)(value & 0xFFFF);
        colorMatrix[2][1] = (Int16)(value >> 16);
        break;

    case LCM4:
        regs[adr] = (Int32)(Int16)(value);
        colorMatrix[2][2] = (Int16)(value & 0xFFFF);
        break;

    case 63: // FLAG
        regs[GTEREGPOS::FLAGS] = value & 0x7FFFF000;
        if ((regs[GTEREGPOS::FLAGS] & 0x7F87E000) > 0) regs[GTEREGPOS::FLAGS] |= 1 << 31;
        break;

    default:
        regs[adr] = value;

    }

#ifdef GTEFILEOUT
    GTEoutRegCapture(2, cmdOffset);
#endif
}

UInt32 GTE::read_reg(byte adr)
{
    UInt32 retval;

    switch (adr)
    {
    case 15: // SXY3/P -> // mirror of SXY2     
        retval = regs[GTEREGPOS::SXY2];
        break;
    
    case 28: // IRGB
    case 29: // ORGB
    {
        // ORGB register, convert 16-bit to 555
        Int16 r = (Int16)(regs[GTEREGPOS::IR1] & 0xFFFF) / 0x80;
        Int16 g = (Int16)(regs[GTEREGPOS::IR2] & 0xFFFF) / 0x80;
        Int16 b = (Int16)(regs[GTEREGPOS::IR3] & 0xFFFF) / 0x80;
        if (r < 0) r = 0; if (r > 0x1F) r = 0x1F;
        if (g < 0) g = 0; if (g > 0x1F) g = 0x1F;
        if (b < 0) b = 0; if (b > 0x1F) b = 0x1F;
        retval =  r | (g << 5) | (b << 10);
    }
    break;

    default:
        retval = regs[adr];
    }

#ifdef GTEFILEOUT
    if (debug_GTEOutCount < GTEOutCountMax - 100)
    {
        debug_GTEOutTime[debug_GTEOutCount] = CPU.commands;
        debug_GTEOutAddr[debug_GTEOutCount] = adr;
        debug_GTEOutData[debug_GTEOutCount] = retval;
        debug_GTEOutType[debug_GTEOutCount] = 5;
        debug_GTEOutCount++;
    }
#endif

    //if (debug_GTEOutCount > 100)
    //{
    //    debug_GTEOutCount = 0;
    //    return 0;
    //}

    return retval;
}

byte GTE::command(UInt32 instruction, byte cmdOffset)
{
#ifdef GTEFILEOUT
    GTEoutCommandCapture(instruction, cmdOffset);
#endif

    if (instruction == 0x4a0ca030)
    {
        int a = 5;
    }

    byte time = 0;

    regs[GTEREGPOS::FLAGS] = 0;

    byte command = instruction & 0x7F;
    bool shift = (instruction >> 19) & 1;
    bool satIR = (instruction >> 10) & 1;

    switch (command)
    {
    case 0x01:
        time = 14;
        RTPS(0, satIR, shift, true);
        break;

    case 0x06:
        time = 8;
        NCLIP();
        break;    

    case 0x0C:
        time = 5;
        OP(satIR, shift);
        break;

    case 0x10:
        time = 8;
        DPCS(regs[GTEREGPOS::RGBC], satIR, shift);
        break;

    case 0x11:
        time = 8;
        INTPL(satIR, shift);
        break;
    
    case 0x12:
        time = 7;
        MVMVA(instruction);
        break;

    case 0x13:
        time = 14;
        NCDS(0, satIR, shift);
        break;   
    
    case 0x14:
        CDP(satIR, shift);
        break;

    case 0x16:
        time = 36;
        NCDS(0, satIR, shift);
        NCDS(1, satIR, shift);
        NCDS(2, satIR, shift);
        break;

    case 0x1B:
        NCCS(0, satIR, shift);
        break;

    case 0x1C:
        CC(satIR, shift);
        break;

    case 0x1E:
        NCS(0, satIR, shift);
        break;

    case 0x20:
        NCS(0, satIR, shift);
        NCS(1, satIR, shift);
        NCS(2, satIR, shift);
        break;

    case 0x28:
        time = 4;
        SQR(satIR, shift);
        break;

    case 0x29:
        DPCL(satIR, shift);
        break;

    case 0x2A:
        time = 17;
        DPCS(regs[GTEREGPOS::RGB0], satIR, shift);
        DPCS(regs[GTEREGPOS::RGB0], satIR, shift);
        DPCS(regs[GTEREGPOS::RGB0], satIR, shift);
        break;

    case 0x2D: 
        AVSZ3(); 
        break;

    case 0x2E:
        time = 7;
        AVSZ4();
        break;

    case 0x30: // RTPT
        time = 35;
        RTPS(0, satIR, shift, false);
        RTPS(1, satIR, shift, false);
        RTPS(2, satIR, shift, true);
        break;

    case 0x3D:
        time = 6;
        GPF(satIR, shift);
        break;    
    
    case 0x3E:
        GPL(satIR, shift);
        break;

    case 0x3F:
        NCCS(0, satIR, shift);
        NCCS(1, satIR, shift);
        NCCS(2, satIR, shift);
        break;

    default:
        _wassert(_CRT_WIDE("GTE Command not implemented"), _CRT_WIDE("GTE"), command);
        break;
    }

    if ((regs[GTEREGPOS::FLAGS] & 0x7F87E000) > 0) regs[GTEREGPOS::FLAGS] |= 1 << 31;

#ifdef GTEFILEOUT
    GTEoutRegCapture(3, cmdOffset);

    commandCnt[command]++;
#endif

    return time;
}

// ALU
void GTE::pushSXY(UInt32 value)
{
    regs[GTEREGPOS::SXY0] = regs[GTEREGPOS::SXY1];
    regs[GTEREGPOS::SXY1] = regs[GTEREGPOS::SXY2];
    regs[GTEREGPOS::SXY2] = value;
}

void GTE::pushSXY(Int32 sx, Int32 sy)
{
    if (sx < -1024)
    {
        sx = -1024;
        regs[GTEREGPOS::FLAGS] |= 1 << 14;
    }
    else if (sx > 1023)
    {
        sx = 1023;
        regs[GTEREGPOS::FLAGS] |= 1 << 14;
    }

    if (sy < -1024)
    {
        sy = -1024;
        regs[GTEREGPOS::FLAGS] |= 1 << 13;
    }
    else if (sy > 1023)
    {
        sy = 1023;
        regs[GTEREGPOS::FLAGS] |= 1 << 13;
    }

    pushSXY(((sy & 0xFFFF) << 16) | (sx & 0xFFFF));
}

void GTE::pushSZ(Int32 value)
{
    if (value < 0)
    {
        value = 0;
        regs[GTEREGPOS::FLAGS] |= 1 << SZ1OTZSAT;
    }
    else if (value > 0xFFFF)
    {
        value = 0xFFFF;
        regs[GTEREGPOS::FLAGS] |= 1 << SZ1OTZSAT;
    }

    regs[GTEREGPOS::SZ0] = regs[GTEREGPOS::SZ1];
    regs[GTEREGPOS::SZ1] = regs[GTEREGPOS::SZ2];
    regs[GTEREGPOS::SZ2] = regs[GTEREGPOS::SZ3];
    regs[GTEREGPOS::SZ3] = value;
}

void GTE::UNRDivide(UInt32 lhs, UInt32 rhs)
{
    if (rhs * 2 <= lhs)
    {
        regs[GTEREGPOS::FLAGS] |= 1 << 17;
        divResult = 0x1FFFF;
        return;
    }

    byte shift = 0;
    for (int i = 0; i < 16; i++)
    {
        if ((rhs >> i) == 0)
        {
            shift = 16 - i;
            break;
        }
    }
    lhs <<= shift;
    rhs <<= shift;

    byte unr_table[257] = {
      0xFF, 0xFD, 0xFB, 0xF9, 0xF7, 0xF5, 0xF3, 0xF1, 0xEF, 0xEE, 0xEC, 0xEA, 0xE8, 0xE6, 0xE4, 0xE3, //
      0xE1, 0xDF, 0xDD, 0xDC, 0xDA, 0xD8, 0xD6, 0xD5, 0xD3, 0xD1, 0xD0, 0xCE, 0xCD, 0xCB, 0xC9, 0xC8, //  00h..3Fh
      0xC6, 0xC5, 0xC3, 0xC1, 0xC0, 0xBE, 0xBD, 0xBB, 0xBA, 0xB8, 0xB7, 0xB5, 0xB4, 0xB2, 0xB1, 0xB0, //
      0xAE, 0xAD, 0xAB, 0xAA, 0xA9, 0xA7, 0xA6, 0xA4, 0xA3, 0xA2, 0xA0, 0x9F, 0x9E, 0x9C, 0x9B, 0x9A, //
      0x99, 0x97, 0x96, 0x95, 0x94, 0x92, 0x91, 0x90, 0x8F, 0x8D, 0x8C, 0x8B, 0x8A, 0x89, 0x87, 0x86, //
      0x85, 0x84, 0x83, 0x82, 0x81, 0x7F, 0x7E, 0x7D, 0x7C, 0x7B, 0x7A, 0x79, 0x78, 0x77, 0x75, 0x74, //  40h..7Fh
      0x73, 0x72, 0x71, 0x70, 0x6F, 0x6E, 0x6D, 0x6C, 0x6B, 0x6A, 0x69, 0x68, 0x67, 0x66, 0x65, 0x64, //
      0x63, 0x62, 0x61, 0x60, 0x5F, 0x5E, 0x5D, 0x5D, 0x5C, 0x5B, 0x5A, 0x59, 0x58, 0x57, 0x56, 0x55, //
      0x54, 0x53, 0x53, 0x52, 0x51, 0x50, 0x4F, 0x4E, 0x4D, 0x4D, 0x4C, 0x4B, 0x4A, 0x49, 0x48, 0x48, //
      0x47, 0x46, 0x45, 0x44, 0x43, 0x43, 0x42, 0x41, 0x40, 0x3F, 0x3F, 0x3E, 0x3D, 0x3C, 0x3C, 0x3B, //  80h..BFh
      0x3A, 0x39, 0x39, 0x38, 0x37, 0x36, 0x36, 0x35, 0x34, 0x33, 0x33, 0x32, 0x31, 0x31, 0x30, 0x2F, //
      0x2E, 0x2E, 0x2D, 0x2C, 0x2C, 0x2B, 0x2A, 0x2A, 0x29, 0x28, 0x28, 0x27, 0x26, 0x26, 0x25, 0x24, //
      0x24, 0x23, 0x22, 0x22, 0x21, 0x20, 0x20, 0x1F, 0x1E, 0x1E, 0x1D, 0x1D, 0x1C, 0x1B, 0x1B, 0x1A, //
      0x19, 0x19, 0x18, 0x18, 0x17, 0x16, 0x16, 0x15, 0x15, 0x14, 0x14, 0x13, 0x12, 0x12, 0x11, 0x11, //  C0h..FFh
      0x10, 0x0F, 0x0F, 0x0E, 0x0E, 0x0D, 0x0D, 0x0C, 0x0C, 0x0B, 0x0A, 0x0A, 0x09, 0x09, 0x08, 0x08, //
      0x07, 0x07, 0x06, 0x06, 0x05, 0x05, 0x04, 0x04, 0x03, 0x03, 0x02, 0x02, 0x01, 0x01, 0x00, 0x00, //
      0x00 // <-- one extra table entry (for "(d-7FC0h)/80h"=100h)
    };

    Int32 divisor = rhs | 0x8000;
    Int32 x = 0x101 + (Int32)unr_table[((divisor & 0x7FFF) + 0x40) >> 7];
    Int32 d = (Int32)(((((divisor)) * -x) + 0x80) >> 8);
    UInt32 recip = (((x * (0x20000 + d)) + 0x80) >> 8);

    divResult = (((UInt64)lhs) * ((UInt64)recip) + ((UInt64)0x8000)) >> 16;

    if (divResult > 0x1FFFF) divResult = 0x1FFFF;
}

void GTE::MacOP0(Int32 mul1, Int32 mul2, Int64 add, bool sub, bool swap, bool useIR, bool IRshift, bool checkOverflow)
{
    mac0Last = mac0Result;

    mac0Result = (Int64)mul1 * (Int64)mul2;
    if (swap && sub) mac0Result = mac0Result - add;
    if (sub) mac0Result = (Int64)add - mac0Result;
    else mac0Result = (Int64)add + mac0Result;

    if (checkOverflow) MAC0OverflowCheck(mac0Result);

    regs[GTEREGPOS::MAC0] = (UInt32)mac0Result;

    // ?
    //if ((mac0Result >> 31 & 1) == 0)
    //    mac0Result = mac0Result & 0x7FFFFFFF;
    //else
    //    mac0Result = mac0Result |= 0xFFFFFFFF80000000;

    if (useIR)
    {
        if (IRshift) mac0Result >>= 12;
        IR0OverflowSet(mac0Result);
    }
}

void GTE::MacOP123(byte index, Int32 mul1, Int32 mul2, Int64 add, bool sub, bool swap, bool saveShifted, bool useIR, bool IRshift, bool IRshiftFlag, bool satIR, bool satIRFlag)
{
    mac123Last[index - 1] = mac123Result[index - 1];

    Int64 result = (Int64)mul1 * (Int64)mul2;
    if (swap && sub) result = result - add;
    else if (sub) result = (Int64)add - result;
    else result = (Int64)add + result;

    MAC123OverflowCheck(result, index);

    if (saveShifted) regs[GTEREGPOS::MAC0 + index] = (UInt32)(result >> 12);
    else regs[GTEREGPOS::MAC0 + index] = (UInt32)result;

    if ((result >> 43 & 1) == 0) 
        mac123Result[index - 1] = result & 0xFFFFFFFFFFF;
    else 
        mac123Result[index - 1] = result |= 0xFFFFF00000000000;

    mac123Shifted[index - 1] = (UInt32)(result >> 12);

    if (useIR)
    {
        if (IRshiftFlag) IR123OverflowCheck(result >> 12, satIRFlag, index);
        else IR123OverflowCheck(result, satIRFlag, index);

        if (IRshift) IR123OverflowSet(result >> 12, satIR, index);
        else IR123OverflowSet(result, satIR, index);
    }
}

void GTE::PushRGBFromMAC()
{
    byte r = TruncateColor((Int32)regs[GTEREGPOS::MAC1] >> 4, 0);
    byte g = TruncateColor((Int32)regs[GTEREGPOS::MAC2] >> 4, 1);
    byte b = TruncateColor((Int32)regs[GTEREGPOS::MAC3] >> 4, 2);
    byte c = regs[GTEREGPOS::RGBC] >> 24;

    regs[GTEREGPOS::RGB0] = regs[GTEREGPOS::RGB1];
    regs[GTEREGPOS::RGB1] = regs[GTEREGPOS::RGB2];
    regs[GTEREGPOS::RGB2] = r | (g << 8) | (b << 16) | (c << 24);
}

// helpers

void GTE::MAC0OverflowCheck(Int64 value)
{
    Int64 min = 0xffffffff80000000;
    Int64 max = 0x000000007fffffff;
    if (value < min)
    {
        regs[GTEREGPOS::FLAGS] |= 1 << 15;
    }
    else if (value > max)
    {
        regs[GTEREGPOS::FLAGS] |= 1 << 16;
    }
}

void GTE::MAC123OverflowCheck(Int64 value, byte index)
{
    Int64 min = 0xfffff80000000000;
    Int64 max = 0x000007ffffffffff;
    if (value < min)
    {
        switch (index)
        {
        case 1:  regs[GTEREGPOS::FLAGS] |= 1 << 27; break;
        case 2:  regs[GTEREGPOS::FLAGS] |= 1 << 26; break;
        case 3:  regs[GTEREGPOS::FLAGS] |= 1 << 25; break;
        }
    }
    else if (value > max)
    {
        switch (index)
        {
        case 1:  regs[GTEREGPOS::FLAGS] |= 1 << 30; break;
        case 2:  regs[GTEREGPOS::FLAGS] |= 1 << 29; break;
        case 3:  regs[GTEREGPOS::FLAGS] |= 1 << 28; break;
        }
    }
}

void GTE::IR0OverflowSet(Int32 value)
{
    Int32 min = 0x0000;
    Int32 max = 0x1000;
    if (value < min)
    {
        regs[GTEREGPOS::FLAGS] |= 1 << 12;
        value = min;
    }
    else if (value > max)
    {
        regs[GTEREGPOS::FLAGS] |= 1 << 12;
        value = max;
    }

    regs[GTEREGPOS::IR0] = value;
}

void GTE::IR123OverflowSet(Int32 value, bool satIR, byte index)
{
    Int32 min = 0xFFFF8000;
    Int32 max = 0x7FFF;
    if (satIR) min = 0;
    if (value < min)
    {
        value = min;
    }
    else if (value > max)
    {
        value = max;
    }

    regs[GTEREGPOS::IR0 + index] = value;
}

void GTE::IR123OverflowCheck(Int32 value, bool satIR, byte index)
{
    Int32 min = 0xFFFF8000;
    Int32 max = 0x7FFF;
    if (satIR) min = 0;
    if (value < min)
    {
        switch (index)
        {
        case 1:  regs[GTEREGPOS::FLAGS] |= 1 << 24; break;
        case 2:  regs[GTEREGPOS::FLAGS] |= 1 << 23; break;
        case 3:  regs[GTEREGPOS::FLAGS] |= 1 << 22; break;
        }
    }
    else if (value > max)
    {
        switch (index)
        {
        case 1:  regs[GTEREGPOS::FLAGS] |= 1 << 24; break;
        case 2:  regs[GTEREGPOS::FLAGS] |= 1 << 23; break;
        case 3:  regs[GTEREGPOS::FLAGS] |= 1 << 22; break;
        }
    }
}

void GTE::SetOTZ(Int32 value)
{
    if (value < 0)      
    {
        regs[GTEREGPOS::FLAGS] |= 1 << 18;
        value = 0;
    } 
    else if (value > 0xFFFF)
    {
        regs[GTEREGPOS::FLAGS] |= 1 << 18;
        value = 0xFFFF;
    }
    regs[GTEREGPOS::OTZ] = value;
}

byte GTE::TruncateColor(Int32 value, byte index)
{
    byte newvalue = value & 0xFF;
    if (value < 0 || value > 0xFF)
    {
        switch (index)
        {
        case 0: regs[GTEREGPOS::FLAGS] |= 1 << 21; break;
        case 1: regs[GTEREGPOS::FLAGS] |= 1 << 20; break;
        case 2: regs[GTEREGPOS::FLAGS] |= 1 << 19; break;
        }
        if (value < 0) newvalue = 0;
        else newvalue = 0xFF;
    }
    return newvalue;
}

// commands

void GTE::RTPS(byte index, bool satIR, bool shift, bool last)
{
    Int16 vector[3] = { vectors[index][0], vectors[index][1], vectors[index][2] };

    for (int i = 0; i < 3; i++)
    {
        MacOP123(i + 1, rotMatrix[i][0], vector[0], ((Int64)(Int32)regs[GTEREGPOS::TR0 + i]) << 12, false, false, false, false, false, false, false, false);
        MacOP123(i + 1, rotMatrix[i][1], vector[1], mac123Result[i]                               , false, false, false, false, false, false, false, false);
        if (i == 2)
            MacOP123(i + 1, rotMatrix[i][2], vector[2], mac123Result[i], false, false, shift, true, shift, true, satIR, false);
        else
            MacOP123(i + 1, rotMatrix[i][2], vector[2], mac123Result[i], false, false, shift, true, shift, shift, satIR, satIR);
    }

    // calculate SZ3
    pushSZ(mac123Shifted[2]);

    // div result
    UNRDivide(regs[GTEREGPOS::H] & 0xFFFF, regs[GTEREGPOS::SZ3]);

    Int16 ir1 = (Int16)regs[GTEREGPOS::IR1];
    //ir1 = ir1 * 3 / 4; // widescreen
    //ir1 = ir1 * 14 / 16; // widescreen

    MacOP0(ir1, divResult, (Int32)regs[GTEREGPOS::OFX], false, false, false, false, true);
    MacOP0((Int16)regs[GTEREGPOS::IR2], divResult, (Int32)regs[GTEREGPOS::OFY], false, false, false, false, true);
    pushSXY(mac0Last >> 16, mac0Result >> 16);

    if (last) 
    {
        MacOP0((Int16)regs[GTEREGPOS::DQA], divResult, (Int32)regs[GTEREGPOS::DQB], false, false, true, true, true);
    }
}

void GTE::NCLIP() // 0x06
{
    MacOP0((Int16)(regs[GTEREGPOS::SXY0] & 0xFFFF), (Int16)(regs[GTEREGPOS::SXY1] >> 16),          0, false, false, false, false, false);
    MacOP0((Int16)(regs[GTEREGPOS::SXY1] & 0xFFFF), (Int16)(regs[GTEREGPOS::SXY2] >> 16), mac0Result, false, false, false, false, false);
    MacOP0((Int16)(regs[GTEREGPOS::SXY2] & 0xFFFF), (Int16)(regs[GTEREGPOS::SXY0] >> 16), mac0Result, false, false, false, false, false);
    MacOP0((Int16)(regs[GTEREGPOS::SXY0] & 0xFFFF), (Int16)(regs[GTEREGPOS::SXY2] >> 16), mac0Result, true, false, false, false, false);
    MacOP0((Int16)(regs[GTEREGPOS::SXY1] & 0xFFFF), (Int16)(regs[GTEREGPOS::SXY0] >> 16), mac0Result, true, false, false, false, false);
    MacOP0((Int16)(regs[GTEREGPOS::SXY2] & 0xFFFF), (Int16)(regs[GTEREGPOS::SXY1] >> 16), mac0Result, true, false, false, false, true);
}

void GTE::OP(bool satIR, bool shift) //  0x0C
{
    Int32 ir1 = (Int32)regs[GTEREGPOS::IR1];
    Int32 ir2 = (Int32)regs[GTEREGPOS::IR2];
    Int32 ir3 = (Int32)regs[GTEREGPOS::IR3];

    MacOP123(1, ir3, rotMatrix[1][1],               0, false, false, false, false, false, false, false, false);
    MacOP123(1, ir2, rotMatrix[2][2], mac123Result[0], true, false, shift, true, shift, shift, satIR, satIR);
    MacOP123(2, ir1, rotMatrix[2][2],               0, false, false, false, false, false, false, false, false);
    MacOP123(2, ir3, rotMatrix[0][0], mac123Result[1], true, false, shift, true, shift, shift, satIR, satIR);
    MacOP123(3, ir2, rotMatrix[0][0],               0, false, false, false, false, false, false, false, false);
    MacOP123(3, ir1, rotMatrix[1][1], mac123Result[2], true, false, shift, true, shift, shift, satIR, satIR);
}

void GTE::DPCS(UInt32 color, bool satIR, bool shift) // 0x10
{
    MacOP123(1, (color & 0xFF)      , 0x10000, 0, false, false, false, false, false, false, false, false);
    MacOP123(2, (color >> 8) & 0xFF , 0x10000, 0, false, false, false, false, false, false, false, false);
    MacOP123(3, (color >> 16) & 0xFF, 0x10000, 0, false, false, false, false, false, false, false, false);

    //InterpolateColor
    MacOP123(1, (Int32)regs[GTEREGPOS::FC0], 0x1000, mac123Result[0], true, true, shift, true, shift, shift, false, false);
    MacOP123(2, (Int32)regs[GTEREGPOS::FC1], 0x1000, mac123Result[1], true, true, shift, true, shift, shift, false, false);
    MacOP123(3, (Int32)regs[GTEREGPOS::FC2], 0x1000, mac123Result[2], true, true, shift, true, shift, shift, false, false);

    MacOP123(1, (Int32)(Int16)regs[GTEREGPOS::IR1], (Int32)(Int16)regs[GTEREGPOS::IR0], mac123Last[0], false, false, shift, true, shift, shift, satIR, satIR);
    MacOP123(2, (Int32)(Int16)regs[GTEREGPOS::IR2], (Int32)(Int16)regs[GTEREGPOS::IR0], mac123Last[1], false, false, shift, true, shift, shift, satIR, satIR);
    MacOP123(3, (Int32)(Int16)regs[GTEREGPOS::IR3], (Int32)(Int16)regs[GTEREGPOS::IR0], mac123Last[2], false, false, shift, true, shift, shift, satIR, satIR);

    PushRGBFromMAC();
}

void GTE::INTPL(bool satIR, bool shift) // 0x11
{
    MacOP123(1, (Int32)(Int16)regs[GTEREGPOS::IR1], 0x1000, 0, false, false, false, false, false, false, false, false);
    MacOP123(2, (Int32)(Int16)regs[GTEREGPOS::IR2], 0x1000, 0, false, false, false, false, false, false, false, false);
    MacOP123(3, (Int32)(Int16)regs[GTEREGPOS::IR3], 0x1000, 0, false, false, false, false, false, false, false, false);

    //InterpolateColor
    MacOP123(1, (Int32)regs[GTEREGPOS::FC0], 0x1000, mac123Result[0], true, true, shift, true, shift, shift, false, false);
    MacOP123(2, (Int32)regs[GTEREGPOS::FC1], 0x1000, mac123Result[1], true, true, shift, true, shift, shift, false, false);
    MacOP123(3, (Int32)regs[GTEREGPOS::FC2], 0x1000, mac123Result[2], true, true, shift, true, shift, shift, false, false);

    MacOP123(1, (Int32)(Int16)regs[GTEREGPOS::IR1], (Int32)(Int16)regs[GTEREGPOS::IR0], mac123Last[0], false, false, shift, true, shift, shift, satIR, satIR);
    MacOP123(2, (Int32)(Int16)regs[GTEREGPOS::IR2], (Int32)(Int16)regs[GTEREGPOS::IR0], mac123Last[1], false, false, shift, true, shift, shift, satIR, satIR);
    MacOP123(3, (Int32)(Int16)regs[GTEREGPOS::IR3], (Int32)(Int16)regs[GTEREGPOS::IR0], mac123Last[2], false, false, shift, true, shift, shift, satIR, satIR);

    PushRGBFromMAC();
}

void GTE::MVMVA(UInt32 instruction) // 0x12
{
    Int16 matrix[3][3];

    if (((instruction >> 17) & 3) == 3)
    {
        matrix[0][0] = 0 - ((regs[GTEREGPOS::RGBC] & 0xFF) << 4);
        matrix[0][1] = (regs[GTEREGPOS::RGBC] & 0xFF) << 4;
        matrix[0][2] = regs[GTEREGPOS::IR0] & 0xFFFF;
        matrix[1][0] = regs[GTEREGPOS::RT1] & 0xFFFF;
        matrix[1][1] = regs[GTEREGPOS::RT1] & 0xFFFF;
        matrix[1][2] = regs[GTEREGPOS::RT1] & 0xFFFF;
        matrix[2][0] = regs[GTEREGPOS::RT2] & 0xFFFF;
        matrix[2][1] = regs[GTEREGPOS::RT2] & 0xFFFF;
        matrix[2][2] = regs[GTEREGPOS::RT2] & 0xFFFF;
    }
    else
    {
        for (int x = 0; x < 3; x++)
        {
            for (int y = 0; y < 3; y++)
            {
                switch ((instruction >> 17) & 3)
                {
                case 0: matrix[x][y] = rotMatrix[x][y]; break;
                case 1: matrix[x][y] = lightMatrix[x][y]; break;
                case 2: matrix[x][y] = colorMatrix[x][y]; break;
                }
            }
        }
    }
    

    Int16 vector[3];
    if (((instruction >> 15) & 3) == 3)
    {
        vector[0] = regs[GTEREGPOS::IR1] & 0xFFFF;
        vector[1] = regs[GTEREGPOS::IR2] & 0xFFFF;
        vector[2] = regs[GTEREGPOS::IR3] & 0xFFFF;
    }
    else
    {
        byte pos = ((instruction >> 15) & 3);
        for (int i = 0; i < 3; i++) vector[i] = vectors[pos][i];
    }

    Int32 translate[3];
    switch ((instruction >> 13) & 3)
    {
    case 0:
        translate[0] = regs[GTEREGPOS::TR0];
        translate[1] = regs[GTEREGPOS::TR1];
        translate[2] = regs[GTEREGPOS::TR2];
        break;

    case 1:
        translate[0] = regs[GTEREGPOS::BK0];
        translate[1] = regs[GTEREGPOS::BK1];
        translate[2] = regs[GTEREGPOS::BK2];
        break;

    case 2:
        translate[0] = regs[GTEREGPOS::FC0];
        translate[1] = regs[GTEREGPOS::FC1];
        translate[2] = regs[GTEREGPOS::FC2];
        break;

    default:
        translate[0] = 0;
        translate[1] = 0;
        translate[2] = 0;
        break;
    }

    bool shift = (instruction >> 19) & 1;
    bool satIR = (instruction >> 10) & 1;

    if (((instruction >> 13) & 3) == 2) // buggy
    {
        for (int i = 0; i < 3; i++)
        {
            MacOP123(i + 1, translate[i], 0x1000, 0, false, false, false, false, false, false, false, false);
            MacOP123(i + 1, matrix[i][0], vector[0], mac123Result[i], false, false, false, true, shift, shift, false, false);
            MacOP123(i + 1, matrix[i][1], vector[1], 0, false, false, false, false, false, false, false, false);
            MacOP123(i + 1, matrix[i][2], vector[2], mac123Result[i], false, false, shift, true, shift, shift, satIR, satIR);
        }
    }
    else
    {
        for (int i = 0; i < 3; i++)
        {
            MacOP123(i + 1, translate[i], 0x1000, 0, false, false, false, false, false, false, false, false);
            MacOP123(i + 1, matrix[i][0], vector[0], mac123Result[i], false, false, false, false, false, false, false, false);
            MacOP123(i + 1, matrix[i][1], vector[1], mac123Result[i], false, false, false, false, false, false, false, false);
            MacOP123(i + 1, matrix[i][2], vector[2], mac123Result[i], false, false, shift, true, shift, shift, satIR, satIR);
        }
    }


}

void GTE::NCDS(byte index, bool satIR, bool shift)
{
    Int16 vector[3] = { vectors[index][0], vectors[index][1], vectors[index][2] };

    // mat mul with light matrix
    for (int i = 0; i < 3; i++)
    {
        MacOP123(i + 1, lightMatrix[i][0], vector[0], 0, false, false, false, false, false, false, false, false);
        MacOP123(i + 1, lightMatrix[i][1], vector[1], mac123Result[i], false, false, false, false, false, false, false, false);
        MacOP123(i + 1, lightMatrix[i][2], vector[2], mac123Result[i], false, false, shift, true, shift, shift, satIR, satIR);
    }

    Int32 ir1 = (Int32)regs[GTEREGPOS::IR1];
    Int32 ir2 = (Int32)regs[GTEREGPOS::IR2];
    Int32 ir3 = (Int32)regs[GTEREGPOS::IR3];

    // mat mul with color matrix
    for (int i = 0; i < 3; i++)
    {
        MacOP123(i + 1, (Int32)regs[GTEREGPOS::BK0 + i], 0x1000, 0, false, false, false, false, false, false, false, false);
        MacOP123(i + 1, colorMatrix[i][0], ir1, mac123Result[i], false, false, false, false, false, false, false, false);
        MacOP123(i + 1, colorMatrix[i][1], ir2, mac123Result[i], false, false, false, false, false, false, false, false);
        MacOP123(i + 1, colorMatrix[i][2], ir3, mac123Result[i], false, false, shift, true, shift, shift, satIR, satIR);
    }

    // prepare IR for InterpolateColor
    MacOP123(1, (Int32)(Int16)regs[GTEREGPOS::IR1], (regs[GTEREGPOS::RGBC] & 0xFF)      , 0, false, false, false, false, false, false, false, false);
    MacOP123(2, (Int32)(Int16)regs[GTEREGPOS::IR2], (regs[GTEREGPOS::RGBC] >> 8) & 0xFF , 0, false, false, false, false, false, false, false, false);
    MacOP123(3, (Int32)(Int16)regs[GTEREGPOS::IR3], (regs[GTEREGPOS::RGBC] >> 16) & 0xFF, 0, false, false, false, false, false, false, false, false);

    MacOP123(1, mac123Result[0], 0x10, 0, false, false, false, false, false, false, false, false);
    MacOP123(2, mac123Result[1], 0x10, 0, false, false, false, false, false, false, false, false);
    MacOP123(3, mac123Result[2], 0x10, 0, false, false, false, false, false, false, false, false);

    //InterpolateColor
    MacOP123(1, (Int32)regs[GTEREGPOS::FC0], 0x1000, mac123Result[0], true, true, shift, true, shift, shift, false, false);
    MacOP123(2, (Int32)regs[GTEREGPOS::FC1], 0x1000, mac123Result[1], true, true, shift, true, shift, shift, false, false);
    MacOP123(3, (Int32)regs[GTEREGPOS::FC2], 0x1000, mac123Result[2], true, true, shift, true, shift, shift, false, false);

    MacOP123(1, (Int32)(Int16)regs[GTEREGPOS::IR1], (Int32)(Int16)regs[GTEREGPOS::IR0], mac123Last[0], false, false, shift, true, shift, shift, satIR, satIR);
    MacOP123(2, (Int32)(Int16)regs[GTEREGPOS::IR2], (Int32)(Int16)regs[GTEREGPOS::IR0], mac123Last[1], false, false, shift, true, shift, shift, satIR, satIR);
    MacOP123(3, (Int32)(Int16)regs[GTEREGPOS::IR3], (Int32)(Int16)regs[GTEREGPOS::IR0], mac123Last[2], false, false, shift, true, shift, shift, satIR, satIR);

    PushRGBFromMAC();
}

void GTE::CDP(bool satIR, bool shift)
{
    Int32 ir1 = (Int32)regs[GTEREGPOS::IR1];
    Int32 ir2 = (Int32)regs[GTEREGPOS::IR2];
    Int32 ir3 = (Int32)regs[GTEREGPOS::IR3];

    // mat mul with color matrix
    for (int i = 0; i < 3; i++)
    {
        MacOP123(i + 1, (Int32)regs[GTEREGPOS::BK0 + i], 0x1000, 0, false, false, false, false, false, false, false, false);
        MacOP123(i + 1, colorMatrix[i][0], ir1, mac123Result[i], false, false, false, false, false, false, false, false);
        MacOP123(i + 1, colorMatrix[i][1], ir2, mac123Result[i], false, false, false, false, false, false, false, false);
        MacOP123(i + 1, colorMatrix[i][2], ir3, mac123Result[i], false, false, shift, true, shift, shift, satIR, satIR);
    }

    // prepare IR for InterpolateColor
    MacOP123(1, (Int32)(Int16)regs[GTEREGPOS::IR1], (regs[GTEREGPOS::RGBC] & 0xFF), 0, false, false, false, false, false, false, false, false);
    MacOP123(2, (Int32)(Int16)regs[GTEREGPOS::IR2], (regs[GTEREGPOS::RGBC] >> 8) & 0xFF, 0, false, false, false, false, false, false, false, false);
    MacOP123(3, (Int32)(Int16)regs[GTEREGPOS::IR3], (regs[GTEREGPOS::RGBC] >> 16) & 0xFF, 0, false, false, false, false, false, false, false, false);

    MacOP123(1, mac123Result[0], 0x10, 0, false, false, false, false, false, false, false, false);
    MacOP123(2, mac123Result[1], 0x10, 0, false, false, false, false, false, false, false, false);
    MacOP123(3, mac123Result[2], 0x10, 0, false, false, false, false, false, false, false, false);

    //InterpolateColor
    MacOP123(1, (Int32)regs[GTEREGPOS::FC0], 0x1000, mac123Result[0], true, true, shift, true, shift, shift, false, false);
    MacOP123(2, (Int32)regs[GTEREGPOS::FC1], 0x1000, mac123Result[1], true, true, shift, true, shift, shift, false, false);
    MacOP123(3, (Int32)regs[GTEREGPOS::FC2], 0x1000, mac123Result[2], true, true, shift, true, shift, shift, false, false);

    MacOP123(1, (Int32)(Int16)regs[GTEREGPOS::IR1], (Int32)(Int16)regs[GTEREGPOS::IR0], mac123Last[0], false, false, shift, true, shift, shift, satIR, satIR);
    MacOP123(2, (Int32)(Int16)regs[GTEREGPOS::IR2], (Int32)(Int16)regs[GTEREGPOS::IR0], mac123Last[1], false, false, shift, true, shift, shift, satIR, satIR);
    MacOP123(3, (Int32)(Int16)regs[GTEREGPOS::IR3], (Int32)(Int16)regs[GTEREGPOS::IR0], mac123Last[2], false, false, shift, true, shift, shift, satIR, satIR);

    PushRGBFromMAC();
}

void GTE::NCCS(byte index, bool satIR, bool shift)
{
    Int16 vector[3] = { vectors[index][0], vectors[index][1], vectors[index][2] };

    // mat mul with light matrix
    for (int i = 0; i < 3; i++)
    {
        MacOP123(i + 1, lightMatrix[i][0], vector[0], 0, false, false, false, false, false, false, false, false);
        MacOP123(i + 1, lightMatrix[i][1], vector[1], mac123Result[i], false, false, false, false, false, false, false, false);
        MacOP123(i + 1, lightMatrix[i][2], vector[2], mac123Result[i], false, false, shift, true, shift, shift, satIR, satIR);
    }

    Int32 ir1 = (Int32)regs[GTEREGPOS::IR1];
    Int32 ir2 = (Int32)regs[GTEREGPOS::IR2];
    Int32 ir3 = (Int32)regs[GTEREGPOS::IR3];

    // mat mul with color matrix
    for (int i = 0; i < 3; i++)
    {
        MacOP123(i + 1, (Int32)regs[GTEREGPOS::BK0 + i], 0x1000, 0, false, false, false, false, false, false, false, false);
        MacOP123(i + 1, colorMatrix[i][0], ir1, mac123Result[i], false, false, false, false, false, false, false, false);
        MacOP123(i + 1, colorMatrix[i][1], ir2, mac123Result[i], false, false, false, false, false, false, false, false);
        MacOP123(i + 1, colorMatrix[i][2], ir3, mac123Result[i], false, false, shift, true, shift, shift, satIR, satIR);
    }

    // prepare IR
    MacOP123(1, (Int32)(Int16)regs[GTEREGPOS::IR1], (regs[GTEREGPOS::RGBC] & 0xFF), 0, false, false, false, false, false, false, false, false);
    MacOP123(2, (Int32)(Int16)regs[GTEREGPOS::IR2], (regs[GTEREGPOS::RGBC] >> 8) & 0xFF, 0, false, false, false, false, false, false, false, false);
    MacOP123(3, (Int32)(Int16)regs[GTEREGPOS::IR3], (regs[GTEREGPOS::RGBC] >> 16) & 0xFF, 0, false, false, false, false, false, false, false, false);

    MacOP123(1, mac123Result[0], 0x10, 0, false, false, shift, true, shift, shift, satIR, satIR);
    MacOP123(2, mac123Result[1], 0x10, 0, false, false, shift, true, shift, shift, satIR, satIR);
    MacOP123(3, mac123Result[2], 0x10, 0, false, false, shift, true, shift, shift, satIR, satIR);

    PushRGBFromMAC();
}

void GTE::CC(bool satIR, bool shift)
{
    Int32 ir1 = (Int32)regs[GTEREGPOS::IR1];
    Int32 ir2 = (Int32)regs[GTEREGPOS::IR2];
    Int32 ir3 = (Int32)regs[GTEREGPOS::IR3];

    // mat mul with color matrix
    for (int i = 0; i < 3; i++)
    {
        MacOP123(i + 1, (Int32)regs[GTEREGPOS::BK0 + i], 0x1000, 0, false, false, false, false, false, false, false, false);
        MacOP123(i + 1, colorMatrix[i][0], ir1, mac123Result[i], false, false, false, false, false, false, false, false);
        MacOP123(i + 1, colorMatrix[i][1], ir2, mac123Result[i], false, false, false, false, false, false, false, false);
        MacOP123(i + 1, colorMatrix[i][2], ir3, mac123Result[i], false, false, shift, true, shift, shift, satIR, satIR);
    }

    // prepare IR
    MacOP123(1, (Int32)(Int16)regs[GTEREGPOS::IR1], (regs[GTEREGPOS::RGBC] & 0xFF), 0, false, false, false, false, false, false, false, false);
    MacOP123(2, (Int32)(Int16)regs[GTEREGPOS::IR2], (regs[GTEREGPOS::RGBC] >> 8) & 0xFF, 0, false, false, false, false, false, false, false, false);
    MacOP123(3, (Int32)(Int16)regs[GTEREGPOS::IR3], (regs[GTEREGPOS::RGBC] >> 16) & 0xFF, 0, false, false, false, false, false, false, false, false);

    MacOP123(1, mac123Result[0], 0x10, 0, false, false, shift, true, shift, shift, satIR, satIR);
    MacOP123(2, mac123Result[1], 0x10, 0, false, false, shift, true, shift, shift, satIR, satIR);
    MacOP123(3, mac123Result[2], 0x10, 0, false, false, shift, true, shift, shift, satIR, satIR);

    PushRGBFromMAC();
}

void GTE::NCS(byte index, bool satIR, bool shift)
{
    Int16 vector[3] = { vectors[index][0], vectors[index][1], vectors[index][2] };

    // mat mul with light matrix
    for (int i = 0; i < 3; i++)
    {
        MacOP123(i + 1, lightMatrix[i][0], vector[0], 0, false, false, false, false, false, false, false, false);
        MacOP123(i + 1, lightMatrix[i][1], vector[1], mac123Result[i], false, false, false, false, false, false, false, false);
        MacOP123(i + 1, lightMatrix[i][2], vector[2], mac123Result[i], false, false, shift, true, shift, shift, satIR, satIR);
    }

    Int32 ir1 = (Int32)regs[GTEREGPOS::IR1];
    Int32 ir2 = (Int32)regs[GTEREGPOS::IR2];
    Int32 ir3 = (Int32)regs[GTEREGPOS::IR3];

    // mat mul with color matrix
    for (int i = 0; i < 3; i++)
    {
        MacOP123(i + 1, (Int32)regs[GTEREGPOS::BK0 + i], 0x1000, 0, false, false, false, false, false, false, false, false);
        MacOP123(i + 1, colorMatrix[i][0], ir1, mac123Result[i], false, false, false, false, false, false, false, false);
        MacOP123(i + 1, colorMatrix[i][1], ir2, mac123Result[i], false, false, false, false, false, false, false, false);
        MacOP123(i + 1, colorMatrix[i][2], ir3, mac123Result[i], false, false, shift, true, shift, shift, satIR, satIR);
    }

    PushRGBFromMAC();
}

void GTE::SQR(bool satIR, bool shift)
{
    MacOP123(1, (Int32)regs[GTEREGPOS::IR1], (Int32)regs[GTEREGPOS::IR1], 0, false, false, shift, true, shift, shift, satIR, satIR);
    MacOP123(2, (Int32)regs[GTEREGPOS::IR2], (Int32)regs[GTEREGPOS::IR2], 0, false, false, shift, true, shift, shift, satIR, satIR);
    MacOP123(3, (Int32)regs[GTEREGPOS::IR3], (Int32)regs[GTEREGPOS::IR3], 0, false, false, shift, true, shift, shift, satIR, satIR);
}

void GTE::DPCL(bool satIR, bool shift)
{
    // prepare IR for InterpolateColor
    MacOP123(1, (Int32)(Int16)regs[GTEREGPOS::IR1], (regs[GTEREGPOS::RGBC] & 0xFF), 0, false, false, false, false, false, false, false, false);
    MacOP123(2, (Int32)(Int16)regs[GTEREGPOS::IR2], (regs[GTEREGPOS::RGBC] >> 8) & 0xFF, 0, false, false, false, false, false, false, false, false);
    MacOP123(3, (Int32)(Int16)regs[GTEREGPOS::IR3], (regs[GTEREGPOS::RGBC] >> 16) & 0xFF, 0, false, false, false, false, false, false, false, false);

    MacOP123(1, mac123Result[0], 0x10, 0, false, false, false, false, false, false, false, false);
    MacOP123(2, mac123Result[1], 0x10, 0, false, false, false, false, false, false, false, false);
    MacOP123(3, mac123Result[2], 0x10, 0, false, false, false, false, false, false, false, false);

    //InterpolateColor
    MacOP123(1, (Int32)regs[GTEREGPOS::FC0], 0x1000, mac123Result[0], true, true, shift, true, shift, shift, false, false);
    MacOP123(2, (Int32)regs[GTEREGPOS::FC1], 0x1000, mac123Result[1], true, true, shift, true, shift, shift, false, false);
    MacOP123(3, (Int32)regs[GTEREGPOS::FC2], 0x1000, mac123Result[2], true, true, shift, true, shift, shift, false, false);

    MacOP123(1, (Int32)(Int16)regs[GTEREGPOS::IR1], (Int32)(Int16)regs[GTEREGPOS::IR0], mac123Last[0], false, false, shift, true, shift, shift, satIR, satIR);
    MacOP123(2, (Int32)(Int16)regs[GTEREGPOS::IR2], (Int32)(Int16)regs[GTEREGPOS::IR0], mac123Last[1], false, false, shift, true, shift, shift, satIR, satIR);
    MacOP123(3, (Int32)(Int16)regs[GTEREGPOS::IR3], (Int32)(Int16)regs[GTEREGPOS::IR0], mac123Last[2], false, false, shift, true, shift, shift, satIR, satIR);

    PushRGBFromMAC();
}

void GTE::AVSZ3() // 0x2D
{
    MacOP0(regs[GTEREGPOS::SZ1] & 0xFFFF, (Int16)regs[GTEREGPOS::ZSF3],          0, false, false, false, false, true);
    MacOP0(regs[GTEREGPOS::SZ2] & 0xFFFF, (Int16)regs[GTEREGPOS::ZSF3], mac0Result, false, false, false, false, true);
    MacOP0(regs[GTEREGPOS::SZ3] & 0xFFFF, (Int16)regs[GTEREGPOS::ZSF3], mac0Result, false, false, false, false, true);
    SetOTZ(mac0Result >> 12);
}

void GTE::AVSZ4() // 0x2E
{
    MacOP0(regs[GTEREGPOS::SZ0] & 0xFFFF, (Int16)regs[GTEREGPOS::ZSF4], 0, false, false, false, false, true);
    MacOP0(regs[GTEREGPOS::SZ1] & 0xFFFF, (Int16)regs[GTEREGPOS::ZSF4], mac0Result, false, false, false, false, true);
    MacOP0(regs[GTEREGPOS::SZ2] & 0xFFFF, (Int16)regs[GTEREGPOS::ZSF4], mac0Result, false, false, false, false, true);
    MacOP0(regs[GTEREGPOS::SZ3] & 0xFFFF, (Int16)regs[GTEREGPOS::ZSF4], mac0Result, false, false, false, false, true);
    SetOTZ(mac0Result >> 12);
}

void GTE::GPF(bool satIR, bool shift)
{
    MacOP123(1, (Int32)(Int16)regs[GTEREGPOS::IR1], (Int32)(Int16)regs[GTEREGPOS::IR0], 0, false, false, shift, true, shift, shift, satIR, satIR);
    MacOP123(2, (Int32)(Int16)regs[GTEREGPOS::IR2], (Int32)(Int16)regs[GTEREGPOS::IR0], 0, false, false, shift, true, shift, shift, satIR, satIR);
    MacOP123(3, (Int32)(Int16)regs[GTEREGPOS::IR3], (Int32)(Int16)regs[GTEREGPOS::IR0], 0, false, false, shift, true, shift, shift, satIR, satIR);

    PushRGBFromMAC();
}

void GTE::GPL(bool satIR, bool shift)
{
    Int32 shiftvalue = 1;
    if (shift) shiftvalue = 0x1000;

    MacOP123(1, (Int32)regs[GTEREGPOS::MAC1], shiftvalue, 0, false, false, false, false, false, false, false, false);
    MacOP123(2, (Int32)regs[GTEREGPOS::MAC2], shiftvalue, 0, false, false, false, false, false, false, false, false);
    MacOP123(3, (Int32)regs[GTEREGPOS::MAC3], shiftvalue, 0, false, false, false, false, false, false, false, false);

    MacOP123(1, (Int32)(Int16)regs[GTEREGPOS::IR1], (Int32)(Int16)regs[GTEREGPOS::IR0], mac123Result[0], false, false, shift, true, shift, shift, satIR, satIR);
    MacOP123(2, (Int32)(Int16)regs[GTEREGPOS::IR2], (Int32)(Int16)regs[GTEREGPOS::IR0], mac123Result[1], false, false, shift, true, shift, shift, satIR, satIR);
    MacOP123(3, (Int32)(Int16)regs[GTEREGPOS::IR3], (Int32)(Int16)regs[GTEREGPOS::IR0], mac123Result[2], false, false, shift, true, shift, shift, satIR, satIR);

    PushRGBFromMAC();
}

void GTE::saveState(UInt32 offset)
{
    for (int i = 0; i < 64; i++) psx.savestate_addvalue(offset + i, 31, 0, regs[i]);
}

void GTE::loadState(UInt32 offset)
{
    for (int i = 0; i < 64; i++) write_reg(i, psx.savestate_loadvalue(offset + i, 31, 0), 0, true);

    for (int i = 0; i < 64; i++)
    {
        if (psx.savestate[offset + i] != regs[i])
        {
            _wassert(_CRT_WIDE("GTE savestate loading modifies register"), _CRT_WIDE("GTE"), i);
        }
    }
}

// debug

void GTE::GTEoutWriteFile(bool writeTest)
{
#ifdef GTEFILEOUT
    FILE* file = fopen("R:\\debug_gte.txt", "w");

    for (int i = 0; i < debug_GTEOutCount; i++)
    {
        if (debug_GTEOutType[i] == 1) fputs("COMMAND: ", file);
        if (debug_GTEOutType[i] == 2) fputs("WRITE REG: ", file);
        if (debug_GTEOutType[i] == 3) fputs("COMMAND REG: ", file);
#ifdef FPGACOMPATIBLE
        if (debug_GTEOutType[i] == 4) fputs("REG IN: ", file);
        if (debug_GTEOutType[i] == 5) fputs("REG READ: ", file);
#endif
        char buf[10];
#ifndef FPGACOMPATIBLE
        _itoa(debug_GTEOutTime[i], buf, 16);
        for (int c = strlen(buf); c < 8; c++) fputc('0', file);
        fputs(buf, file);
        fputc(' ', file);
#endif
        _itoa(debug_GTEOutAddr[i], buf, 10);
        for (int c = strlen(buf); c < 2; c++) fputc('0', file);
        fputs(buf, file);
        fputc(' ', file);
        _itoa(debug_GTEOutData[i], buf, 16);
        for (int c = strlen(buf); c < 8; c++) fputc('0', file);
        fputs(buf, file);

        fputc('\n', file);
    }
    fclose(file);

    if (writeTest)
    {
        file = fopen("R:\\gte_test_fpsxa.txt", "w");

        for (int i = 0; i < debug_GTEOutCount; i++)
        {
            if (debug_GTEOutType[i] == 1 || debug_GTEOutType[i] == 4 || debug_GTEOutType[i] == 5)
            {
                char buf[10];
                _itoa(debug_GTEOutType[i], buf, 16);
                for (int c = strlen(buf); c < 2; c++) fputc('0', file);
                fputs(buf, file);
                fputc(' ', file);
                _itoa(debug_GTEOutAddr[i], buf, 16);
                for (int c = strlen(buf); c < 2; c++) fputc('0', file);
                fputs(buf, file);
                fputc(' ', file);
                _itoa(debug_GTEOutData[i], buf, 16);
                for (int c = strlen(buf); c < 8; c++) fputc('0', file);
                fputs(buf, file);

                fputc('\n', file);
            }
        }
        fclose(file);
    }
#endif
}

void GTE::GTEoutRegCapture(byte regtype, byte cmdOffset)
{
#ifdef GTEFILEOUT
    for (int i = 0; i < 64; i++)
    {
        if (debug_GTELast[i] != regs[i])
        {
            if (debug_GTEOutCount < GTEOutCountMax - 100)
            {
                debug_GTEOutTime[debug_GTEOutCount] = CPU.commands + cmdOffset;
                debug_GTEOutAddr[debug_GTEOutCount] = i;
                debug_GTEOutData[debug_GTEOutCount] = regs[i];
                debug_GTEOutType[debug_GTEOutCount] = regtype;
                debug_GTEOutCount++;
            }
            debug_GTELast[i] = regs[i];
        }
    }
#endif
}

void GTE::GTEoutCommandCapture(UInt32 command, byte cmdOffset)
{
#ifdef GTEFILEOUT
    if (debug_GTEOutCount > 27540)
    {
        int a = 5;
    }

    if (debug_GTEOutCount < GTEOutCountMax - 100)
    {
        debug_GTEOutTime[debug_GTEOutCount] = CPU.commands + cmdOffset;
        debug_GTEOutAddr[debug_GTEOutCount] = 0;
        debug_GTEOutData[debug_GTEOutCount] = command;
        debug_GTEOutType[debug_GTEOutCount] = 1;
        debug_GTEOutCount++;
    }
#endif
}

void GTE::GTETest()
{
#ifdef FPGACOMPATIBLE
    std::ifstream infile("R:\\gte_test_fpsxa.txt");
    std::string line;
    int cmdCount = 0;
    while (std::getline(infile, line))
    {
        std::string type = line.substr(0, 2);
        std::string addr = line.substr(3, 2);
        std::string data = line.substr(6, 8);
        byte typeI = std::stoul(type, nullptr, 16);
        byte addrI = std::stoul(addr, nullptr, 16);
        UInt32 dataI = std::stoul(data, nullptr, 16);
        if (cmdCount == 43802)
        {
            int a = 5;
        }
        //if (debug_GTEOutCount == 24010)
        {
            int a = 5;
        }
        switch (typeI)
        {
        case 1:  
            Gte.command(dataI, 0); 
            break;
        case 4:  Gte.write_reg(addrI, dataI, 0, false); break;
        case 5:  
            UInt32 value = Gte.read_reg(addrI); 
            if (value != dataI)
            {
                int error = 1;
            }
            break;
        }
        cmdCount++;
    }
#else
    std::ifstream infile("R:\\gte_test_duck.txt");
    std::string line;
    while (std::getline(infile, line))
    {
        std::string type = line.substr(0, 2);
        std::string addr = line.substr(3, 2);
        std::string data = line.substr(6, 8);
        byte typeI = std::stoul(type, nullptr, 16);
        byte addrI = std::stoul(addr, nullptr, 16);
        UInt32 dataI = std::stoul(data, nullptr, 16);
        switch (typeI)
        {
        case 1:  Gte.command(dataI, 0); break;
        case 2:  Gte.write_reg(addrI, dataI, 0, false); break;
        }
    }
#endif
    GTEoutWriteFile(false);
}