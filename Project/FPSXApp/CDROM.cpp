#undef NDEBUG
#include <assert.h>
#include <fstream>
#include <string>
#include <sstream>

#include "CDROM.h"
#include "PSXRegs.h"
#include "CPU.h"
#include "Memory.h"
#include "DMA.h"
#include "psx.h"
#include "Sound.h"
#include "FileIO.h"

CDROM CDRom;

void CDROM::reset()
{
	while (!fifoParam.empty()) fifoParam.pop_front();
	while (!fifoData.empty()) fifoData.pop_front();
	while (!fifoResponse.empty()) fifoResponse.pop_front();

	busy = false;
	delay = 0;
	cmdPending = false;

	pendingDriveIRQ = 0;
	pendingDriveResponse = 0;

	working = false;
	workDelay = 0;
	workCommand = 0;

	driveState = DRIVESTATE::IDLE;
	driveBusy = false;
	driveDelay = 0;

	readAfterSeek = false;
	playAfterSeek = false;

	fastForwardRate = 0;

	modeReg = 0x20; // read_raw_sector set

	setLocActive = false;
	setLocMinute = 0;
	setLocSecond = 0;
	setLocFrame = 0;

	seekStartLBA = 0;
	seekEndLBA = 0;
	currentLBA = 0;
	trackNumberBCD = 0;

	writeSectorPointer = 0;
	readSectorPointer = 0;
	lastReadSector = 0;

	session = 0;

	internalStatus = 0x10; // shell open

	startLBA = 0x96;
	positionInIndex = 0;

	physicalLBA = 0;
	physicalLBATick = 0;
	physicalLBAcarry = 0;

	lastSectorHeaderValid = false;
	for (int i = 0; i < 4; i++)
	{
		header[i] = 0;
		subheader[i] = 0;
	}	
	
	for (int i = 0; i < 12; i++)
	{
		nextSubdata[i] = 0;
		subdata[i] = 0;
	}

	XaFilterFile = 0;
	XaFilterChannel = 0;

	XaCurrentFile = 0;
	XaCurrentChannel = 0;
	XaCurrentSet = false;

	resetXaDecoder();

	while (!XaFifo.empty()) XaFifo.pop_front();

	for (int i = 0; i < NUM_SECTOR_BUFFERS; i++)
	{
		sectorBufferSizes[i] = 0;
	}

	// sound related

	muted = false;
	xa_muted = false;

	lastreportCDDA = 0xFF;

	cdvol_next00 = 0x80;
	cdvol_next01 = 0;
	cdvol_next10 = 0;
	cdvol_next11 = 0x80;
	cdvol_00 = 0x80;
	cdvol_01 = 0;
	cdvol_10 = 0;
	cdvol_11 = 0x80;

	startMotor();

	LIDcount = 0;
}

void CDROM::softReset()
{
	while (!fifoParam.empty()) fifoParam.pop_front();
	while (!fifoData.empty()) fifoData.pop_front();

	bool wasDoubleSpeed = (modeReg >> 7) & 1;
	modeReg = 0x20; // read_raw_sector set

	lastSectorHeaderValid = false;

	internalStatus = 0;
	if (hasCD)
	{
		internalStatus |= 2;

		if (currentLBA != 0)
		{
			driveState = DRIVESTATE::SEEKIMPLICIT;
			seekStartLBA = currentLBA;
			seekEndLBA = 0;
			readOnDisk(0);
		}
		else
		{
			driveState = DRIVESTATE::SPEEDCHANGEORTOCREAD;
		}
		driveBusy = true;
		driveDelay = 16934400;
		if (wasDoubleSpeed) driveDelay += 33868800;
		driveDelay += getSeekTicks();
		driveDelayNext = driveDelay;
	}
	else
	{
		driveState = DRIVESTATE::IDLE;
		driveBusy = false;
		driveDelay = 0;
	}

	resetXaDecoder();

	lastreportCDDA = 0xFF;
}

byte CDROM::BCDBIN(byte bcd)
{
	return ((bcd >> 4) * 10) + (bcd & 0xF);
}

byte CDROM::BINBCD(byte bin)
{
	return ((bin / 10) << 4) + (bin % 10);
}

void CDROM::readCue(string filename)
{
	Int64 size = FileIO.fileSize(filename, true);
	Byte* cuedata = (Byte*)std::malloc(size);
	FileIO.readfile(cuedata, filename, true);
	string cuestring(reinterpret_cast<char const*>(cuedata), size);
	auto ss = std::stringstream{ cuestring };

	string basepath = filename.substr(0, filename.find_last_of("\\\\") + 1);

	bool infile = false;
	string binname;
	trackcount = 1;
	totalLBAs = 0;
	int lbasize = 0;
	bool isAudio = false;
	byte second;
	byte minute;
	UInt32 lba;
	for (string line; getline(ss, line, '\n');)
	{
		int start = line.find("FILE");
		int end = line.find("BINARY");
		if (start > -1 && end > -1)
		{
			if (infile)
			{
				lba = totalLBAs;
				if (trackcount == 1 && !isAudio) totalLBAs += PREGAPSIZE;
				lba += PREGAPSIZE;
				lba /= FRAMES_PER_SECOND;
				second = BINBCD(lba % SECONDS_PER_MINUTE);
				lba /= SECONDS_PER_MINUTE;
				minute = BINBCD(lba);

				trackinfos[trackcount] = { basepath + binname, isAudio, totalLBAs, totalLBAs + lbasize - 1, minute, second };
				trackcount++;
				totalLBAs += lbasize;
				binname = line.substr(start + 6, end - start - 8);
				lbasize = (FileIO.fileSize(basepath + binname, true) / RAW_SECTOR_SIZE);
			}
			else
			{
				infile = true;
				binname = line.substr(start + 6, end - start - 8);
				lbasize = (FileIO.fileSize(basepath + binname, true) / RAW_SECTOR_SIZE);
			}
		}
		else if (infile)
		{
			if ((int)line.find("TRACK") >= 0)
			{
				isAudio = false;
				if ((int)line.find("AUDIO") >= 0)
				{
					isAudio = true;
				}
			}
		}
	}

	if (trackcount == 1) trackinfos[1].lbaStart = 0;

	if (infile)
	{
		lba = totalLBAs;
		if (trackcount == 1 && !isAudio) totalLBAs += PREGAPSIZE;
		lba += PREGAPSIZE;
		lba /= FRAMES_PER_SECOND;
		second = BINBCD(lba % SECONDS_PER_MINUTE);
		lba /= SECONDS_PER_MINUTE;
		minute = BINBCD(lba);

		trackinfos[trackcount] = { basepath + binname, isAudio, totalLBAs, totalLBAs + lbasize - 1, minute, second };
		totalLBAs += lbasize;
	}

	lba = totalLBAs;
	lba /= FRAMES_PER_SECOND;
	totalSecondsBCD = BINBCD(lba % SECONDS_PER_MINUTE);
	lba /= SECONDS_PER_MINUTE;
	totalMinutesBCD = BINBCD(lba);

	// write binary file with content
	UInt32 exportdata[400];
	exportdata[0] = trackcount | (BINBCD(trackcount) << 8);
	exportdata[1] = totalLBAs;
	exportdata[2] = totalSecondsBCD | (totalMinutesBCD << 8);
	exportdata[3] = 0; // reserved

	for (int i = 1; i < 100; i++)
	{
		if (trackcount >= i)
		{
			exportdata[0 + i * 4] = trackinfos[i].lbaStart;
			exportdata[1 + i * 4] = trackinfos[i].lbaEnd;
			exportdata[2 + i * 4] = trackinfos[i].secondsBCD | (trackinfos[i].minutesBCD << 8) | (trackinfos[i].isAudio << 16);
			exportdata[3 + i * 4] = 0; // reserved
		}
		else
		{
			exportdata[0 + i * 4] = 0;
			exportdata[1 + i * 4] = 0;
			exportdata[2 + i * 4] = 0;
			exportdata[3 + i * 4] = 0;
		}
	}

	// write list of tracks
	FileIO.writefile(exportdata, "R:\\cuedata.bin", 400 * 4, true);

	FILE*  file = fopen("R:\\cdtracks.txt", "w");
	for (int i = 1; i <= trackcount; i++)
	{
		fputs(trackinfos[i].filename.c_str(), file);
		fputc('\n', file);
	}
	fclose(file);

	std::free(cuedata);
}

void CDROM::writeReg(byte index, byte value)
{
	CDOutCapture(9, index, value);

	if (index == 0)
	{
		PSXRegs.CDROM_STATUS &= ~3;
		PSXRegs.CDROM_STATUS |= (value & 3);
	}
	else
	{
		switch (PSXRegs.CDROM_STATUS & 3)
		{
		case 0:
		{
			switch (index)
			{
			case 1: 
				beginCommand(value); 
				updateStatus();
				break;
			case 2:
				if (fifoParam.size() == 16) 
					fifoParam.pop_front();
				fifoParam.push_back(value);
				updateStatus();
				break;
			case 3:
				if ((value >> 7) & 1)
				{
					if (fifoData.empty()) // don't do anything when data still inside?
					{
						// todo load data
						if (sectorBufferSizes[readSectorPointer] == 0) 
							sectorBufferSizes[readSectorPointer] = RAW_SECTOR_OUTPUT_SIZE;
						for (int i = 0; i < sectorBufferSizes[readSectorPointer]; i++)
						{
							fifoData.push_back(sectorBuffers[readSectorPointer][i]);
						}
						for (int i = 0; i < sectorBufferSizes[readSectorPointer]; i+=4)
						{
							CDOutCapture(2, i, sectorBuffers[readSectorPointer][i] | (sectorBuffers[readSectorPointer][i + 1] << 8) | (sectorBuffers[readSectorPointer][i + 2] << 16) | (sectorBuffers[readSectorPointer][i + 3] << 24));
						}
						sectorBufferSizes[readSectorPointer] = 0;
						if (sectorBufferSizes[writeSectorPointer] > 0) // additional irq for missed sector
						{
							ackRead();
						}
					}
				}
				else
				{
					while (!fifoData.empty()) fifoData.pop_front();
				}
				updateStatus();
				break;
			}
		}
			break;

		case 1:
		{
			switch (index)
			{
			case 1: break; // sound map write -> do nothing
			case 2:
				PSXRegs.CDROM_IRQENA = value & 0x1F;
				if (PSXRegs.CDROM_IRQENA & PSXRegs.CDROM_IRQFLAG) PSXRegs.setIRQ(2);
				break;
			case 3:
				PSXRegs.CDROM_IRQFLAG &= ~(value & 0x1F);
				if (PSXRegs.CDROM_IRQFLAG == 0)
				{
					if (pendingDriveIRQ > 0)
					{
						if (pendingDriveIRQ == 1) readSectorPointer = writeSectorPointer;
						while (!fifoResponse.empty()) fifoResponse.pop_front();
						ackDrive(pendingDriveResponse);
						setIRQ(pendingDriveIRQ);
						pendingDriveIRQ = 0;
						updateStatus();
					}
					else
					{
						if (delay > 0) 
							busy = true; // if interrupt is gone and work is still to be done -> reactivate
					}
				}
				if ((value >> 6) & 1)
				{
					while (!fifoParam.empty()) fifoParam.pop_front();
					updateStatus();
				}
				break;
			}
		}
		break;

		case 2:
		{
			switch (index)
			{
			case 1: break; // sound map coding info write -> do nothing
			case 2: cdvol_next00 = value; break;
			case 3: cdvol_next01 = value; break;
			}
		}
		break;

		case 3:
		{
			switch (index)
			{
			case 1: cdvol_next11 = value; break;
			case 2: cdvol_next10 = value; break;
			case 3: 
				xa_muted = (value & 1) == 1;
				if (((value >> 5) & 1) == 1)
				{
					cdvol_00 = cdvol_next00;
					cdvol_01 = cdvol_next01;
					cdvol_10 = cdvol_next10;
					cdvol_11 = cdvol_next11;
				}
				break; 
			}
		}
		break;
		}
	}
}

