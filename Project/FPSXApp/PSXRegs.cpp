#include "PSXRegs.h"
#include "Joypad.h"
#include "psx.h"
#include "Memory.h"
#include "DMA.h"
#include "sound.h"
#include "GPU.h"
#include "CPU.h"
#include "MDEC.h"
#include "Timer.h"
PSXREGS PSXRegs;

void PSXREGS::reset()
{
	CACHECONTROL = 0;

	MC_EXP1_BASE    = 0x1F000000;
	MC_EXP2_BASE    = 0x1F802000;
	MC_EXP1_DELAY   = 0x0013243F;
	MC_EXP3_DELAY   = 0x00003022;
	MC_BIOS_DELAY   = 0x0013243F;
	MC_SPU_DELAY    = 0x200931E1;
	MC_CDROM_DELAY  = 0x00020843;
	MC_EXP2_DELAY   = 0x00070777;
	MC_COMMON_DELAY = 0x00031125;

	JOY_STAT = 0;
	JOY_MODE = 0;
	JOY_CTRL = 0;
	JOY_BAUD = 0;

	SIO_CTRL = 0;
	SIO_STAT = 5;
	SIO_MODE = 0;
	SIO_BAUD = 0xDC;

	MC_RAMSIZE = 0xB88;

	I_STATUS = 0;
	I_MASK = 0;

	for (int i = 0; i < 7; i++)
	{
		D_MADR[i] = 0;
		D_BCR[i] = 0;
		D_CHCR[i] = 0;
	}
	DPCR = 0x07654321;
	DICR = 0;

	for (int i = 0; i < 3; i++)
	{
#ifdef FIXEDMEMTIME
		T_CURRENT[i] = -5 - (FIXEDMEMTIME * 4);
#elif defined(NOMEMTIME)
		T_CURRENT[i] = -5;
#elif defined(FPGACOMPATIBLE)
		T_CURRENT[i] = 1;
#else
		T_CURRENT[i] = -5 - 0x60;
#endif
		T_MODE[i] = 0x400; // interrupt_n
		T_TARGET[i] = 0;
	}

	CDROM_STATUS = 0x18;
	CDROM_IRQENA = 0;
	CDROM_IRQFLAG = 0;

	GPUREAD = 0;
	GPUSTAT = 0x14902000;

	MDECStatus = 0;
	MDECControl = 0;

	for (int i = 0; i < 192; i++)
	{
		SPU_VOICEREGS[i] = 0;
	}	
	
	for (int i = 0; i < 24; i++)
	{
		SPU_VOICEVOLUME[i][0] = 0;
		SPU_VOICEVOLUME[i][1] = 0;
	}

	SPU_VOLUME_LEFT = 0;
	SPU_VOLUME_RIGHT = 0;

	SPU_KEYON = 0;
	SPU_KEYOFF = 0;
	SPU_PITCHMODENA = 0;
	SPU_NOISEMODE = 0;
	SPU_REVERBON = 0;
	SPU_ENDX = 0;

	SPU_CNT = 0;
	SPU_TRANSFER_CNT = 0;
	SPU_STAT = 0;

	SPU_CDAUDIO_VOL_L = 0;
	SPU_CDAUDIO_VOL_R = 0;
	SPU_EXT_VOL_L = 0;
	SPU_EXT_VOL_R = 0;
	SPU_CURVOL_L = 0;
	SPU_CURVOL_R = 0;

}

void PSXREGS::write_reg_irq(UInt32 adr, UInt32 value)
{
	if (adr == 0x0) { I_STATUS = I_STATUS & (value & 0x7FF); checkIRQ(false); return; }
	if (adr == 0x4) { I_MASK = value & 0x7FF; checkIRQ(true); return; }
}

UInt32 PSXREGS::read_reg_irq(UInt32 adr)
{
	if (adr == 0x0) { return I_STATUS; }
	if (adr == 0x4) { return I_MASK; }
	return 0xFFFFFFFF;
}

void PSXREGS::setIRQ(UInt16 index)
{
	I_STATUS |= 1 << index;
	checkIRQ(false);
}

void PSXREGS::checkIRQ(bool instant)
{	
	bool irqNext = (I_STATUS & I_MASK) != 0;
#ifdef FPGACOMPATIBLE
	CPU.irqNext3 = irqNext;
	if (instant || CPU.paused)
	{
		CPU.irqNext2 = irqNext;
		CPU.irqNext1 = irqNext;
	}
#else
	if ((I_STATUS & I_MASK) != 0)
	{
		CPU.cop0_CAUSE |= 0x400;
	}
	else
	{
		CPU.cop0_CAUSE &= 0xFFFFFBFF;
	}
#endif // FPGACOMPATIBLE
}

