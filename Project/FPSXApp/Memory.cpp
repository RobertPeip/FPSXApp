#undef NDEBUG
#include <assert.h>

#include "Memory.h"
#include "CPU.h"
#include "PSXRegs.h"
#include "GPU.h"
#include "gpio.h"
#include "fileIO.h"
#include "psx.h"
#include "Sound.h"
#include "Joypad.h"
#include "Sound.h"
#include "DMA.h"
#include "CDROM.h"

MEMORY Memory;

void MEMORY::patchBios(UInt32 address, UInt32 data)
{
	BIOSROM[address] = data;
	BIOSROM[address + 1] = data >> 8;
	BIOSROM[address + 2] = data >> 16;
	BIOSROM[address + 3] = data >> 24;
}

void MEMORY::reset(string filename)
{
	if (psx.coldreset)
	{
		int index = filename.find(".cue");
		if (index > -1)
		{
			CDRom.readCue(filename);
			GameRom_max = FileIO.fileSize(CDRom.trackinfos[1].filename, true);
			GameRom = (Byte*)std::malloc(GameRom_max);
			FileIO.readfile(GameRom, CDRom.trackinfos[1].filename, true);
		}
		else
		{
			GameRom_max = FileIO.fileSize(filename, true);
			GameRom = (Byte*)std::malloc(GameRom_max);
			FileIO.readfile(GameRom, filename, true);
			CDRom.trackcount = 1;
			CDRom.trackinfos[1] = { filename, false, 0, (int)((GameRom_max / RAW_SECTOR_SIZE) + PREGAPSIZE), 0, 2 };

			UInt32 lba = CDRom.trackinfos[1].lbaEnd - 149;
			lba /= FRAMES_PER_SECOND;
			CDRom.totalSecondsBCD = CDRom.BINBCD(lba % SECONDS_PER_MINUTE);
			lba /= SECONDS_PER_MINUTE;
			CDRom.totalMinutesBCD = CDRom.BINBCD(lba);
		}

		FileIO.readfile(BIOSROM, "scph1001.bin", false);
		//FileIO.readfile(BIOSROM, "SCPH1002.BIN", false);
		//FileIO.readfile(BIOSROM, "scph7001.bin", false);

		for (int i = 0; i < 2097152; i++) RAM[i] = 0x00;
	}

	// tty
#ifdef USETTY
	patchBios(0x6F0C, 0x24010001);
	patchBios(0x6F14, 0xAF81A9C0);
#endif
	lineBuffer = "";
	FILE* file = fopen("R:\\debug_tty.txt", "w");
	fclose(file);
	ttyCount = 0;

	if (GameRom[0] == 'P' && GameRom[1] == 'S' && GameRom[2] == '-' && GameRom[3] == 'X' && GameRom[5] == 'E' && GameRom[6] == 'X' && GameRom[7] == 'E')
	{
		CDRom.hasCD = false;

		UInt32 initial_pc = *(UInt32*)&GameRom[0x10];
		UInt32 initial_gp = *(UInt32*)&GameRom[0x14];
		UInt32 load_address = *(UInt32*)&GameRom[0x18];
		UInt32 file_size = *(UInt32*)&GameRom[0x1C];
		UInt32 memfill_start = *(UInt32*)&GameRom[0x28];
		UInt32 memfill_size = *(UInt32*)&GameRom[0x2C];
		UInt32 initial_sp_base = *(UInt32*)&GameRom[0x30];
		UInt32 initial_sp_offset = *(UInt32*)&GameRom[0x34];

		if (file_size >= 4)
		{
			file_size = (file_size + 3) / 4;
			UInt32 addressRead = 0x800;
			UInt32 addressWrite = load_address & 0x1FFFFF;
			for (int i = 0; i < file_size; i++)
			{
				UInt32 data = *(UInt32*)&GameRom[addressRead];
				RAM[addressWrite] = data;
				RAM[addressWrite + 1] = data >> 8;
				RAM[addressWrite + 2] = data >> 16;
				RAM[addressWrite + 3] = data >> 24;
				addressRead += 4;
				addressWrite += 4;
			}

			// pc has to be done first because we can't load it in the delay slot
			patchBios(0x6FF0, UINT32_C(0x3C080000) | initial_pc >> 16);                // lui $t0, (r_pc >> 16)
			patchBios(0x6FF4, UINT32_C(0x35080000) | (initial_pc & UINT32_C(0xFFFF))); // ori $t0, $t0, (r_pc & 0xFFFF)
			patchBios(0x6FF8, UINT32_C(0x3C1C0000) | initial_gp >> 16);                // lui $gp, (r_gp >> 16)
			patchBios(0x6FFC, UINT32_C(0x379C0000) | (initial_gp & UINT32_C(0xFFFF))); // ori $gp, $gp, (r_gp & 0xFFFF)

			UInt32 r_sp = initial_sp_base + initial_sp_offset;
			if (r_sp != 0)
			{
				patchBios(0x7000, UINT32_C(0x3C1D0000) | r_sp >> 16);                // lui $sp, (r_sp >> 16)
				patchBios(0x7004, UINT32_C(0x37BD0000) | (r_sp & UINT32_C(0xFFFF))); // ori $sp, $sp, (r_sp & 0xFFFF)
			}
			else
			{
				patchBios(0x7000, UINT32_C(0x00000000)); // nop
				patchBios(0x7004, UINT32_C(0x00000000)); // nop
			}

			UInt32 r_fp = r_sp;
			if (r_fp != 0)
			{
				patchBios(0x7008, UINT32_C(0x3C1E0000) | r_fp >> 16);                // lui $fp, (r_fp >> 16)
				patchBios(0x700C, UINT32_C(0x01000008));                             // jr $t0
				patchBios(0x7010, UINT32_C(0x37DE0000) | (r_fp & UINT32_C(0xFFFF))); // ori $fp, $fp, (r_fp & 0xFFFF)
			}
			else
			{
				patchBios(0x7008, UINT32_C(0x00000000)); // nop
				patchBios(0x700C, UINT32_C(0x01000008)); // jr $t0
				patchBios(0x7010, UINT32_C(0x00000000)); // nop
			}
		}
	}
	else
	{
		CDRom.hasCD = true;
		CDRom.lbaCount = GameRom_max / RAW_SECTOR_SIZE;

#ifdef FASTBOOT
		patchBios(0x18000, 0x3C011F80);
		patchBios(0x18004, 0x3C0A0300);
		patchBios(0x18008, 0xAC2A1814);
		patchBios(0x1800C, 0x03E00008);
		patchBios(0x18010, 0x00000000);
#endif
	}

	for (int i = 0; i < 2097152; i++)
	{
		cachetest[i] = 0;
	}

	requestData = false;
	requestWrite = false;
	requestCode = false;
	done = false;
	cd_transfer = false;
}

