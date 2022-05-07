#include "GPU_Timing.h"
#include "PSXRegs.h"
#include "Memory.h"
#include "GPU.h"
#include "Timer.h"
#include "psx.h"

GPUTiming GPU_Timing;

void GPUTiming::reset()
{
	isPAL = true;
	vpos = 0;
	if (isPAL) PSXRegs.GPUSTAT |= 1 << 20;

#ifdef FIXEDMEMTIME
	hpos = -5 - (FIXEDMEMTIME * 4);
	nextHCount = 0x878 + 5 + (FIXEDMEMTIME * 4);
#elif defined(NOMEMTIME)
	hpos = -5;
	nextHCount = 0x878 + 5;
#else
	hpos = -6 - 0x60;
	nextHCount = 0x878 + 5 + 0x60;
#endif

#ifdef FPGACOMPATIBLE
	nextHCount = 2172;
#endif

	inVsync = false;
	interlacedField = false;
	interlacedDisplayField = false;
	activeLineLSB = false;

	vramRange = 0;
	hDisplayRange = 0xC60260;
	vDisplayRange = 0x3FC10;
	recalc(false);
}

void GPUTiming::softreset()
{
	PSXRegs.GPUSTAT = 0x14802000;
	if (isPAL) PSXRegs.GPUSTAT |= 1 << 20;

	nextHCount = 0x879;
#ifdef FPGACOMPATIBLE
	nextHCount = 2173;
#endif
	hpos = 0;
	vpos = 0;
	inVsync = false;

	vramRange = 0;
	hDisplayRange = 0xC60260;
	vDisplayRange = 0x3FC10;
	recalc(false);

#ifndef FPGACOMPATIBLE
	// timer 1
	if ((PSXRegs.T_MODE[1] >> 8) & 1)
	{
		Timer.tick(1);
	}
#endif
}