byte CDROM::readReg(byte index)
{
	byte retval = 0;

	// Trying to do a 32bit read from 1F801800h returns the 8bit value at 1F801800h multiplied by 01010101h -> really?
	switch (index)
	{
	case 0:
		retval = PSXRegs.CDROM_STATUS;
		break;

	case 1: // response fifo
	{
		byte data = 0;
		if (!fifoResponse.empty())
		{
			data = fifoResponse.front();
			fifoResponse.pop_front();
		}
		updateStatus();
		retval = data;
	}
	break;

	case 2: // data fifo
	{
		if (fifoData.empty()) return 0; // check not in duckstation, what happens there when empty?
		byte data = fifoData.front();
		fifoData.pop_front();
		updateStatus();
		retval = data;
	}
	break;

	case 3:
		if (PSXRegs.CDROM_STATUS & 1)
		{
			retval = PSXRegs.CDROM_IRQFLAG | 0xE0;
		}
		else
		{
			retval = PSXRegs.CDROM_IRQENA | 0xE0;
		}
		break;
	}

	CDOutCapture(8, index, retval);

	return retval;
}

byte CDROM::readDMA()
{
	if (fifoData.empty())
	{
		return 0;
	}
	byte data = fifoData.front();
	fifoData.pop_front();
	updateStatus();

#ifdef FPGACOMPATIBLE
	CDOutCapture(10, 0, data);
#endif

	return data;
}

void CDROM::updateStatus()
{
	PSXRegs.CDROM_STATUS &= 0x3;
	if (fifoParam.empty()) PSXRegs.CDROM_STATUS |= 1 << 3;
	if (!(fifoParam.size() == 16)) PSXRegs.CDROM_STATUS |= 1 << 4;
	if (!fifoResponse.empty()) PSXRegs.CDROM_STATUS |= 1 << 5;
	if (!fifoData.empty()) PSXRegs.CDROM_STATUS |= 1 << 6;
	if (cmdPending) PSXRegs.CDROM_STATUS |= 1 << 7;

	DMA.requests[3] = !fifoData.empty();
}

void CDROM::setIRQ(byte value)
{
	if (PSXRegs.CDROM_IRQFLAG != 0)
	{
		int a = 5;
	}

	PSXRegs.CDROM_IRQFLAG = value;
#ifdef FPGACOMPATIBLE
	if (value == 1)
	{
		irqDelay = 3;
	}
	else if (value == 2)
	{
		//if (PSXRegs.CDROM_IRQENA & PSXRegs.CDROM_IRQFLAG) PSXRegs.setIRQ(2);
		irqDelay = 1;
	}
	else if(value == 3)
	{
		irqDelay = 1;
	}
	else
	{
		irqDelay = 2;
	}
#else
	if (PSXRegs.CDROM_IRQENA & PSXRegs.CDROM_IRQFLAG) PSXRegs.setIRQ(2);
#endif
}

void CDROM::errorResponse(byte error, byte reason)
{
	fifoResponse.push_back(internalStatus | error);
	fifoResponse.push_back(reason);
	setIRQ(5);

	updateStatus();

	CDOutCapture(5, 0, internalStatus | (error << 8) | (reason << 16));
	CDOutCapture(4, internalStatus | error, fifoResponse.size());
}

void CDROM::ack()
{
	fifoResponse.push_back(internalStatus);
	setIRQ(3);

	updateStatus();

	CDOutCapture(3, internalStatus, fifoResponse.size());
}

void CDROM::ackDrive(byte value)
{
	fifoResponse.push_back(value);

	updateStatus();

	CDOutCapture(4, value, fifoResponse.size());
}

void CDROM::ackRead()
{
	if (PSXRegs.CDROM_IRQFLAG == 1)
	{
		// IRQ still pending -> sector missed 
		int a = 5;
	}

	//if (PSXRegs.CDROM_IRQFLAG != 0)
	else if (PSXRegs.CDROM_IRQFLAG != 0)
	{
		pendingDriveIRQ = 1;
		pendingDriveResponse = internalStatus;
	}
	else
	{
		readSectorPointer = writeSectorPointer;
		ackDrive(internalStatus);
		setIRQ(1);
	}

	updateStatus();
}

void CDROM::startMotor()
{
	if (driveState != DRIVESTATE::SPINNINGUP)
	{
		driveState = DRIVESTATE::SPINNINGUP;
		driveBusy = true;
		driveDelay = 44100 * 300;
	}
}

void CDROM::updatePositionWhileSeeking()
{
	float done = 1.0f - ((float)driveDelay / (float)driveDelayNext);

	if (seekEndLBA > seekStartLBA)
	{
		currentLBA = seekStartLBA + ((UInt32)((seekEndLBA - seekStartLBA) * done));
	}
	else
	{
		currentLBA = seekStartLBA - ((UInt32)((seekStartLBA - seekEndLBA) * done));
	}

	readSubchannel(currentLBA);
	for (int i = 0; i < 12; i++)
	{
		subdata[i] = nextSubdata[i];
	}

	physicalLBA = currentLBA;
	physicalLBATick = CPU.totalticks;
	physicalLBAcarry = 0;
}

void CDROM::updatePhysicalPosition(bool logical)
{
	if (driveState == DRIVESTATE::SEEKLOGICAL || driveState == DRIVESTATE::SEEKPHYSICAL || driveState == DRIVESTATE::SEEKIMPLICIT || driveState == DRIVESTATE::READING || driveState == DRIVESTATE::PLAYING || ((internalStatus >> 1) & 1) == 0)
	{
		return;
	}

	UInt32 ticks = CPU.totalticks;

	UInt32 readTicks = 44100 * 0x300;
	if ((modeReg >> 7) & 1) readTicks /= 150;
	else readTicks /= 75;

	UInt32 diff = ticks - physicalLBATick + physicalLBAcarry;
	UInt32 sectorDiff = diff / readTicks;

#ifdef FPGACOMPATIBLE
	sectorDiff = 1; // hack
#endif

	if (sectorDiff > 0)
	{
		UInt32 holdOffset = 2;
		UInt32 sectorPerTrack = 4;
		if (!lastSectorHeaderValid)
		{
			holdOffset = 0;
			sectorPerTrack = 7.0f + 2.811844405f * std::log((float)(currentLBA / 4500) + 1);
		}
#ifdef FPGACOMPATIBLE
		sectorPerTrack = 32; // hack
#endif

		UInt32 holdPosition = currentLBA + holdOffset;
		UInt32 base = holdPosition;
		if (holdPosition >= (sectorPerTrack - 1)) base = holdPosition - (sectorPerTrack - 1);

		if (physicalLBA < base) physicalLBA = base;

		UInt32 oldOffset = physicalLBA - base;
		UInt32 newOffset = (oldOffset + sectorDiff) % sectorPerTrack;
		Uint32 newPhysicalLBA = base + newOffset;

		if (newPhysicalLBA != physicalLBA)
		{
			physicalLBA = newPhysicalLBA;
			physicalLBATick = ticks;
			physicalLBAcarry = diff % readTicks;

			readSubchannel(physicalLBA);
			for (int i = 0; i < 12; i++)
			{
				subdata[i] = nextSubdata[i];
			}

			if (logical)
			{
				UInt32 filePos = (physicalLBA - startLBA) * RAW_SECTOR_SIZE;

				for (int i = 0; i < 4; i++)
				{
					if ((filePos + SECTOR_SYNC_SIZE + 4 + i) >= Memory.GameRom_max) header[i] = 0;
					else header[i] = Memory.GameRom[filePos + SECTOR_SYNC_SIZE + i];

					if ((filePos + SECTOR_SYNC_SIZE + i) >= Memory.GameRom_max) subheader[i] = 0;
					else subheader[i] = Memory.GameRom[filePos + SECTOR_SYNC_SIZE + 4 + i];
				}
			}
		}
	}
}

Int32 CDROM::getSeekTicks()
{
	Int32 ticks = 20000;

#ifdef FPGACOMPATIBLE
	if ((modeReg >> 7) & 1) ticks = 44100 * 0x300 / 150;
	else ticks = 44100 * 0x300 / 75;

	UInt32 diffLBA;
	if (seekEndLBA > currentLBA) diffLBA = seekEndLBA - currentLBA;
	else diffLBA = currentLBA - seekEndLBA;

	if (diffLBA == 0)
	{
		ticks = ticks;
	}
	else if (diffLBA < 5)
	{
		ticks += ticks * diffLBA;
	}
	else if (diffLBA < 32)
	{
		ticks += ticks * 5;
	}
	else if (diffLBA <= 75)
	{
		//ticks = 3060619;
		ticks += ticks * (5 + (diffLBA / 8));
	}
	else if (diffLBA <= 4500)
	{
		//ticks = 7810537;
		ticks += ticks * (14 + (diffLBA / 256));
	}
	else
	{
		//ticks = 21408428;
		ticks += ticks * (31 + (diffLBA / 8192));
		if (seekEndLBA == 57982)
		{
			int a = 5;
			//ticks = 20000;
		}
	}

#else
	ticks = 20000;

#ifndef INSTANTSEEK
	if (driveState == DRIVESTATE::SEEKLOGICAL || driveState == DRIVESTATE::SEEKPHYSICAL || driveState == DRIVESTATE::SEEKIMPLICIT)
		ticks += driveDelay;
	else
		updatePhysicalPosition(false);

	UInt32 diffLBA;
	if (seekEndLBA > currentLBA) diffLBA = seekEndLBA - currentLBA;
	else diffLBA = currentLBA - seekEndLBA;

	UInt32 sectorticks = 44100 * 0x300 / 75;
	if ((modeReg >> 7) & 1) sectorticks = 44100 * 0x300 / 150;

	if (diffLBA < 5)
	{
		ticks = sectorticks * diffLBA;
	}
	else if (diffLBA < 32)
	{
		ticks += sectorticks * 5; // 1128960 or 2257920
	}
	else
	{
		// This is a still not a very accurate model, but it's roughly in line with the behavior of hardware tests.
		const float disc_distance = 0.2323384936f * std::log(static_cast<float>((seekEndLBA / 4500) + 1u)); // about 0.0-1.0

		float seconds;
		if (diffLBA <= 75)
		{
			// 30ms + (diff * 30ms) + (disc distance * 30ms) => 1449584 - 2032128(new_lba 0) / 3060619 (new_lba 350000)
			seconds = 0.03f + ((static_cast<float>(diffLBA) / static_cast<float>(75)) * 0.03f) +
				(disc_distance * 0.03f);
		}
		else if (diffLBA <= 4500)
		{
			// 150ms + (diff * 30ms) + (disc distance * 50ms) => 5097480 - 6096384(new_lba 0)  / 7810537 (new_lba 350000)
			seconds = 0.15f + ((static_cast<float>(diffLBA) / static_cast<float>(4500)) * 0.03f) +
				(disc_distance * 0.05f);
		}
		else
		{
			// 200ms + (diff * 500ms) => 6961962 - 21408428
			seconds = 0.2f + ((static_cast<float>(diffLBA) / static_cast<float>(72 * 4500)) * 0.4f);
		}

		ticks += static_cast<UInt32>(seconds * static_cast<float>(44100 * 0x300));
	}
#endif

#endif

	return ticks - 1;
}

