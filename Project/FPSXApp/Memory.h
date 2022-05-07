#pragma once
#include <string>
using namespace std;

#include "types.h"

// timing hacks
//#define NOMEMTIME
//#define NOMEMREADTIME
//#define FIXMEMTIME
//#define FIXEDMEMTIME 2
//#define DISABLEINSTRUCTIONCACHE
//#define COMPATIRQ
//#define STAGE5WRITE

#define HILOTIME 1

//#define INSTANTMDECDECODE

//#define FASTBOOT

//#define FASTCD

//#define ENDLESSDMA

//#define DUCKCOMPATIBLE
#ifdef DUCKCOMPATIBLE
#define NOMEMTIME
#define COMPATIRQ
#define STAGE5WRITE
#define INSTANTMDECDECODE
#define ENDLESSDMA
#define HILOTIME 1
#define INSTANTSEEK
#endif

#define FPGACOMPATIBLE
#ifdef FPGACOMPATIBLE
#define ENDLESSDMA
#define NOMEMWAITFPGA
//#define REPRODUCIBLEGPUTIMING
#define REPRODUCIBLEDMATIMING
#define FPGADMACPUBUSY
#endif

//#define BILINEAR

//#define MDECDEBUGOUT

#define USETTY

//#define IGNOREAREACHECKS

class MEMORY
{
public:
	Byte RAM[2097152];
	Byte *GameRom;

	UInt32 GameRom_max;

	Byte BIOSROM[524288];

	byte accessdelay;

	bool createGameRAMSnapshot;

	// internal handling
	bool requestData;
	bool requestCode;
	byte requestSize;
	UInt32 address;
	UInt32 dataRead;
	bool done;
	UInt32 cacheLine[4];

	bool requestWrite;
	UInt32 dataWrite;
	UInt32 addressWrite;
	byte writeMask;

	byte busy;
	bool waitforCache;

	bool cd_transfer;
	UInt32 cd_addr;
	UInt32 cd_data;
	byte cd_mask;
	byte cd_reqsize;
	bool cd_rnw;

	void patchBios(UInt32 address, UInt32 data);
	void reset(string filename);
	UInt32 rotateRead16(UInt32 data, byte offset);
	UInt32 rotateRead32(UInt32 data, byte offset);
	void work();
	void GameRAMSnapshot();
	void load_gameram(string gamename);
	void save_gameram(string gamename);

	void saveState(UInt32 offset);
	void loadState(UInt32 offset);

	// debug
	string lineBuffer;
	int ttyCount;
	int cachetest[2097152];
	void test_datacache();
};
extern MEMORY Memory;