void GPUTiming::recalc(bool loadSS)
{
	if ((PSXRegs.GPUSTAT >> 20 ) & 1) // pal
	{
		htotal = 3406; // gpu clock domain
		vtotal = 314;
	}
	else
	{
#ifndef FPGACOMPATIBLE
		if (htotal == 3406 && !loadSS) nextHCount += 5;
#endif
		htotal = 3413; // gpu clock domain
		vtotal = 263;
	}

	byte dotClock = (PSXRegs.GPUSTAT >> 16) & 7;
	switch (dotClock)
	{
	case 0: dotClock = 10; break;
	case 1: dotClock = 7; break;
	case 2: dotClock = 8; break;
	case 3: dotClock = 7; break;
	case 4: dotClock = 5; break;
	case 5: dotClock = 7; break;
	case 6: dotClock = 4; break;
	case 7: dotClock = 7; break;
	}

	hDisplayStart = hDisplayRange & 0xFFF;
	hDisplayEnd = (hDisplayRange >> 12) & 0xFFF;
	if (hDisplayStart > htotal) hDisplayStart = htotal;
	if (hDisplayEnd > htotal) hDisplayEnd = htotal;
	hDisplayStart = hDisplayStart / dotClock * dotClock;
	hDisplayEnd = hDisplayEnd / dotClock * dotClock;

	vDisplayStart = vDisplayRange & 0x3FF;
	vDisplayEnd = (vDisplayRange >> 10) & 0x3FF;
	if (vDisplayStart > 0x13A) vDisplayStart = 0x13A;
	if (vDisplayEnd > 0x13A) vDisplayEnd = 0x13A;

	// todo: crop mode select -> visible area?
	if ((PSXRegs.GPUSTAT >> 20) & 1) // pal
	{
		hVisibleStart = 628;
		hVisibleEnd = 3188;
		vVisibleStart = 20;
		vVisibleEnd = 308;
	}
	else
	{
		hVisibleStart = 608;
		hVisibleEnd = 3168;
		vVisibleStart = 16;
		vVisibleEnd = 256;
	}

	displayWidth = (hVisibleEnd - hVisibleStart) / dotClock;
	displayHeight = vVisibleEnd - vVisibleStart;

	if (hDisplayEnd < hDisplayStart) vramDisplayWidth = 0;
	else vramDisplayWidth = hDisplayEnd - hDisplayStart;

	vramDisplayWidth = vramDisplayWidth / dotClock;
	if (vramDisplayWidth == 1) vramDisplayWidth = 4;
	else vramDisplayWidth = (vramDisplayWidth + 2) & 0xFFFC;

	UInt16 hSkipPixels;
	if (hDisplayStart >= hVisibleStart)
	{
		displayOriginLeft = (hDisplayStart - hVisibleStart) / dotClock;
		vramDisplayLeft = (vramRange & 0x3FF);
		hSkipPixels = 0;
	}
	else
	{
		hSkipPixels = (hVisibleStart - hDisplayStart) / dotClock;
		displayOriginLeft = 0;
		vramDisplayLeft = ((vramRange & 0x3FF) + hSkipPixels) % 1024;
	}

	if (hSkipPixels < vramDisplayWidth) vramDisplayWidth -= hSkipPixels;
	else vramDisplayWidth = 0;

	if (displayWidth - displayOriginLeft < vramDisplayWidth) vramDisplayWidth = displayWidth - displayOriginLeft;

	byte yShift = 0;
	if (((PSXRegs.GPUSTAT >> 22) & 1) || ((PSXRegs.GPUSTAT >> 19) & 1)) yShift = 1;

	if (vDisplayStart > vVisibleStart)
	{
		displayOriginTop = (vDisplayStart - vVisibleStart) << yShift;
		vramDisplayTop = (vramRange >> 10) & 0x1FF;
	}
	else
	{
		displayOriginTop = 0;
		vramDisplayTop = (((vramRange >> 10) & 0x1FF) + ((vVisibleStart - vDisplayStart) << yShift)) % 512;
	}

	byte heightShift = yShift;
	if (vDisplayEnd <= vVisibleEnd)
	{
		vramDisplayHeight = (vDisplayEnd - std::min(vDisplayEnd, std::max(vDisplayStart, vVisibleStart))) << heightShift;
	}
	else
	{
		vramDisplayHeight = (vVisibleEnd - std::min(vVisibleEnd, std::max(vDisplayStart, vVisibleStart))) << heightShift;
	}
}