void CDROM::seekOnDisk(bool useLBA, UInt32 lba)
{
	setLocActive = false;
	UInt32 seekLBA = lba;
	if (!useLBA)
	{
		seekLBA = setLocMinute * FRAMES_PER_MINUTE + setLocSecond * FRAMES_PER_SECOND + setLocFrame;
	}
	seekStartLBA = currentLBA;
	seekEndLBA = seekLBA;
	readOnDisk(seekLBA);
	driveBusy = true;
	driveDelay = getSeekTicks();
	driveDelayNext = driveDelay;
	internalStatus &= 0x1F; // ClearActiveBits
	internalStatus |= (1 << 1); // motor on
	internalStatus |= (1 << 6); // seeking
	if (nextCmd == 0x15) driveState = DRIVESTATE::SEEKLOGICAL;
	else driveState = DRIVESTATE::SEEKPHYSICAL;
	lastSectorHeaderValid = false;
	resetXaDecoder();
}

bool CDROM::completeSeek()
{
	bool seekOk = true;

	// check for crc valid
	// check if current position matches subchannel position

	for (int i = 0; i < 12; i++)
	{
		subdata[i] = nextSubdata[i];
	}

	UInt32 lba = lastReadSector;
	byte frame = lba % FRAMES_PER_SECOND;
	lba /= FRAMES_PER_SECOND;
	byte second = lba % SECONDS_PER_MINUTE;
	lba /= SECONDS_PER_MINUTE;
	byte minute = lba;

	frame = BINBCD(frame);
	second = BINBCD(second);
	minute = BINBCD(minute);

	if (subdata[7] != minute || subdata[8] != second || subdata[9] != frame) seekOk = false;

	if (seekOk)
	{
		if ((subdata[0] >> 6) & 1) // isData
		{
			if (driveState == DRIVESTATE::SEEKLOGICAL)
			{
				// header
				for (int i = 0; i < 4; i++)
				{
					header[i] = sectorBuffer[SECTOR_SYNC_SIZE + i];
					subheader[i] = sectorBuffer[SECTOR_SYNC_SIZE + i + 4];
				}
				lastSectorHeaderValid = true;
				if (header[0] != minute || header[1] != second || header[2] != frame) seekOk = false;
			}
		}
		else
		{
			if (driveState == DRIVESTATE::SEEKLOGICAL)
			{
				seekOk = modeReg & 1; // cdda
			}
		}
		if (subdata[1] == LEAD_OUT_TRACK_NUMBER)
		{
			seekOk = false;
		}
	}

	currentLBA = lastReadSector;
	physicalLBA = currentLBA;
	physicalLBATick = CPU.totalticks;
	physicalLBAcarry = 0;

	if (!seekOk)
	{
		int a = 5;
	}

	return seekOk;
}

void CDROM::readOnDisk(UInt32 sector)
{
	int track = LEAD_OUT_TRACK_NUMBER;
	for (int i = 1; i <= trackcount; i++)
	{
		if (sector >= trackinfos[i].lbaStart && sector <= trackinfos[i].lbaEnd)
		{
			track = i;
			break;
		}
	}

	if (track != LEAD_OUT_TRACK_NUMBER && track > 0 && (trackNumberBCD != BINBCD(track) || AudioRom == NULL))
	{
		trackNumber = track;
		trackNumberBCD = BINBCD(track);
		if (trackinfos[track].isAudio)
		{
			if (AudioRom != NULL)
			{
				std::free(AudioRom);
			}
			AudioRom_max = FileIO.fileSize(trackinfos[track].filename, true);
			AudioRom = (Byte*)std::malloc(AudioRom_max);
			FileIO.readfile(AudioRom, trackinfos[track].filename, true);
		}
	}

	positionInIndex = sector - trackinfos[track].lbaStart;
	//if (track == 1 && !trackinfos[1].isAudio) positionInIndex -= PREGAPSIZE; // ?? why not for audio? Audio still gets the pregap

	UInt32 filePos = positionInIndex * RAW_SECTOR_SIZE;

	if (track == 1)
	{
		for (int i = 0; i < RAW_SECTOR_SIZE; i++)
		{
			if (filePos + i >= Memory.GameRom_max) sectorBuffer[i] = 0;
			else sectorBuffer[i] = Memory.GameRom[filePos + i];
		}
	}
	else
	{
		for (int i = 0; i < RAW_SECTOR_SIZE; i++)
		{
			if (filePos + i >= AudioRom_max) sectorBuffer[i] = 0;
			else sectorBuffer[i] = AudioRom[filePos + i];
		}
	}

	if (sector == 0x4750)
	{
		int a = 5;
	}

	CDOutCapture(12, 0, sector);

	lastReadSector = sector;

	readSubchannel(sector);
}

void CDROM::readSubchannel(UInt32 sector)
{
	positionInIndex = sector - trackinfos[trackNumber].lbaStart;

	// subchannel data -> don't update when sbi hit
	nextSubdata[0] = 0x00; // index control bits
	if (!trackinfos[trackNumber].isAudio) nextSubdata[0] |= 0x40;
	nextSubdata[1] = trackNumberBCD;

	UInt32 lba = positionInIndex;
	if (positionInIndex >= PREGAPSIZE)
	{
		nextSubdata[2] = 1; // index number
		if (trackinfos[trackNumber].isAudio) lba -= PREGAPSIZE;
	}
	else
	{
		nextSubdata[2] = 0; // index number
		lba = PREGAPSIZE - positionInIndex - 1;
	}

#ifdef FPGACOMPATIBLE
	if (sector < startLBA)
	{
		lba = 0;
	}
#endif
	byte frame = lba % FRAMES_PER_SECOND;
	lba /= FRAMES_PER_SECOND;
	byte second = lba % SECONDS_PER_MINUTE;
	lba /= SECONDS_PER_MINUTE;
	byte minute = lba;

	nextSubdata[3] = BINBCD(minute);
	nextSubdata[4] = BINBCD(second);
	nextSubdata[5] = BINBCD(frame);

	lba = sector;
	frame = lba % FRAMES_PER_SECOND;
	lba /= FRAMES_PER_SECOND;
	second = lba % SECONDS_PER_MINUTE;
	lba /= SECONDS_PER_MINUTE;
	minute = lba;

	nextSubdata[7] = BINBCD(minute);
	nextSubdata[8] = BINBCD(second);
	nextSubdata[9] = BINBCD(frame);

	// crc not checked anywhere?
}

void CDROM::startReading(bool afterSeek)
{
	for (int i = 0; i < NUM_SECTOR_BUFFERS; i++)
	{
		sectorBufferSizes[i] = 0;
	}

	if (!afterSeek && setLocActive)
	{
		seekOnDisk(false, 0);
		readAfterSeek = true;
		playAfterSeek = false;
	}
	else if (driveState == DRIVESTATE::SEEKLOGICAL || driveState == DRIVESTATE::SEEKPHYSICAL || driveState == DRIVESTATE::SEEKIMPLICIT)
	{
		readAfterSeek = true;
		playAfterSeek = false;
	}
	else
	{
		internalStatus &= 0x1F; // ClearActiveBits
		internalStatus |= (1 << 1); // motor on
		driveBusy = true;
		driveState = DRIVESTATE::READING;
		writeSectorPointer = 0;
		readSectorPointer = 0;
		resetXaDecoder();
#ifdef FASTCD
		driveDelay = 10000;
		driveDelayNext = 10000;
		driveDelay--;
#else
		driveDelayNext = (44100 * 0x300);
		if ((modeReg >> 7) & 1) driveDelayNext /= 150;
		else driveDelayNext /= 75;
		driveDelay = driveDelayNext;
#ifndef FPGACOMPATIBLE
		if (!afterSeek)
		{
			driveDelay += getSeekTicks();
		}
#endif // !FPGACOMPATIBLE

		readOnDisk(currentLBA);
#endif
	}
}

void CDROM::startPlaying(byte track, bool afterSeek)
{
	currentTrackBCD = track;
	fastForwardRate = 0;

	//fastForwardRate = -4; // debug testing!

	for (int i = 0; i < NUM_SECTOR_BUFFERS; i++)
	{
		sectorBufferSizes[i] = 0;
	}

	if (track > 0)
	{
		seekOnDisk(true, trackinfos[BCDBIN(track)].lbaStart + PREGAPSIZE);
		readAfterSeek = false;
		playAfterSeek = true;
	}
	else if (setLocActive)
	{
		seekOnDisk(false, 0);
		readAfterSeek = false;
		playAfterSeek = true;
	}
	else
	{
		internalStatus &= 0x1F; // ClearActiveBits
		internalStatus |= (1 << 1); // motor on
		internalStatus |= (1 << 7); // playing_cdda
		driveBusy = true;
		driveState = DRIVESTATE::PLAYING;
		writeSectorPointer = 0;
		readSectorPointer = 0;
		resetXaDecoder();
#ifdef FASTCD
		driveDelay = 10000;
		driveDelayNext = 10000;
#else
		driveDelayNext = (44100 * 0x300);
		if ((modeReg >> 7) & 1) driveDelayNext /= 150;
		else driveDelayNext /= 75;
		driveDelay = driveDelayNext;
#ifndef FPGACOMPATIBLE
		if (!afterSeek)
		{
			driveDelay += getSeekTicks();
		}
#endif
#endif
		driveDelay--;

		readOnDisk(currentLBA);
	}
}

void CDROM::processDataSector()
{
	byte newPointer = (writeSectorPointer + 1) & 0x7;

	// header
	for (int i = 0; i < 4; i++)
	{
		header[i] = sectorBuffer[SECTOR_SYNC_SIZE + i];
		subheader[i] = sectorBuffer[SECTOR_SYNC_SIZE + i + 4];
	}
	lastSectorHeaderValid = true;

	internalStatus |= (1 << 5); // reading

	if ((modeReg >> 6) & 1) // xa_enable
	{
		if (header[3] == 2)
		{
			if ((subheader[2] >> 6) & 1) // realtime
			{
				if ((subheader[2] >> 2) & 1) // audio
				{
					processXAADPCMSector();
					return;
				}
			}
		}
	}

	byte offset;
	UInt16 size;
	if ((modeReg >> 5) & 1) // raw sector read
	{
		offset = 0;
		size = RAW_SECTOR_OUTPUT_SIZE;
	}
	else
	{
		if (header[3] != 2) return;
		offset = 12;
		size = DATA_SECTOR_SIZE;
	}
	sectorBufferSizes[newPointer] = size;
	for (int i = 0; i < size; i++)
	{
		sectorBuffers[newPointer][i] = sectorBuffer[i + offset + SECTOR_SYNC_SIZE];
	}

	writeSectorPointer = newPointer;

	CDOutCapture(7, size & 0xFF, writeSectorPointer);

	busy = false;

	ackRead();
}

