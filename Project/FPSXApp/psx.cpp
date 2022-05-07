#include "psx.h"
#include "CPU.h"
#include "GTE.h"
#include "Memory.h"
#include "PSXRegs.h"
#include "GPU.h"
#include "GPU_Timing.h"
#include "Sound.h"
#include "Joypad.h"
#include "gpio.h"
#include "DMA.h"
#include "FileIO.h"
#include "GTE.h"
#include "MDEC.h"
#include "CDROM.h"
#include "Timer.h"

Psx psx;

void Psx::reset()
{
	Memory.reset(filename);
	CPU.reset();
	PSXRegs.reset();
	GPU.reset();
	GPU_Timing.reset();
	DMA.reset();
	Sound.reset();
	Joypad.reset();
	Gte.reset();
	MDEC.reset();
	CDRom.reset();
	Timer.reset();

	loading_state = false;
	coldreset = false;

	FILE* file = fopen("R:\\debug_log.txt", "w"); fclose(file);

#ifdef MDECDEBUGOUT
	file = fopen("R:\\debug_mdec_RL.txt", "w"); fclose(file);
	file = fopen("R:\\debug_mdec_IDCT.txt", "w"); fclose(file);
	file = fopen("R:\\debug_mdec_COLOR.txt", "w"); fclose(file);
	file = fopen("R:\\debug_mdec_FIFOOUT.txt", "w"); fclose(file);
#endif

	pause = false;

	if (SDL_LockMutex(psxlock) == 0)
	{
		on = true;
		SDL_UnlockMutex(psxlock);
	}
}