void GPUTiming::work()
{
	hpos++;
	nextHCount--;
	Int64 hpos_gpu = hpos;
	if (isPAL) hpos_gpu *= 709379;
	else hpos_gpu *= 715909;
	hpos_gpu /= 451584;

	//if (hpos_gpu >= htotal)
	if (nextHCount == 0)
	{
		vpos++;
		hpos = 0;

#ifdef FPGACOMPATIBLE
		if ((PSXRegs.GPUSTAT >> 16) & 1) // hor2
			nextHCount = 2169; // 368
		else
		{
			switch ((PSXRegs.GPUSTAT >> 17) & 3) // hor1
			{
			case 0: nextHCount = 2172; break; // 256;
			case 1: nextHCount = 2170; break; // 320;
			case 2: nextHCount = 2169; break; // 512;
			case 3: nextHCount = 2170; break; // 640;
			}
		}
#else
		if ((PSXRegs.GPUSTAT >> 20) & 1) // pal
		{
			nextHCount = 0x879;
		}
		else
		{
			nextHCount = 0x87D;
		}
#endif

		if (vpos >= vtotal)
		{
			vpos = 0;
			if (PSXRegs.GPUSTAT >> 22 & 1)
			{
				interlacedField = !interlacedField;
				PSXRegs.GPUSTAT &= ~(1 << 13);
				if (interlacedField) PSXRegs.GPUSTAT |= (1 << 13);
			}
			else
			{
				PSXRegs.GPUSTAT &= ~(1 << 13);
				interlacedField = false;
			}
		}

		// timer 1
		if ((PSXRegs.T_MODE[1] >> 8) & 1)
		{
			Timer.tick(1);
		}

		bool mode480i = ((PSXRegs.GPUSTAT >> 19 & 1) && (PSXRegs.GPUSTAT >> 22 & 1));

		bool isVsync = (vpos < vDisplayStart || vpos >= vDisplayEnd);
		if (isVsync != inVsync)
		{
			if (isVsync)
			{
				PSXRegs.setIRQ(0);
				if (mode480i) interlacedDisplayField = !interlacedField;
				else interlacedDisplayField = false;
				GPU.finishFrame();
			}
			inVsync = isVsync;
			Timer.gateChange(1, inVsync);
		}

		PSXRegs.GPUSTAT &= 0x7FFFFFFF;
		UInt16 vramRangeY = vramRange >> 10;
		activeLineLSB = false;
		if (mode480i)
		{
			if ((((vramRangeY) & 1) == 0) &&  interlacedDisplayField) activeLineLSB = true;
			if ((((vramRangeY) & 1) == 1) && !interlacedDisplayField) activeLineLSB = true;

			if ((((vramRangeY) & 1) == 0) && !isVsync && interlacedDisplayField) PSXRegs.GPUSTAT |= 0x80000000;
			if ((((vramRangeY) & 1) == 1) && !isVsync && !interlacedDisplayField) PSXRegs.GPUSTAT |= 0x80000000;
		}
		else
		{
			if ((vramRangeY + vpos) & 1) PSXRegs.GPUSTAT |= 0x80000000;
		}
	}
}

void GPUTiming::saveState(UInt32 offset)
{
	psx.savestate_addvalue(offset + 0, 31,  0, vDisplayRange);
	psx.savestate_addvalue(offset + 1, 31,  0, hDisplayRange);
	psx.savestate_addvalue(offset + 2, 31,  0, vramRange);
	psx.savestate_addvalue(offset + 3, 15,  0, hpos); // not required for FPGA
	psx.savestate_addvalue(offset + 3, 31, 16, vpos);
	psx.savestate_addvalue(offset + 4, 15,  0, nextHCount);
	psx.savestate_addvalue(offset + 4, 16, 16, isPAL);
	psx.savestate_addvalue(offset + 4, 17, 17, inVsync);
	psx.savestate_addvalue(offset + 4, 18, 18, interlacedField); // not required for FPGA -> in gpustat
	psx.savestate_addvalue(offset + 4, 19, 19, interlacedDisplayField);
	psx.savestate_addvalue(offset + 4, 20, 20, activeLineLSB);
}

void GPUTiming::loadState(UInt32 offset)
{
	vDisplayRange          = psx.savestate_loadvalue(offset + 0, 31,  0); 
	hDisplayRange		   = psx.savestate_loadvalue(offset + 1, 31,  0); 
	vramRange			   = psx.savestate_loadvalue(offset + 2, 31,  0); 
	hpos				   = psx.savestate_loadvalue(offset + 3, 15,  0); 
	vpos				   = psx.savestate_loadvalue(offset + 3, 31, 16); 
	nextHCount			   = psx.savestate_loadvalue(offset + 4, 15,  0); 
	isPAL				   = psx.savestate_loadvalue(offset + 4, 16, 16); 
	inVsync				   = psx.savestate_loadvalue(offset + 4, 17, 17); 
	interlacedField		   = psx.savestate_loadvalue(offset + 4, 18, 18); 
	interlacedDisplayField = psx.savestate_loadvalue(offset + 4, 19, 19); 
	activeLineLSB		   = psx.savestate_loadvalue(offset + 4, 20, 20); 

#ifdef FPGACOMPATIBLE
	if (nextHCount > 0) nextHCount--;
	if (isPAL) PSXRegs.GPUSTAT |= 1 << 20;
#endif

	recalc(true);
}