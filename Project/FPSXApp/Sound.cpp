#include <fstream>
#include <string>

#include "Sound.h"
#include "PSXRegs.h"
#include "Memory.h"
#include "CPU.h"
#include "DMA.h"
#include "psx.h"
#include "CDROM.h"

SOUND Sound;

void SOUND::reset()
{
	while (!fifo.empty()) fifo.pop_front();

	cmdTicks = 0;
#ifdef FPGACOMPATIBLE
#else
	sampleticks = -5;
#endif


	capturePosition = 0;

	reverbRight = false;
	reverbCurrentAddress = 0;

	noiseCnt = 0;
	noiseLevel = 1;

	irq_saved = false;
}

void SOUND::write_reg(UInt32 adr, UInt16 value)
{
	OutCapture(1, adr, value, 0);

	if (adr <= 0x17F)
	{ 
		PSXRegs.SPU_VOICEREGS[adr / 2] = value; 

		byte reg_index = (adr % 0x10);
		byte voice_index = (adr / 0x10);
		switch (reg_index)
		{
		case 0x08:
		case 0x0A:
			voice_adsrWrite[voice_index] = true;
		}

		return; 
	}
	if (adr == 0x180) { PSXRegs.SPU_VOLUME_LEFT = value; return; }
	if (adr == 0x182) { PSXRegs.SPU_VOLUME_RIGHT = value; return; }

	if (adr == 0x188) { PSXRegs.SPU_KEYON = PSXRegs.SPU_KEYON & 0xFFFF0000 | value; return; }
	if (adr == 0x18A) { PSXRegs.SPU_KEYON = PSXRegs.SPU_KEYON & 0xFFFF | (value << 16); return; }
	if (adr == 0x18C) { PSXRegs.SPU_KEYOFF = PSXRegs.SPU_KEYOFF & 0xFFFF0000 | value; return; }
	if (adr == 0x18E) { PSXRegs.SPU_KEYOFF = PSXRegs.SPU_KEYOFF & 0xFFFF | (value << 16); return; }
	if (adr == 0x190) { PSXRegs.SPU_PITCHMODENA = PSXRegs.SPU_PITCHMODENA & 0xFFFF0000 | value; return; }
	if (adr == 0x192) { PSXRegs.SPU_PITCHMODENA = PSXRegs.SPU_PITCHMODENA & 0xFFFF | (value << 16); return; }
	if (adr == 0x194) { PSXRegs.SPU_NOISEMODE = PSXRegs.SPU_NOISEMODE & 0xFFFF0000 | value; return; }
	if (adr == 0x196) { PSXRegs.SPU_NOISEMODE = PSXRegs.SPU_NOISEMODE & 0xFFFF | (value << 16); return; }
	if (adr == 0x198) { PSXRegs.SPU_REVERBON = PSXRegs.SPU_REVERBON & 0xFFFF0000 | value; return; }
	if (adr == 0x19A) { PSXRegs.SPU_REVERBON = PSXRegs.SPU_REVERBON & 0xFFFF | (value << 16); return; }

	if (adr == 0x1A4) { PSXRegs.SPU_IRQ_ADDR = value; return; }

	if (adr == 0x1A6)
	{
		PSXRegs.SPU_TRANSFERADDR = value;
		ramTransferAddr = value * 8;
	}

	if (adr == 0x1A8)
	{
		if (fifo.size() < 32)
		{
			fifo.push_back(value);
			updateTransferEvent();
		}
		return;
	}

	if (adr == 0x1AA)
	{
		if ((((PSXRegs.SPU_CNT >> 4) & 3) != ((value >> 4) & 3)) && ((value >> 4) & 3) == 0 && !Sound.fifo.empty())
		{
			if (((PSXRegs.SPU_CNT >> 4) & 3) == 3) // DMARead
			{
				while (!fifo.empty()) fifo.pop_front(); // clear fifo
			}
			else
			{
				while (!fifo.empty()) Sound.fifo.pop_front();
				// todo: copy to RAM
			}
		}
		PSXRegs.SPU_CNT = value;
		PSXRegs.SPU_STAT = (PSXRegs.SPU_STAT & 0xFFC0) | (value & 0x3F);

		if (((PSXRegs.SPU_CNT >> 6) & 1) == 0)
		{
			PSXRegs.SPU_STAT &= ~(1 << 6);
		}

		Sound.updateStatus();
		Sound.updateTransferEvent();
		return;
	}
	if (adr == 0x1AC) { PSXRegs.SPU_TRANSFER_CNT = value; return; }

	if (adr == 0x1B0) { 
		PSXRegs.SPU_CDAUDIO_VOL_L = value; return; }
	if (adr == 0x1B2) { 
		PSXRegs.SPU_CDAUDIO_VOL_R = value; return; }
	if (adr == 0x1B4) { PSXRegs.SPU_EXT_VOL_L = value; return; }
	if (adr == 0x1B6) { PSXRegs.SPU_EXT_VOL_R = value; return; }

	if (adr == 0x184) { PSXRegs.SPU_REVERB_vLOUT = value; return; }
	if (adr == 0x186) { PSXRegs.SPU_REVERB_vROUT = value; return; }
	if (adr == 0x1A2) { PSXRegs.SPU_REVERB_mBASE = value; reverbCurrentAddress = value * 4; return; }
	if (adr == 0x1C0) { PSXRegs.SPU_REVERB_dAPF1 = value; return; }
	if (adr == 0x1C2) { PSXRegs.SPU_REVERB_dAPF2 = value; return; }
	if (adr == 0x1C4) { PSXRegs.SPU_REVERB_vIIR = value; return; }
	if (adr == 0x1C6) { PSXRegs.SPU_REVERB_vCOMB1 = value; return; }
	if (adr == 0x1C8) { PSXRegs.SPU_REVERB_vCOMB2 = value; return; }
	if (adr == 0x1CA) { PSXRegs.SPU_REVERB_vCOMB3 = value; return; }
	if (adr == 0x1CC) { PSXRegs.SPU_REVERB_vCOMB4 = value; return; }
	if (adr == 0x1CE) { PSXRegs.SPU_REVERB_vWALL = value; return; }
	if (adr == 0x1D0) { PSXRegs.SPU_REVERB_vAPF1 = value; return; }
	if (adr == 0x1D2) { PSXRegs.SPU_REVERB_vAPF2 = value; return; }
	if (adr == 0x1D4) { PSXRegs.SPU_REVERB_mLSAME = value; return; }
	if (adr == 0x1D6) { PSXRegs.SPU_REVERB_mRSAME = value; return; }
	if (adr == 0x1D8) { PSXRegs.SPU_REVERB_mLCOMB1 = value; return; }
	if (adr == 0x1DA) { PSXRegs.SPU_REVERB_mRCOMB1 = value; return; }
	if (adr == 0x1DC) { PSXRegs.SPU_REVERB_mLCOMB2 = value; return; }
	if (adr == 0x1DE) { PSXRegs.SPU_REVERB_mRCOMB2 = value; return; }
	if (adr == 0x1E0) { PSXRegs.SPU_REVERB_dLSAME = value; return; }
	if (adr == 0x1E2) { PSXRegs.SPU_REVERB_dRSAME = value; return; }
	if (adr == 0x1E4) { PSXRegs.SPU_REVERB_mLDIFF = value; return; }
	if (adr == 0x1E6) { PSXRegs.SPU_REVERB_mRDIFF = value; return; }
	if (adr == 0x1E8) { PSXRegs.SPU_REVERB_mLCOMB3 = value; return; }
	if (adr == 0x1EA) { PSXRegs.SPU_REVERB_mRCOMB3 = value; return; }
	if (adr == 0x1EC) { PSXRegs.SPU_REVERB_mLCOMB4 = value; return; }
	if (adr == 0x1EE) { PSXRegs.SPU_REVERB_mRCOMB4 = value; return; }
	if (adr == 0x1F0) { PSXRegs.SPU_REVERB_dLDIFF = value; return; }
	if (adr == 0x1F2) { PSXRegs.SPU_REVERB_dRDIFF = value; return; }
	if (adr == 0x1F4) { PSXRegs.SPU_REVERB_mLAPF1 = value; return; }
	if (adr == 0x1F6) { PSXRegs.SPU_REVERB_mRAPF1 = value; return; }
	if (adr == 0x1F8) { PSXRegs.SPU_REVERB_mLAPF2 = value; return; }
	if (adr == 0x1FA) { PSXRegs.SPU_REVERB_mRAPF2 = value; return; }
	if (adr == 0x1FC) { PSXRegs.SPU_REVERB_vLIN = value; return; }
	if (adr == 0x1FE) { PSXRegs.SPU_REVERB_vRIN = value; return; }
}