void MEMORY::GameRAMSnapshot()
{
}

void MEMORY::load_gameram(string gamename)
{
	string filename = gamename.substr(gamename.find_last_of("/\\") + 1);
	filename = filename.substr(0, filename.find_last_of(".") + 1) + "sav";
	
	if (FileIO.fileExists(filename, false))
	{
		FileIO.readfile(Joypad.memcardData, filename, false);
	}
}

void MEMORY::save_gameram(string gamename)
{
	string filename = gamename.substr(gamename.find_last_of("/\\") + 1);
	filename = filename.substr(0, filename.find_last_of(".") + 1) + "sav";
	
	FileIO.writefile(Joypad.memcardData, filename, 131072, false);
}

UInt32 MEMORY::rotateRead16(UInt32 data, byte offset)
{
	switch (offset)
	{
	case 1: return data >> 8;
	case 3: return data >> 8;
	}
	return data;
}

UInt32 MEMORY::rotateRead32(UInt32 data, byte offset)
{
	switch (offset)
	{
	case 1: return data >> 8;
	case 2: return data >> 16;
	case 3: return data >> 24;
	}
	return data;
}

void MEMORY::work()
{
	done = false;

	if (busy > 0)
	{
#ifdef FPGACOMPATIBLE
		if (cd_transfer)
		{
			if (cd_rnw)
			{
				dataRead = CDRom.readReg(cd_addr);
				if (cd_reqsize > 1)
				{
					dataRead |= CDRom.readReg(cd_addr + 1) << 8;
				}
				if (cd_reqsize > 2)
				{
					dataRead |= CDRom.readReg(cd_addr + 2) << 16;
					dataRead |= CDRom.readReg(cd_addr + 3) << 24;
				}
				cd_transfer = false;
			}
			else
			{
				if (busy == 4 && (cd_mask & 0x1)) CDRom.writeReg(cd_addr, cd_data);
				if (busy == 3 && (cd_mask & 0x2)) CDRom.writeReg(cd_addr + 1, cd_data >> 8);
				if (busy == 2 && (cd_mask & 0x4)) CDRom.writeReg(cd_addr + 2, cd_data >> 16);
				if (busy == 1 && (cd_mask & 0x8)) CDRom.writeReg(cd_addr + 3, cd_data >> 24);
				if (busy == 1) cd_transfer = false;
			}
		}
#endif

		busy--;
		if (busy == 0)
		{
			done = true;
			waitforCache = true;
		}
	}

	if (requestWrite)
	{
		requestWrite = false;

		busy = 1;

		switch (addressWrite >> 29)
		{
		case 0x00: // KUSEG - cached
		case 0x04: // KSEG0 - cached
			addressWrite &= 0x1FFFFFFF;
			// check scratchpad
			break;

		case 0x05: // KSEG1 - uncached
			addressWrite &= 0x1FFFFFFF;
			break;
		}

		if (addressWrite < 0x800000) // RAM
		{
			if (addressWrite == 0x015d920)
			{
				int a = 5;
			}
			if (addressWrite == 0x00ff058 && dataWrite == 0x000ff708)
			{
				int a = 5;
			}

			addressWrite &= 0x1FFFFF;
			if (writeMask & 0b0001) RAM[addressWrite] = dataWrite;
			if (writeMask & 0b0010) RAM[addressWrite + 1] = dataWrite >> 8;
			if (writeMask & 0b0100) RAM[addressWrite + 2] = dataWrite >> 16;
			if (writeMask & 0b1000) RAM[addressWrite + 3] = dataWrite >> 24;
			CPU.updateCache(addressWrite, dataWrite, writeMask);
			busy = 2;
		}
		else if (addressWrite >= 0x1F000000 && addressWrite < 0x1F800000) // EXP1
		{
			// do nothing
			int a = 5;
		}
		else if (addressWrite >= 0x1F801000 && addressWrite < 0x1F801040) // MEMCTRL
		{
			PSXRegs.write_reg_memctrl(addressWrite - 0x1F801000, dataWrite);
		}
		else if (addressWrite >= 0x1F801040 && addressWrite < 0x1F801050) // PAD
		{
			Joypad.write_reg(addressWrite - 0x1F801040, dataWrite, writeMask);
		}
		else if (addressWrite >= 0x1F801050 && addressWrite < 0x1F801060) // SIO
		{
			if (writeMask & 0x3) PSXRegs.write_reg_sio(addressWrite - 0x1F801050, dataWrite);
			if (writeMask & 0xC) PSXRegs.write_reg_sio(addressWrite - 0x1F801050 + 2, dataWrite >> 16);
		}
		else if (addressWrite >= 0x1F801060 && addressWrite < 0x1F801070) // MEMCTRL2
		{
			PSXRegs.write_reg_memctrl2(addressWrite - 0x1F801060, dataWrite);
		}
		else if (addressWrite >= 0x1F801070 && addressWrite < 0x1F801080) // IRQ
		{
			PSXRegs.write_reg_irq(addressWrite - 0x1F801070, dataWrite);
		}
		else if (addressWrite >= 0x1F801080 && addressWrite < 0x1F801100) // DMA
		{
			PSXRegs.write_reg_dma(addressWrite - 0x1F801080, dataWrite);
		}
		else if (addressWrite >= 0x1F801100 && addressWrite < 0x1F801140) // TIMER
		{
			PSXRegs.write_reg_timer(addressWrite - 0x1F801100, dataWrite);
		}
		else if (addressWrite >= 0x1F801800 && addressWrite < 0x1F801810) // CDROM
		{
#ifdef FPGACOMPATIBLE
			busy += 4;
			cd_transfer = true;
			cd_rnw = false;
			cd_addr = addressWrite - 0x1F801800;
			cd_data = dataWrite;
			cd_mask = writeMask;
#else
			if (writeMask & 0x1) CDRom.writeReg(addressWrite - 0x1F801800, dataWrite);
			if (writeMask & 0x2) CDRom.writeReg(addressWrite - 0x1F801800 + 1, dataWrite >> 8);
			if (writeMask & 0x4) CDRom.writeReg(addressWrite - 0x1F801800 + 2, dataWrite >> 16);
			if (writeMask & 0x8) CDRom.writeReg(addressWrite - 0x1F801800 + 3, dataWrite >> 24);
#endif
		}
		else if (addressWrite >= 0x1F801810 && addressWrite < 0x1F801820) // GPU
		{
			PSXRegs.write_reg_gpu(addressWrite - 0x1F801810, dataWrite);
		}
		else if (addressWrite >= 0x1F801820 && addressWrite < 0x1F801830) // MDEC
		{
			PSXRegs.write_reg_mdec(addressWrite - 0x1F801820, dataWrite);
		}
		else if (addressWrite >= 0x1F801C00 && addressWrite < 0x1F802000) // SPU
		{
			if (writeMask & 0x3) Sound.write_reg(addressWrite - 0x1F801C00, dataWrite);
			if (writeMask & 0xC) Sound.write_reg(addressWrite - 0x1F801C00 + 2, dataWrite >> 16);
#ifdef FPGACOMPATIBLE
			busy = 3;
#endif
		}
		else if (addressWrite >= 0x1F802000 && addressWrite < 0x1F804000) // EXP2
		{
			if ((addressWrite == 0x1F802020 && (writeMask & 0x8)) || (addressWrite == 0x1F802080 && (writeMask & 0x1)))
			{
				byte character;
				if (addressWrite == 0x1F802020) character = dataWrite >> 24;
				else character = dataWrite & 0xFF;
				if (character == '\r') {}
				else if (character == '\n')
				{
					if (ttyCount == 207)
					{
						int a = 5;
					}
					FILE* file = fopen("R:\\debug_tty.txt", "a");
					fputs(lineBuffer.c_str(), file);
					fputc('\n', file);
					fclose(file);
					lineBuffer = "";
					ttyCount++;
				}
				else
				{
					lineBuffer += (char)character;
				}
			}
			// do nothing
		}
		else if (addressWrite >= 0x1FA00000 && addressWrite < 0x1FA00001) // EXP3
		{
			int a = 5;
			// do nothing
		}
		else
		{
#ifndef IGNOREAREACHECKS
			_wassert(_CRT_WIDE("Memory write undefined area -> should trigger exception"), _CRT_WIDE("Memory"), addressWrite);
#endif
		}
	}

	if (requestData)
	{
		if (address == 0x1F8010A8)
		{
			int a = 5;
		}

		bool readArea = true;
		switch (address >> 29)
		{
		case 0x00: // KUSEG - cached
		case 0x04: // KSEG0 - cached
			address &= 0x1FFFFFFF;
			break;

		case 0x01: // KUSEG 512M-1024M
		case 0x02: // KUSEG 1024M-1536M
		case 0x03: // KUSEG 1536M-2048M
			dataRead = 0xFFFFFFFF;
			_wassert(_CRT_WIDE("Memory read in KUSEG1-3 -> should trigger exception"), _CRT_WIDE("Memory"), addressWrite);
			readArea = false;
			break;

		case 0x05: // KSEG1 - uncached
			address &= 0x1FFFFFFF;
			break;

		case 06: // KSEG2
		case 07: // KSEG2
			if (address == 0xFFFE0130)
			{
				dataRead = PSXRegs.CACHECONTROL;
			}
			else
			{
				dataRead = 0xFFFFFFFF; 
				_wassert(_CRT_WIDE("Memory read in KSEG 2 -> should trigger exception"), _CRT_WIDE("Memory"), addressWrite);
			}
			readArea = false;
			break;
		}

		if (readArea)
		{
			if (address == 0x00000300)
			{
				int a = 5;
			}			
			

			if (address < 0x800000) // RAM
			{
				if ((address & 0x1fffff) == 0x00101FC)
				{
					int a = 5;
				}

				dataRead = *(UInt32*)&RAM[address & 0x1fffff];
				busy = 3;
				cachetest[address & 0x1fffff]++;
			}
			else if (address >= 0x1FC00000 && address < 0x1FC80000) // BIOS
			{
				dataRead = *(UInt32*)&BIOSROM[address & 0x7FFFF];
#if defined(NOMEMWAITFPGA)
				busy = 4;
#else
				busy = (requestSize * 6) - 1;
				if (requestSize == 1) busy += 3;
				busy -= 2;
#endif
			}
			else if (address >= 0x1F000000 && address < 0x1F800000) // EXP1
			{
				dataRead = 0xFFFFFFFF;
				busy = 1;
			}
			else if (address >= 0x1F801000 && address < 0x1F801040) // MEMCTRL
			{
				dataRead = PSXRegs.read_reg_memctrl(address - 0x1F801000);
				if (requestSize != 2) dataRead = rotateRead16(dataRead, address & 3);
				busy = 1;
			}
			else if (address >= 0x1F801040 && address < 0x1F801050) // PAD
			{
				dataRead = Joypad.read_reg(address - 0x1F801040);
				busy = 1;
			}
			else if (address >= 0x1F801050 && address < 0x1F801060) // SIO
			{
				dataRead = PSXRegs.read_reg_sio(address - 0x1F801050);
				busy = 1;
			}
			else if (address >= 0x1F801060 && address < 0x1F801070) // MEMCTRL2
			{
				dataRead = PSXRegs.read_reg_memctrl2(address - 0x1F801060);
				busy = 1;
			}
			else if (address >= 0x1F801070 && address < 0x1F801080) // IRQ
			{
				dataRead = PSXRegs.read_reg_irq(address - 0x1F801070);
				busy = 1;
			}
			else if (address >= 0x1F801080 && address < 0x1F801100) // DMA
			{
				dataRead = PSXRegs.read_reg_dma((address - 0x1F801080) & 0xFC);
				if (requestSize != 4) dataRead = rotateRead32(dataRead, address & 3);
				busy = 1;
			}
			else if (address >= 0x1F801100 && address < 0x1F801140) // TIMER
			{
				dataRead = PSXRegs.read_reg_timer(address - 0x1F801100);
				busy = 1;
			}
			else if (address >= 0x1F801800 && address < 0x1F801810) // CDROM
			{
#ifdef FPGACOMPATIBLE
				busy = 1 + 2 * requestSize;
				cd_transfer = true;
				cd_rnw = true;
				cd_addr = address - 0x1F801800;
				cd_reqsize = requestSize;
#else
				dataRead = CDRom.readReg(address - 0x1F801800);
				if (requestSize > 1)
				{
					dataRead |= CDRom.readReg(address - 0x1F801800 + 1) << 8;
				}
				if (requestSize > 2)
				{
					dataRead |= CDRom.readReg(address - 0x1F801800 + 2) << 16;
					dataRead |= CDRom.readReg(address - 0x1F801800 + 3) << 24;
				}
				busy = 1;
#endif
			}
			else if (address >= 0x1F801810 && address < 0x1F801820) // GPU
			{
				dataRead = PSXRegs.read_reg_gpu(address - 0x1F801810);
				busy = 1;
			}
			else if (address >= 0x1F801820 && address < 0x1F801830) // MDEC
			{
				dataRead = PSXRegs.read_reg_mdec(address - 0x1F801820);
				busy = 1;
			}
			else if (address >= 0x1F801C00 && address < 0x1F802000) // SPU
			{
				dataRead = Sound.read_reg(address - 0x1F801C00);
				if (requestSize == 4) dataRead |= Sound.read_reg(address - 0x1F801C00 + 2) << 16;
				busy = 1;
#ifdef FPGACOMPATIBLE
				if (requestSize == 4) busy = 19; else busy = 18; // todo: busy for dword is wrong
#endif
			}
			else if (address >= 0x1F802000 && address < 0x1F804000) // EXP2
			{
				if (address == 0x1f802021) dataRead = 0xC;
				else dataRead = 0xFFFFFFFF;
				busy = 1;
			}
			else if (address >= 0x1FA00000 && address < 0x1FA00001) // EXP3
			{
				dataRead = 0xFFFFFFFF;
				busy = 1;
			}
			else
			{
#ifndef IGNOREAREACHECKS
				_wassert(_CRT_WIDE("Memory read undefined area -> should trigger exception"), _CRT_WIDE("Memory"), address);
#endif
				dataRead = 0xFFFFFFFF;
				done = true;
			}
		}
	}
	else if(requestCode)
	{
		switch (address >> 29)
		{
		case 0x00: // KUSEG - cached
		case 0x04: // KSEG0 - cached
			address &= 0x1FFFFFFF;
			if (address < 0x800000) // RAM
			{
				address &= 0x1ffff0;
				for (int i = 0; i < 4; i++)
				{
					cacheLine[i] = *(UInt32*)&RAM[address];
					address += 4;
				}
#ifdef FIXMEMTIME
				busy = FIXEDMEMTIME - 1;
#elif defined(NOMEMWAITFPGA)
				waitforCache = true;
				busy = 6;
#else
				busy = 6;
#endif
			}
			else if (address >= 0x1FC00000 && address < 0x1FC80000) // BIOS
			{
				address &= 0x7FFFF;
				for (int i = 0; i < 4; i++)
				{
					cacheLine[i] = *(UInt32*)&BIOSROM[address];
					address += 4;
				}
				busy = 95;
			}
			else
			{
				_wassert(_CRT_WIDE("Code read undefined area -> should trigger exception?"), _CRT_WIDE("Memory"), address);
				dataRead = 0x12345678;
				done = true;
			}
			break;

		case 0x05: // KSEG1 - uncached
			address &= 0x1FFFFFFF;
			if (address < 0x800000) // RAM
			{
				dataRead = *(UInt32*)&RAM[address & 0x1fffff];
#ifdef FIXMEMTIME
				busy = FIXEDMEMTIME - 1;
#else
				busy = 3;
#endif
			}
			else if (address >= 0x1FC00000 && address < 0x1FC80000) // BIOS
			{
				dataRead = *(UInt32*)&BIOSROM[address & 0x7FFFF];
#ifdef FIXMEMTIME
				busy = FIXEDMEMTIME - 1;
#elif defined(NOMEMWAITFPGA)
				busy = 4;
#else
				busy = 21;
#endif
			}
			else
			{
				_wassert(_CRT_WIDE("Code read undefined area -> should trigger exception"), _CRT_WIDE("Memory"), address);
				dataRead = 0x12345678;
				done = true;
			}
			break;

			// unmapped
		default:
			_wassert(_CRT_WIDE("Memory read unmapped area -> should trigger exception"), _CRT_WIDE("Memory"), address);
			dataRead = 0x12345678;
			done = true;
			return;
		}
	}
}

