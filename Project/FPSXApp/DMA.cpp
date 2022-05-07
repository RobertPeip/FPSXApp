#include <algorithm>
using namespace std;
#undef NDEBUG
#include <assert.h>

#include "DMA.h"
#include "Memory.h"
#include "PSXRegs.h"
#include "GPU.h"
#include "MDEC.h"
#include "Sound.h"
#include "CPU.h"
#include "CDROM.h"
#include "Joypad.h"
#include "psx.h"

Dma DMA;

void Dma::reset()
{
	dmaState = DMASTATE::OFF;
	waitcnt = 0;
	on = false;
	paused = false;
	gpupaused = false;

	patchStep = 0;

	for (int i = 0; i < 7; i++)
	{
		requests[i] = false;
		requestsPending[i] = false;
		timeupPending[i] = false;
		pauseCnt[i] = 0;
		lastStates[i] = DMASTATE::OFF;
	}
	dmaOn = false;
}


void Dma::work()
{
	if (on && !paused)
	{
		Memory.requestData = false;
		Memory.requestWrite = false;
		Memory.requestCode = false;
		dmaTime = dmaTime + 1;

		for (int i = 0; i < 7; i++)
		{
			if (timeupPending[i]) pauseCnt[i]++;
		}

		if (waitcnt > 0)
		{
			waitcnt--;
		}
		else
		{
			switch (dmaState)
			{
			case DMASTATE::OFF:
				on = false;
				break;

			case DMASTATE::WAITING:
				dmaState = DMASTATE::OFF; // default

				switch ((PSXRegs.D_CHCR[activeChannel] >> 9) & 3) // sync mode
				{
				case 0: // Manual
					wordcount = PSXRegs.D_BCR[activeChannel] & 0xFFFF;
					if (wordcount == 0) wordcount = 0x10000;
					dmaState = DMASTATE::READING;
					break;

				case 1: // request
					dmaState = DMASTATE::READING;
					wordcount = PSXRegs.D_BCR[activeChannel] & 0xFFFF;
					if (activeChannel == 1) MDEC.MDECOutCapture(9, 0, wordcount);
					break;

				case 2: // linked list
					dmaState = DMASTATE::READHEADER;
					break;

				case 3: // unused
					_wassert(_CRT_WIDE("DMA sync mode 3 -> unused"), _CRT_WIDE("DMA"), activeChannel);
					waitcnt = 4;
					break;
				}
				break;

			case DMASTATE::READHEADER:
				REP_target += 16;
				data = *(UInt32*)&Memory.RAM[PSXRegs.D_MADR[activeChannel] & 0x001FFFFC];
				wordcount = data >> 24;
				nextAddr = data & 0xFFFFFF;

				{
					for (int i = 0; i < 1024; i++) linkedListDebug[i] = 0;
					linkedListDebug[0] = PSXRegs.D_MADR[activeChannel];
					int index = 1;
					UInt32 next = data;
					while (index < 1024)
					{
						linkedListDebug[index] = next;
						if (next & 0x800000 || next == 0) break;
						next = *(UInt32*)&Memory.RAM[next & 0x001FFFFC];
						next &= 0xFFFFFF;
						index++;
						if (next == 0xFF45C)
						{
							int a = 5;
						}
					}
				}

				if (PSXRegs.D_MADR[activeChannel] == nextAddr)
				{
					int a = 5;
					nextAddr = 0;
				}

				//if (pauseCnt[activeChannel] > 0) pauseCnt[activeChannel]--;
#ifdef VRAMFILEOUT
				if (GPU.debug_VramOutCount < VramOutCountMAX)
				{
					if (GPU.debug_VramOutCount >= 157663)
					{
						int a = 5;
					}

					GPU.debug_VramOutTime[GPU.debug_VramOutCount] = nextAddr;
					GPU.debug_VramOutAddr[GPU.debug_VramOutCount] = wordcount;
					GPU.debug_VramOutData[GPU.debug_VramOutCount] = dmaTime;
					GPU.debug_VramOutType[GPU.debug_VramOutCount] = 3;
					GPU.debug_VramOutCount++;
				}
#endif
				if (wordcount > 16)
				{
					int a = 5;
				}
				if (wordcount > 0)
				{
					PSXRegs.D_MADR[activeChannel] += 4;
					dmaState = DMASTATE::READING;
					waitcnt = 4;
				}
				else if (nextAddr & 0x800000 || nextAddr == 0 || ((PSXRegs.D_CHCR[activeChannel] & 1) == 0))
				{
					dmaState = DMASTATE::STOPPING;
				}
				else
				{
					PSXRegs.D_MADR[activeChannel] = nextAddr;
					if (dmaTime >= maxDMATime)
					{
						lastStates[activeChannel] = dmaState;
						dmaState = DMASTATE::TIMEUP;
						paused = true;
						waitcnt = 0;
						pauseTime = 0;
					}
					else
					{
#ifdef FPGACOMPATIBLE
						dmaState = DMASTATE::GPU_PAUSING;
#else
						waitcnt = 9;
#endif
					}
				}
				break;

			case DMASTATE::READING:
			{
				REP_target += 1;
				bool toDevice = PSXRegs.D_CHCR[activeChannel] & 1;
				if (activeChannel == 0 && toDevice) //MDEC IN
				{
					data = *(UInt32*)&Memory.RAM[PSXRegs.D_MADR[activeChannel] & 0x001FFFFC];
					if (data != 0)
					{
						int a = 5;
					}
					MDEC.MDECOutCapture(10, 0, data);
					MDEC.CMDWrite(data);
				}
				else if (activeChannel == 1 && !toDevice) //MDEC OUT
				{
					data = MDEC.fifoRead(true);
					memcpy(&Memory.RAM[PSXRegs.D_MADR[activeChannel] & 0x001FFFFC], &data, sizeof(UInt32));
					CPU.updateCache(PSXRegs.D_MADR[activeChannel] & 0x001FFFFC, data, 0xF);
				}
				else if (activeChannel == 2) // GPU
				{
					if (toDevice)
					{
						data = *(UInt32*)&Memory.RAM[PSXRegs.D_MADR[activeChannel] & 0x001FFFFC];
						GPU.GP0Write(data);
					}
					else
					{
						data = GPU.ReadVRAMtoCPU();
						memcpy(&Memory.RAM[PSXRegs.D_MADR[activeChannel] & 0x001FFFFC], &data, sizeof(UInt32));
						CPU.updateCache(PSXRegs.D_MADR[activeChannel] & 0x001FFFFC, data, 0xF);
						REP_target += 2;
					}
				}
				else if (activeChannel == 3 && !toDevice) // CDROM
				{
					data = 0;
					for (int i = 0; i < 4; i++)
					{
						data |= CDRom.readDMA() << (i * 8);
					}
					memcpy(&Memory.RAM[PSXRegs.D_MADR[activeChannel] & 0x001FFFFC], &data, sizeof(UInt32));
					CPU.updateCache(PSXRegs.D_MADR[activeChannel] & 0x001FFFFC, data, 0xF);
					// missing status register update?
#ifdef FPGACOMPATIBLE
					REP_target += 3;
#endif
				}
				else if (activeChannel == 4) // SPU
				{
					if (toDevice)
					{
						data = *(UInt32*)&Memory.RAM[PSXRegs.D_MADR[activeChannel] & 0x001FFFFC];
						if (Sound.cmdTicks == 0) Sound.cmdTicks += 1;
						Sound.fifoWrite(data & 0xFFFF, false, false, false);
						Sound.fifoWrite(data >> 16, true, false, false);
					}
					else
					{
						// todo: add sound DMA read
						data = Sound.fifoRead(false, false, false);
						data |= Sound.fifoRead(true, false, false) << 16;
						memcpy(&Memory.RAM[PSXRegs.D_MADR[activeChannel] & 0x001FFFFC], &data, sizeof(UInt32));
						CPU.updateCache(PSXRegs.D_MADR[activeChannel] & 0x001FFFFC, data, 0xF);
					}
					waitcnt = 1;
					REP_target += 1;
				}
				else if (activeChannel == 5 && !toDevice) // PIO
				{
					data = 0xFFFFFFFF;
					memcpy(&Memory.RAM[PSXRegs.D_MADR[activeChannel] & 0x001FFFFC], &data, sizeof(UInt32));
					CPU.updateCache(PSXRegs.D_MADR[activeChannel] & 0x001FFFFC, data, 0xF);
				}
				else if (activeChannel == 6 && !toDevice) //OTC
				{
					data = (PSXRegs.D_MADR[activeChannel] & 0x001FFFFC) - 4;
					if (wordcount == 1) data = 0xFFFFFF;
					memcpy(&Memory.RAM[PSXRegs.D_MADR[activeChannel] & 0x001FFFFC], &data, sizeof(UInt32));
					CPU.updateCache(PSXRegs.D_MADR[activeChannel] & 0x001FFFFC, data, 0xF);
					REP_target += 2;
				}
				else
				{
					_wassert(_CRT_WIDE("DMA channel missing/wrong mode"), _CRT_WIDE("DMA"), activeChannel);
				}

				if ((((PSXRegs.D_CHCR[activeChannel] >> 9) & 3) < 2) && ((PSXRegs.D_CHCR[activeChannel] >> 1) & 1))
					PSXRegs.D_MADR[activeChannel] -= 4;
				else
					PSXRegs.D_MADR[activeChannel] += 4;

				wordcount--;

				switch ((PSXRegs.D_CHCR[activeChannel] >> 9) & 3) // sync mode
				{
				case 0: // Manual
					if (wordcount == 0)
					{
						dmaState = DMASTATE::STOPPING;
					}
					break;

				case 1: // request
					if (wordcount == 0)
					{
						UInt16 blockcount = PSXRegs.D_BCR[activeChannel] >> 16;
						blockcount--;
						PSXRegs.D_BCR[activeChannel] = (PSXRegs.D_BCR[activeChannel] & 0xFFFF) | blockcount << 16;
						if (blockcount == 0)
						{
							dmaState = DMASTATE::STOPPING;
						}
						else
						{
							wordcount = PSXRegs.D_BCR[activeChannel] & 0xFFFF;
							if (!requests[activeChannel])
							{
								dmaState = DMASTATE::PAUSING;
							}
							else if (dmaTime >= (maxDMATime - 1))
							{
								lastStates[activeChannel] = dmaState;
								wordcounts[activeChannel] = wordcount;
								dmaState = DMASTATE::TIMEUP;
								paused = true;
								waitcnt = 1;
								pauseTime = 0;
							}
						}
					}
					break;

				case 2: // linked list
					if (wordcount == 0)
					{
						if (nextAddr & 0x800000)
						{
							dmaState = DMASTATE::STOPPING;
						}
						else
						{
							if (dmaTime >= maxDMATime)
							{
								lastStates[activeChannel] = DMASTATE::READHEADER;
								dmaState = DMASTATE::TIMEUP;
								paused = true;
								waitcnt = 1;
								pauseTime = 0;
							}
							else if ((PSXRegs.GPUSTAT >> 25) & 1)
							{
#ifdef FPGACOMPATIBLE
								dmaState = DMASTATE::GPU_PAUSING;
#else
								dmaState = DMASTATE::READHEADER;
								waitcnt = 10;
#endif
							}
							else
							{
								waitcnt = 1;
#ifdef FPGACOMPATIBLE
								dmaState = DMASTATE::GPU_PAUSING;
#else
								dmaState = DMASTATE::GPUBUSY;
								paused = true;
#endif
							}
							PSXRegs.D_MADR[activeChannel] = nextAddr;
						}
					}
					break;
				}
#ifdef FPGACOMPATIBLE
				if (!toDevice) waitcnt++;
#endif
			}
			break;

			case DMASTATE::WRITING:
				dmaState = DMASTATE::READING;
				break;

			case DMASTATE::STOPPING:
#ifdef REPRODUCIBLEDMATIMING
				if (REP_counter > REP_target)
#endif
				{
					dmaState = DMASTATE::OFF;
					PSXRegs.D_CHCR[activeChannel] &= 0xFEFFFFFF; // clear start

					if (DMA.patchStep == 2)
					{
						CPU.writebackData = PSXRegs.D_CHCR[activeChannel];
					}
					DMA.patchStep = 0;

					on = false;
					pauseCnt[activeChannel] = 0;
					if ((PSXRegs.DICR >> (16 + activeChannel)) & 1)
					{
						if ((PSXRegs.DICR >> 24) == 0x0C)
						{
							int a = 5;
						}

						PSXRegs.DICR |= 1 << (activeChannel + 24);
						if (updateMasterFlag())
							PSXRegs.setIRQ(3);
					}

					if (gpupaused)
					{
						on = true;
						paused = true;
						dmaState = DMASTATE::GPUBUSY;
						activeChannel = 2;
						gpupaused = false;
					}
					else
					{
						for (int i = 0; i < 7; i++)
						{
							if (requestsPending[i])
							{
								trigger(i);
								break;
							}
							if (timeupPending[i])
							{
								timeupPending[i] = false;
								activeChannel = i;
								on = true;
								paused = true;
								dmaState = DMASTATE::TIMEUP;
								break;
							}
						}
					}
				}
				break;

			case DMASTATE::PAUSING:
#ifdef REPRODUCIBLEDMATIMING
				if (REP_counter > REP_target)
#endif
				{
					dmaState = DMASTATE::OFF;
					on = false;
					for (int i = 0; i < 7; i++)
					{
						if (requestsPending[i])
						{
							trigger(i);
							break;
						}
						if (timeupPending[i])
						{
							timeupPending[i] = false;
							activeChannel = i;
							on = true;
							paused = true;
							dmaState = DMASTATE::TIMEUP;
							break;
						}
					}
				}
				break;

			case DMASTATE::GPU_PAUSING:
#ifdef REPRODUCIBLEDMATIMING
				if (REP_counter > REP_target)
#endif
				{
					dmaState = DMASTATE::GPUBUSY;
					paused = true;
					DMA.dmaEndWait = 0;
				}
			}
		}

		REP_counter++;
	}
}