UInt16 SOUND::read_reg(UInt32 adr)
{
	UInt16 retval = 0xFFFF;

	if (adr <= 0x17F) 
	{ 
		retval = PSXRegs.SPU_VOICEREGS[adr / 2]; 
	}

	if (adr >= 0x200 && adr < 0x260) { retval = PSXRegs.SPU_VOICEVOLUME[(adr - 0x200) / 4][(adr >> 1) & 1]; }

	if (adr == 0x180) { retval = PSXRegs.SPU_VOLUME_LEFT; }
	if (adr == 0x182) { retval = PSXRegs.SPU_VOLUME_RIGHT; }

	if (adr == 0x188) { retval = PSXRegs.SPU_KEYON; }
	if (adr == 0x18A) { retval = PSXRegs.SPU_KEYON >> 16; }
	if (adr == 0x18C) { retval = PSXRegs.SPU_KEYOFF; }
	if (adr == 0x18E) { retval = PSXRegs.SPU_KEYOFF >> 16; }
	if (adr == 0x190) { retval = PSXRegs.SPU_PITCHMODENA; }
	if (adr == 0x192) { retval = PSXRegs.SPU_PITCHMODENA >> 16; }
	if (adr == 0x194) { retval = PSXRegs.SPU_NOISEMODE; }
	if (adr == 0x196) { retval = PSXRegs.SPU_NOISEMODE >> 16; }
	if (adr == 0x198) { retval = PSXRegs.SPU_REVERBON; }
	if (adr == 0x19A) { retval = PSXRegs.SPU_REVERBON >> 16; }
	if (adr == 0x19C) { retval = PSXRegs.SPU_ENDX; }
	if (adr == 0x19E) { retval = PSXRegs.SPU_ENDX >> 16; }

	if (adr == 0x1A4) { retval = PSXRegs.SPU_IRQ_ADDR; }

	if (adr == 0x1A6) { retval = PSXRegs.SPU_TRANSFERADDR; }

	if (adr == 0x1AA) { retval = PSXRegs.SPU_CNT; }
	if (adr == 0x1AC) { retval = PSXRegs.SPU_TRANSFER_CNT; }
	if (adr == 0x1AE) { retval = PSXRegs.SPU_STAT; }

	if (adr == 0x1B0) { retval = PSXRegs.SPU_CDAUDIO_VOL_L; }
	if (adr == 0x1B2) { retval = PSXRegs.SPU_CDAUDIO_VOL_R; }
	if (adr == 0x1B4) { retval = PSXRegs.SPU_EXT_VOL_L; }
	if (adr == 0x1B6) { retval = PSXRegs.SPU_EXT_VOL_R; }
	if (adr == 0x1B8) { retval = PSXRegs.SPU_CURVOL_L; }
	if (adr == 0x1BA) { retval = PSXRegs.SPU_CURVOL_R; }

	if (adr == 0x184) { retval = PSXRegs.SPU_REVERB_vLOUT; }
	if (adr == 0x186) { retval = PSXRegs.SPU_REVERB_vROUT; }
	if (adr == 0x1A2) { retval = PSXRegs.SPU_REVERB_mBASE; }
	if (adr == 0x1C0) { retval = PSXRegs.SPU_REVERB_dAPF1; }
	if (adr == 0x1C2) { retval = PSXRegs.SPU_REVERB_dAPF2; }
	if (adr == 0x1C4) { retval = PSXRegs.SPU_REVERB_vIIR; }
	if (adr == 0x1C6) { retval = PSXRegs.SPU_REVERB_vCOMB1; }
	if (adr == 0x1C8) { retval = PSXRegs.SPU_REVERB_vCOMB2; }
	if (adr == 0x1CA) { retval = PSXRegs.SPU_REVERB_vCOMB3; }
	if (adr == 0x1CC) { retval = PSXRegs.SPU_REVERB_vCOMB4; }
	if (adr == 0x1CE) { retval = PSXRegs.SPU_REVERB_vWALL; }
	if (adr == 0x1D0) { retval = PSXRegs.SPU_REVERB_vAPF1; }
	if (adr == 0x1D2) { retval = PSXRegs.SPU_REVERB_vAPF2; }
	if (adr == 0x1D4) { retval = PSXRegs.SPU_REVERB_mLSAME; }
	if (adr == 0x1D6) { retval = PSXRegs.SPU_REVERB_mRSAME; }
	if (adr == 0x1D8) { retval = PSXRegs.SPU_REVERB_mLCOMB1; }
	if (adr == 0x1DA) { retval = PSXRegs.SPU_REVERB_mRCOMB1; }
	if (adr == 0x1DC) { retval = PSXRegs.SPU_REVERB_mLCOMB2; }
	if (adr == 0x1DE) { retval = PSXRegs.SPU_REVERB_mRCOMB2; }
	if (adr == 0x1E0) { retval = PSXRegs.SPU_REVERB_dLSAME; }
	if (adr == 0x1E2) { retval = PSXRegs.SPU_REVERB_dRSAME; }
	if (adr == 0x1E4) { retval = PSXRegs.SPU_REVERB_mLDIFF; }
	if (adr == 0x1E6) { retval = PSXRegs.SPU_REVERB_mRDIFF; }
	if (adr == 0x1E8) { retval = PSXRegs.SPU_REVERB_mLCOMB3; }
	if (adr == 0x1EA) { retval = PSXRegs.SPU_REVERB_mRCOMB3; }
	if (adr == 0x1EC) { retval = PSXRegs.SPU_REVERB_mLCOMB4; }
	if (adr == 0x1EE) { retval = PSXRegs.SPU_REVERB_mRCOMB4; }
	if (adr == 0x1F0) { retval = PSXRegs.SPU_REVERB_dLDIFF; }
	if (adr == 0x1F2) { retval = PSXRegs.SPU_REVERB_dRDIFF; }
	if (adr == 0x1F4) { retval = PSXRegs.SPU_REVERB_mLAPF1; }
	if (adr == 0x1F6) { retval = PSXRegs.SPU_REVERB_mRAPF1; }
	if (adr == 0x1F8) { retval = PSXRegs.SPU_REVERB_mLAPF2; }
	if (adr == 0x1FA) { retval = PSXRegs.SPU_REVERB_mRAPF2; }
	if (adr == 0x1FC) { retval = PSXRegs.SPU_REVERB_vLIN; }
	if (adr == 0x1FE) { retval = PSXRegs.SPU_REVERB_vRIN; }

	OutCapture(2, adr, retval, 0);

	return retval;
}

void SOUND::updateStatus()
{
	PSXRegs.SPU_STAT &= 0xFC7F;
	bool dmaRequest = false;

	switch ((PSXRegs.SPU_CNT >> 4) & 3)
	{
	case 0: // Stop
		break;

	case 1: // ManualWrite
		break;

	case 2: // DMAWrite
		if (fifo.empty())
		{
			PSXRegs.SPU_STAT |= 1 << 9; // write request 
			PSXRegs.SPU_STAT |= 1 << 7; // dma request
			dmaRequest = true;
		}
		break;

	case 3: // DMARead
		if (!fifo.empty())
		{
			PSXRegs.SPU_STAT |= 1 << 8; // read request
			PSXRegs.SPU_STAT |= 1 << 7; // dma request
			dmaRequest = true;
		}
		break;
	}

	DMA.requests[4] = dmaRequest;
	if (dmaRequest) DMA.trigger(4);
}

void SOUND::updateTransferEvent()
{
	if (((PSXRegs.SPU_CNT >> 4) & 3) == 0) // stopped
	{
		cmdTicks = 1;
	}
	else if (((PSXRegs.SPU_CNT >> 4) & 3) == 3) // DMARead
	{
		if (fifo.empty())
		{
#ifdef FPGACOMPATIBLE
			cmdTicks = (32 * 36) + 1;
#else
			cmdTicks = (32 * 16) + 1;
#endif
		}
		else
		{
			cmdTicks = 1;
		}
	}
	else
	{
		if (!fifo.empty())
		{
#ifdef FPGACOMPATIBLE
			cmdTicks = fifo.size() * 36;
#else
			cmdTicks = fifo.size() * 16;
#endif
		}
		else
		{
			cmdTicks = 1;
		}
	}
	if (cmdTicks > 0) PSXRegs.SPU_STAT |= 1 << 10; // spu busy
}

void SOUND::fifoWrite(UInt16 data, bool second, bool first, bool last)
{
	fifo.push_back(data);

	updateStatus();

	byte position = 0;
#ifdef DUCKCOMPATIBLE
	if (first) position = 1;
	else if (last) position = 3;
	else position = 2;
#endif

	if (second)
	{
		OutCapture(3, position, data, 1);
	}
	else
	{
		OutCapture(3, position, data, 0);
	}
#ifdef FPGACOMPATIBLE
	if (Sound.cmdTicks == 1) Sound.cmdTicks += 3;
	cmdTicks += 36;
#else
	cmdTicks += 16;
#endif

}

UInt16 SOUND::fifoRead(bool second, bool first, bool last)
{
	UInt16 data = 0;
	if (!fifo.empty())
	{
		data = fifo.front();
		fifo.pop_front();
	}

	updateStatus();
	updateTransferEvent();

	byte position = 0;
#ifdef DUCKCOMPATIBLE
	if (first) position = 1;
	else if (last) position = 3;
	else position = 2;
#endif

	if (second)
	{
		OutCapture(4, position, data, 1);
	}
	else
	{
		OutCapture(4, position, data, 0);
	}

	return data;
}

void SOUND::ramAction()
{
	if (((PSXRegs.SPU_CNT >> 4) & 3) == 3) // DMARead
	{
		for (int i = 0; i < 32; i++)
		{
			UInt16 data = *(UInt16*)&SOUNDRAM[ramTransferAddr];
			checkIRQ(ramTransferAddr);
			fifo.push_back(data);
			ramTransferAddr = (ramTransferAddr + 2) & 0x7FFFF;
		}
		updateStatus();
		PSXRegs.SPU_STAT &= ~(1 << 10); // remove busy
	}
	else
	{
		int ramcount = 0;
		while (!fifo.empty() && ramcount < 24)
		{
			UInt16 data = fifo.front();
			fifo.pop_front();
			memcpy(&SOUNDRAM[ramTransferAddr], &data, sizeof(UInt16));
			checkIRQ(ramTransferAddr);
			OutCapture(15, ramTransferAddr, data, 0);
			ramTransferAddr = (ramTransferAddr + 2) & 0x7FFFF;
			ramcount++;
			if (ramTransferAddr == 0x2BB2)
			{
				int a = 5;
			}
			if (data == 0xBC01)
			{
				int a = 5;
			}
		}
		if (fifo.empty()) PSXRegs.SPU_STAT &= ~(1 << 10); // remove busy
	}
}

void SOUND::checkIRQ(UInt32 addr)
{
	if (addr == 0x1A60)
	{
		int a = 5;
	}

	if ((((PSXRegs.SPU_CNT >> 15) & 1) == 1) && (((PSXRegs.SPU_CNT >> 6) & 1) == 1) && (((PSXRegs.SPU_STAT >> 6) & 1) == 0))
	{
		//if ((addr & 0xFFFFFFF8) == (PSXRegs.SPU_IRQ_ADDR * 8))
		if ((addr >> 3) == PSXRegs.SPU_IRQ_ADDR)
		{
#ifdef FPGACOMPATIBLE
			if (!irq_saved)
			{
				irq_saved = true;
			}
#else
			PSXRegs.SPU_STAT |= 1 << 6;
			PSXRegs.setIRQ(9);
			OutCapture(17, 0, 0, 0);
#endif
		}
	}
}

