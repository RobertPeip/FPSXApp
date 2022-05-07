#undef NDEBUG
#include <assert.h>

#include "Timer.h"
#include "CPU.h"
#include "PSXRegs.h"
#include "psx.h"
#include "memory.h"

TIMER Timer;

void TIMER::reset()
{
	for (int i = 0; i < 3; i++)
	{
		gateOn[i] = false;
		irqDone[i] = false;
	}
	timer2subcount = 0;
	patchStep = 0;
}
void TIMER::gateChange(byte index, bool newGate)
{
	gateOn[index] = newGate;

	if (index == 1)
	{
		int a = 5;
	}

	if ((PSXRegs.T_MODE[index] >> 0) & 1) // sync enable
	{
		switch ((PSXRegs.T_MODE[index] >> 1) & 3) // sync mode
		{
		case 1: // ResetOnGate
		case 2: // ResetAndRunOnGate
			if (newGate) PSXRegs.T_CURRENT[index] = 0;
			break;

		case 3: // FreeRunOnGate
			if (gateOn[index]) PSXRegs.T_MODE[index] &= ~1; // clear sync enable
			break;
		}
	}
}

void TIMER::tick(byte index)
{
	bool tick = true;
	if ((PSXRegs.T_MODE[index] >> 0) & 1) // sync enable
	{
		if (index == 0)
		{
			//_wassert(_CRT_WIDE("Timer 0 blank gate mode not implemented"), _CRT_WIDE("Timer"), 0);
		}

		if (index < 2)
		{
			switch ((PSXRegs.T_MODE[index] >> 1) & 3) // sync mode
			{
			case 0: // PauseOnGate
				if (gateOn[index]) tick = false;
				break;

			case 2: // ResetAndRunOnGate
			case 3: // FreeRunOnGate
				if (!gateOn[index]) tick = false;
				break;
			}
		}
		else
		{
			if (((PSXRegs.T_MODE[index] >> 1) & 3) == 0 || ((PSXRegs.T_MODE[index] >> 1) & 3) == 3)
			{
				tick = false;
			}
		}
	}
	if (tick)
	{
		if (index == 1)
		{
			int a = 5;
		}

		PSXRegs.T_CURRENT[index]++;
		bool irq = false;
		//if (PSXRegs.T_CURRENT[index] == PSXRegs.T_TARGET[index])
		if (PSXRegs.T_CURRENT[index] == PSXRegs.T_TARGET[index] || (PSXRegs.T_CURRENT[index] > PSXRegs.T_TARGET[index] && PSXRegs.T_TARGET[index] == 0))
		{
			PSXRegs.T_MODE[index] |= 1 << 11; // set target reached
			if ((PSXRegs.T_MODE[index] >> 4) & 1) irq = true; // irq at target
			if ((PSXRegs.T_MODE[index] >> 3) & 1) PSXRegs.T_CURRENT[index] = 0; // reset on target
		}

		if (PSXRegs.T_CURRENT[index] == 0xFFFF)
		{
			if ((PSXRegs.T_MODE[index] >> 5) & 1) irq = true; // irq at overflow
			PSXRegs.T_MODE[index] |= 1 << 12; // set overflow reached
			PSXRegs.T_CURRENT[index] = 0;
		}

		if (irq)
		{
			if ((PSXRegs.T_MODE[index] >> 7) & 1) // irq toggle mode
			{
				PSXRegs.T_MODE[index] ^= 1 << 10;
			}
			else
			{
				PSXRegs.T_MODE[index] &= 0xFBFF; // todo : lower bit for some cycles
				checkIRQ(index);
				PSXRegs.T_MODE[index] |= 1 << 10;
			}
		}
	}
}

void TIMER::checkIRQ(byte index)
{
	if ((((PSXRegs.T_MODE[index] >> 10) & 1) == 0) && (((PSXRegs.T_MODE[index] >> 6) & 1) || irqDone[index] == false)) // irq_n && repeat || !done
	{
		irqDone[index] = true;
		PSXRegs.setIRQ(4 + index);
	}
}

