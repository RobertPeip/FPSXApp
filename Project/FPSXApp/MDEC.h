#pragma once
#include <queue>
#include "types.h"
#include "Memory.h"

class Mdec
{
public:

	Byte zigZag[64] = {0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,
					  12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6,  7,  14, 21, 28,
					  35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
					  58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

	// fifo
	std::deque<UInt32> fifoIn;
	std::deque<UInt32> fifoOut;
	bool fifoSecondAvail;
	UInt16 fifoSecondBuffer;
	bool fifoPopNextCycle;

	Byte iqUV[64];
	Byte iqY[64];
	Int16 scaleTable[64];
	UInt32 outputBlock[256];

	Int16 blocks[6][64];

	Int32 cmdTicks;
	bool busy;
	bool decoding;
	bool writing;
	UInt16 wordsRemain;
	bool decodeDone;

	Byte currentBlock;
	Byte currentCoeff;
	UInt16 currentQScale;

	void reset();

	void CMDWrite(UInt32 value);
	UInt32 fifoRead(bool fromDMA);

	void work();
	void updateStatus();

	bool decodeMacroBlock();
	void finishMacroBlock();
	bool rlDecode(UInt16 data);
    void IDCT(byte blockCount);
    void yuvToRGB(byte block);
    void yToMono();

	void saveState(UInt32 offset);
	void loadState(UInt32 offset);
	bool isSSIdle();

	// debug
#if DEBUG
//#define MDECFILEOUT
#endif
#ifdef MDECFILEOUT
	UInt32 debug_MDECOutTime[2000000];
	Byte   debug_MDECOutAddr[2000000];
	UInt32 debug_MDECOutData[2000000];
	Byte   debug_MDECOutType[2000000];
	UInt32 debug_MDECOutCount;
#endif
#ifdef MDECDEBUGOUT
	UInt32 debugcnt_RL;
	UInt32 debugcnt_IDCT;
	UInt32 debugcnt_COLOR;
	UInt32 debugcnt_FIFOOUT;
#endif
	void MDECOutWriteFile(bool writeTest);
	void MDECOutCapture(Byte type, Byte addr, UInt32 data);
	void MDECTEST();

};
extern Mdec MDEC;