Int16 SOUND::clamp16(Int32 valueIn)
{
	Int16 value;

	if (valueIn < (Int16)0x8000) value = 0x8000; else if (valueIn > 0x7FFF) value = 0x7FFF; else value = (Int16)valueIn;

	return value;
}

byte SOUND::getAdsrRate(byte index)
{
	byte rate = 0;
	switch (voice_adsrphase[index])
	{
	case ADSRPHASE::ATTACK: rate = (PSXRegs.SPU_VOICEREGS[index * 8 + 4] >> 8) & 0x7F; break;
	case ADSRPHASE::DECAY: rate = ((PSXRegs.SPU_VOICEREGS[index * 8 + 4] >> 4) & 0xF) << 2; break;
	case ADSRPHASE::SUSTAIN: rate = (PSXRegs.SPU_VOICEREGS[index * 8 + 5] >> 6) & 0x7F; break;
	case ADSRPHASE::RELEASE: rate = ((PSXRegs.SPU_VOICEREGS[index * 8 + 5] >> 0) & 0x1F) << 2; break;
	}

	return rate;
}

UInt32 SOUND::getAdsrTicks(byte index)
{
	Uint32 ticks = 1;

	byte rate = getAdsrRate(index);
	if (rate >= 48)
	{
		ticks = 1 << ((rate >> 2) - 11);
	}

	return ticks;
}

Int32 SOUND::getAdsrStep(byte index)
{
	Int32 step;

	byte rate = getAdsrRate(index);
	bool decreasing = getAdsrDecreasing(index);

	if (rate < 48)
	{
		if (decreasing) step = (-8 + (Int32)(rate & 3)) << (11 - (rate >> 2));
		else step = (7 - (Int32)(rate & 3)) << (11 - (rate >> 2));
	}
	else
	{
		if (decreasing) step = (-8 + (Int32)(rate & 3));
		else step = (7 - (Int32)(rate & 3));
	}

	return step;
}

bool SOUND::getAdsrDecreasing(byte index)
{
	bool decreasing = true;
	switch (voice_adsrphase[index])
	{
	case ADSRPHASE::ATTACK: decreasing = false; break;
	case ADSRPHASE::SUSTAIN: decreasing = (((PSXRegs.SPU_VOICEREGS[index * 8 + 5] >> 14) & 1) == 1); break;
	}

	return decreasing;
}

bool SOUND::getAdsrExponential(byte index)
{
	bool exponential = true;
	switch (voice_adsrphase[index])
	{
	case ADSRPHASE::ATTACK: exponential = (((PSXRegs.SPU_VOICEREGS[index * 8 + 4] >> 15) & 1) == 1); break;
	case ADSRPHASE::DECAY: exponential = true; break;
	case ADSRPHASE::SUSTAIN: exponential = (((PSXRegs.SPU_VOICEREGS[index * 8 + 5] >> 15) & 1) == 1); break;
	case ADSRPHASE::RELEASE: exponential = (((PSXRegs.SPU_VOICEREGS[index * 8 + 5] >> 5) & 1) == 1); break;
	}

	return exponential;
}

void SOUND::tickADSR(byte index)
{
	if (voice_adsrWrite[index])
	{
		voice_adsrWrite[index] = false;
		voice_adsrTicks[index] = getAdsrTicks(index);
	}

	if (voice_adsrphase[index] != ADSRPHASE::OFF)
	{
		Int16 current_level = PSXRegs.SPU_VOICEREGS[index * 8 + 6];

		if (voice_adsrTicks[index] < 2)
		{
			voice_adsrTicks[index] = getAdsrTicks(index);
			Int32 step = getAdsrStep(index);
			bool exponential = getAdsrExponential(index);
			if (exponential)
			{
				bool decreasing = getAdsrDecreasing(index);
				if (decreasing)
				{
					step = (step * current_level) >> 15;
				}
				else
				{
					if (current_level >= 0x6000)
					{
						byte rate = getAdsrRate(index);
						if (rate < 40)
						{
							step >>= 2;
						}
						else if (rate >= 44)
						{
							voice_adsrTicks[index] >>= 2;
						}
						else
						{
							step >>= 1;
							voice_adsrTicks[index] >>= 1;
						}
					}
				}
			}

			if (current_level + step < 0) current_level = 0;
			else if (current_level + step > 0x7FFF) current_level = 0x7FFF;
			else current_level = current_level + step;

			PSXRegs.SPU_VOICEREGS[index * 8 + 6] = current_level;
		}
		else
		{
			voice_adsrTicks[index]--;
		}

		if (voice_adsrphase[index] != ADSRPHASE::SUSTAIN)
		{
			UInt16 adsrTarget = 0;
			switch (voice_adsrphase[index])
			{
			case ADSRPHASE::ATTACK: adsrTarget = 0x7FFF; break;
			case ADSRPHASE::DECAY: adsrTarget = ((PSXRegs.SPU_VOICEREGS[index * 8 + 4] & 0xF) + 1) * 0x800; if (adsrTarget > 0x7FFF) adsrTarget = 0x7FFF; break;
			}

			bool decreasing = getAdsrDecreasing(index);
			if ((decreasing && current_level <= adsrTarget) || (!decreasing && current_level >= adsrTarget))
			{
				switch (voice_adsrphase[index])
				{
				case ADSRPHASE::ATTACK: voice_adsrphase[index] = ADSRPHASE::DECAY; break;
				case ADSRPHASE::DECAY: voice_adsrphase[index] = ADSRPHASE::SUSTAIN; break;
				case ADSRPHASE::SUSTAIN: voice_adsrphase[index] = ADSRPHASE::SUSTAIN; break;
				case ADSRPHASE::RELEASE: voice_adsrphase[index] = ADSRPHASE::OFF; break;
				}
				voice_adsrTicks[index] = getAdsrTicks(index);
			}
		}
	}
}

void SOUND::decodeBlock(byte index)
{
	UInt32 addr = voice_currentAddr[index] * 8;
	checkIRQ(addr);
#ifdef DUCKCOMPATIBLE
	checkIRQ((addr + 8) & 0x7FFFF); // from duckstation
#endif
	byte shiftfilter = SOUNDRAM[addr];
	byte voice_AdpcmShift = shiftfilter & 0xF;
	if (voice_AdpcmShift > 12) voice_AdpcmShift = 9;
	byte voice_AdpcmFilter = (shiftfilter >> 4) & 7;
	if (voice_AdpcmFilter > 4) voice_AdpcmFilter = 4;

	SByte filter_pos = filtertablePos[voice_AdpcmFilter];
	SByte filter_neg = filtertableNeg[voice_AdpcmFilter];

	byte start = voice_AdpcmDecodePtr[index];
	byte stop = start + 4;
	for (int i = start; i < stop; i++)
	{
		UInt32 addr = (voice_currentAddr[index] * 8) + 2 + (i / 2);
		byte nibble = SOUNDRAM[addr];
#ifdef FPGACOMPATIBLE
		checkIRQ(addr);
#endif
		if ((i & 1) == 1)
		{
			nibble >>= 4;
		}
		else
		{
			nibble &= 0xF;
		}

		Int16 nibble16 = ((Int16)nibble) << 12;
		Int32 sample = (Int32)(nibble16 >> voice_AdpcmShift);
		sample += (voice_AdpcmLast0[index] * filter_pos) >> 6;
		sample += (voice_AdpcmLast1[index] * filter_neg) >> 6;
		voice_AdpcmLast1[index] = voice_AdpcmLast0[index];

		// clamp
		if (sample < (Int16)0x8000) voice_AdpcmLast0[index] = 0x8000;
		else if (sample > 0x7FFF) voice_AdpcmLast0[index] = 0x7FFF;
		else voice_AdpcmLast0[index] = (Int16)sample;

		voice_AdpcmSamples[index][i + 3] = voice_AdpcmLast0[index];

		voice_AdpcmDecodePtr[index]++;
	}
}

Int16 SOUND::interpolateSample(byte index)
{
	Int16 sample;
	byte sampleIndex = voice_AdpcmSamplePos[index] >> 12;
	byte interpolIndex = (voice_AdpcmSamplePos[index] >> 4) & 0xFF;

	Int16 test = gauss[255 - interpolIndex];

	Int32 sum = gauss[255 - interpolIndex] * (Int32)voice_AdpcmSamples[index][sampleIndex];
	sum += gauss[511 - interpolIndex] * (Int32)voice_AdpcmSamples[index][sampleIndex + 1];
	sum += gauss[256 + interpolIndex] * (Int32)voice_AdpcmSamples[index][sampleIndex + 2];
	sum += gauss[0 + interpolIndex] * (Int32)voice_AdpcmSamples[index][sampleIndex + 3];

	sample = sum >> 15;
	// no interpolation
	//sample = voice_AdpcmSamples[index][sampleIndex + 3];

	return sample;
}

void SOUND::envVolume(byte index, byte right)
{
	UInt16 setting;

	if (index < 24) setting = PSXRegs.SPU_VOICEREGS[index * 8 + right];
	else
	{
		if (right) setting = PSXRegs.SPU_VOLUME_RIGHT;
		else setting = PSXRegs.SPU_VOLUME_LEFT;
	}

	Int16 volume;

	if (((setting >> 15) & 1) == 0)
	{
		volume = (setting & 0x7FFF) * 2;
		voice_envelopeTicks[index][right] = -1;
	}
	else
	{
		if (index < 24) volume = PSXRegs.SPU_VOICEVOLUME[index][right];
		else
		{
			if (right) volume = PSXRegs.SPU_CURVOL_R;
			else volume = PSXRegs.SPU_CURVOL_L;
		}

		byte rate = setting & 0x7F;

		Uint32 ticks = 1;
		if (rate >= 48)
		{
			ticks = 1 << ((rate >> 2) - 11);
		}
		if (voice_envelopeTicks[index][right] == -1)
		{
			voice_envelopeTicks[index][right] = ticks;
		}

		if (voice_envelopeTicks[index][right] < 2)
		{
			bool decreasing = ((setting >> 13) & 1) == 1;
			bool exponential = ((setting >> 14) & 1) == 1;

			Int32 step;
			if (rate < 48)
			{
				if (decreasing) step = (-8 + (Int32)(rate & 3)) << (11 - (rate >> 2));
				else step = (7 - (Int32)(rate & 3)) << (11 - (rate >> 2));
			}
			else
			{
				if (decreasing) step = (-8 + (Int32)(rate & 3));
				else step = (7 - (Int32)(rate & 3));
			}

			voice_envelopeTicks[index][right] = ticks;

			if (exponential)
			{
				bool decreasing = getAdsrDecreasing(index);
				if (decreasing)
				{
					step = (step * volume) >> 15;
				}
				else
				{
					if (volume >= 0x6000)
					{
						byte rate = getAdsrRate(index);
						if (rate < 40)
						{
							step >>= 2;
						}
						else if (rate >= 44)
						{
							voice_envelopeTicks[index][right] >>= 2;
						}
						else
						{
							step >>= 1;
							voice_envelopeTicks[index][right] >>= 1;
						}
					}
				}
			}

			if (volume + step < 0) volume = 0;
			else if (volume + step > 0x7FFF) volume = 0x7FFF;
			else volume = volume + step;

			//if ((decreasing && volume <= 0) || (!decreasing && volume >= 0x7FFF)) -> not required, it will just clamp forever
		}
		else
		{
			voice_envelopeTicks[index][right]--;
		}

		// todo: sweep phase -> not used in duckstation?
	}

	if (index < 24) PSXRegs.SPU_VOICEVOLUME[index][right] = volume;
	else
	{
		if (right) PSXRegs.SPU_CURVOL_R = volume;
		else PSXRegs.SPU_CURVOL_L = volume;
	}
}