void CDROM::processCDDASector()
{
	if (driveState == DRIVESTATE::PLAYING && ((modeReg >> 2) & 1) == 1)
	{
		if (lastreportCDDA != (nextSubdata[9] >> 4))
		{
			lastreportCDDA = nextSubdata[9] >> 4;

			if (PSXRegs.CDROM_IRQFLAG == 0)
			{
				while (!fifoResponse.empty()) fifoResponse.pop_front();

				fifoResponse.push_back(internalStatus);
				fifoResponse.push_back(nextSubdata[1]); // track number BCD
				fifoResponse.push_back(nextSubdata[2]); // track index

				if ((nextSubdata[9] & 0x10) == 0x10)
				{
					fifoResponse.push_back(nextSubdata[3]);
					fifoResponse.push_back(0x80 | nextSubdata[4]);
					fifoResponse.push_back(nextSubdata[5]);
				}
				else
				{
					fifoResponse.push_back(nextSubdata[7]);
					fifoResponse.push_back(nextSubdata[8]);
					fifoResponse.push_back(nextSubdata[9]);
				}

				byte channel = nextSubdata[8] & 1;
				Int16 peakvolume = 0;
				int start = channel * 2;
				for (int i = start; i < RAW_SECTOR_SIZE; i += 4) // find peak volume -> no$ absolute peak of lower 15 bit, duckstation -> only use positive values
				{
					Int16 value = sectorBuffer[i + 0] | (sectorBuffer[i + 1] << 8);

					if (value == 0x2A48)
					{
						int a = 5;
					}

					if (value > peakvolume) peakvolume = value;
				}
				Int16 peakvalue = (((Int16)channel) << 15) | peakvolume;
				fifoResponse.push_back(peakvalue & 0xFF);
				fifoResponse.push_back(peakvalue >> 8);

				setIRQ(1);
#ifndef FPGACOMPATIBLE
				CDOutCapture(4, internalStatus, fifoResponse.size());
#endif // !FPGACOMPATIBLE
			}
		}
	}

	// muted
	if (muted) return;

	for (int i = 0; i < RAW_SECTOR_SIZE; i+=4)
	{
		Int16 left  = sectorBuffer[i+0] | (sectorBuffer[i + 1] << 8);
		Int16 right = sectorBuffer[i+2] | (sectorBuffer[i + 3] << 8);
		CDOutCapture(11, i / 4, (UInt16)left | (((UInt32)((UInt16)right)) << 16));
		if (testaudio)
		{
			Sound.soundGenerator.fill(left);
			Sound.soundGenerator.fill(right);
		}

		Int32 left_sum = (left * cdvol_00) >> 7;
		left_sum += (right * cdvol_10) >> 7;

		Int32 right_sum = (left * cdvol_01) >> 7;
		right_sum += (right * cdvol_11) >> 7;

		if (left_sum < (Int16)0x8000) left = 0x8000; else if (left_sum > 0x7FFF) left = 0x7FFF; else left = (Int16)left_sum;
		if (right_sum < (Int16)0x8000) right = 0x8000; else if (right_sum > 0x7FFF) right = 0x7FFF; else right = (Int16)right_sum;

		UInt32 value = ((UInt32)right << 16);
		value |= ((UInt32)left) & 0xFFFF;
		if (XaFifo.size() < 50000) XaFifo.push_back(value);

	}
}

void CDROM::resetXaDecoder()
{
	XaCurrentFile = 0;
	XaCurrentChannel = 0;
	XaCurrentSet = false;

	XaRing_pointer = 0;
	for (int i = 0; i < 32; i++)
	{
		XaRing_buffer[i][0] = 0;
		XaRing_buffer[i][1] = 0;
	}
	AdpcmLast0_L = 0;
	AdpcmLast1_L = 0;
	AdpcmLast0_R = 0;
	AdpcmLast1_R = 0;
	XaSixStep = 0;
}

void CDROM::processXAADPCMSector()
{
	byte fileNumber = sectorBuffer[16 + 0];
	byte channelNumber = sectorBuffer[16 + 1];
	byte submode = sectorBuffer[16 + 2];
	byte codingInfo = sectorBuffer[16 + 3];

	byte bitsPerSample = (codingInfo >> 4) & 3;
	byte stereo = (codingInfo >> 0) & 3;
	bool sampleRate = ((codingInfo >> 2) & 1) == 1;
	bool eof = ((submode >> 7) & 1) == 1;

	// debug tests
	//stereo = 1;
	//bitsPerSample = 0;
	//sampleRate = false;
	//eof = false;

	if ((modeReg >> 3) & 1) // xa filter
	{
		if (XaFilterChannel != subheader[1] || XaFilterFile != subheader[0]) 
			return;
	}

	if (!XaCurrentSet)
	{
		if (subheader[1] == 0xFF)
		{
			if (((modeReg >> 3) & 1) == 0 || XaFilterChannel != 255) 
				return;
		}

		XaCurrentFile = subheader[0];
		XaCurrentChannel = subheader[1];
		XaCurrentSet = true;
	}
	else if (subheader[0] != XaCurrentFile || subheader[1] != XaCurrentChannel) 
		return;

	if (eof) // EOF
	{
		XaCurrentFile = 0;
		XaCurrentChannel = 0;
		XaCurrentSet = false;
	}

	// decode sector
	for (int i = 0; i < 588; i++)
	{
		XAOutCapture(1, *(unsigned int*)&sectorBuffer[i * 4]);
	}

	// decode chunks
	UInt16 chunkPtr = 24;
	byte blockCount = 8;
	if (bitsPerSample == 1) blockCount = 4;

	int countleft = 0;
	int countright = 0;
	for (int i = 0; i < 18; i++)
	{
		for (int block = 0; block < blockCount; block++)
		{
			byte shiftfilter = sectorBuffer[chunkPtr + 4 + block];
			byte adpcmShift = shiftfilter & 0xF;
			if (adpcmShift > 12) adpcmShift = 9;
			byte AdpcmFilter = (shiftfilter >> 4) & 3;

			SByte filter_pos = filtertablePos[AdpcmFilter];
			SByte filter_neg = filtertableNeg[AdpcmFilter];

			for (int s = 0; s < 28; s++)
			{
#ifdef CDFILEOUT
				if (debug_XAOutCount == 44017)
				{
					int a = 5;
				}
#endif

				byte nibble;
				Int16 nibble16;
				if (bitsPerSample == 1)
				{
					UInt16 addr = chunkPtr + 16 + block + s * 4;
					nibble = sectorBuffer[addr];
					nibble16 = ((Int16)nibble) << 8;
				}
				else
				{
					UInt16 addr = chunkPtr + 16 + (block / 2) + s * 4;
					if ((block & 1) == 1)
					{
						nibble = sectorBuffer[addr] >> 4;
					}
					else
					{
						nibble = sectorBuffer[addr] & 0xF;
					}
					nibble16 = ((Int16)nibble) << 12;
				}

				Int32 sample = (Int32)(nibble16 >> adpcmShift);

				Int32 AdpcmLast0;
				Int32 AdpcmLast1;
				if (stereo == 1 && ((block & 1) == 1))
				{
					AdpcmLast0 = AdpcmLast0_R;
					AdpcmLast1 = AdpcmLast1_R;
				}
				else
				{
					AdpcmLast0 = AdpcmLast0_L;
					AdpcmLast1 = AdpcmLast1_L;
				}

				Int32 oldSum = (AdpcmLast0 * filter_pos);
				oldSum += (AdpcmLast1 * filter_neg) + 32;
				sample += oldSum / 64;
				AdpcmLast1 = AdpcmLast0;
				AdpcmLast0 = sample;

				if (stereo == 1 && ((block & 1) == 1))
				{
					AdpcmLast0_R = AdpcmLast0;
					AdpcmLast1_R = AdpcmLast1;
				}
				else
				{
					AdpcmLast0_L = AdpcmLast0;
					AdpcmLast1_L = AdpcmLast1;
				}

				// clamp
				Int16 clamped;
				if (sample < (Int16)0x8000) clamped = 0x8000;
				else if (sample > 0x7FFF) clamped = 0x7FFF;
				else clamped = (Int16)sample;

				if (stereo == 1)
				{
					if ((block & 1) == 1)
					{
						XASamples[countleft][1] = clamped;
						countleft++;
					}
					else
					{
						XASamples[countright][0] = clamped;
						countright++;
					}
				}
				else
				{
					XASamples[countleft][0] = clamped;
					XASamples[countleft][1] = clamped;
					countleft++;
				}
				XAOutCapture(2, clamped);
			}
		}

		chunkPtr += 128;
	}

#ifndef FPGACOMPATIBLE
	for (int i = 0; i < countleft; i++)
	{
		if (stereo == 1)
		{
			XAOutCapture(3, XASamples[i][0] | (XASamples[i][1] << 16));
		}
		else
		{
			XAOutCapture(3, XASamples[i][0]);
		}
	}
#endif

	// muted
	if (muted || xa_muted) return;
	
	// resample
	for (int i = 0; i < countleft; i++)
	{
		byte dup = 1;
		if (sampleRate) dup = 2;

		for (int j = 0; j < dup; j++)
		{
			XaRing_buffer[XaRing_pointer][0] = XASamples[i][0];
			XaRing_buffer[XaRing_pointer][1] = XASamples[i][1];
			XaRing_pointer = (XaRing_pointer + 1) & 0x1F;
			XaSixStep++;
			if (XaSixStep == 6)
			{
				XaSixStep = 0;
				for (int s = 0; s < 7; s++)
				{
#ifdef CDFILEOUT
					if (debug_XAOutCount == 135736)
					{
						int a = 5;
					}
#endif

					Int16 samples[2];
					for (byte dir = 0; dir < 2; dir++)
					{
						Int32 sum = 0;
						Int16 bufvalues[29];
						for (int r = 0; r < 29; r++)
						{
							byte index = (XaRing_pointer - r) & 0x1F;
							Int16 bufvalue = XaRing_buffer[index][dir];
							bufvalues[r] = bufvalue;
							Int16 mul = zigzagTable[s][r];
							sum += ((Int32)bufvalue) * ((Int32)mul) / 0x8000;
						}
						Int16 clamped;
						if (sum < (Int16)0x8000) clamped = 0x8000;
						else if (sum > 0x7FFF) clamped = 0x7FFF;
						else clamped = (Int16)sum;

						samples[dir] = clamped;
					}

					UInt32 value = ((UInt32)samples[1] << 16);
					value |= ((UInt32)samples[0]) & 0xFFFF;
					XAOutCapture(4, value);

					Int32 left = (samples[0] * cdvol_00) >> 7;
					left += (samples[1] * cdvol_10) >> 7;

					Int32 right = (samples[0] * cdvol_01) >> 7;
					right += (samples[1] * cdvol_11) >> 7;

					if (left  < (Int16)0x8000) samples[0] = 0x8000; else if (left  > 0x7FFF) samples[0] = 0x7FFF; else samples[0] = (Int16)left;
					if (right < (Int16)0x8000) samples[1] = 0x8000; else if (right > 0x7FFF) samples[1] = 0x7FFF; else samples[1] = (Int16)right;

					value = ((UInt32)samples[1] << 16);
					value |= ((UInt32)samples[0]) & 0xFFFF;
					if (XaFifo.size() < 50000) XaFifo.push_back(value);
					if (testaudio)
					{
						Sound.soundGenerator.fill(samples[0]);
						Sound.soundGenerator.fill(samples[1]);
					}
				}
			}
		}
	}
}

void CDROM::beginCommand(byte value)
{
	//hack if busy
	bool skip = false;
	if (cmdPending)
	{
		byte paramCountNew = 0;
		switch (value)
		{
		case 0x02: paramCountNew = 3; break; // Setloc
		case 0x0D: paramCountNew = 2; break; // SetFilter
		case 0x0E: paramCountNew = 1; break; // Setmode
		case 0x12: paramCountNew = 1; break; // SetSession
		case 0x14: paramCountNew = 1; break; // GetTD
		case 0x19: paramCountNew = 1; break; // Test
		case 0x1D: paramCountNew = 2; break; // GetQ
		case 0x1F: paramCountNew = 6; break; // VideoCD
		}
		byte paramCountOld = 0;
		switch (nextCmd)
		{
		case 0x02: paramCountOld = 3; break; // Setloc
		case 0x0D: paramCountOld = 2; break; // SetFilter
		case 0x0E: paramCountOld = 1; break; // Setmode
		case 0x12: paramCountOld = 1; break; // SetSession
		case 0x14: paramCountOld = 1; break; // GetTD
		case 0x19: paramCountOld = 1; break; // Test
		case 0x1D: paramCountOld = 2; break; // GetQ
		case 0x1F: paramCountOld = 6; break; // VideoCD
		}

		if (paramCountOld > paramCountNew)
		{
			while (!fifoParam.empty()) fifoParam.pop_front();
			skip = true;
		}
		else
		{
			// todo: subtract the currently-elapsed ack ticks from the new command 
			// -> seems wrong, why should the cd controller be faster on the new command?
		}
	}

	if (!skip)
	{
		if (value == 0x1C) delay = 120000; // init
		else
		{
			if (driveState == DRIVESTATE::OPENING)
				delay = 15000;
			else
				delay = 25000;
		}

		busy = true;
		cmdPending = true;
		nextCmd = value;

		if (PSXRegs.CDROM_IRQFLAG != 0)
		{
			busy = false;
		}

		if (value == 0x09)
		{
			int a = 5;
		}
	}

	CDOutCapture(1, value, 0);
}