void MEMORY::saveState(UInt32 offset)
{
	psx.savestate_addvalue(offset + 0, 31, 0, PSXRegs.MC_RAMSIZE);
	psx.savestate_addvalue(offset + 1, 31, 0, PSXRegs.MC_EXP1_BASE);
	psx.savestate_addvalue(offset + 2, 31, 0, PSXRegs.MC_EXP2_BASE);
	psx.savestate_addvalue(offset + 3, 31, 0, PSXRegs.MC_EXP1_DELAY);
	psx.savestate_addvalue(offset + 4, 31, 0, PSXRegs.MC_EXP3_DELAY);
	psx.savestate_addvalue(offset + 5, 31, 0, PSXRegs.MC_BIOS_DELAY);
	psx.savestate_addvalue(offset + 6, 31, 0, PSXRegs.MC_SPU_DELAY);
	psx.savestate_addvalue(offset + 7, 31, 0, PSXRegs.MC_CDROM_DELAY);
	psx.savestate_addvalue(offset + 8, 31, 0, PSXRegs.MC_EXP2_DELAY);
	psx.savestate_addvalue(offset + 9, 31, 0, PSXRegs.MC_COMMON_DELAY);

	for (int i = 0; i < 4; i++) psx.savestate_addvalue(offset + 10 + i, 31, 0, cacheLine[i]);

	psx.savestate_addvalue(offset + 14, 31, 0, address);
	psx.savestate_addvalue(offset + 15, 31, 0, dataRead);
	psx.savestate_addvalue(offset + 16, 31, 0, dataWrite);
	psx.savestate_addvalue(offset + 17, 31, 0, addressWrite);
	psx.savestate_addvalue(offset + 18,  7,  0, requestSize);
	psx.savestate_addvalue(offset + 18, 15,  8, writeMask);
	psx.savestate_addvalue(offset + 18, 23, 16, busy);
	psx.savestate_addvalue(offset + 18, 24, 24, requestData);
	psx.savestate_addvalue(offset + 18, 24, 24, requestCode);
	psx.savestate_addvalue(offset + 18, 24, 24, requestWrite);
	psx.savestate_addvalue(offset + 18, 24, 24, done);
}