void PSXREGS::saveStateIRQ(UInt32 offset)
{
	psx.savestate_addvalue(offset + 0, 31, 0, I_STATUS);
	psx.savestate_addvalue(offset + 1, 31, 0, I_MASK);
}

void PSXREGS::loadStateIRQ(UInt32 offset)
{
	I_STATUS = psx.savestate_loadvalue(offset + 0, 31, 0);
	I_MASK   = psx.savestate_loadvalue(offset + 1, 31, 0);
}

void PSXREGS::write_reg_dma(UInt32 adr, UInt32 value)
{
	byte channel = adr / 16;

	if (channel == 4)
	{
		int a = 5;
	}

	if (channel < 7)
	{
		switch (adr & 0xF)
		{
		case 0x0: 
			D_MADR[channel] = value & 0xFFFFFF; break;
		case 0x4: D_BCR[channel] = value; break;
		case 0x8: 
			D_CHCR[channel] &= ~(0b01110001'01110111'00000111'00000011);
			D_CHCR[channel] |= (value & 0b01110001'01110111'00000111'00000011);
			if (channel == 6 && ((value >> 28) & 1)) DMA.trigger(6);
			if (DMA.requests[channel]) 
				DMA.trigger(channel);
			break;
		}
	}
	else
	{
		switch (adr & 0xF)
		{
		case 0x0:
			DPCR = value;
			for (int i = 0; i < 7; i++)
			{
				if (DMA.requests[i]) DMA.trigger(i);
			}
			break;

		case 0x4: 
			DICR &= 0b11111111'00000000'01111111'11000000;
			DICR |= (value & 0b00000000'11111111'10000000'00111111);
			DICR &= ~(value & 0b11111111'00000000'00000000'00000000);
			DMA.updateMasterFlag();
			break;
		}
	}
}

UInt32 PSXREGS::read_reg_dma(UInt32 adr)
{
	byte channel = adr / 16;

	if (channel < 7)
	{
		switch (adr & 0xF)
		{
		case 0x0: return D_MADR[channel];
		case 0x4: return D_BCR[channel];
		case 0x8: 
			//if (DMA.patchStep == 1) DMA.patchStep = 2;
			return D_CHCR[channel]; 
		}
	}
	else
	{
		switch (adr & 0xF)
		{
		case 0x0: return DPCR; break;
		case 0x4: return DICR; break;
		}
	}
	return 0xFFFFFFFF;
}

void PSXREGS::write_reg_timer(UInt32 adr, UInt32 value)
{
	byte channel = adr / 16;
	if (channel < 3)
	{
		switch (adr & 0xF)
		{
		case 0x0: T_CURRENT[channel] = value; break;
		case 0x4:
			T_MODE[channel] &= ~0xE3FF;
			T_MODE[channel] |= value & 0xE3FF;
			T_CURRENT[channel] = 0;
			Timer.irqDone[channel] = false;
			Timer.checkIRQ(channel);
			break;
		case 0x8: T_TARGET[channel] = value; break;
		}
	}
}

UInt32 PSXREGS::read_reg_timer(UInt32 adr)
{
	byte channel = adr / 16;
	if (channel < 3)
	{
		switch (adr & 0xF)
		{
		case 0x0: 
			Timer.patchStep = channel + 1;
#ifdef FPGACOMPATIBLE
			return Timer.T_CURRENT_2[channel];
#else
			return T_CURRENT[channel];
#endif
		case 0x4:
		{
			UInt32 value = T_MODE[channel];
			T_MODE[channel] &= 0xE7FF; // clear reached bits
			return value;
		}
			break;
		case 0x8: return T_TARGET[channel];
		}
	}
	return 0xFFFFFFFF;
}

void PSXREGS::write_reg_gpu(UInt32 adr, UInt32 value)
{
	if (adr == 0x0) { GPU.GP0Write(value); return; }
	if (adr == 0x4) { GPU.GP1Write(value); return; }
}

UInt32 PSXREGS::read_reg_gpu(UInt32 adr)
{
	if (adr == 0x0) { 
		GPU.ReadVRAMtoCPU(); return GPUREAD; }
	if (adr == 0x4) 
	{ 
#ifndef FPGACOMPATIBLE
		GPU.work(); 
		GPU.skipnext = true;
#endif
		return GPUSTAT; 
	}
	return 0xFFFFFFFF;
}

void PSXREGS::write_reg_mdec(UInt32 adr, UInt32 value)
{
	MDEC.MDECOutCapture(8, adr, value);

	if (adr == 0x0) { MDEC.CMDWrite(value); return; }
	if (adr == 0x4) 
	{ 
		MDECControl = value; 
		if (value & 0x80000000) MDEC.reset();
		MDEC.updateStatus();
		return; 
	}
}

UInt32 PSXREGS::read_reg_mdec(UInt32 adr)
{
	UInt32 retval = 0xFFFFFFFF;
	if (adr == 0x0) { retval = MDEC.fifoRead(false); }
	if (adr == 0x4) { retval = MDECStatus; }

	MDEC.MDECOutCapture(7, adr, retval);

	return retval;
}

void PSXREGS::write_reg_memctrl(UInt32 adr, UInt32 value)
{
	if (adr == 0x00) { MC_EXP1_BASE    = (MC_EXP1_BASE    & 0x50E00000) | (value & 0xAF1FFFFF); return; }
	if (adr == 0x04) { MC_EXP2_BASE    = (MC_EXP2_BASE    & 0x50E00000) | (value & 0xAF1FFFFF); return; }
	if (adr == 0x08) { MC_EXP1_DELAY   = (MC_EXP1_DELAY   & 0x50E00000) | (value & 0xAF1FFFFF); return; }
	if (adr == 0x0C) { MC_EXP3_DELAY   = (MC_EXP3_DELAY   & 0x50E00000) | (value & 0xAF1FFFFF); return; }
	if (adr == 0x10) { MC_BIOS_DELAY   = (MC_BIOS_DELAY   & 0x50E00000) | (value & 0xAF1FFFFF); return; }
	if (adr == 0x14) { MC_SPU_DELAY    = (MC_SPU_DELAY    & 0x50E00000) | (value & 0xAF1FFFFF); return; }
	if (adr == 0x18) { MC_CDROM_DELAY  = (MC_CDROM_DELAY  & 0x50E00000) | (value & 0xAF1FFFFF); return; }
	if (adr == 0x1C) { MC_EXP2_DELAY   = (MC_EXP2_DELAY   & 0x50E00000) | (value & 0xAF1FFFFF); return; }
	if (adr == 0x20) { MC_COMMON_DELAY = (MC_COMMON_DELAY & 0xFFFC0000) | (value & 0x0003FFFF); return; }
}

UInt32 PSXREGS::read_reg_memctrl(UInt32 adr)
{
	if (adr == 0x00) { return MC_EXP1_BASE;    }
	if (adr == 0x04) { return MC_EXP2_BASE;    }
	if (adr == 0x08) { return MC_EXP1_DELAY;   }
	if (adr == 0x0C) { return MC_EXP3_DELAY;   }
	if (adr == 0x10) { return MC_BIOS_DELAY;   }
	if (adr == 0x14) { return MC_SPU_DELAY;    }
	if (adr == 0x18) { return MC_CDROM_DELAY;  }
	if (adr == 0x1C) { return MC_EXP2_DELAY;   }
	if (adr == 0x20) { return MC_COMMON_DELAY; }
	return 0;
}

void PSXREGS::write_reg_memctrl2(UInt32 adr, UInt32 value)
{
	if (adr == 0x00) { MC_RAMSIZE = value; return; }
}

UInt32 PSXREGS::read_reg_memctrl2(UInt32 adr)
{
	if (adr == 0x00) { return MC_RAMSIZE; }
	return 0xFFFFFFFF;
}

void PSXREGS::write_reg_sio(UInt32 adr, UInt16 value)
{
	if (adr == 0x08) { SIO_MODE = value; return; }
	if (adr == 0x0A) 
	{ 
		SIO_CTRL = value; 
		if ((value >> 6) & 1) // reset
		{
			SIO_CTRL = 0;
			SIO_STAT = 5;
			SIO_MODE = 0;
			SIO_BAUD = 0xDC;
		}
		return; 
	}
	if (adr == 0x0E) { SIO_BAUD = value; return; }
}

UInt32 PSXREGS::read_reg_sio(UInt32 adr)
{
	if (adr == 0x00) { return 0xFFFFFFFF; } // sio data dummy
	if (adr == 0x04) { return SIO_STAT; }
	if (adr == 0x08) { return SIO_MODE; }
	if (adr == 0x0A) { return SIO_CTRL; }
	if (adr == 0x0E) { return SIO_BAUD; }
	return 0xFFFFFFFF;
}

void PSXREGS::saveState_sio(UInt32 offset)
{
	psx.savestate_addvalue(offset + 0, 31, 0, SIO_STAT);
	psx.savestate_addvalue(offset + 1, 31, 0, SIO_MODE);
	psx.savestate_addvalue(offset + 2, 31, 0, SIO_CTRL);
	psx.savestate_addvalue(offset + 3, 16, 0, SIO_BAUD);
}

void PSXREGS::loadState_sio(UInt32 offset)
{
	SIO_STAT = psx.savestate_loadvalue(offset + 0, 31, 0);
	SIO_MODE = psx.savestate_loadvalue(offset + 1, 31, 0);
	SIO_CTRL = psx.savestate_loadvalue(offset + 2, 31, 0);
	SIO_BAUD = psx.savestate_loadvalue(offset + 3, 16, 0);
}