void Dma::check_unpause()
{
	if (waitcnt > 0)
	{
		waitcnt--;
	}
	else
	{
		switch (dmaState)
		{
		case DMASTATE::GPUBUSY:
			if ((PSXRegs.GPUSTAT >> 25) & 1)
			{
#ifdef FPGACOMPATIBLE
				if (DMA.dmaEndWait < 4) return;
				dmaState = DMASTATE::WAITING;
				REP_target = 32;
#else
				dmaState = DMASTATE::READHEADER;
#endif
				waitcnt = 9 ;
				paused = false;
				dmaTime = 0;
				patchStep = 1;
#ifdef ENDLESSDMA
				maxDMATime = 1000000;
#else
				maxDMATime = 1000;
#endif
				if (Joypad.transmitWait > 0) maxDMATime = 100;
			}
			else
			{
				if (dmaState == DMASTATE::OFF || dmaState == DMASTATE::GPUBUSY)
				{
					for (int i = 0; i < 7; i++)
					{
						if (requestsPending[i])
						{
							trigger(i);
							break;
					}
				}
			}
			}
			break;

		case DMASTATE::TIMEUP:
			if (pauseTime + pauseCnt[activeChannel] < 100)
			{
				pauseTime++;
			}
			else
			{
				if (pauseCnt[activeChannel] < 100) pauseCnt[activeChannel] = 0;
				else pauseCnt[activeChannel] -= 100;
				dmaState = lastStates[activeChannel];
				wordcount = wordcounts[activeChannel];
				paused = false;
				dmaTime = 0;
				waitcnt = 9;
#ifdef ENDLESSDMA
				maxDMATime = 1000000;
#else
				maxDMATime = 1000;
#endif
				if (Joypad.transmitWait > 0) maxDMATime = 100;
				if (pauseCnt[activeChannel] == 0)
				{
					pauseCnt[activeChannel] = 1;
				}
			}
			break;
		}
	}
}

