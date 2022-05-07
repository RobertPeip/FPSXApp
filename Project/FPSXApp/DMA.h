#pragma once
#include "SDL.h"
#include "types.h"

class Dma
{
public:
	enum class DMASTATE
	{
		OFF,
		WAITING,
		READHEADER,
		READING,
		WRITING,
		STOPPING,
		PAUSING,

		GPUBUSY,
		TIMEUP,
		GPU_PAUSING
	};

	DMASTATE dmaState;
	bool on;
	bool paused;
	byte waitcnt = 0;
	UInt32 data;
	byte activeChannel;

	UInt32 wordcount;
	UInt32 nextAddr;
	UInt16 dmaTime;

	UInt16 pauseTime;
	UInt16 pauseCnt[7];

	bool requests[7];
	bool requestsPending[7];
	bool timeupPending[7];
	bool gpupaused;
	DMASTATE lastStates[7];
	UInt32 wordcounts[7];

	byte patchStep; // patch return value of DMA status read, in case DMA was unpaused when status read in same cycle

	int maxDMATime;

	int REP_counter;
	int REP_target;
	int dmaEndWait;

	bool dmaOn;
	bool dmaOn_1;

	UInt32 linkedListDebug[1024];

	void reset();
	void work();
	void check_unpause();
	void trigger(byte channel);
	bool updateMasterFlag();

	void saveState(UInt32 offset);
	void loadState(UInt32 offset);
};
extern Dma DMA;