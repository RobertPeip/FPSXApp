#pragma once

#include "types.h"

class GPUTiming
{
public:
	Int16 hpos;
	Int16 vpos;
	Int16 nextHCount;

	bool isPAL;
	UInt16 htotal;
	UInt16 vtotal;
	bool inVsync;

	UInt32 vDisplayRange;
	UInt16 vDisplayStart;
	UInt16 vDisplayEnd;

	UInt32 hDisplayRange;
	UInt16 hDisplayStart;
	UInt16 hDisplayEnd;

	UInt16 hVisibleStart;
	UInt16 hVisibleEnd;
	UInt16 vVisibleStart;
	UInt16 vVisibleEnd;

	UInt16 displayWidth;
	UInt16 displayHeight;

	UInt32 vramRange;
	UInt16 vramDisplayWidth;
	UInt16 vramDisplayHeight;
	UInt16 vramDisplayLeft;
	UInt16 vramDisplayTop;

	UInt16 displayOriginLeft;
	UInt16 displayOriginTop;

	bool interlacedField;
	bool interlacedDisplayField;
	bool activeLineLSB;

	void reset();
	void softreset();
	void recalc(bool loadSS);
	void work();

	void saveState(UInt32 offset);
	void loadState(UInt32 offset);
};
extern GPUTiming GPU_Timing;