void Dma::trigger(byte channel)
{
	if ((PSXRegs.DPCR >> (channel * 4) + 3) & 1) // enable
	{
		if ((PSXRegs.D_CHCR[channel] >> 24) & 1) // start/busy
		{
			if (on && channel == activeChannel) return;

			patchStep = 0;

			if (dmaState == DMASTATE::GPUBUSY)
			{
				gpupaused = true;
				dmaState = DMASTATE::OFF;
				paused = false;
			}

			if (dmaState == DMASTATE::TIMEUP)
			{
				timeupPending[activeChannel] = true;
				dmaState = DMASTATE::OFF;
				paused = false;
			}
			
			if (dmaState != DMASTATE::OFF)
			{
				requestsPending[channel] = true;
			}
			else
			{
				// if (((PSXRegs.D_CHCR[channel] >> 9) & 3) == 0 || !halted) don't start if halted in nonsync mode?
				dmaState = DMASTATE::WAITING;

				waitcnt = 8;
#ifdef FPGACOMPATIBLE
				waitcnt++;
				paused = false;
#endif
				REP_target = 32;
				on = true;
				activeChannel = channel;
				dmaTime = 0;
				requests[channel] = true;
				requestsPending[channel] = false;
				timeupPending[channel] = false;

				PSXRegs.D_CHCR[activeChannel] &= 0xEFFFFFFF; // clear start/trigger

				if (channel == 4) PSXRegs.SPU_STAT |= 1 << 10; // hack: must set spu busy instantly

#ifdef ENDLESSDMA
				maxDMATime = 1000000;
#else
				maxDMATime = 1000;
#endif
				if (Joypad.transmitWait > 0) maxDMATime = 100;
			}
		}
	}
}