void MEMORY::loadState(UInt32 offset)
{
	PSXRegs.MC_RAMSIZE      = psx.savestate_loadvalue(offset + 0, 31, 0);
	PSXRegs.MC_EXP1_BASE	= psx.savestate_loadvalue(offset + 1, 31, 0);
	PSXRegs.MC_EXP2_BASE	= psx.savestate_loadvalue(offset + 2, 31, 0);
	PSXRegs.MC_EXP1_DELAY   = psx.savestate_loadvalue(offset + 3, 31, 0);
	PSXRegs.MC_EXP3_DELAY   = psx.savestate_loadvalue(offset + 4, 31, 0);
	PSXRegs.MC_BIOS_DELAY   = psx.savestate_loadvalue(offset + 5, 31, 0);
	PSXRegs.MC_SPU_DELAY	= psx.savestate_loadvalue(offset + 6, 31, 0);
	PSXRegs.MC_CDROM_DELAY  = psx.savestate_loadvalue(offset + 7, 31, 0);
	PSXRegs.MC_EXP2_DELAY   = psx.savestate_loadvalue(offset + 8, 31, 0);
	PSXRegs.MC_COMMON_DELAY = psx.savestate_loadvalue(offset + 9, 31, 0);

	for (int i = 0; i < 4; i++) cacheLine[i] = psx.savestate_loadvalue(offset + 10 + i, 31, 0);

	address       = psx.savestate_loadvalue(offset + 14, 31,  0); 
	dataRead	  = psx.savestate_loadvalue(offset + 15, 31,  0); 
	dataWrite	  = psx.savestate_loadvalue(offset + 16, 31,  0); 
	addressWrite  = psx.savestate_loadvalue(offset + 17, 31,  0); 
	requestSize   = psx.savestate_loadvalue(offset + 18,  7,  0); 
	writeMask	  = psx.savestate_loadvalue(offset + 18, 15,  8); 
	busy		  = psx.savestate_loadvalue(offset + 18, 23, 16); 
	requestData   = psx.savestate_loadvalue(offset + 18, 24, 24); 
	requestCode   = psx.savestate_loadvalue(offset + 18, 24, 24); 
	requestWrite  = psx.savestate_loadvalue(offset + 18, 24, 24); 
	done		  = psx.savestate_loadvalue(offset + 18, 24, 24); 
}

void MEMORY::test_datacache()
{
	FILE* file = fopen("R:\\debug_cache.txt", "w");

	std::sort(std::begin(cachetest), std::end(cachetest), std::greater<int>());

	int totalcount = 0;
	for (int i = 0; i < 2097152; i++)
	{
		totalcount += cachetest[i];
	}

	int count = 0;
	for (int i = 0; i < 2097152; i++)
	{
		if (cachetest[i] > 0)
		{
			count += cachetest[i];

			char buf[10];
			_itoa(i, buf, 16);
			for (int c = strlen(buf); c < 6; c++) fputc('0', file);
			fputs(buf, file);

			fputc(' ', file);
			_itoa(cachetest[i], buf, 10);
			for (int c = strlen(buf); c < 8; c++) fputc(' ', file);
			fputs(buf, file);

			fputc(' ', file);
			_itoa(count * 100 / totalcount, buf, 10);
			for (int c = strlen(buf); c < 3; c++) fputc(' ', file);
			fputs(buf, file);

			fputc('\n', file);
		}
	}
	fclose(file);
}