void SOUND::calcVoice(byte index)
{
	//if (voice_adsrphase[index] == ADSRPHASE::OFF && ((PSXRegs.SPU_CNT >> 6) & 1) == 0)
	//{
	//	voice_lastvolume[index] = 0;
	//}
	//else
	{
		//if (CPU.totalticks >= 0x025e5fff)
		if (CPU.totalticks >= 0x000fc5ff && index == 0x02)
		{
			int a = 5;
		}

		byte flags = SOUNDRAM[(voice_currentAddr[index] * 8) + 1];
		bool voice_loopEnd = flags & 1;
		bool voice_loopRepeat = (flags >> 1) & 1;
		if (voice_AdpcmDecodePtr[index] == 0 && ((flags >> 2) & 1) == 1) // start - todo: ignore copy?
		{
			PSXRegs.SPU_VOICEREGS[index * 8 + 7] = voice_currentAddr[index];
		}

		byte sampleIndex = voice_AdpcmSamplePos[index] >> 12;
		if (voice_AdpcmDecodePtr[index] <= sampleIndex)
		{
			if (index == 0x02)
			{
				int a = 5;
			}
			decodeBlock(index);
		}
		else
		{
			UInt32 addr = voice_currentAddr[index] * 8;
			//checkIRQ(addr);
			byte start = voice_AdpcmDecodePtr[index];
			addr = (voice_currentAddr[index] * 8) + 2 + (start / 2);
			//checkIRQ(addr);
		}

		Int16 adsrVolume = PSXRegs.SPU_VOICEREGS[index * 8 + 6];

		Int16 sample = 0;
#ifndef FPGACOMPATIBLE
		if (adsrVolume != 0)
#endif
		{
			if (((PSXRegs.SPU_NOISEMODE >> index) & 1) == 1)
			{
				sample = noiseLevel;
			}
			else
			{
				sample = interpolateSample(index);
			}
		}

		if (voice_adsrphase[index] != ADSRPHASE::OFF) OutCapture(6, sampleIndex + (index << 8), sample, 0); // ADPCM

		Int32 volume = (adsrVolume * sample) >> 15;

		voice_lastvolume[index] = clamp16(volume);

		tickADSR(index);
		//OutCapture(8, (index << 8) | ((voice_adsrTicks[index] >> 16) & 0xFF), voice_adsrTicks[index] & 0xFFFF, 0);
		if (voice_adsrphase[index] != ADSRPHASE::OFF) OutCapture(8, (index << 8) | ((byte)voice_adsrphase[index]), voice_adsrTicks[index] & 0xFFFF, 0);
		if (voice_adsrphase[index] != ADSRPHASE::OFF) OutCapture(16, index << 8, PSXRegs.SPU_VOICEREGS[index * 8 + 6], 0);

		UInt16 step = PSXRegs.SPU_VOICEREGS[index * 8 + 2];
		if (index > 0 && (((PSXRegs.SPU_PITCHMODENA >> index) & 1) == 1)) // from previous channel
		//if (index > 0) // from previous channel
		{
			UInt16 factor = ((Int32)voice_lastvolume[index - 1]) + 0x8000;
			step = (UInt16)((((UInt32)step) * factor) >> 15);
		}
		if (step > 0x3FFF) step = 0x3FFF;
		voice_AdpcmSamplePos[index] += step;

		// sampleIndex overflow
		sampleIndex = voice_AdpcmSamplePos[index] >> 12;
		if (sampleIndex >= 28)
		{
			voice_AdpcmSamplePos[index] -= (28 << 12);
			voice_AdpcmDecodePtr[index] = 0;

			voice_AdpcmSamples[index][0] = voice_AdpcmSamples[index][28];
			voice_AdpcmSamples[index][1] = voice_AdpcmSamples[index][29];
			voice_AdpcmSamples[index][2] = voice_AdpcmSamples[index][30];

			if (index == 0xD)
			{
				int a = 5;
			}

			if (voice_loopEnd)
			{
				PSXRegs.SPU_ENDX |= 1 << index;
				voice_currentAddr[index] = PSXRegs.SPU_VOICEREGS[index * 8 + 7] & 0xFFFFFFFE;

				if (!voice_loopRepeat)
				{
					voice_adsrphase[index] = ADSRPHASE::OFF;
					PSXRegs.SPU_VOICEREGS[index * 8 + 6] = 0;
				}
			}
			else
			{
				voice_currentAddr[index] += 2;
			}
		}

#ifdef DUCKCOMPATIBLE
		if (((PSXRegs.SPU_VOICEREGS[index * 8 + 0] >> 15) & 1) == 0) envVolume(index, 0);
		if (((PSXRegs.SPU_VOICEREGS[index * 8 + 1] >> 15) & 1) == 0) envVolume(index, 1);
#endif

		Int16 volLeft = PSXRegs.SPU_VOICEVOLUME[index][0];
		Int16 volRight = PSXRegs.SPU_VOICEVOLUME[index][1];
		envVolume(index, 0);
		envVolume(index, 1);

		Int32 left = (volLeft * volume) >> 15;
		Int32 right = (volRight * volume) >> 15;
		soundleft += left;
		soundright += right;

		if (((PSXRegs.SPU_REVERBON >> index) & 1) == 1)
		{
			reverbleft += left;
			reverbright += right;
		}

		//if (voice_adsrphase[index] != ADSRPHASE::OFF)
		{
			if (PSXRegs.SPU_VOICEVOLUME[index][0] != 0) OutCapture(13, index, PSXRegs.SPU_VOICEVOLUME[index][0], 0);
			if (PSXRegs.SPU_VOICEVOLUME[index][1] != 0) OutCapture(13, index + 0x100, PSXRegs.SPU_VOICEVOLUME[index][1], 0);
		}

		if (left != 0) OutCapture(7, index, left, 0);
		if (right != 0) OutCapture(7, index + 0x100, right, 0);
	}

	//if (index == 0xD) OutCapture(18, PSXRegs.SPU_VOICEREGS[index * 8 + 7], voice_currentAddr[index], 0);

	if (((PSXRegs.SPU_KEYON >> index) & 1) == 1)
	{
		voice_AdpcmDecodePtr[index] = 0;
		voice_currentAddr[index] = PSXRegs.SPU_VOICEREGS[index * 8 + 3] & 0xFFFE;
		voice_adsrphase[index] = ADSRPHASE::ATTACK;
		voice_AdpcmSamplePos[index] = 0;
		voice_AdpcmLast0[index] = 0;
		voice_AdpcmLast1[index] = 0;
		voice_adsrTicks[index] = getAdsrTicks(index);
		voice_adsrWrite[index] = false;
		// todo : more stuff here

		PSXRegs.SPU_KEYON &= ~(1 << index);
		PSXRegs.SPU_ENDX &= ~(1 << index);

		for (int i = 0; i < 3; i++)
		{
			voice_AdpcmSamples[index][i] = 0;
		}
	}

	if ((PSXRegs.SPU_KEYOFF >> index) & 1)
	{
		if (voice_adsrphase[index] != ADSRPHASE::OFF && voice_adsrphase[index] != ADSRPHASE::RELEASE)
		{
			voice_adsrphase[index] = ADSRPHASE::RELEASE;
			voice_adsrTicks[index] = getAdsrTicks(index);
		}
		PSXRegs.SPU_KEYOFF &= ~(1 << index);
	}
}

void SOUND::reverbWrite(UInt16 addr, Int16 value)
{
	UInt32 address = (addr * 4) & 0x3FFFF;
	address += reverbCurrentAddress;
	if (((address >> 18) & 1) == 1) // stay in reverb area
	{
		address += (PSXRegs.SPU_REVERB_mBASE * 4);
	}
	address = (address & 0x3FFFF) * 2;

	memcpy(&SOUNDRAM[address], &value, sizeof(Int16));
	checkIRQ(address);

	OutCapture(9, address, value, 0);
}

Int16 SOUND::reverbRead(UInt16 addr, SByte offset)
{
	UInt32 address = (addr * 4 + offset) & 0x3FFFF;
	address += reverbCurrentAddress;
	if (((address >> 18) & 1) == 1) // stay in reverb area
	{
		address += (PSXRegs.SPU_REVERB_mBASE * 4);
	}
	address = (address & 0x3FFFF) * 2;

	Int16 value = *(UInt16*)&SOUNDRAM[address];
	checkIRQ(address);

	OutCapture(10, address, value, 0);

	return value;
}

Int32 SOUND::IIASM(Int16 IIR_ALPHA, Int16 insamp)
{
	if (IIR_ALPHA == -32768)
	{
		if (insamp == -32768)
			return 0;
		else
			return insamp * -65536;
	}
	else
		return insamp * (32768 - IIR_ALPHA);
}