bool Dma::updateMasterFlag()
{
	bool masterEnable = ((PSXRegs.DICR >> 23) & 1);
	byte IRQEnables = (PSXRegs.DICR >> 16) & 0x7F;
	byte IRQFlags = (PSXRegs.DICR >> 24) & 0x7F;
	bool masterFlag = ((PSXRegs.DICR >> 15) & 1) || (masterEnable && (IRQEnables & IRQFlags) != 0); // force bit not used in duckstation, why?
	PSXRegs.DICR &= ~(1 << 31);
	PSXRegs.DICR |= masterFlag << 31;
	return masterFlag;
}

void Dma::saveState(UInt32 offset)
{
	psx.savestate_addvalue(offset + 0, 31,  0, nextAddr);
	psx.savestate_addvalue(offset + 1, 31,  0, wordcount);
	psx.savestate_addvalue(offset + 2, 15,  0, pauseTime);
	psx.savestate_addvalue(offset + 2, 23, 16, activeChannel);
	psx.savestate_addvalue(offset + 2, 31, 24, waitcnt);
	psx.savestate_addvalue(offset + 3, 31,  0, wordcount);
	psx.savestate_addvalue(offset + 4,  7,  0, (byte)dmaState);
	psx.savestate_addvalue(offset + 4,  8,  8, on);
	psx.savestate_addvalue(offset + 4,  9,  9, paused);
	psx.savestate_addvalue(offset + 4, 10, 10, gpupaused);

	for (int i = 0; i < 7; i++) psx.savestate_addvalue(offset + 5 + i, 31, 0, pauseCnt[i]);
	for (int i = 0; i < 7; i++) psx.savestate_addvalue(offset + 12 + i, 31, 0, wordcounts[i]);
	for (int i = 0; i < 7; i++)
	{
		psx.savestate_addvalue(offset + 19 + i,  7,  0, (byte)lastStates[i]);
		psx.savestate_addvalue(offset + 19 + i,  8,  8, requests[i]);
		psx.savestate_addvalue(offset + 19 + i,  9,  9, requestsPending[i]);
		psx.savestate_addvalue(offset + 19 + i, 10, 10, timeupPending[i]);
	}

	psx.savestate_addvalue(offset + 26, 31, 0, PSXRegs.DPCR);
	psx.savestate_addvalue(offset + 27, 31, 0, PSXRegs.DICR);
	for (int i = 0; i < 7; i++) psx.savestate_addvalue(offset + 28 + i, 31, 0, PSXRegs.D_MADR[i]);
	for (int i = 0; i < 7; i++) psx.savestate_addvalue(offset + 35 + i, 31, 0, PSXRegs.D_BCR[i]);
	for (int i = 0; i < 7; i++) psx.savestate_addvalue(offset + 42 + i, 31, 0, PSXRegs.D_CHCR[i]);

}