void CDROM::handleCommand()
{
	byte paramCount = 0;
	switch (nextCmd)
	{
	case 0x02: paramCount = 3; break; // Setloc
	case 0x0D: paramCount = 2; break; // SetFilter
	case 0x0E: paramCount = 1; break; // Setmode
	case 0x12: paramCount = 1; break; // SetSession
	case 0x14: paramCount = 1; break; // GetTD
	case 0x19: paramCount = 1; break; // Test
	case 0x1D: paramCount = 2; break; // GetQ
	case 0x1F: paramCount = 6; break; // VideoCD
	}

	if (fifoParam.size() < paramCount)
	{
		errorResponse(1, 0x20);
		cmdPending = false;
		updateStatus();
		return;
	}

	while (!fifoResponse.empty()) fifoResponse.pop_front();

	switch (nextCmd)
	{
	case 0x00: // Sync
		errorResponse(1, 0x40);
		cmdPending = false;
		break;

	case 0x01: // Getstat
		ack();
		cmdPending = false;
		if (hasCD && driveState != DRIVESTATE::OPENING)
		{
			internalStatus &= 0xEF; // shell close
		}
		break;

	case 0x02: // Setloc
		setLocMinute = BCDBIN(fifoParam.front()); fifoParam.pop_front();
		setLocSecond = BCDBIN(fifoParam.front()); fifoParam.pop_front();
		setLocFrame = BCDBIN(fifoParam.front()); fifoParam.pop_front();
		setLocActive = true;
		ack();
		cmdPending = false;
		break;

	case 0x03: // play
		if (!hasCD)
		{
			errorResponse(1, 0x80);
		}
		else
		{
			ack();

			byte track = 0;
			if (!fifoParam.empty())
			{
				track = fifoParam.front(); fifoParam.pop_front();
			}

			//track = 2; // debug testing!

			UInt32 seekLBA = setLocMinute * FRAMES_PER_MINUTE + setLocSecond * FRAMES_PER_SECOND + setLocFrame;
			if (track == 0 && (!setLocActive || seekLBA == lastReadSector) && (driveState == DRIVESTATE::PLAYING || ((driveState == DRIVESTATE::SEEKLOGICAL || driveState == DRIVESTATE::SEEKPHYSICAL || driveState == DRIVESTATE::SEEKIMPLICIT) && playAfterSeek)))
			{
				fastForwardRate = 0;
			}
			else
			{
				if (driveState == DRIVESTATE::SEEKLOGICAL || driveState == DRIVESTATE::SEEKPHYSICAL || driveState == DRIVESTATE::SEEKIMPLICIT)
				{
#ifndef FPGACOMPATIBLE
					updatePositionWhileSeeking();
#endif
				}
				startPlaying(track, false);

				lastreportCDDA = 0xFF;
			}
		}
		cmdPending = false;
		break;

	case 0x04: // forward
		if (driveState != DRIVESTATE::PLAYING)
		{
			errorResponse(1, 0x80);
		}
		else
		{
			if (fastForwardRate < 0) fastForwardRate = 0;
			fastForwardRate += 4;
			if (fastForwardRate > 12) fastForwardRate = 12;
			ack();
		}
		cmdPending = false;
		break;

	case 0x05: // backward
		if (driveState != DRIVESTATE::PLAYING)
		{
			errorResponse(1, 0x80);
		}
		else
		{
			if (fastForwardRate > 0) fastForwardRate = 0;
			fastForwardRate -= 4;
			if (fastForwardRate < -12) fastForwardRate = -12;
			ack();
		}
		cmdPending = false;
		break;

	// case 06: readN at ReadS 0x1B

	case 0x07: // MotorOn
		if ((internalStatus >> 1) & 1)
		{
			errorResponse(1, 0x20);
		}
		else
		{
			ack();
			if (!working)
			{
				working = true;
				workDelay = 399999;
				workCommand = nextCmd;
				if (hasCD)
				{
					if (driveState != DRIVESTATE::SPINNINGUP)
					{
						driveState = DRIVESTATE::SPINNINGUP;
						driveBusy = true;
						driveDelay = 44100 * 0x300;
						driveDelayNext = driveDelay;
					}
				}
			}
		}
		cmdPending = false;
		break;

	case 0x08: // stop
		ack();
		if ((internalStatus >> 1) & 1)
		{
			if ((modeReg >> 7) & 1) workDelay = 25000000;
			else workDelay = 13000000;
		}
		else workDelay = 7000;
		workDelay--;
		working = true;
		workCommand = nextCmd;
		cmdPending = false;
		internalStatus &= 0x1D; // ClearActiveBits and motor
		driveState = DRIVESTATE::IDLE;
		driveBusy = false;
		lastSectorHeaderValid = false;
		break;

	case 0x09: // pause
	{
		ack();
		cmdPending = false;
		Int32 pauseTime = 7000;
		if (driveState == DRIVESTATE::READING || driveState == DRIVESTATE::PLAYING)
		{
			if ((modeReg >> 7) & 1) pauseTime = 2000000;
			else pauseTime = 1000000;
		}
		if (driveState == DRIVESTATE::SEEKLOGICAL || driveState == DRIVESTATE::SEEKPHYSICAL || driveState == DRIVESTATE::SEEKIMPLICIT)
		{
			// todo: complete seek
			int a = 5;
		}
		else
		{
			driveState = DRIVESTATE::IDLE;
			driveBusy = false;
			internalStatus &= 0x1F; // ClearActiveBits
		}
		resetXaDecoder();
		working = true;
		workDelay = pauseTime - 1;
		workCommand = nextCmd;
	}
		break;

	case 0x0A: // reset
		ack();

		if (currentLBA == 0x174)
		{
			int a = 5;
		}

		if (working && workCommand == 0x0A)
		{
			cmdPending = false;
		}
		else
		{
			if (driveState == DRIVESTATE::SEEKLOGICAL || driveState == DRIVESTATE::SEEKPHYSICAL || driveState == DRIVESTATE::SEEKIMPLICIT)
			{
#ifndef FPGACOMPATIBLE
				//updatePositionWhileSeeking();
#endif
			}
			softReset();
			working = true;
			workDelay = 399999;
			workCommand = nextCmd;

			delay = 24999; // call here second time, so response has new values after reset?
			busy = true;
		}
		break;

	case 0x0B: // mute
		muted = true;
		ack();
		cmdPending = false;
		break;

	case 0x0C: // demute
		muted = false;
		ack();
		cmdPending = false;
		break;

	case 0x0D: // setfilter
		XaFilterFile = fifoParam.front(); fifoParam.pop_front();
		XaFilterChannel = fifoParam.front(); fifoParam.pop_front();
		XaCurrentSet = false;
		ack();
		cmdPending = false;
		break;

	case 0x0E: // Setmode
	{
		byte newMode = fifoParam.front();
		bool speedchange = ((newMode & 0x80) != (modeReg & 0x80));
		modeReg = newMode;
		ack();
		cmdPending = false;
		if (speedchange)
		{
			UInt32 tickSpeedChange = 44100 * 0x300;
			if ((modeReg >> 7) & 1) tickSpeedChange = 27095040; // 80%
			if (driveState == DRIVESTATE::SPEEDCHANGEORTOCREAD)
			{
				if (driveDelay >= tickSpeedChange / 4)
				{
					driveState = DRIVESTATE::IDLE;
					driveBusy = false;
					driveDelay = 0;
				}
			}
			else if (driveState != DRIVESTATE::SEEKIMPLICIT)
			{
				if (driveState != DRIVESTATE::IDLE)
				{
					driveDelay += tickSpeedChange;
#ifdef  FPGACOMPATIBLE
					driveDelay = tickSpeedChange;
#endif //  FPGACOMPATIBLE

				}
				else
				{
					driveBusy = true;
					driveDelay = tickSpeedChange - 1;
					driveState = DRIVESTATE::SPEEDCHANGEORTOCREAD;
				}
				driveDelayNext = driveDelay;
			}
		}
	}
		break;

	case 0x0F: // getparam
		fifoResponse.push_back(internalStatus);
		fifoResponse.push_back(modeReg);
		fifoResponse.push_back(0);
		fifoResponse.push_back(XaFilterFile);
		fifoResponse.push_back(XaFilterChannel);
		setIRQ(3);
		CDOutCapture(3, XaFilterChannel, fifoResponse.size());
		cmdPending = false;
		break;

	case 0x10: // GetLocL
		if (!lastSectorHeaderValid)
		{
			errorResponse(1, 0x80);
		}
		else
		{
			updatePhysicalPosition(true);
			for (int i = 0; i < 4; i++) fifoResponse.push_back(header[i]);
			for (int i = 0; i < 4; i++) fifoResponse.push_back(subheader[i]);
			setIRQ(3);
			CDOutCapture(3, subheader[3], fifoResponse.size());
		}
		cmdPending = false;
		break;

	case 0x11: // GetLocP
		if (!hasCD)
		{
			errorResponse(1, 0x80);
		}
		else
		{
			//if (debug_CDOutCount > 5440)
			//{
			//	int a = 5;
			//}

			if (driveState == DRIVESTATE::SEEKLOGICAL || driveState == DRIVESTATE::SEEKPHYSICAL || driveState == DRIVESTATE::SEEKIMPLICIT)
			{
#ifndef FPGACOMPATIBLE
				updatePositionWhileSeeking();
#endif
			}
			else
			{
//#ifndef FPGACOMPATIBLE
				updatePhysicalPosition(false);
//#endif
			}
			fifoResponse.push_back(subdata[1]);
			fifoResponse.push_back(subdata[2]);
			fifoResponse.push_back(subdata[3]);
			fifoResponse.push_back(subdata[4]);
			fifoResponse.push_back(subdata[5]);
			fifoResponse.push_back(subdata[7]);
			fifoResponse.push_back(subdata[8]);
			fifoResponse.push_back(subdata[9]);
			setIRQ(3);
			CDOutCapture(3, subdata[9], fifoResponse.size());
		}
		cmdPending = false;
		break;

	case 0x12: // SetSession
		if (!hasCD || driveState == DRIVESTATE::READING || driveState == DRIVESTATE::PLAYING)
		{
			errorResponse(1, 0x80);
		}
		else if (fifoParam.front() == 0) // cannot get here, because of check above? But the error code is different
		{
			errorResponse(1, 0x10);
		}
		else
		{
			session = fifoParam.front();
			driveBusy = true;
			driveDelay = (44100 * 0x300) / 2;
			driveDelayNext = driveDelay;
			driveState = DRIVESTATE::CHANGESESSION;
			ack();
		}
		cmdPending = false;
		break;

	case 0x13: // GetTN
		if (!hasCD)
		{
			errorResponse(1, 0x80);
		}
		else
		{
			fifoResponse.push_back(internalStatus);
			fifoResponse.push_back(1); // first track number
			fifoResponse.push_back(BINBCD(trackcount)); // last track number
			setIRQ(3);
			CDOutCapture(3, BINBCD(trackcount), fifoResponse.size());
		}
		cmdPending = false;
		break;

	case 0x14: // GetTD
	{
		byte track = BCDBIN(fifoParam.front());
		if (!hasCD)
		{
			errorResponse(1, 0x80);
		}
		else if (track > trackcount)
		{
			errorResponse(1, 0x10);
		}
		else
		{
			byte second = 0;
			byte minute = 0;

			if (track == 0)
			{
				second = totalSecondsBCD;
				minute = totalMinutesBCD;
			}
			else
			{
				second = trackinfos[track].secondsBCD;
				minute = trackinfos[track].minutesBCD;
			}
			fifoResponse.push_back(internalStatus);
			fifoResponse.push_back(minute);
			fifoResponse.push_back(second);
			setIRQ(3);
#ifdef FPGACOMPATIBLE
			CDOutCapture(3, second, fifoResponse.size());
#else
			CDOutCapture(3, track, fifoResponse.size());
#endif
		}
		cmdPending = false;
	}
		break;

	case 0x15: // SeekL
	case 0x16: // SeekP
	{
		// todo: if seeking, update position
		ack();
		cmdPending = false;
		seekOnDisk(false, 0);
		readAfterSeek = false;
		playAfterSeek = false;
	}
		break;

	case 0x17: // SetClock
	case 0x18: // GetClock
		errorResponse(1, 0x40);
		cmdPending = false;
		break;

	case 0x19: // test
		switch (fifoParam.front())
		{
		case 0x04: // Reset SCEx counters
			internalStatus |= 2; //motor on
			fifoResponse.push_back(internalStatus);
			setIRQ(3);
			CDOutCapture(3, internalStatus, fifoResponse.size());
			break;

		case 0x05: // Read SCEx counters
			fifoResponse.push_back(internalStatus);
			fifoResponse.push_back(0); // ?
			fifoResponse.push_back(0); // ?
			setIRQ(3);
			CDOutCapture(3, 0, fifoResponse.size());
			break;

		case 0x20: // Get CDROM BIOS Date/Version
			fifoResponse.push_back(0x95);
			fifoResponse.push_back(0x05);
			fifoResponse.push_back(0x16);
			fifoResponse.push_back(0xC1);
			setIRQ(3);
			CDOutCapture(3, 0xC1, fifoResponse.size());
			break;

		case 0x22: // region
			// todo different regions
			fifoResponse.push_back('f');
			fifoResponse.push_back('o');
			fifoResponse.push_back('r');
			fifoResponse.push_back(' ');
			fifoResponse.push_back('E');
			fifoResponse.push_back('u');
			fifoResponse.push_back('r');
			fifoResponse.push_back('o');
			fifoResponse.push_back('p');
			fifoResponse.push_back('e');
			setIRQ(3);
			CDOutCapture(3, 'e', fifoResponse.size());

			default:
				errorResponse(1, 0x40);
		}
		cmdPending = false;
		break;
	
	case 0x1A: // getID
		if (!hasCD)
		{
			errorResponse(1, 0x80);
		}
		else
		{
			ack();
			working = true;
			workDelay = 33867;
			if (driveState == DRIVESTATE::SPINNINGUP && driveBusy) workDelay += driveDelay;
			workCommand = nextCmd;
		}
		cmdPending = false;
		break;


	case 0x06: // ReadN
	case 0x1B: // ReadS
		if (!hasCD)
		{
			errorResponse(1, 0x80);
		}
		else if (trackinfos[1].isAudio && ((modeReg & 1) == 0))
		{
			errorResponse(1, 0x40);
		}
		else
		{
			// todo: missing checks
			ack();
			
			UInt32 seekLBA = setLocMinute * FRAMES_PER_MINUTE + setLocSecond * FRAMES_PER_SECOND + setLocFrame;
			if ((!setLocActive || seekLBA == lastReadSector) && (driveState == DRIVESTATE::READING || ((driveState == DRIVESTATE::SEEKLOGICAL || driveState == DRIVESTATE::SEEKPHYSICAL || driveState == DRIVESTATE::SEEKIMPLICIT) && readAfterSeek)))
			{
				setLocActive = false;
			}
			else
			{
				if (driveState == DRIVESTATE::SEEKLOGICAL || driveState == DRIVESTATE::SEEKPHYSICAL || driveState == DRIVESTATE::SEEKIMPLICIT)
				{
#ifndef FPGACOMPATIBLE
				updatePositionWhileSeeking();
#endif
				}
				if (setLocActive)
				{
					seekOnDisk(false, 0);
					readAfterSeek = true;
					playAfterSeek = false;
				}
				else
				{
					startReading(false);
				}
			}
		}
		cmdPending = false;
		break;

	case 0x1C: // init -> todo: check is that really correct?
		errorResponse(1, 0x40);
		cmdPending = false;
		break;

	case 0x1D: // GetQ
		errorResponse(1, 0x40);
		cmdPending = false;
		break;

	case 0x1E: // ReadTOC
		if (!hasCD)
		{
			errorResponse(1, 0x80);
		}
		else
		{
			ack();
			// todo : SetHoldPosition
			working = true;
			workDelay = (44100 * 0x300) / 2;
			if (!hasCD) workDelay = 1;
			workCommand = nextCmd;
		}
		cmdPending = false;
		break;

	case 0x1F: // VideoCD
		errorResponse(1, 0x40);
		cmdPending = false;
		// doesn't clear parameter fifo?
		updateStatus();
		return; 
		break;

	default:
		errorResponse(1, 0x40);
		cmdPending = false;
	}

	//endCommand
	while (!fifoParam.empty()) fifoParam.pop_front();
	updateStatus();
}