void TIMER::work()
{
#ifdef FPGACOMPATIBLE
	T_CURRENT_2[0] = PSXRegs.T_CURRENT[0];

	T_CURRENT_2[1] = T_CURRENT_1[1];
	T_CURRENT_1[1] = PSXRegs.T_CURRENT[1];

	T_CURRENT_2[2] = PSXRegs.T_CURRENT[2];
#endif

	if (((PSXRegs.T_MODE[0] >> 8) & 1) == 0) 
		tick(0);
	else
	{
		//_wassert(_CRT_WIDE("Timer 0 extern mode not implemented"), _CRT_WIDE("Timer"), 0);
		//tick(0);
	}

	if (((PSXRegs.T_MODE[1] >> 8) & 1) == 0) tick(1);
	if (((PSXRegs.T_MODE[2] >> 9) & 1) == 1)
	{
		timer2subcount++;
		if (timer2subcount >= 8)
		{
			timer2subcount = 0;
			tick(2);
		}
	}
	else
	{
		tick(2);
	}
	//if (patchStep > 0)
	//{
	//	CPU.writebackData = PSXRegs.T_CURRENT[patchStep - 1];
	//	patchStep = 0;
	//}
}

void TIMER::saveState(UInt32 offset)
{
	for (int i = 0; i < 3; i++) psx.savestate_addvalue(offset + 0 + i, 15, 0, PSXRegs.T_CURRENT[i]);
	for (int i = 0; i < 3; i++) psx.savestate_addvalue(offset + 3 + i, 15, 0, PSXRegs.T_MODE[i]);
	for (int i = 0; i < 3; i++) psx.savestate_addvalue(offset + 6 + i, 15, 0, PSXRegs.T_TARGET[i]);
	
	psx.savestate_addvalue(offset + 9,  7,  0, timer2subcount);
	psx.savestate_addvalue(offset + 9,  8,  8, gateOn[0]);
	psx.savestate_addvalue(offset + 9,  9,  9, gateOn[1]);
	psx.savestate_addvalue(offset + 9, 10, 10, gateOn[2]);
	psx.savestate_addvalue(offset + 9, 11, 11, irqDone[0]);
	psx.savestate_addvalue(offset + 9, 12, 12, irqDone[1]);
	psx.savestate_addvalue(offset + 9, 13, 13, irqDone[2]);
}

void TIMER::loadState(UInt32 offset)
{
	for (int i = 0; i < 3; i++) PSXRegs.T_CURRENT[i] = psx.savestate_loadvalue(offset + 0 + i, 15, 0);
	for (int i = 0; i < 3; i++) PSXRegs.T_MODE[i]    = psx.savestate_loadvalue(offset + 3 + i, 15, 0);
	for (int i = 0; i < 3; i++) PSXRegs.T_TARGET[i]  = psx.savestate_loadvalue(offset + 6 + i, 15, 0);

	timer2subcount = psx.savestate_loadvalue(offset + 9,  7,  0);
	gateOn[0]	   = psx.savestate_loadvalue(offset + 9,  8,  8); // not used in FPGA
	gateOn[1]	   = psx.savestate_loadvalue(offset + 9,  9,  9); // not used in FPGA
	gateOn[2]	   = psx.savestate_loadvalue(offset + 9, 10, 10); // not used in FPGA
	irqDone[0]	   = psx.savestate_loadvalue(offset + 9, 11, 11);
	irqDone[1]	   = psx.savestate_loadvalue(offset + 9, 12, 12);
	irqDone[2]	   = psx.savestate_loadvalue(offset + 9, 13, 13);

#ifdef FPGACOMPATIBLE
	if (((PSXRegs.T_MODE[0] >> 8) & 1) == 0) PSXRegs.T_CURRENT[0]++;
	if (((PSXRegs.T_MODE[1] >> 8) & 1) == 0) PSXRegs.T_CURRENT[1]++;
	if (((PSXRegs.T_MODE[2] >> 9) & 1) == 0)
	{
		PSXRegs.T_CURRENT[2]++;
	}
	else
	{
		timer2subcount++;
	}
	for (int i = 0; i < 3; i++)
	{
		T_CURRENT_1[i] = PSXRegs.T_CURRENT[i];
		T_CURRENT_2[i] = T_CURRENT_1[i];
	}

#endif
}