void Dma::loadState(UInt32 offset)
{
	nextAddr       = psx.savestate_loadvalue(offset + 0, 31,  0); 
	wordcount	   = psx.savestate_loadvalue(offset + 1, 31,  0); 
	pauseTime	   = psx.savestate_loadvalue(offset + 2, 15,  0); 
	activeChannel  = psx.savestate_loadvalue(offset + 2, 23, 16); 
	waitcnt		   = psx.savestate_loadvalue(offset + 2, 31, 24); 
	wordcount	   = psx.savestate_loadvalue(offset + 3, 31,  0); 
	dmaState       = (DMASTATE)psx.savestate_loadvalue(offset + 4, 7, 0); 
	on			   = psx.savestate_loadvalue(offset + 4, 8,   8); 
	paused		   = psx.savestate_loadvalue(offset + 4, 9,   9); 
	gpupaused	   = psx.savestate_loadvalue(offset + 4, 10, 10); 

	for (int i = 0; i < 7; i++) pauseCnt[i] = psx.savestate_loadvalue(offset + 5 + i, 31, 0);
	for (int i = 0; i < 7; i++) wordcounts[i] = psx.savestate_loadvalue(offset + 12 + i, 31, 0);
	for (int i = 0; i < 7; i++)
	{
		lastStates[i] = (DMASTATE)psx.savestate_loadvalue(offset + 19 + i, 7, 0);
		requests[i] = psx.savestate_loadvalue(offset + 19 + i, 8, 8);
		requestsPending[i] = psx.savestate_loadvalue(offset + 19 + i, 9, 9);
		timeupPending[i] = psx.savestate_loadvalue(offset + 19 + i, 10, 10);
	}

	PSXRegs.DPCR = psx.savestate_loadvalue(offset + 26, 31, 0);
	PSXRegs.DICR = psx.savestate_loadvalue(offset + 27, 31, 0);
	for (int i = 0; i < 7; i++) PSXRegs.D_MADR[i] = psx.savestate_loadvalue(offset + 28 + i, 31, 0);
	for (int i = 0; i < 7; i++) PSXRegs.D_BCR[i] = psx.savestate_loadvalue(offset + 35 + i, 31, 0);
	for (int i = 0; i < 7; i++) PSXRegs.D_CHCR[i] = psx.savestate_loadvalue(offset + 42 + i, 31, 0);

#ifdef ENDLESSDMA
	maxDMATime = 1000000;
#else
	maxDMATime = 1000;
#endif
}