void CDROM::handleDrive()
{
	switch (driveState)
	{
	case DRIVESTATE::SEEKIMPLICIT:
		completeSeek();
		break;

	case DRIVESTATE::SEEKLOGICAL:
	case DRIVESTATE::SEEKPHYSICAL:
	{
		bool seekOk = completeSeek();
		driveState = DRIVESTATE::IDLE;
		internalStatus &= 0x1F; // ClearActiveBits
		if (seekOk)
		{
			if (readAfterSeek)
			{
				startReading(true);
				readAfterSeek = false;
			}
			else if (playAfterSeek)
			{
				startPlaying(0, true);
			}
			else
			{
				ackDrive(internalStatus);
				updateStatus();
				setIRQ(2);
			}
		}
		else
		{
			lastSectorHeaderValid = false;
			errorResponse(4, 4);
		}
	}
		break;

	case DRIVESTATE::READING:
	case DRIVESTATE::PLAYING:
	{
		// sector read
		// todo: more things to do here 
		//busy = false;
		if (nextSubdata[1] == LEAD_OUT_TRACK_NUMBER)
		{
			ackDrive(internalStatus);
			updateStatus();
			setIRQ(4);
			internalStatus &= 0x1F; // ClearActiveBits
			internalStatus &= 0xFD; // Stop Motor
			driveState = DRIVESTATE::IDLE;
		}
		else
		{
			bool dataSector = (((nextSubdata[0] >> 6) & 1) == 1);
			bool skipReading = false;
			//modeReg &= 0xFD; // debug testing of auto pause!
			if (!dataSector)
			{
				if (currentTrackBCD == 0) // auto find track number from subheader
				{
					currentTrackBCD = nextSubdata[1];
				}
				else if (nextSubdata[1] != currentTrackBCD && ((modeReg >> 1) & 1) == 1) // auto pause when track switches
				{
					skipReading = true;
					if (PSXRegs.CDROM_IRQFLAG != 0)
					{
						pendingDriveIRQ = 4;
						pendingDriveResponse = internalStatus;
					}
					else
					{
						ackDrive(internalStatus);
						updateStatus();
						setIRQ(4);
					}
					internalStatus &= 0x1F; // ClearActiveBits
					driveState = DRIVESTATE::IDLE;
				}
			}

			if (!skipReading)
			{
				Uint32 nextSector = lastReadSector + 1;
				if (dataSector && driveState == DRIVESTATE::READING)
				{
					processDataSector();
				}
				else if (!dataSector && driveState == DRIVESTATE::PLAYING || (driveState == DRIVESTATE::READING && (modeReg & 1) == 1))
				{
					processCDDASector();

					if (fastForwardRate != 0)
					{
						nextSector = lastReadSector + fastForwardRate;
					}
				}
				updateStatus();
				driveBusy = true;
				driveDelay = driveDelayNext;
				driveDelay--;
				currentLBA = lastReadSector;
				physicalLBA = currentLBA;
				physicalLBATick = CPU.totalticks;
				physicalLBAcarry = 0;

				//if (dataSector)
				{
					for (int i = 0; i < 12; i++)
					{
						subdata[i] = nextSubdata[i];
					}
				}
				readOnDisk(nextSector);
			}
		}
	}
		break;

	case DRIVESTATE::SPEEDCHANGEORTOCREAD:
		driveState = DRIVESTATE::IDLE;
		break;

	case DRIVESTATE::SPINNINGUP:
		driveState = DRIVESTATE::IDLE;
		internalStatus &= 0x1F; // ClearActiveBits
		if (hasCD) internalStatus |= 2; // motor on
		break;

	case DRIVESTATE::CHANGESESSION:
		driveState = DRIVESTATE::IDLE;
		internalStatus &= 0x1F; // ClearActiveBits
		if (hasCD) internalStatus |= 2; // motor on
		if (session == 1)
		{
			ackDrive(internalStatus);
			setIRQ(2);
		}
		else
		{
			errorResponse(4, 0x40);
		}
		break;

	case DRIVESTATE::OPENING:
		startMotor();
		break;

	default:
		_wassert(_CRT_WIDE("CD DRIVE mode not implemented"), _CRT_WIDE("CD"), (int)driveState);
	}
}

void CDROM::work()
{
#ifdef FPGACOMPATIBLE
	if (irqDelay > 0)
	{
		irqDelay--;
		if (irqDelay == 0)
		{
			if (PSXRegs.CDROM_IRQENA & PSXRegs.CDROM_IRQFLAG) PSXRegs.setIRQ(2);
		}
	}
#endif

	if (swapdisk)
	{
		internalStatus &= 0x1D; // ClearActiveBits + motor off
		internalStatus |= 0x10; // LID open
		swapdisk = false;
		while (!fifoResponse.empty()) fifoResponse.pop_front();
		cmdPending = false;
		busy = false;
		delay = 0;
		driveBusy = true;
		if ((modeReg >> 7) & 1) driveDelay = 25000000;
		else driveDelay = 13000000;
		driveState = DRIVESTATE::OPENING;
		errorResponse(1, 0x08);
	}

	if (working)
	{
		if (workDelay > 0)
		{
			workDelay--;
		}
		else
		{
			if (workCommand == 0x1A) // GetID
			{
				internalStatus &= 0x1F; // ClearActiveBits
				if (hasCD) internalStatus |= 2; // motor on
				while (!fifoResponse.empty()) fifoResponse.pop_front();
				byte stat = internalStatus;
				byte flags = 0;
				if (!hasCD)
				{
					stat |= 8;
					flags |= 0x40;
				}
				else
				{
					if (trackinfos[1].isAudio)
					{
						stat |= 8;
						flags |= 0x90;  // Unlicensed + Audio CD
					}
					// todo: region check
				}
				fifoResponse.push_back(stat);
				fifoResponse.push_back(flags);
				fifoResponse.push_back(0x20); // ??
				fifoResponse.push_back(0x00); // ??
				// todo: different region
				fifoResponse.push_back('S');
				fifoResponse.push_back('C');
				fifoResponse.push_back('E');
				fifoResponse.push_back('E');
				CDOutCapture(4, stat, fifoResponse.size());
				if (flags > 0) setIRQ(5);
				else setIRQ(2);
				working = false;
				updateStatus();
			}
			else
			{
				working = false;
				busy = false;
				ackDrive(internalStatus);
				updateStatus();
				setIRQ(2);
			}
		}
	}

	if (driveBusy)
	{
		if (driveDelay > 0)
		{
			driveDelay--;
		}
		else
		{
			driveBusy = false;
			handleDrive();
			updateStatus();
		}
	}

	if (busy)
	{
		if (delay > 0)
		{
			delay--;
		}
		else
		{
			busy = false;
			handleCommand();
		}
	}
}

