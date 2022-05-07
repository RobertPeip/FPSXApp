#pragma once
#include "types.h"

class TIMER
{
public:

	bool gateOn[3];
	bool irqDone[3];

	byte timer2subcount;
	byte patchStep;

	UInt16 T_CURRENT_1[3];
	UInt16 T_CURRENT_2[3];

	void reset();
	void gateChange(byte index, bool newGate);
	void tick(byte index);
	void checkIRQ(byte index);
	void work();

	void saveState(UInt32 offset);
	void loadState(UInt32 offset);
};
extern TIMER Timer;