void SOUND::processReverb()
{
	UInt16 REVERB_mSAME;
	UInt16 REVERB_mCOMB1;
	UInt16 REVERB_mCOMB2;
	UInt16 REVERB_dSAME;
	UInt16 REVERB_mDIFF;
	UInt16 REVERB_mCOMB3;
	UInt16 REVERB_mCOMB4;
	UInt16 REVERB_dDIFF;
	UInt16 REVERB_mAPF1;
	UInt16 REVERB_mAPF2;
	Int16 REVERB_vIN;

	Int32 sample;

	if (!reverbRight)
	{
		REVERB_mSAME = PSXRegs.SPU_REVERB_mLSAME;
		REVERB_mCOMB1 = PSXRegs.SPU_REVERB_mLCOMB1;
		REVERB_mCOMB2 = PSXRegs.SPU_REVERB_mLCOMB2;
		REVERB_dSAME = PSXRegs.SPU_REVERB_dLSAME;
		REVERB_mDIFF = PSXRegs.SPU_REVERB_mLDIFF;
		REVERB_mCOMB3 = PSXRegs.SPU_REVERB_mLCOMB3;
		REVERB_mCOMB4 = PSXRegs.SPU_REVERB_mLCOMB4;
		REVERB_dDIFF = PSXRegs.SPU_REVERB_dRDIFF; // swapped
		REVERB_mAPF1 = PSXRegs.SPU_REVERB_mLAPF1;
		REVERB_mAPF2 = PSXRegs.SPU_REVERB_mLAPF2;
		REVERB_vIN = PSXRegs.SPU_REVERB_vLIN;
		sample = reverbleft;
	}
	else
	{
		REVERB_mSAME = PSXRegs.SPU_REVERB_mRSAME;
		REVERB_mCOMB1 = PSXRegs.SPU_REVERB_mRCOMB1;
		REVERB_mCOMB2 = PSXRegs.SPU_REVERB_mRCOMB2;
		REVERB_dSAME = PSXRegs.SPU_REVERB_dRSAME;
		REVERB_mDIFF = PSXRegs.SPU_REVERB_mRDIFF;
		REVERB_mCOMB3 = PSXRegs.SPU_REVERB_mRCOMB3;
		REVERB_mCOMB4 = PSXRegs.SPU_REVERB_mRCOMB4;
		REVERB_dDIFF = PSXRegs.SPU_REVERB_dLDIFF; // swapped
		REVERB_mAPF1 = PSXRegs.SPU_REVERB_mRAPF1;
		REVERB_mAPF2 = PSXRegs.SPU_REVERB_mRAPF2;
		REVERB_vIN = PSXRegs.SPU_REVERB_vRIN;
		sample = reverbright;
	}

	if (CPU.totalticks >= 0x029675ff)
	{
		int a = 5;
	}

	// todo: order is wrong
	// todo: why only 1 diff swapped?
	if (((PSXRegs.SPU_CNT >> 7) & 1) == 1)
	{
		Int16 value1 = reverbRead(REVERB_dSAME, 0);
		Int16 value2 = reverbRead(REVERB_dDIFF, 0);
		Int16 value3 = reverbRead(REVERB_mSAME, -1);
		Int16 value4 = reverbRead(REVERB_mDIFF, -1);

		Int32 wallmult = (value1 * PSXRegs.SPU_REVERB_vWALL) >> 14;
		Int32 volmult = (sample * REVERB_vIN) >> 14;
		Int16 IIR_INPUT_A = clamp16((wallmult + volmult) >> 1);

		wallmult = (value2 * PSXRegs.SPU_REVERB_vWALL) >> 14;
		volmult = (sample * REVERB_vIN) >> 14;
		Int16 IIR_INPUT_B = clamp16((wallmult + volmult) >> 1);

		Int32 IIRmultNew = (IIR_INPUT_A * PSXRegs.SPU_REVERB_vIIR) >> 14;
		Int32 IIRMultOld = IIASM(PSXRegs.SPU_REVERB_vIIR, value3) >> 14;
		Int16 IIR_A = clamp16((IIRmultNew + IIRMultOld) >> 1);

		IIRmultNew = (IIR_INPUT_B * PSXRegs.SPU_REVERB_vIIR) >> 14;
		IIRMultOld = IIASM(PSXRegs.SPU_REVERB_vIIR, value4) >> 14;
		Int16 IIR_B = clamp16((IIRmultNew + IIRMultOld) >> 1);

		reverbWrite(REVERB_mSAME, IIR_A);
		reverbWrite(REVERB_mDIFF, IIR_B);
	}

	Int16 valueOut1 = reverbRead(REVERB_mCOMB1, 0);
	Int16 valueOut2 = reverbRead(REVERB_mCOMB2, 0);
	Int16 valueOut3 = reverbRead(REVERB_mCOMB3, 0);
	Int16 valueOut4 = reverbRead(REVERB_mCOMB4, 0);

	Int32 acc_mul1 = (valueOut1 * PSXRegs.SPU_REVERB_vCOMB1) >> 14;
	Int32 acc_mul2 = (valueOut2 * PSXRegs.SPU_REVERB_vCOMB2) >> 14;
	Int32 acc_mul3 = (valueOut3 * PSXRegs.SPU_REVERB_vCOMB3) >> 14;
	Int32 acc_mul4 = (valueOut4 * PSXRegs.SPU_REVERB_vCOMB4) >> 14;
	Int32 ACC = acc_mul1 + acc_mul2 + acc_mul3 + acc_mul4;

	Int16 valueOut5 = reverbRead(REVERB_mAPF1 - PSXRegs.SPU_REVERB_dAPF1, 0);
	Int16 valueOut6 = reverbRead(REVERB_mAPF2 - PSXRegs.SPU_REVERB_dAPF2, 0);

	Int16 apf1neg;
	if (PSXRegs.SPU_REVERB_vAPF1 == 0x8000)	apf1neg = 0x7FFF; else apf1neg = 0 - PSXRegs.SPU_REVERB_vAPF1;

	Int16 apf2neg;
	if (PSXRegs.SPU_REVERB_vAPF2 == 0x8000)	apf2neg = 0x7FFF; else apf2neg = 0 - PSXRegs.SPU_REVERB_vAPF2;

	Int32 apf1negMul = (valueOut5 * apf1neg) >> 14;
	Int16 MDA = clamp16((ACC + apf1negMul) >> 1);

	Int32 apf1posMul = (MDA * PSXRegs.SPU_REVERB_vAPF1) >> 14;
	Int32 apf2negMul = (valueOut6 * apf2neg) >> 14;
	Int16 MDB = clamp16(valueOut5 + ((apf1posMul + apf2negMul) >> 1));

	Int32 apf2posMul = (MDB * PSXRegs.SPU_REVERB_vAPF2) >> 15;
	Int16 IVB = clamp16(valueOut6 + apf2posMul);

	if (((PSXRegs.SPU_CNT >> 7) & 1) == 1)
	{
		reverbWrite(REVERB_mAPF1, MDA);
		reverbWrite(REVERB_mAPF2, MDB);
	}

	if (reverbRight)
	{
		reverbLastRight = IVB;
		if (reverbCurrentAddress == 0x3FFFF)
			reverbCurrentAddress = PSXRegs.SPU_REVERB_mBASE * 4;
		else
			reverbCurrentAddress++;
	}
	else
	{
		reverbLastLeft = IVB;
	}

	reverbRight = !reverbRight;

	if (reverbLastLeft != 0 || reverbLastRight != 0)
	{
		OutCapture(11, reverbLastLeft, reverbLastRight, 0);
	}

	// apply volume
	Int32 left = (reverbLastLeft * PSXRegs.SPU_REVERB_vLOUT) >> 15;
	Int32 right = (reverbLastRight * PSXRegs.SPU_REVERB_vROUT) >> 15;

	soundleft += left;
	soundright += right;
}

void SOUND::WriteCapture(byte index, Int16 value)
{
	UInt32 ram_address = (index * 1024) | capturePosition;
	memcpy(&SOUNDRAM[ram_address], &value, sizeof(Int16));
	checkIRQ(ram_address);

	if (value != 0) OutCapture(12, ram_address, value, 0);
}

void SOUND::updateNoise()
{
	if (CPU.totalticks >= 0x000077ff)
	{
		int a = 5;
	}

	byte noiseSmall = (PSXRegs.SPU_CNT >> 8) & 3;
	switch (noiseSmall)
	{
	case 0: noiseCnt += 0; break;
	case 1: noiseCnt += 84; break;
	case 2: noiseCnt += 140; break;
	case 3: noiseCnt += 180; break;
	}

	if ((noiseCnt & 0xFFFF) >= 210)
	{
		noiseCnt += 0x10000;
		noiseCnt -= 210;
	}

	noiseCnt += 0x10000;

	UInt32 noise_shift = (PSXRegs.SPU_CNT >> 10) & 0xF;
	UInt32 level = (0x8000 >> noise_shift) << 16;

	if (noiseCnt >= level)
	{
		noiseCnt &= (level - 1);
		byte newnoise = ((noiseLevel >> 15) & 1) ^ ((noiseLevel >> 12) & 1) ^ ((noiseLevel >> 11) & 1) ^ ((noiseLevel >> 10) & 1) ^ 1;
		noiseLevel = (noiseLevel << 1) | newnoise;

		OutCapture(14, 0, noiseLevel, 0);
	}

	// auto test
	//byte noisesetting = (PSXRegs.SPU_CNT >> 8) & 0x3F;
	//noisesetting++;
	//PSXRegs.SPU_CNT = (PSXRegs.SPU_CNT & 0xC0FF) | (noisesetting << 8);
}