void CDROM::saveState(UInt32 offset)
{
	psx.savestate_addvalue(offset + 0, 31, 0, workDelay);
	psx.savestate_addvalue(offset + 1, 31, 0, seekStartLBA);
	psx.savestate_addvalue(offset + 2, 31, 0, seekEndLBA);
	psx.savestate_addvalue(offset + 3, 31, 0, currentLBA);
	psx.savestate_addvalue(offset + 4, 31, 0, driveDelay);
	psx.savestate_addvalue(offset + 5, 31, 0, driveDelayNext);
	psx.savestate_addvalue(offset + 6, 31, 0, lastReadSector);
	psx.savestate_addvalue(offset + 7, 31, 0, physicalLBA);
	psx.savestate_addvalue(offset + 8, 31, 0, physicalLBATick);
	psx.savestate_addvalue(offset + 9, 31, 0, physicalLBAcarry);
	psx.savestate_addvalue(offset + 10, 31, 0, startLBA);
	psx.savestate_addvalue(offset + 11, 31, 0, positionInIndex);
	psx.savestate_addvalue(offset + 12, 31, 0, delay);

	psx.savestate_addvalue(offset + 13,  7,  0, internalStatus);
	psx.savestate_addvalue(offset + 13, 15,  8, modeReg);
	psx.savestate_addvalue(offset + 13, 23, 16, nextCmd);
	psx.savestate_addvalue(offset + 13, 31, 24, pendingDriveIRQ);
	psx.savestate_addvalue(offset + 14,  7,  0, pendingDriveResponse);
	psx.savestate_addvalue(offset + 14, 15,  8, workCommand);
	psx.savestate_addvalue(offset + 14, 23, 16, setLocMinute);
	psx.savestate_addvalue(offset + 14, 31, 24, setLocSecond);
	psx.savestate_addvalue(offset + 15,  7,  0, setLocFrame);
	psx.savestate_addvalue(offset + 15, 15,  8, trackNumberBCD);
	psx.savestate_addvalue(offset + 15, 23, 16, fastForwardRate);
	psx.savestate_addvalue(offset + 15, 31, 24, (byte)driveState);
	psx.savestate_addvalue(offset + 16,  7,  0, writeSectorPointer);
	psx.savestate_addvalue(offset + 16, 15,  8, readSectorPointer);
	psx.savestate_addvalue(offset + 16, 23, 16, XaFilterFile);
	psx.savestate_addvalue(offset + 16, 31, 24, XaFilterChannel);
	psx.savestate_addvalue(offset + 17,  7,  0, session);

	psx.savestate_addvalue(offset + 18,  0,  0, busy);
	psx.savestate_addvalue(offset + 18,  1,  1, cmdPending);
	psx.savestate_addvalue(offset + 18,  2,  2, working);
	psx.savestate_addvalue(offset + 18,  3,  3, setLocActive);
	psx.savestate_addvalue(offset + 18,  4,  4, driveBusy);
	psx.savestate_addvalue(offset + 18,  5,  5, readAfterSeek);
	psx.savestate_addvalue(offset + 18,  6,  6, playAfterSeek);
	psx.savestate_addvalue(offset + 18,  7,  7, muted);
	psx.savestate_addvalue(offset + 18,  8,  8, lastSectorHeaderValid);

	psx.savestate_addvalue(offset + 19,  7,  0, header[0]);
	psx.savestate_addvalue(offset + 19, 15,  8, header[1]);
	psx.savestate_addvalue(offset + 19, 23, 16, header[2]);
	psx.savestate_addvalue(offset + 19, 31, 24, header[3]);

	psx.savestate_addvalue(offset + 20,  7,  0, subheader[0]);
	psx.savestate_addvalue(offset + 20, 15,  8, subheader[1]);
	psx.savestate_addvalue(offset + 20, 23, 16, subheader[2]);
	psx.savestate_addvalue(offset + 20, 31, 24, subheader[3]);

	psx.savestate_addvalue(offset + 21,  7,  0, PSXRegs.CDROM_STATUS);
	psx.savestate_addvalue(offset + 21, 15,  8, PSXRegs.CDROM_IRQENA);
	psx.savestate_addvalue(offset + 21, 23, 16, PSXRegs.CDROM_IRQFLAG);

	psx.savestate_addvalue(offset + 22,  7,  0, XaCurrentFile);
	psx.savestate_addvalue(offset + 22, 15,  8, XaCurrentChannel);
	psx.savestate_addvalue(offset + 22, 16, 16, XaCurrentSet);
	psx.savestate_addvalue(offset + 22, 17, 17, xa_muted);

	psx.savestate_addvalue(offset + 23,  7,  0, cdvol_next00);
	psx.savestate_addvalue(offset + 23, 15,  8, cdvol_next01);
	psx.savestate_addvalue(offset + 23, 23, 16, cdvol_next10);
	psx.savestate_addvalue(offset + 23, 31, 24, cdvol_next11);
	psx.savestate_addvalue(offset + 24,  7,  0, cdvol_00);
	psx.savestate_addvalue(offset + 24, 15,  8, cdvol_01);
	psx.savestate_addvalue(offset + 24, 23, 16, cdvol_10);
	psx.savestate_addvalue(offset + 24, 31, 24, cdvol_11);

	psx.savestate_addvalue(offset + 25, 7, 0, currentTrackBCD);
	psx.savestate_addvalue(offset + 25, 15, 8, trackNumber);
	psx.savestate_addvalue(offset + 25, 20, 16, lastreportCDDA);

	for (int i = 0; i < 12; i++) psx.savestate_addvalue(offset + 64 + i, 7, 0, nextSubdata[i]);
	for (int i = 0; i < 12; i++) psx.savestate_addvalue(offset + 76 + i, 7, 0, subdata[i]);

	for (int i = 0; i < 8; i++) psx.savestate_addvalue(offset + 96 + i, 15, 0, sectorBufferSizes[i]);

	for (int i = 0; i < RAW_SECTOR_SIZE / 4; i++)
	{
		UInt32 dword = sectorBuffer[i * 4];
		dword |= sectorBuffer[i * 4 + 1] << 8;
		dword |= sectorBuffer[i * 4 + 2] << 16;
		dword |= sectorBuffer[i * 4 + 3] << 24;
		psx.savestate[offset + 1024 + i] = dword;
	}

	for (int b = 0; b < 8; b++)
	{
		for (int i = 0; i < RAW_SECTOR_SIZE / 4; i++)
		{
			UInt32 dword = sectorBuffers[b][i * 4];
			dword |= sectorBuffers[b][i * 4 + 1] << 8;
			dword |= sectorBuffers[b][i * 4 + 2] << 16;
			dword |= sectorBuffers[b][i * 4 + 3] << 24;
			psx.savestate[offset + 2048 + b * 1024 + i] = dword;
		}
	}
}

void CDROM::loadState(UInt32 offset)
{
	workDelay        = psx.savestate_loadvalue(offset +  0, 31, 0);
	seekStartLBA	 = psx.savestate_loadvalue(offset +  1, 31, 0);
	seekEndLBA		 = psx.savestate_loadvalue(offset +  2, 31, 0);
	currentLBA		 = psx.savestate_loadvalue(offset +  3, 31, 0);
	driveDelay		 = psx.savestate_loadvalue(offset +  4, 31, 0);
	driveDelayNext	 = psx.savestate_loadvalue(offset +  5, 31, 0);
	lastReadSector	 = psx.savestate_loadvalue(offset +  6, 31, 0);
	physicalLBA		 = psx.savestate_loadvalue(offset +  7, 31, 0);
	physicalLBATick	 = psx.savestate_loadvalue(offset +  8, 31, 0);
	physicalLBAcarry = psx.savestate_loadvalue(offset +  9, 31, 0);
	startLBA		 = psx.savestate_loadvalue(offset + 10, 31, 0);
	positionInIndex	 = psx.savestate_loadvalue(offset + 11, 31, 0);
	delay			 = psx.savestate_loadvalue(offset + 12, 31, 0);

	internalStatus       = psx.savestate_loadvalue(offset + 13,  7,  0);
	modeReg			     = psx.savestate_loadvalue(offset + 13, 15,  8);
	nextCmd			     = psx.savestate_loadvalue(offset + 13, 23, 16);
	pendingDriveIRQ	     = psx.savestate_loadvalue(offset + 13, 31, 24);
	pendingDriveResponse = psx.savestate_loadvalue(offset + 14,  7,  0);
	workCommand		     = psx.savestate_loadvalue(offset + 14, 15,  8);
	setLocMinute		 = psx.savestate_loadvalue(offset + 14, 23, 16);
	setLocSecond		 = psx.savestate_loadvalue(offset + 14, 31, 24);
	setLocFrame		     = psx.savestate_loadvalue(offset + 15,  7,  0);
	trackNumberBCD		 = psx.savestate_loadvalue(offset + 15, 15,  8);
	fastForwardRate	     = psx.savestate_loadvalue(offset + 15, 23, 16);
	driveState	         = (DRIVESTATE)psx.savestate_loadvalue(offset + 15, 31, 24);
	writeSectorPointer	 = psx.savestate_loadvalue(offset + 16,  7,  0);
	readSectorPointer	 = psx.savestate_loadvalue(offset + 16, 15,  8);
	XaFilterFile		 = psx.savestate_loadvalue(offset + 16, 23, 16);
	XaFilterChannel	     = psx.savestate_loadvalue(offset + 16, 31, 24);
	session			     = psx.savestate_loadvalue(offset + 17,  7,  0);

	busy                  = psx.savestate_loadvalue(offset + 18, 0, 0);
	cmdPending	          = psx.savestate_loadvalue(offset + 18, 1, 1);
	working		          = psx.savestate_loadvalue(offset + 18, 2, 2);
	setLocActive	      = psx.savestate_loadvalue(offset + 18, 3, 3);
	driveBusy	          = psx.savestate_loadvalue(offset + 18, 4, 4);
	readAfterSeek         = psx.savestate_loadvalue(offset + 18, 5, 5);
	playAfterSeek         = psx.savestate_loadvalue(offset + 18, 6, 6);
	muted				  = psx.savestate_loadvalue(offset + 18, 7, 7);
	lastSectorHeaderValid = psx.savestate_loadvalue(offset + 18, 8, 8);

	header[0]    = psx.savestate_loadvalue(offset + 19,  7,  0); 
	header[1]	 = psx.savestate_loadvalue(offset + 19, 15,  8); 
	header[2]	 = psx.savestate_loadvalue(offset + 19, 23, 16); 
	header[3]	 = psx.savestate_loadvalue(offset + 19, 31, 24); 
	subheader[0] = psx.savestate_loadvalue(offset + 20,  7,  0); 
	subheader[1] = psx.savestate_loadvalue(offset + 20, 15,  8); 
	subheader[2] = psx.savestate_loadvalue(offset + 20, 23, 16); 
	subheader[3] = psx.savestate_loadvalue(offset + 20, 31, 24); 

    PSXRegs.CDROM_STATUS  = psx.savestate_loadvalue(offset + 21,  7,  0);
	PSXRegs.CDROM_IRQENA  = psx.savestate_loadvalue(offset + 21, 15,  8);
	PSXRegs.CDROM_IRQFLAG = psx.savestate_loadvalue(offset + 21, 23, 16);

	XaCurrentFile    = psx.savestate_loadvalue(offset + 22,  7,  0);
	XaCurrentChannel = psx.savestate_loadvalue(offset + 22, 15,  8);
	XaCurrentSet     = psx.savestate_loadvalue(offset + 22, 16, 16);
	xa_muted         = psx.savestate_loadvalue(offset + 22, 17, 17);

	cdvol_next00 = psx.savestate_loadvalue(offset + 23, 7, 0);
	cdvol_next01 = psx.savestate_loadvalue(offset + 23, 15, 8);
	cdvol_next10 = psx.savestate_loadvalue(offset + 23, 23, 16);
	cdvol_next11 = psx.savestate_loadvalue(offset + 23, 31, 24);	
	cdvol_00 = psx.savestate_loadvalue(offset + 24, 7, 0);
	cdvol_01 = psx.savestate_loadvalue(offset + 24, 15, 8);
	cdvol_10 = psx.savestate_loadvalue(offset + 24, 23, 16);
	cdvol_11 = psx.savestate_loadvalue(offset + 24, 31, 24);

	currentTrackBCD = psx.savestate_loadvalue(offset + 25, 7, 0);
	trackNumber = psx.savestate_loadvalue(offset + 25, 15, 8);
	lastreportCDDA = psx.savestate_loadvalue(offset + 25, 20, 16);

	for (int i = 0; i < 12; i++) nextSubdata[i] = psx.savestate_loadvalue(offset + 64 + i, 7, 0);
	for (int i = 0; i < 12; i++) subdata[i]     = psx.savestate_loadvalue(offset + 76 + i, 7, 0);

	for (int i = 0; i < 8; i++) sectorBufferSizes[i] = psx.savestate_loadvalue(offset + 96 + i, 15, 0);

	for (int i = 0; i < (RAW_SECTOR_SIZE / 4); i++)
	{
		UInt32 dword = psx.savestate[offset + 1024 + i];
		sectorBuffer[i * 4] = dword;
		sectorBuffer[i * 4 + 1] = dword >> 8;
		sectorBuffer[i * 4 + 2] = dword >> 16;
		sectorBuffer[i * 4 + 3] = dword >> 24;
	}

	for (int b = 0; b < 8; b++)
	{
		for (int i = 0; i < (RAW_SECTOR_SIZE / 4); i++)
		{
			UInt32 dword = psx.savestate[offset + 2048 + b * 1024 + i];
			sectorBuffers[b][i * 4] = dword;
			sectorBuffers[b][i * 4 + 1] = dword >> 8;
			sectorBuffers[b][i * 4 + 2] = dword >> 16;
			sectorBuffers[b][i * 4 + 3] = dword >> 24;
		}
	}

#ifdef FPGACOMPATIBLE
	driveDelay -= 2;
	driveDelayNext += 2;
#endif
}