void Psx::run()
{
	reset();

	UInt16 checkcount = 0;

	if (do_loadstate)
	{
		load_savestate();
		do_loadstate = false;
	}

	while (true)
	{
#if DEBUG
#ifdef FPGACOMPATIBLE
		if (CPU.stall == 0 && CPU.emptyPipe == 0 && (!DMA.on || (DMA.paused && DMA.waitcnt == 0)))
#else
		if (CPU.stall == 0 && CPU.emptyPipe == 0 && !CPU.writeDoneException && (!CPU.writebackException || CPU.lastExcIRQ) && (!DMA.on || (DMA.paused && DMA.waitcnt == 0)))
#endif

		{
			if (CPU.commands == 000000 && CPU.runmoretrace == -1)
			{
				//Gte.GTETest();
				//CDRom.CDTEST();
				//MDEC.MDECTEST();
				//GPU.GPUTEST();
				Sound.TEST();
				//Joypad.padTest();
				CPU.traclist_ptr = 0;
				CPU.runmoretrace = 3000000000;
				CPU.debug_outdiv = 1;
				if (CPU.commands > 0)
				{
					CPU.cop0_SR_next = CPU.cop0_SR;
					CPU.totalticks = 1;
				}
			}

			if (CPU.runmoretrace > 0 && CPU.debug_outdivcnt == 0)
			{
				if (CPU.traclist_ptr < CPU.Tracelist_Length) CPU.Tracelist[CPU.traclist_ptr].update();
				CPU.runmoretrace = CPU.runmoretrace - 1;
				CPU.traclist_ptr++;
				if (CPU.runmoretrace == 0)
				{
					//Memory.test_datacache();
					//GPU.VramOutWriteFile(true);
					//Gte.GTEoutWriteFile(true);
					//MDEC.MDECOutWriteFile(true);
					//CDRom.CDOutWriteFile(true);
					//Joypad.padOutWriteFile(true);
					//Sound.OutWriteFile(true);
 					//CPU.trace_file_last();
					int a = 5;
				}
			}
			CPU.debug_outdivcnt = (CPU.debug_outdivcnt + 1) % CPU.debug_outdiv;

			if (CPU.pc == 0xBFC02BB8)
			{
				int a = 5;
			}

			if (CPU.traclist_ptr == 1247412)
			//if (CPU.traclist_ptr == 0x0042dcf9)
			{
 				int xx = 0;
			}
			UInt32 debug_outdiv = 1;
			
			bool dosavestate = false;
			if (dosavestate)
			{
				if (CPU.commands >= 1900000)
				{
					if (CPU.stall == 0 && GPU.isSSIdle() && (!DMA.on || DMA.paused) && MDEC.isSSIdle() && Sound.fifo.empty() && CDRom.isSSIdle() && ((PSXRegs.I_STATUS & PSXRegs.I_MASK) == 0))
					{
						create_savestate();
						string savefilename = filename.substr(filename.find_last_of("/\\") + 1); 
						savefilename = savefilename.substr(0, savefilename.find_last_of(".") + 1) + "sst"; 
						FileIO.writefile(psx.savestate, savefilename, 1048576 * 4, false);
					}
				}
			}

			//if (CPU.commands >= 100000 && CPU.commands % 100000 == 0){ Joypad.blockbuttons = true; Joypad.KeyA = !Joypad.KeyA;}
			//if (CPU.commands == 32000000) { Joypad.blockbuttons = true; Joypad.KeyCross = true; }
			//if (CPU.commands == 10) { Joypad.blockbuttons = true; Joypad.KeySelect = true; }

			//if (CPU.commands == 24218000) GPU_Timing.nextHCount += 1;
			// 
			//if (CPU.commands == 531712) GPU.cmdTicks += 180;
			// 
			GPU.cmdTicksLast = 0;
			CPU.commands++;
		}
#else
		if (CPU.stall == 0 && CPU.emptyPipe == 0 && !CPU.writeDoneException && (!CPU.writebackException || CPU.lastExcIRQ) && (!DMA.on || (DMA.paused && DMA.waitcnt == 0)))
			CPU.commands++;
#endif

#ifndef FPGACOMPATIBLE
		GPU_Timing.work();
		Timer.work();
#endif

#ifdef FPGADMACPUBUSY
		if (DMA.on && !DMA.paused && CPU.paused)
#else
		if (DMA.on && !DMA.paused && !CPU.stall)
#endif
		{
			DMA.work();
		}
		else if (DMA.paused && DMA.waitcnt > 0)
		{
			DMA.check_unpause();
		}
		else
		{
			if (DMA.paused)
			{
				DMA.check_unpause();
			}
#ifdef FPGADMACPUBUSY
			if (!CPU.paused && DMA.dmaEndWait < 4) DMA.dmaEndWait++;
			if (!CPU.paused)	
#endif
				CPU.work();
#ifdef FPGACOMPATIBLE
			DMA.REP_counter = 0;
#endif
		}
		
#ifdef FPGADMACPUBUSY
		bool stallNext = ((CPU.newStall >> 2 & 1) == 1 || Memory.requestCode || Memory.requestData || Memory.requestWrite);
		if ((CPU.paused && (DMA.on && !DMA.paused)) || ((DMA.on && !DMA.paused) && Memory.busy == 0 && !stallNext))
		{
			if (!CPU.paused)
			{
				CPU.paused = true;
			}
		}
		else if (!DMA.on || DMA.paused)
		{
			CPU.paused = false;
		}
		if ((DMA.on && !DMA.paused) && Memory.busy > 0 && !stallNext && Memory.waitforCache)
		{
			DMA.waitcnt--;
			Memory.waitforCache = false;
		}
#endif

		Memory.work();
#ifdef FPGACOMPATIBLE
		GPU_Timing.work();
		Timer.work();
#endif
		if (GPU.skipnext) GPU.skipnext = false;
		else GPU.work();
		MDEC.work();
		Sound.work();
		Joypad.work();
		CDRom.work();
		CPU.totalticks++;

#ifdef FPGACOMPATIBLE
		//if (CPU.commands == 253476 && Memory.busy == 4) Memory.busy--;
		//if (CPU.commands == 130795 && DMA.REP_counter == 30) DMA.REP_counter += 24;		
#endif

		checkcount++;
		//if (checkcount == 0)
		{
			if (do_savestate)
			{
				//Gte.GTEoutWriteFile(true);
				if (CPU.stall == 0 && GPU.isSSIdle() && (!DMA.on || DMA.paused) && MDEC.isSSIdle() && Sound.fifo.empty() && CDRom.isSSIdle() && ((PSXRegs.I_STATUS & PSXRegs.I_MASK) == 0))
				{
					create_savestate();
					do_savestate = false;
				}
			}

			if (do_loadstate)
			{
				load_savestate();
				do_loadstate = false;
			}

			if (SDL_LockMutex(psxlock) == 0)
			{
				bool ret = !on;
				SDL_UnlockMutex(psxlock);
				if (ret)
				{
					std::free(Memory.GameRom);
					return;
				}
			}
		}
	}
}

void Psx::savestate_addvalue(int index, int bitend, int bitstart, UInt32 value)
{
	UInt32 oldvalue = savestate[index];
	UInt32 oldfilter = ~((((UInt32)pow(2, bitend + 1)) - 1) - (((UInt32)pow(2, bitstart)) - 1));
	oldvalue = oldvalue & oldfilter;

	UInt32 newfilter = ~oldfilter;
	UInt32 newvalue = value << bitstart;
	newvalue = newvalue & newfilter;

	newvalue |= oldvalue;

	savestate[index] = newvalue;
}

UInt32 Psx::savestate_loadvalue(int index, int bitend, int bitstart)
{
	UInt32 value = savestate[index];
	UInt32 filter = ((((UInt32)pow(2, bitend + 1)) - 1) - (((UInt32)pow(2, bitstart)) - 1));
	value &= filter;
	value = value >> bitstart;
	return value;
}