void SOUND::work()
{
	if (cmdTicks > 0)
	{
#ifdef FPGADMACPUBUSY
		if (!CPU.paused) 
			cmdTicks--;
#else
		cmdTicks--;
#endif
		if (cmdTicks == 0)
		{
			//#ifndef FPGACOMPATIBLE
			if (((PSXRegs.SPU_CNT >> 4) & 3) == 3) // DMARead
			{
				for (int i = 0; i < 32; i++)
				{
					UInt16 data = *(UInt16*)&SOUNDRAM[ramTransferAddr];
					checkIRQ(ramTransferAddr);
					fifo.push_back(data);
					ramTransferAddr = (ramTransferAddr + 2) & 0x7FFFF;
				}
				updateStatus();
				PSXRegs.SPU_STAT &= ~(1 << 10); // remove busy
			}
			else
			{
				PSXRegs.SPU_STAT &= ~(1 << 10); // remove busy
				while (!fifo.empty())
				{
					UInt16 data = fifo.front();
					fifo.pop_front();
					memcpy(&SOUNDRAM[ramTransferAddr], &data, sizeof(UInt16));

					if (ramTransferAddr == 0x1A60)
					{
						int a = 5;
					}

					checkIRQ(ramTransferAddr);
					//OutCapture(15, ramTransferAddr, data, 0);
					ramTransferAddr = (ramTransferAddr + 2) & 0x7FFFF;
					if (ramTransferAddr == 0x2BB2)
					{
						int a = 5;
					}
					if (data == 0xBC01)
					{
						int a = 5;
					}
				}
			}
			//#endif
		}
		updateStatus();
	}

	sampleticks++;
#ifdef FPGACOMPATIBLE
	if (irq_saved && sampleticks == 767)
	{
		irq_saved = false;
		PSXRegs.SPU_STAT |= 1 << 6;
		PSXRegs.setIRQ(9);
		OutCapture(17, 0, 0, 0);
	}
	if (sampleticks == 768)
	{
		sampleticks = 0;
		OutLast();
	}
	if (sampleticks == 2)
	{
#else
	if (sampleticks == 768)
	{
		sampleticks = 0;
#endif
		soundleft = 0;
		soundright = 0;
		reverbleft = 0;
		reverbright = 0;

		//int i = 16;
		for (int i = 0; i < 24; i++)
			//for (int i = 21; i < 24; i++)
		{
			calcVoice(i);
		}

		if (soundleft != 0)
		{
			int a = 5;
		}

		if (CPU.totalticks >= 0x037ffaff)
		{
			int a = 5;
		}

		// mute
		if (((PSXRegs.SPU_CNT >> 14) & 1) == 0)
		{
			soundleft = 0;
			soundright = 0;
		}

		// mix cd audio
		Int16 cd_left = 0;
		Int16 cd_right = 0;
		if (!CDRom.XaFifo.empty())
		{
			UInt32 value = CDRom.XaFifo.front();
			CDRom.XaFifo.pop_front();
			cd_left = (Int16)(value & 0xFFFF);
			cd_right = (Int16)((value >> 16) & 0xFFFF);
		}
		if ((PSXRegs.SPU_CNT & 1) == 1)
		{
			Int32 cd_audio_volume_left = (((Int32)cd_left) * PSXRegs.SPU_CDAUDIO_VOL_L) >> 15;
			Int32 cd_audio_volume_right = (((Int32)cd_right) * PSXRegs.SPU_CDAUDIO_VOL_R) >> 15;

			soundleft += cd_audio_volume_left;
			soundright += cd_audio_volume_right;

			if (((PSXRegs.SPU_CNT >> 2) & 1) == 1)
			{
				reverbleft += cd_audio_volume_left;
				reverbright += cd_audio_volume_right;
			}
		}

		processReverb();

		Int16 main_volume_left = PSXRegs.SPU_CURVOL_L;
		Int16 main_volume_right = PSXRegs.SPU_CURVOL_R;

		envVolume(24, 0);
		envVolume(24, 1);

		Int32 endLeft = (soundleft * main_volume_left) >> 15;
		Int32 endRight = (soundright * main_volume_right) >> 15;

		if (endLeft < (Int16)0x8000) soundleft = 0x8000; else if (endLeft > 0x7FFF) soundleft = 0x7FFF; else soundleft = (Int16)endLeft;
		if (endRight < (Int16)0x8000) soundright = 0x8000; else if (endRight > 0x7FFF) soundright = 0x7FFF; else soundright = (Int16)endRight;

		soundGenerator.fill(soundleft);
		soundGenerator.fill(soundright);

		// write to capture buffer
		WriteCapture(0, cd_left);
		WriteCapture(1, cd_right);
		WriteCapture(2, voice_lastvolume[1]);
		WriteCapture(3, voice_lastvolume[3]);

		// set capture buffer position
		capturePosition += 2;
		capturePosition &= 0x3FF;
		PSXRegs.SPU_STAT &= ~(1 << 11);
		if (capturePosition >> 9) PSXRegs.SPU_STAT |= 1 << 11; // second half of capture buffer

		updateNoise();

		OutCapture(5, 0, soundleft, 0);
		OutCapture(5, 1, soundright, 0);
	}
	}

void SOUND::saveState(UInt32 offset)
{
	psx.savestate_addvalue(offset + 0, 31, 0, cmdTicks);
	psx.savestate_addvalue(offset + 1, 31, 0, sampleticks);
	psx.savestate_addvalue(offset + 2, 15, 0, capturePosition);
	psx.savestate_addvalue(offset + 3, 15, 0, ramTransferAddr);
	psx.savestate_addvalue(offset + 4, 30, 0, reverbCurrentAddress);
	psx.savestate_addvalue(offset + 4, 31, 31, reverbRight);
	psx.savestate_addvalue(offset + 5, 15, 0, noiseLevel);
	psx.savestate_addvalue(offset + 6, 31, 0, noiseCnt);

	psx.savestate_addvalue(offset + 32, 31, 0, PSXRegs.SPU_KEYON);
	psx.savestate_addvalue(offset + 33, 31, 0, PSXRegs.SPU_KEYOFF);
	psx.savestate_addvalue(offset + 34, 31, 0, PSXRegs.SPU_PITCHMODENA);
	psx.savestate_addvalue(offset + 35, 31, 0, PSXRegs.SPU_NOISEMODE);
	psx.savestate_addvalue(offset + 36, 31, 0, PSXRegs.SPU_REVERBON);
	psx.savestate_addvalue(offset + 37, 31, 0, PSXRegs.SPU_ENDX);

	psx.savestate_addvalue(offset + 38, 15, 0, PSXRegs.SPU_VOLUME_LEFT);
	psx.savestate_addvalue(offset + 38, 31, 16, PSXRegs.SPU_VOLUME_RIGHT);
	psx.savestate_addvalue(offset + 39, 15, 0, PSXRegs.SPU_TRANSFERADDR);
	psx.savestate_addvalue(offset + 39, 31, 16, PSXRegs.SPU_CNT);
	psx.savestate_addvalue(offset + 40, 15, 0, PSXRegs.SPU_TRANSFER_CNT);
	psx.savestate_addvalue(offset + 40, 31, 16, PSXRegs.SPU_STAT);
	psx.savestate_addvalue(offset + 41, 15, 0, PSXRegs.SPU_CDAUDIO_VOL_L);
	psx.savestate_addvalue(offset + 41, 31, 16, PSXRegs.SPU_CDAUDIO_VOL_R);
	psx.savestate_addvalue(offset + 42, 15, 0, PSXRegs.SPU_EXT_VOL_L);
	psx.savestate_addvalue(offset + 42, 31, 16, PSXRegs.SPU_EXT_VOL_R);
	psx.savestate_addvalue(offset + 43, 15, 0, PSXRegs.SPU_CURVOL_L);
	psx.savestate_addvalue(offset + 43, 31, 16, PSXRegs.SPU_CURVOL_R);
	psx.savestate_addvalue(offset + 44, 15, 0, PSXRegs.SPU_IRQ_ADDR);
	psx.savestate_addvalue(offset + 44, 31, 16, PSXRegs.SPU_REVERB_vLOUT);
	psx.savestate_addvalue(offset + 45, 15, 0, PSXRegs.SPU_REVERB_vROUT);
	psx.savestate_addvalue(offset + 45, 31, 16, PSXRegs.SPU_REVERB_mBASE);
	psx.savestate_addvalue(offset + 46, 15, 0, PSXRegs.SPU_REVERB_dAPF1);
	psx.savestate_addvalue(offset + 46, 31, 16, PSXRegs.SPU_REVERB_dAPF2);
	psx.savestate_addvalue(offset + 47, 15, 0, PSXRegs.SPU_REVERB_vIIR);
	psx.savestate_addvalue(offset + 47, 31, 16, PSXRegs.SPU_REVERB_vCOMB1);
	psx.savestate_addvalue(offset + 48, 15, 0, PSXRegs.SPU_REVERB_vCOMB2);
	psx.savestate_addvalue(offset + 48, 31, 16, PSXRegs.SPU_REVERB_vCOMB3);
	psx.savestate_addvalue(offset + 49, 15, 0, PSXRegs.SPU_REVERB_vCOMB4);
	psx.savestate_addvalue(offset + 49, 31, 16, PSXRegs.SPU_REVERB_vWALL);
	psx.savestate_addvalue(offset + 50, 15, 0, PSXRegs.SPU_REVERB_vAPF1);
	psx.savestate_addvalue(offset + 50, 31, 16, PSXRegs.SPU_REVERB_vAPF2);
	psx.savestate_addvalue(offset + 51, 15, 0, PSXRegs.SPU_REVERB_mLSAME);
	psx.savestate_addvalue(offset + 51, 31, 16, PSXRegs.SPU_REVERB_mRSAME);
	psx.savestate_addvalue(offset + 52, 15, 0, PSXRegs.SPU_REVERB_mLCOMB1);
	psx.savestate_addvalue(offset + 52, 31, 16, PSXRegs.SPU_REVERB_mRCOMB1);
	psx.savestate_addvalue(offset + 53, 15, 0, PSXRegs.SPU_REVERB_mLCOMB2);
	psx.savestate_addvalue(offset + 53, 31, 16, PSXRegs.SPU_REVERB_mRCOMB2);
	psx.savestate_addvalue(offset + 54, 15, 0, PSXRegs.SPU_REVERB_dLSAME);
	psx.savestate_addvalue(offset + 54, 31, 16, PSXRegs.SPU_REVERB_dRSAME);
	psx.savestate_addvalue(offset + 55, 15, 0, PSXRegs.SPU_REVERB_mLDIFF);
	psx.savestate_addvalue(offset + 55, 31, 16, PSXRegs.SPU_REVERB_mRDIFF);
	psx.savestate_addvalue(offset + 56, 15, 0, PSXRegs.SPU_REVERB_mLCOMB3);
	psx.savestate_addvalue(offset + 56, 31, 16, PSXRegs.SPU_REVERB_mRCOMB3);
	psx.savestate_addvalue(offset + 57, 15, 0, PSXRegs.SPU_REVERB_mLCOMB4);
	psx.savestate_addvalue(offset + 57, 31, 16, PSXRegs.SPU_REVERB_mRCOMB4);
	psx.savestate_addvalue(offset + 58, 15, 0, PSXRegs.SPU_REVERB_dLDIFF);
	psx.savestate_addvalue(offset + 58, 31, 16, PSXRegs.SPU_REVERB_dRDIFF);
	psx.savestate_addvalue(offset + 59, 15, 0, PSXRegs.SPU_REVERB_mLAPF1);
	psx.savestate_addvalue(offset + 59, 31, 16, PSXRegs.SPU_REVERB_mRAPF1);
	psx.savestate_addvalue(offset + 60, 15, 0, PSXRegs.SPU_REVERB_mLAPF2);
	psx.savestate_addvalue(offset + 60, 31, 16, PSXRegs.SPU_REVERB_mRAPF2);
	psx.savestate_addvalue(offset + 61, 15, 0, PSXRegs.SPU_REVERB_vLIN);
	psx.savestate_addvalue(offset + 61, 31, 16, PSXRegs.SPU_REVERB_vRIN);

	for (int i = 0; i < 192; i++) psx.savestate_addvalue(offset + 64 + i, 15, 0, PSXRegs.SPU_VOICEREGS[i]);

	for (int i = 0; i < 24; i++)
	{
		psx.savestate_addvalue(offset + 256 +  0 + i, 15, 0, PSXRegs.SPU_VOICEVOLUME[i][0]);
		psx.savestate_addvalue(offset + 256 + 24 + i, 15, 0, PSXRegs.SPU_VOICEVOLUME[i][1]);
	}

	for (int i = 0; i < 25; i++)
	{
		psx.savestate_addvalue(offset + 320 +  0 + i, 24, 0, (UInt32)voice_envelopeTicks[i][0]);
		psx.savestate_addvalue(offset + 320 + 25 + i, 24, 0, (UInt32)voice_envelopeTicks[i][1]);
	}

	for (int i = 0; i < 24; i++)
	{
		psx.savestate_addvalue(offset + 384 + i * 4, 15, 0, voice_currentAddr[i]);
		psx.savestate_addvalue(offset + 385 + i * 4, 15, 0, voice_AdpcmLast0[i]);
		psx.savestate_addvalue(offset + 385 + i * 4, 31, 16, voice_AdpcmLast1[i]);
		psx.savestate_addvalue(offset + 386 + i * 4, 19, 0, voice_AdpcmSamplePos[i]);
		psx.savestate_addvalue(offset + 386 + i * 4, 25, 20, voice_AdpcmDecodePtr[i]);
		psx.savestate_addvalue(offset + 386 + i * 4, 28, 26, (byte)voice_adsrphase[i]);
		psx.savestate_addvalue(offset + 387 + i * 4, 23, 0, voice_adsrTicks[i]);
	}
}

void SOUND::loadState(UInt32 offset)
{
	cmdTicks = psx.savestate_loadvalue(offset + 0, 31, 0);
	//sampleticks	         = psx.savestate_loadvalue(offset + 1, 31, 0);
	capturePosition = psx.savestate_loadvalue(offset + 2, 15, 0);
	ramTransferAddr = psx.savestate_loadvalue(offset + 3, 31, 0);
	reverbCurrentAddress = psx.savestate_loadvalue(offset + 4, 30, 0);
	reverbRight = psx.savestate_loadvalue(offset + 4, 31, 31);
	noiseLevel = psx.savestate_loadvalue(offset + 5, 15, 0);
	noiseCnt = psx.savestate_loadvalue(offset + 6, 31, 0);

	PSXRegs.SPU_KEYON = psx.savestate_loadvalue(offset + 32, 31, 0);
	PSXRegs.SPU_KEYOFF = psx.savestate_loadvalue(offset + 33, 31, 0);
	PSXRegs.SPU_PITCHMODENA = psx.savestate_loadvalue(offset + 34, 31, 0);
	PSXRegs.SPU_NOISEMODE = psx.savestate_loadvalue(offset + 35, 31, 0);
	PSXRegs.SPU_REVERBON = psx.savestate_loadvalue(offset + 36, 31, 0);
	PSXRegs.SPU_ENDX = psx.savestate_loadvalue(offset + 37, 31, 0);

	PSXRegs.SPU_VOLUME_LEFT = psx.savestate_loadvalue(offset + 38, 15, 0);
	PSXRegs.SPU_VOLUME_RIGHT = psx.savestate_loadvalue(offset + 38, 31, 16);
	PSXRegs.SPU_TRANSFERADDR = psx.savestate_loadvalue(offset + 39, 15, 0);
	PSXRegs.SPU_CNT = psx.savestate_loadvalue(offset + 39, 31, 16);
	PSXRegs.SPU_TRANSFER_CNT = psx.savestate_loadvalue(offset + 40, 15, 0);
	PSXRegs.SPU_STAT = psx.savestate_loadvalue(offset + 40, 31, 16);
	PSXRegs.SPU_CDAUDIO_VOL_L = psx.savestate_loadvalue(offset + 41, 15, 0);
	PSXRegs.SPU_CDAUDIO_VOL_R = psx.savestate_loadvalue(offset + 41, 31, 16);
	PSXRegs.SPU_EXT_VOL_L = psx.savestate_loadvalue(offset + 42, 15, 0);
	PSXRegs.SPU_EXT_VOL_R = psx.savestate_loadvalue(offset + 42, 31, 16);
	PSXRegs.SPU_CURVOL_L = psx.savestate_loadvalue(offset + 43, 15, 0);
	PSXRegs.SPU_CURVOL_R = psx.savestate_loadvalue(offset + 43, 31, 16);
	PSXRegs.SPU_IRQ_ADDR = psx.savestate_loadvalue(offset + 44, 15, 0);
	PSXRegs.SPU_REVERB_vLOUT = psx.savestate_loadvalue(offset + 44, 31, 16);
	PSXRegs.SPU_REVERB_vROUT = psx.savestate_loadvalue(offset + 45, 15, 0);
	PSXRegs.SPU_REVERB_mBASE = psx.savestate_loadvalue(offset + 45, 31, 16);
	PSXRegs.SPU_REVERB_dAPF1 = psx.savestate_loadvalue(offset + 46, 15, 0);
	PSXRegs.SPU_REVERB_dAPF2 = psx.savestate_loadvalue(offset + 46, 31, 16);
	PSXRegs.SPU_REVERB_vIIR = psx.savestate_loadvalue(offset + 47, 15, 0);
	PSXRegs.SPU_REVERB_vCOMB1 = psx.savestate_loadvalue(offset + 47, 31, 16);
	PSXRegs.SPU_REVERB_vCOMB2 = psx.savestate_loadvalue(offset + 48, 15, 0);
	PSXRegs.SPU_REVERB_vCOMB3 = psx.savestate_loadvalue(offset + 48, 31, 16);
	PSXRegs.SPU_REVERB_vCOMB4 = psx.savestate_loadvalue(offset + 49, 15, 0);
	PSXRegs.SPU_REVERB_vWALL = psx.savestate_loadvalue(offset + 49, 31, 16);
	PSXRegs.SPU_REVERB_vAPF1 = psx.savestate_loadvalue(offset + 50, 15, 0);
	PSXRegs.SPU_REVERB_vAPF2 = psx.savestate_loadvalue(offset + 50, 31, 16);
	PSXRegs.SPU_REVERB_mLSAME = psx.savestate_loadvalue(offset + 51, 15, 0);
	PSXRegs.SPU_REVERB_mRSAME = psx.savestate_loadvalue(offset + 51, 31, 16);
	PSXRegs.SPU_REVERB_mLCOMB1 = psx.savestate_loadvalue(offset + 52, 15, 0);
	PSXRegs.SPU_REVERB_mRCOMB1 = psx.savestate_loadvalue(offset + 52, 31, 16);
	PSXRegs.SPU_REVERB_mLCOMB2 = psx.savestate_loadvalue(offset + 53, 15, 0);
	PSXRegs.SPU_REVERB_mRCOMB2 = psx.savestate_loadvalue(offset + 53, 31, 16);
	PSXRegs.SPU_REVERB_dLSAME = psx.savestate_loadvalue(offset + 54, 15, 0);
	PSXRegs.SPU_REVERB_dRSAME = psx.savestate_loadvalue(offset + 54, 31, 16);
	PSXRegs.SPU_REVERB_mLDIFF = psx.savestate_loadvalue(offset + 55, 15, 0);
	PSXRegs.SPU_REVERB_mRDIFF = psx.savestate_loadvalue(offset + 55, 31, 16);
	PSXRegs.SPU_REVERB_mLCOMB3 = psx.savestate_loadvalue(offset + 56, 15, 0);
	PSXRegs.SPU_REVERB_mRCOMB3 = psx.savestate_loadvalue(offset + 56, 31, 16);
	PSXRegs.SPU_REVERB_mLCOMB4 = psx.savestate_loadvalue(offset + 57, 15, 0);
	PSXRegs.SPU_REVERB_mRCOMB4 = psx.savestate_loadvalue(offset + 57, 31, 16);
	PSXRegs.SPU_REVERB_dLDIFF = psx.savestate_loadvalue(offset + 58, 15, 0);
	PSXRegs.SPU_REVERB_dRDIFF = psx.savestate_loadvalue(offset + 58, 31, 16);
	PSXRegs.SPU_REVERB_mLAPF1 = psx.savestate_loadvalue(offset + 59, 15, 0);
	PSXRegs.SPU_REVERB_mRAPF1 = psx.savestate_loadvalue(offset + 59, 31, 16);
	PSXRegs.SPU_REVERB_mLAPF2 = psx.savestate_loadvalue(offset + 60, 15, 0);
	PSXRegs.SPU_REVERB_mRAPF2 = psx.savestate_loadvalue(offset + 60, 31, 16);
	PSXRegs.SPU_REVERB_vLIN = psx.savestate_loadvalue(offset + 61, 15, 0);
	PSXRegs.SPU_REVERB_vRIN = psx.savestate_loadvalue(offset + 61, 31, 16);

	for (int i = 0; i < 192; i++) PSXRegs.SPU_VOICEREGS[i] = psx.savestate_loadvalue(offset + 64 + i, 15, 0);

	for (int i = 0; i < 24; i++)
	{
		PSXRegs.SPU_VOICEVOLUME[i][0] = psx.savestate_loadvalue(offset + 256 +  0 + i, 15, 0);
		PSXRegs.SPU_VOICEVOLUME[i][1] = psx.savestate_loadvalue(offset + 256 + 24 + i, 15, 0);
	}

	for (int i = 0; i < 25; i++)
	{
		UInt32 value = psx.savestate_loadvalue(offset + 320 + 0 + i, 24, 0);
		if (((value >> 24) & 1) == 1) voice_envelopeTicks[i][0] = -1; else voice_envelopeTicks[i][0] = value & 0xFFFFFF;
		value = psx.savestate_loadvalue(offset + 320 + 25 + i, 24, 0);
		if (((value >> 24) & 1) == 1) voice_envelopeTicks[i][1] = -1; else voice_envelopeTicks[i][1] = value & 0xFFFFFF;
	}

	for (int i = 0; i < 24; i++)
	{
		voice_currentAddr[i] = psx.savestate_loadvalue(offset + 384 + i * 4, 15, 0);
		voice_AdpcmLast0[i] = psx.savestate_loadvalue(offset + 385 + i * 4, 15, 0);
		voice_AdpcmLast1[i] = psx.savestate_loadvalue(offset + 385 + i * 4, 31, 16);
		voice_AdpcmSamplePos[i] = psx.savestate_loadvalue(offset + 386 + i * 4, 19, 0);
		voice_AdpcmDecodePtr[i] = psx.savestate_loadvalue(offset + 386 + i * 4, 25, 20);
		voice_adsrphase[i] = (ADSRPHASE)psx.savestate_loadvalue(offset + 386 + i * 4, 28, 26);
		voice_adsrTicks[i] = psx.savestate_loadvalue(offset + 387 + i * 4, 23, 0);
	}
}

void SOUND::OutWriteFile(bool writeTest)
{
#ifdef SOUNDFILEOUT
	FILE* file = fopen("R:\\debug_sound.txt", "w");

	for (int i = 0; i < debug_OutCount; i++)
	{
#ifdef FPGACOMPATIBLE
		if (debug_OutType[i] == 3) continue;
#endif

		if (debug_OutType[i] == 1) fputs("WRITEREG: ", file);
		if (debug_OutType[i] == 2) fputs("READREG: ", file);
		if (debug_OutType[i] == 3) fputs("DMAWRITE: ", file);
		if (debug_OutType[i] == 4) fputs("DMAREAD: ", file);
		if (debug_OutType[i] == 5) fputs("SAMPLEOUT: ", file);
		if (debug_OutType[i] == 6) fputs("ADPCM: ", file);
		if (debug_OutType[i] == 7) fputs("CHAN: ", file);
		if (debug_OutType[i] == 8) fputs("ADSRTICKS: ", file);
		if (debug_OutType[i] == 9) fputs("REVERBWRITE: ", file);
		if (debug_OutType[i] == 10) fputs("REVERBREAD: ", file);
		if (debug_OutType[i] == 11) fputs("REVERBSAMPLE: ", file);
		if (debug_OutType[i] == 12) fputs("CAPTURE: ", file);
		if (debug_OutType[i] == 13) fputs("ENVCHAN: ", file);
		if (debug_OutType[i] == 14) fputs("NOISE: ", file);
		if (debug_OutType[i] == 15) fputs("DMARAM: ", file);
		if (debug_OutType[i] == 16) fputs("ADSRVOLUME: ", file);
		if (debug_OutType[i] == 17) fputs("IRQ: ", file);
		if (debug_OutType[i] == 18) fputs("VOICEADDRESS: ", file);

		char buf[10];
		_itoa(debug_OutTime[i], buf, 16);
		for (int c = strlen(buf); c < 8; c++) fputc('0', file);
		fputs(buf, file);
		fputc(' ', file);
		_itoa(debug_OutAddr[i], buf, 16);
		for (int c = strlen(buf); c < 4; c++) fputc('0', file);
		fputs(buf, file);
		fputc(' ', file);
		_itoa(debug_OutData[i], buf, 16);
		for (int c = strlen(buf); c < 4; c++) fputc('0', file);
		fputs(buf, file);

		fputc('\n', file);
	}
	fclose(file);

	if (writeTest)
	{
		file = fopen("R:\\sound_test_FPSXA.txt", "w");

		for (int i = 0; i < debug_OutCount; i++)
		{
			if (debug_OutType[i] <= 4)
			{
				char buf[10];
				_itoa(debug_OutType[i], buf, 16);
				for (int c = strlen(buf); c < 2; c++) fputc('0', file);
				fputs(buf, file);
				fputc(' ', file);
				UInt32 time = debug_OutTime[i];
				time = time - (time % 768) + 600;
				_itoa(time, buf, 16);
				for (int c = strlen(buf); c < 8; c++) fputc('0', file);
				fputs(buf, file);
				fputc(' ', file);
				_itoa(debug_OutAddr[i], buf, 16);
				for (int c = strlen(buf); c < 4; c++) fputc('0', file);
				fputs(buf, file);
				fputc(' ', file);
				_itoa(debug_OutData[i], buf, 16);
				for (int c = strlen(buf); c < 4; c++) fputc('0', file);
				fputs(buf, file);

				fputc('\n', file);
			}
		}
		fclose(file);
	}
#endif
}

void SOUND::OutCapture(Byte type, UInt16 addr, UInt16 data, byte timeadd)
{
#if defined(SOUNDFILEOUT)
	if (debug_OutCount == 152758)
	{
		int a = 5;
	}

	if (debug_OutCount >= SPUFILEOUTMAX) return;

#ifdef FPGACOMPATIBLE
	if (type > 4)
	{
		debug_SampleOutAddr[debug_SampleOutCount] = addr;
		debug_SampleOutData[debug_SampleOutCount] = data;
		debug_SampleOutType[debug_SampleOutCount] = type;
		debug_SampleOutCount++;
	}
	else
	{
		debug_OutTime[debug_OutCount] = CPU.totalticks + timeadd;
		debug_OutAddr[debug_OutCount] = addr;
		debug_OutData[debug_OutCount] = data;
		debug_OutType[debug_OutCount] = type;
		debug_OutCount++;
	}
#else
	//if (!debug_SPUOutAll && type > 4) return;

	debug_OutTime[debug_OutCount] = CPU.totalticks + timeadd;
	debug_OutAddr[debug_OutCount] = addr;
	debug_OutData[debug_OutCount] = data;
	debug_OutType[debug_OutCount] = type;
	debug_OutCount++;
#endif

#endif
}

void SOUND::OutLast()
{
#if defined(SOUNDFILEOUT)
	for (int i = 0; i < debug_SampleOutCount; i++)
	{
		if (debug_OutCount >= SPUFILEOUTMAX) continue;
		debug_OutTime[debug_OutCount] = CPU.totalticks;
		debug_OutAddr[debug_OutCount] = debug_SampleOutAddr[i];
		debug_OutData[debug_OutCount] = debug_SampleOutData[i];
		debug_OutType[debug_OutCount] = debug_SampleOutType[i];
		debug_OutCount++;
	}
	debug_SampleOutCount = 0;
#endif
}

void SOUND::TEST()
{
#ifdef FPGACOMPATIBLE
	std::ifstream infile("R:\\sound_test_FPSXA.txt");
	sampleticks += 1;
#else
	std::ifstream infile("R:\\sound_test_duck.txt");
	sampleticks += 2;
#endif
	std::string line;
	UInt32 timer = 0;
	int cmdcnt = 0;
	bool fifoSecond = false;

	bool soundSpeed = false;

	soundGenerator.play(soundSpeed);

	for (int i = 0; i < 24; i++)
	{
		voice_currentAddr[i] = 0;
		voice_lastvolume[i] = 0;
		voice_AdpcmLast0[i] = 0;
		voice_AdpcmLast1[i] = 0;
		voice_AdpcmSamplePos[i] = 0;
		voice_AdpcmDecodePtr[i] = 0;
		voice_adsrphase[i] = ADSRPHASE::OFF;
		voice_adsrTicks[i] = 0;
		voice_adsrWrite[i] = 0;
		voice_envelopeTicks[i][0] = 0;
		voice_envelopeTicks[i][1] = 0;
	}
	for (int i = 0; i < 192; i++)
	{
		PSXRegs.SPU_VOICEREGS[i] = 0;
	}

	while (std::getline(infile, line))
	{
		std::string type = line.substr(0, 2);
		std::string time = line.substr(3, 8);
		std::string addr = line.substr(12, 4);
		std::string data = line.substr(17, 4);
		byte typeI = std::stoul(type, nullptr, 16);
		UInt32 timeI = std::stoul(time, nullptr, 16);
		UInt16 addrI = std::stoul(addr, nullptr, 16);
		UInt16 dataI = std::stoul(data, nullptr, 16);
		while (timer <= timeI - 1)
		{
			timer++;
			CPU.totalticks++;
			work();
		}
		if (cmdcnt == 200000)
		{
			int a = 5;
			debug_SPUOutAll = true;
		}

		bool first = addrI == 1;
		bool last = addrI == 3;

		switch (typeI)
		{
		case 1: write_reg(addrI, dataI); break;
		case 2: read_reg(addrI); break;
		case 3:
			fifoWrite(dataI, fifoSecond, first, last);
			//fifoSecond = !fifoSecond;
			break;
		case 4:
			fifoRead(fifoSecond, first, last);
			//fifoSecond = !fifoSecond;
			break;
			break;
		}
		cmdcnt++;
#ifdef FPGACOMPATIBLE
		timer++;
		CPU.totalticks++;
		work();
#endif
	}

	if (soundSpeed)
	{
		soundGenerator.play(false);
		while (soundGenerator.nextSamples.size() > 0) {}
	}

	OutWriteFile(false);
}