bool CDROM::isSSIdle()
{
	//if (!fifoData.empty()) return false;
	if (!fifoParam.empty()) return false;
	if (!fifoResponse.empty()) return false;

	return true;
}

void CDROM::CDOutWriteFile(bool writeTest)
{
#ifdef CDFILEOUT
	FILE* file = fopen("R:\\debug_cd.txt", "w");

	for (int i = 0; i < debug_CDOutCount; i++)
	{
		if (debug_CDOutType[i] == 1) fputs("CMD: ", file);
		if (debug_CDOutType[i] == 2) fputs("DATA: ", file);
		if (debug_CDOutType[i] == 3) fputs("RSPFIFO: ", file);
		if (debug_CDOutType[i] == 4) fputs("RSPFIFO2: ", file);
		if (debug_CDOutType[i] == 5) fputs("RSPERROR: ", file);
		if (debug_CDOutType[i] == 6) fputs("RSPREAD: ", file);
		if (debug_CDOutType[i] == 7) fputs("WPTR: ", file);
		if (debug_CDOutType[i] == 8) fputs("CPUREAD: ", file);
		if (debug_CDOutType[i] == 9) fputs("CPUWRITE: ", file);
		if (debug_CDOutType[i] == 12) fputs("SECTORREAD: ", file);
		char buf[10];
#ifdef FPGACOMPATIBLE
		if (debug_CDOutType[i] == 10) continue;
		if (debug_CDOutType[i] == 11) continue;
		if (debug_CDOutType[i] != 2)
		{
			_itoa(debug_CDOutTime[i], buf, 16);
			for (int c = strlen(buf); c < 8; c++) fputc('0', file);
			fputs(buf, file);
			fputc(' ', file);
		}
#else
		if (debug_CDOutType[i] == 10) fputs("DMAREAD: ", file);
		if (debug_CDOutType[i] == 11) fputs("CDDAOUT: ", file);
		_itoa(debug_CDOutTime[i] + 1, buf, 16);
		for (int c = strlen(buf); c < 8; c++) fputc('0', file);
		fputs(buf, file);
		fputc(' ', file);
#endif
		_itoa(debug_CDOutAddr[i], buf, 16);
		for (int c = strlen(buf); c < 2; c++) fputc('0', file);
		fputs(buf, file);
		fputc(' ', file);
		_itoa(debug_CDOutData[i], buf, 16);
		for (int c = strlen(buf); c < 8; c++) fputc('0', file);
		fputs(buf, file);

		fputc('\n', file);
	}
	fclose(file);

#ifdef FPGACOMPATIBLE
	file = fopen("R:\\debug_xa1.txt", "w");

	int count = 0;
	for (int i = 0; i < debug_XAOutCount; i++)
	{
		if (debug_XAOutType[i] > 2) continue;
		if (debug_XAOutType[i] == 1) fputs("DATAIN: ", file);
		if (debug_XAOutType[i] == 2) fputs("ADPCMCALC: ", file);
		char buf[10];
		_itoa(count, buf, 16);
		for (int c = strlen(buf); c < 6; c++) fputc('0', file);
		fputs(buf, file);
		fputc(' ', file);
		_itoa(debug_XAOutData[i], buf, 16);
		for (int c = strlen(buf); c < 8; c++) fputc('0', file);
		fputs(buf, file);

		fputc('\n', file);

		count++;
	}
	fclose(file);

	file = fopen("R:\\debug_xa2.txt", "w");

	count = 0;
	for (int i = 0; i < debug_XAOutCount; i++)
	{
		if (debug_XAOutType[i] < 4) continue;
		if (debug_XAOutType[i] == 4) fputs("OUT: ", file);
		char buf[10];
		_itoa(count, buf, 16);
		for (int c = strlen(buf); c < 6; c++) fputc('0', file);
		fputs(buf, file);
		fputc(' ', file);
		_itoa(debug_XAOutData[i], buf, 16);
		for (int c = strlen(buf); c < 8; c++) fputc('0', file);
		fputs(buf, file);

		fputc('\n', file);

		count++;
	}
	fclose(file);

	file = fopen("R:\\debug_cdda.txt", "w");
	count = 0;
	for (int i = 0; i < debug_CDOutCount; i++)
	{
		if (debug_CDOutType[i] == 11)
		{
			char buf[10];
			_itoa(count, buf, 16);
			for (int c = strlen(buf); c < 6; c++) fputc('0', file);
			fputs(buf, file);
			fputc(' ', file);
			_itoa(debug_CDOutData[i], buf, 16);
			for (int c = strlen(buf); c < 8; c++) fputc('0', file);
			fputs(buf, file);
			fputc('\n', file);
			count++;
		}
	}
	fclose(file);

	file = fopen("R:\\debug_cd_dma.txt", "w");
	count = 0;
	for (int i = 0; i < debug_CDOutCount; i++)
	{
		if (debug_CDOutType[i] == 10)
		{
			char buf[10];
			_itoa(count, buf, 16);
			for (int c = strlen(buf); c < 6; c++) fputc('0', file);
			fputs(buf, file);
			fputc(' ', file);
			_itoa(debug_CDOutData[i], buf, 16);
			for (int c = strlen(buf); c < 8; c++) fputc('0', file);
			fputs(buf, file);
			fputc('\n', file);
			count++;
		}
	}
	fclose(file);
#else
	file = fopen("R:\\debug_xa.txt", "w");

	int count = 0;
	for (int i = 0; i < debug_XAOutCount; i++)
	{
		if (debug_XAOutType[i] == 1) fputs("DATAIN: ", file);
		if (debug_XAOutType[i] == 2) fputs("ADPCMCALC: ", file);
		if (debug_XAOutType[i] == 3) fputs("ADPCMOUT: ", file);
		if (debug_XAOutType[i] == 4) fputs("OUT: ", file);
		char buf[10];
		_itoa(count, buf, 16);
		for (int c = strlen(buf); c < 6; c++) fputc('0', file);
		fputs(buf, file);
		fputc(' ', file);
		_itoa(debug_XAOutData[i], buf, 16);
		for (int c = strlen(buf); c < 8; c++) fputc('0', file);
		fputs(buf, file);

		fputc('\n', file);

		count++;
	}
	fclose(file);
#endif

	if (writeTest)
	{
		file = fopen("R:\\cd_test_FPSXA.txt", "w");

		for (int i = 0; i < debug_CDOutCount; i++)
		{
			if (debug_CDOutType[i] == 8 || debug_CDOutType[i] == 9 || debug_CDOutType[i] == 10)
			{
				char buf[10];
				_itoa(debug_CDOutType[i], buf, 16);
				for (int c = strlen(buf); c < 2; c++) fputc('0', file);
				fputs(buf, file);
				fputc(' ', file);
				_itoa(debug_CDOutTime[i], buf, 16);
				for (int c = strlen(buf); c < 8; c++) fputc('0', file);
				fputs(buf, file);
				fputc(' ', file);
				_itoa(debug_CDOutAddr[i], buf, 16);
				for (int c = strlen(buf); c < 2; c++) fputc('0', file);
				fputs(buf, file);
				fputc(' ', file);
				_itoa(debug_CDOutData[i], buf, 16);
				for (int c = strlen(buf); c < 8; c++) fputc('0', file);
				fputs(buf, file);

				fputc('\n', file);
			}
		}
		fclose(file);
	}
#endif
}

void CDROM::CDOutCapture(Byte type, Byte addr, UInt32 data)
{
#if defined(CDFILEOUT)
	if (debug_CDOutCount == 5442)
	{
		int a = 5;
	}

	if (CPU.totalticks == 0x03F4F6B9)
	{
		int a = 5;
	}

	debug_CDOutTime[debug_CDOutCount] = CPU.totalticks;
	debug_CDOutAddr[debug_CDOutCount] = addr;
	debug_CDOutData[debug_CDOutCount] = data;
	debug_CDOutType[debug_CDOutCount] = type;
	debug_CDOutCount++;
#endif
}

void CDROM::XAOutCapture(Byte type, UInt32 data)
{
#if defined(CDFILEOUT)
	if (debug_XAOutCount >= 1000000) return;

	debug_XAOutData[debug_XAOutCount] = data;
	debug_XAOutType[debug_XAOutCount] = type;
	debug_XAOutCount++;
#endif
}

void CDROM::CDTEST()
{
#ifdef FPGACOMPATIBLE
	std::ifstream infile("R:\\cd_test_FPSXA.txt");
#else
	std::ifstream infile("R:\\cd_test_duck.txt");
#endif
	std::string line;
	Int32 timer = 0;
	int cmdcnt = 0;
	testaudio = true;
	bool soundSpeed = false;
	Sound.soundGenerator.play(soundSpeed);
	while (std::getline(infile, line))
	{
		std::string type = line.substr(0, 2);
		std::string time = line.substr(3, 8);
		std::string addr = line.substr(12, 2);
		std::string data = line.substr(15, 8);
		byte typeI = std::stoul(type, nullptr, 16);
		UInt32 timeI = std::stoul(time, nullptr, 16);
		byte addrI = std::stoul(addr, nullptr, 16);
		UInt32 dataI = std::stoul(data, nullptr, 16);
		if (timeI == 0) timeI++;
		while (timer < timeI - 1)
		{
			work();
			timer++;
			CPU.totalticks++;
		}
		if (cmdcnt == 1004035)
		{
			int a = 5;
		}
		//if (debug_CDOutCount >= 120935)
		//{
		//	int a = 5;
		//}
		switch (typeI)
		{
		case 8:  readReg(addrI); break;
		case 9:  writeReg(addrI, dataI); break;
		case 10:
		{
#ifdef FPGACOMPATIBLE
			byte data = readDMA();
#else
			CDOutCapture(10, 0, dataI);
			for (int i = 0; i < dataI * 4; i++) readDMA();
#endif
		}
		break;
		}
		cmdcnt++;
	}

	if (soundSpeed)
	{
		Sound.soundGenerator.play(false);
		while (Sound.soundGenerator.nextSamples.size() > 0) {}
	}

	CDOutWriteFile(false);
}