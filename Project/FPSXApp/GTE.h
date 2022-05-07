#pragma once

#include "types.h"

class GTE
{
public:

	UInt32 regs[64];
	Int16 rotMatrix[3][3];
	Int16 lightMatrix[3][3];
	Int16 colorMatrix[3][3];
	Int16 vectors[3][3];

	enum GTEREGPOS
	{
		V0XY = 0,
		V0Z = 1,
		V1XY = 2,
		V1Z = 3,
		V2XY = 4,
		V2Z = 5,
		RGBC = 6,
		OTZ = 7,
		IR0 = 8,
		IR1 = 9,
		IR2 = 10,
		IR3 = 11,
		SXY0 = 12,
		SXY1 = 13,
		SXY2 = 14,
		SXYP = 15,
		SZ0 = 16,
		SZ1 = 17,
		SZ2 = 18,
		SZ3 = 19,
		RGB0 = 20,
		RGB1 = 21,
		RGB2 = 22,
		RES1 = 23,
		MAC0 = 24,
		MAC1 = 25,
		MAC2 = 26,
		MAC3 = 27,
		IRGB = 28,
		ORGB = 29,
		LZCS = 30,
		LZCR = 31,
		RT0 = 32,
		RT1 = 33,
		RT2 = 34,
		RT3 = 35,
		RT4 = 36,
		TR0 = 37,
		TR1 = 38,
		TR2 = 39,
		LLM0 = 40,
		LLM1 = 41,
		LLM2 = 42,
		LLM3 = 43,
		LLM4 = 44,
		BK0 = 45,
		BK1 = 46,
		BK2 = 47,
		LCM0 = 48,
		LCM1 = 49,
		LCM2 = 50,
		LCM3 = 51,
		LCM4 = 52,
		FC0 = 53,
		FC1 = 54,
		FC2 = 55,
		OFX = 56,
		OFY = 57,
		H = 58,
		DQA = 59,
		DQB = 60,
		ZSF3 = 61,
		ZSF4 = 62,
		FLAGS = 63
	};

	enum GTEFLAGPOS
	{
		IR0SAT = 12,
		SY2SAT = 13,
		SX2SAT = 14,
		MAC0UF = 15,
		MAC0OF = 16,
		DIVOF = 17,
		SZ1OTZSAT = 18,
		CBSAT = 19,
		CGSAT = 20,
		CRSAT = 21,
		IR3SAT = 22,
		IR2SAT = 23,
		IR1SAT = 24,
		MAC3UF = 25,
		MAC2UF = 26,
		MAC1UF = 27,
		MAC3OF = 28,
		MAC2OF = 29,
		MAC1OF = 30,
		ERROR = 31
	};
	
	UInt32 divResult;

	Int64 mac0Result;
	Int64 mac0Last;

	Int64 mac123Result[3];
	Int64 mac123Last[3];
	Int64 mac123Shifted[3];

	void reset();
	void write_reg(byte adr, UInt32 value, byte cycleOffset, bool loadSS);
	UInt32 read_reg(byte adr);
	byte command(UInt32 instruction, byte cmdOffset);

	// ALU
	void pushSXY(UInt32 sxy);
	void pushSXY(Int32 sx, Int32 sy);
	void pushSZ(Int32 value);
	void UNRDivide(UInt32 lhs, UInt32 rhs);
	void MacOP0(Int32 mul1, Int32 mul2, Int64 add, bool sub, bool swap, bool useIR, bool IRshift, bool checkOverflow);
	void MacOP123(byte index, Int32 mul1, Int32 mul2, Int64 add, bool sub, bool swap, bool saveShifted, bool useIR, bool IRshift, bool IRshiftFlag, bool satIR, bool satIRFlag);
	void PushRGBFromMAC();

	// helpers
	void MAC0OverflowCheck(Int64 value);
	void MAC123OverflowCheck(Int64 value, byte index);
	void IR0OverflowSet(Int32 value);
	void IR123OverflowSet(Int32 value, bool satIR, byte index);
	void IR123OverflowCheck(Int32 value, bool satIR, byte index);
	void SetOTZ(Int32 value);
	byte TruncateColor(Int32 value, byte index);

	// commands
	void RTPS(byte index, bool satIR, bool shift, bool last);
	void NCLIP();
	void OP(bool satIR, bool shift);
	void DPCS(UInt32 color, bool satIR, bool shift);
	void INTPL(bool satIR, bool shift);
	void MVMVA(UInt32 instruction);
	void NCDS(byte index, bool satIR, bool shift);
	void CDP(bool satIR, bool shift);
	void NCCS(byte index, bool satIR, bool shift);
	void CC(bool satIR, bool shift);
	void NCS(byte index, bool satIR, bool shift);
	void SQR(bool satIR, bool shift);
	void DPCL(bool satIR, bool shift);
	void AVSZ3();
	void AVSZ4();
	void GPF(bool satIR, bool shift);
	void GPL(bool satIR, bool shift);

	void saveState(UInt32 offset);
	void loadState(UInt32 offset);

#if DEBUG
//#define GTEFILEOUT
#endif
#ifdef GTEFILEOUT
	#define GTEOutCountMax 10000000
	UInt32 debug_GTEOutTime[GTEOutCountMax];
	byte debug_GTEOutAddr[GTEOutCountMax];
	UInt32 debug_GTEOutData[GTEOutCountMax];
	byte debug_GTEOutType[GTEOutCountMax];
	UInt32 debug_GTEOutCount;
	UInt32 debug_GTELast[64];

	UInt32 commandCnt[128];
#endif
	void GTEoutRegCapture(byte regtype, byte cmdOffset);
	void GTEoutCommandCapture(UInt32 command, byte cmdOffset);
	void GTEoutWriteFile(bool writeTest);
	void GTETest();

};
extern GTE Gte;