void Psx::create_savestate()
{
	debugsavestate = true;

	for (int i = 1; i < 1048576; i++)
	{
		savestate[i] = 0;
	}

	savestate[0]++; // header -> number of savestate, 0 = invalid
	savestate[1] = 1048574; // header -> size
	savestate[2] = CPU.commands;

	CPU.saveState(1024);
	GPU.saveState(2048);
	GPU_Timing.saveState(3072);
	DMA.saveState(4096);
	Gte.saveState(5120);
	Joypad.saveState(6144);
	MDEC.saveState(7168);
	Memory.saveState(8192);
	Timer.saveState(9216);
	Sound.saveState(10240);
	PSXRegs.saveStateIRQ(11264);
	PSXRegs.saveState_sio(12288);

	for (int i = 0; i < 1024; i++) // shouldn't this be 256?
	{
		UInt32 dword = CPU.scratchpad[i * 4];
		dword |= CPU.scratchpad[i * 4 + 1] << 8;
		dword |= CPU.scratchpad[i * 4 + 2] << 16;
		dword |= CPU.scratchpad[i * 4 + 3] << 24;
		savestate[31744 + i] = dword;
	}

	CDRom.saveState(32768);

	for (int i = 0; i < 131072; i++)
	{
		UInt32 dword = Sound.SOUNDRAM[i * 4];
		dword |= Sound.SOUNDRAM[i * 4 + 1] << 8;
		dword |= Sound.SOUNDRAM[i * 4 + 2] << 16;
		dword |= Sound.SOUNDRAM[i * 4 + 3] << 24;
		savestate[131072 + i] = dword;
	}

	for (int i = 0; i < 262144; i++)
	{
		UInt32 dword = GPU.VRAM[i * 4];
		dword |= GPU.VRAM[i * 4 + 1] << 8;
		dword |= GPU.VRAM[i * 4 + 2] << 16;
		dword |= GPU.VRAM[i * 4 + 3] << 24;
		savestate[262144 + i] = dword;
	}

	for (int i = 0; i < 524288; i++)
	{
		UInt32 dword = Memory.RAM[i * 4];
		dword |= Memory.RAM[i * 4 + 1] << 8;
		dword |= Memory.RAM[i * 4 + 2] << 16;
		dword |= Memory.RAM[i * 4 + 3] << 24;
		savestate[524288 + i] = dword;
	}
}

void Psx::load_savestate()
{
	if (savestate[0] != 0)
	{
		reset();

		//CPU.commands = savestate[2];

		loading_state = true;

		CPU.loadState(1024);
		GPU.loadState(2048);
		GPU_Timing.loadState(3072);
		DMA.loadState(4096);
		Gte.loadState(5120);
		Joypad.loadState(6144);
		MDEC.loadState(7168);
		Memory.loadState(8192);
		Timer.loadState(9216);
		Sound.loadState(10240);
		PSXRegs.loadStateIRQ(11264);
		PSXRegs.loadState_sio(12288);
		
		for (int i = 0; i < 1024; i++)
		{
			UInt32 dword = savestate[31744 + i];
			CPU.scratchpad[i * 4] = dword;
			CPU.scratchpad[i * 4 + 1] = dword >> 8;
			CPU.scratchpad[i * 4 + 2] = dword >> 16;
			CPU.scratchpad[i * 4 + 3] = dword >> 24;
		}

		CDRom.loadState(32768);

		for (int i = 0; i < 131072; i++)
		{
			UInt32 dword = savestate[131072 + i];
			Sound.SOUNDRAM[i * 4] = dword;
			Sound.SOUNDRAM[i * 4 + 1] = dword >> 8;
			Sound.SOUNDRAM[i * 4 + 2] = dword >> 16;
			Sound.SOUNDRAM[i * 4 + 3] = dword >> 24;
		}

		//Sound.SOUNDRAM[0x1A60] = 0;
		//Sound.SOUNDRAM[0x1A61] = 0;

		for (int i = 0; i < 262144; i++)
		{
			UInt32 dword = savestate[262144 + i];
			GPU.VRAM[i * 4] = dword;
			GPU.VRAM[i * 4 + 1] = dword >> 8;
			GPU.VRAM[i * 4 + 2] = dword >> 16;
			GPU.VRAM[i * 4 + 3] = dword >> 24;
		}

		for (int i = 0; i < 524288; i++)
		{
			UInt32 dword = savestate[524288 + i];
			Memory.RAM[i * 4] = dword;
			Memory.RAM[i * 4 + 1] = dword >> 8;
			Memory.RAM[i * 4 + 2] = dword >> 16;
			Memory.RAM[i * 4 + 3] = dword >> 24;
		}
		
#ifdef FPGACOMPATIBLE
		FileIO.writefile(GPU.VRAM, "R:\\gpu_vram_FPSXA.bin", 1048576, true);
		FileIO.writefile(Sound.SOUNDRAM, "R:\\spu_ram_FPSXA.bin", 524288, true);
#endif

		loading_state = false;
	}
}

void Psx::log(string text)
{
	//FILE* file = fopen("R:\\debug_log.txt", "a");
	//fputs(text.c_str(), file);
	//fputc('\n', file);
	//fclose(file);
}