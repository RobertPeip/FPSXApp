#include <algorithm>
#include <fstream>
#include <string>
using namespace std;

#include "MDEC.h"
#include "PSXRegs.h"
#include "CPU.h"
#include "DMA.h"
#include "Memory.h"
#include "psx.h"

Mdec MDEC;

void Mdec::reset()
{
	while (!fifoIn.empty()) fifoIn.pop_front();
	while (!fifoOut.empty()) fifoOut.pop_front();

	cmdTicks = 0;
	busy = false;
	decoding = false;
	decodeDone = false;
	writing = false;
	wordsRemain = 0;
	currentBlock = 0;
	currentCoeff = 64;
	currentQScale = 0;
	fifoSecondAvail = false;
	fifoPopNextCycle = false;

	PSXRegs.MDECStatus = 0;
	updateStatus();
}

void Mdec::CMDWrite(UInt32 value)
{
	fifoIn.push_back(value);
}

UInt32 Mdec::fifoRead(bool fromDMA)
{
	UInt32 retval;
	if (!fifoOut.empty())
	{
		UInt32 data = fifoOut.front();
		if (fromDMA)
		{
			fifoOut.pop_front();
			updateStatus();
		}
		else fifoPopNextCycle = true;
		retval = data;
	}
	else
	{
		retval = 0xFFFFFFFF; // todo: stall cpu?
	}
	if (fromDMA)
	{
		MDECOutCapture(12, 0, retval);
	}

	return retval;
}

void Mdec::work()
{
	if (writing)
	{
		if (cmdTicks == 1)
		{
			MDECOutCapture(11, 0, 0);

			cmdTicks = 0;
			writing = false;

			switch ((PSXRegs.MDECStatus >> 25) & 3)
			{
			case 0: // 4 bit
				for (int i = 0; i < 8; i++)
				{
					UInt32 color = (outputBlock[i * 8 + 0] >> 4);
					color |= (outputBlock[i * 8 + 1] >> 4) << 4;
					color |= (outputBlock[i * 8 + 2] >> 4) << 8;
					color |= (outputBlock[i * 8 + 3] >> 4) << 12;
					color |= (outputBlock[i * 8 + 4] >> 4) << 16;
					color |= (outputBlock[i * 8 + 5] >> 4) << 20;
					color |= (outputBlock[i * 8 + 6] >> 4) << 24;
					color |= (outputBlock[i * 8 + 7] >> 4) << 28;
					fifoOut.push_back(color);
				}
				break;

			case 1: // 8 bit
				for (int i = 0; i < 16; i++)
				{
					UInt32 color = outputBlock[i * 4 + 0];
					color |= outputBlock[i * 4 + 1] << 8;
					color |= outputBlock[i * 4 + 2] << 16;
					color |= outputBlock[i * 4 + 3] << 24;
					fifoOut.push_back(color);
				}
				break; 

			case 2:  // 24 bit
			{
				Byte bytesLeft = 0;
				UInt32 colorLeft = 0;
				for (int i = 0; i < 256; i++)
				{
					UInt32 colorNew = outputBlock[i];

					UInt32 colorWrite;
					switch (bytesLeft)
					{
					case 0: 
						colorLeft = colorNew; 
						bytesLeft += 3;
						break;
					
					case 1:
						bytesLeft = 0;
						colorWrite = colorLeft | (colorNew << 8);
						fifoOut.push_back(colorWrite);
						break;

					case 2:
						bytesLeft = 1;
						colorWrite = colorLeft | ((colorNew & 0xFFFF) << 16);
						colorLeft = colorNew >> 16;
						fifoOut.push_back(colorWrite);
						break;

					case 3:
						bytesLeft = 2;
						colorWrite = colorLeft | ((colorNew & 0xFF) << 24);
						colorLeft = colorNew >> 8;
						fifoOut.push_back(colorWrite);
						break;
					}	
				}
			}
				break;

			case 3: // 15 bit
			{
				bool alpha = (PSXRegs.MDECStatus >> 23) & 1;
				for (int i = 0; i < 128; i++)
				{
					UInt32 color = outputBlock[i * 2];
					byte r = (color >> 3) & 0x1F;
					byte g = (color >> 11) & 0x1F;
					byte b = (color >> 19) & 0x1F;
					UInt16 colorlow = r | (g << 5) | (b << 10) | (alpha << 15);
					color = outputBlock[i * 2 + 1];
					r = (color >> 3) & 0x1F;
					g = (color >> 11) & 0x1F;
					b = (color >> 19) & 0x1F;
					UInt16 colorhigh = r | (g << 5) | (b << 10) | (alpha << 15);
					color = (colorhigh << 16) | colorlow;
					fifoOut.push_back(color);
				}
			}
				break; 
			}

#ifdef MDECDEBUGOUT
			FILE* file = fopen("R:\\debug_mdec_FIFOOUT.txt", "a");
			char buf[10];
			_itoa(debugcnt_FIFOOUT, buf, 10);
			fputs(buf, file);
			fputs(" fifo out", file);
			fputc('\n', file);
			for (int i = 0; i < fifoOut.size(); i++)
			{
				_itoa(fifoOut.at(i), buf, 16);
				for (int c = strlen(buf); c < 8; c++) fputc('0', file);
				fputs(buf, file);
				fputc('\n', file);
			}
			fclose(file);
			debugcnt_FIFOOUT++;
#endif

			if (wordsRemain > 0)
			{
				decoding = true;
				decodeDone = false;
			}
			else
			{
				busy = false;
				if (fifoSecondAvail)
				{
					fifoSecondAvail = false;
				}
			}
		}
		else
		{
			cmdTicks -= 1;
		}
		updateStatus();
	}
	
	if (!writing && (!fifoIn.empty() || fifoSecondAvail || (decoding && decodeDone)))
	{
		if (decoding)
		{
			bool finished = decodeDone;
#ifdef  INSTANTMDECDECODE
			while (!finished && (!fifoIn.empty() || fifoSecondAvail))
			{
				finished = decodeMacroBlock();
			}
#else
			//if (!finished && !fifoIn.empty() || fifoSecondAvail)
			if (!finished && (!fifoIn.empty() || fifoSecondAvail))
			{
				finished = decodeMacroBlock();
			}
#endif //INSTANTMDECDECODE
			if (wordsRemain == 0 && !fifoSecondAvail && currentBlock != 6) // todo: may need to check for either 1 or 6?
			{
				currentBlock = 0;
				currentCoeff = 64;
				currentQScale = 0;
				decoding = false;
				busy = false;
				if (fifoSecondAvail)
				{
					fifoSecondAvail = false;
				}
			}

			if (decodeDone)
			{
				finishMacroBlock();
			}

			updateStatus();
		}
		else
		{
			UInt32 fifodata = fifoIn.front();
			byte command = fifodata >> 29;

			//signed, 15bit, data_output_depth
			PSXRegs.MDECStatus &= 0xF87FFFFF;
			PSXRegs.MDECStatus |= ((fifodata >> 27) & 3) << 25; // depth
			PSXRegs.MDECStatus |= ((fifodata >> 26) & 1) << 24; // signed
			PSXRegs.MDECStatus |= ((fifodata >> 25) & 1) << 23; // bit15

			switch (command)
			{
			case 1: // decode macroblock
				wordsRemain = fifodata & 0xFFFF;
				busy = true;
				decoding = true;
				decodeDone = false;
				fifoIn.pop_front();
				break;

			case 2: // Set Quant Table
			{
				wordsRemain = 16;
				bool color = (fifodata & 1);
				if (color) wordsRemain += 16;
				if (fifoIn.size() == (wordsRemain + 1))
				{
					fifoIn.pop_front();
					wordsRemain = wordsRemain - 16; // todo: why?
					for (int i = 0; i < 16; i++)
					{
						UInt32 data = fifoIn.front();
						iqY[i * 4 + 0] = data & 0xFF;
						iqY[i * 4 + 1] = (data >> 8) & 0xFF;
						iqY[i * 4 + 2] = (data >> 16) & 0xFF;
						iqY[i * 4 + 3] = (data >> 24) & 0xFF;
						fifoIn.pop_front();
					}
					if (color)
					{
						for (int i = 0; i < 16; i++)
						{
							UInt32 data = fifoIn.front();
							iqUV[i * 4 + 0] = data & 0xFF;
							iqUV[i * 4 + 1] = (data >> 8) & 0xFF;
							iqUV[i * 4 + 2] = (data >> 16) & 0xFF;
							iqUV[i * 4 + 3] = (data >> 24) & 0xFF;
							fifoIn.pop_front();
						}
					}
					busy = false;
				}
				else
				{
					busy = true;
				}
			}
			break;

			case 3: // Set Scale Table
				wordsRemain = 32;
				if (fifoIn.size() == (wordsRemain + 1))
				{
					fifoIn.pop_front();
					wordsRemain = wordsRemain - 16; // todo: why?
					for (int i = 0; i < 32; i++)
					{
						UInt32 data = fifoIn.front();
						scaleTable[i * 2 + 0] = (Int16)(data & 0xFFFF);
						scaleTable[i * 2 + 1] = (Int16)((data >> 16) & 0xFFFF);
						fifoIn.pop_front();
					}
					busy = false;
				}
				else
				{
					busy = true;
				}
				break;

			default: // no command
				if (wordsRemain > 0)
				{
					fifoIn.pop_front();
					wordsRemain--;
				}
				break;
			}

			updateStatus();
		}
	}

	if (fifoPopNextCycle)
	{
		fifoPopNextCycle = false;
		fifoOut.pop_front();
		updateStatus();
	}
}

void Mdec::updateStatus()
{
	PSXRegs.MDECStatus &= 0x07F80000;

	if (fifoOut.empty()) PSXRegs.MDECStatus |= 1 << 31;
	if (fifoIn.size() == 256 && !fifoSecondAvail) PSXRegs.MDECStatus |= 1 << 30;
	if (busy) PSXRegs.MDECStatus |= 1 << 29;

	int fifosize = (fifoIn.size() * 2) + fifoSecondAvail;
	bool dataInReq = (((PSXRegs.MDECControl >> 30) & 1) && (fifosize < (512 - 64)));
	if (dataInReq) PSXRegs.MDECStatus |= 1 << 28;

	if (DMA.wordcount == 1 && fifosize > (512 - 65)) // hack -> update DMA one cycle earlier, otherwise it will start next block before request is pulled low
	{
		DMA.requests[0] = false;
	}
	else
	{
		if (dataInReq && !DMA.requests[0]) DMA.trigger(0);
		DMA.requests[0] = dataInReq;
	}

	bool dataOutReq = ((PSXRegs.MDECControl >> 29) & 1) && !fifoOut.empty();
	if (dataOutReq) PSXRegs.MDECStatus |= 1 << 27;
	if (DMA.wordcount == 1 && fifoOut.size() == 1) // hack -> update DMA one cycle earlier, otherwise it will start next block before request is pulled low
	{
		DMA.requests[1] = false;
	}
	else if (!dataOutReq) DMA.requests[1] = false;
	else
	{
		if (dataOutReq && !DMA.requests[1]) DMA.trigger(1);
		DMA.requests[1] = dataOutReq;
	}

	PSXRegs.MDECStatus |= (wordsRemain - 1) & 0xFFFF;
	PSXRegs.MDECStatus |= ((currentBlock + 4) % 6) << 16;
}

bool Mdec::decodeMacroBlock()
{
	bool finished = decodeDone;
	if (!decodeDone)
	{
		UInt16 nextData;
		if (fifoSecondAvail)
		{
			nextData = fifoSecondBuffer;
			fifoSecondAvail = false;
		}
		else
		{
			UInt32 fifodata = fifoIn.front();
			fifoIn.pop_front();
			nextData = fifodata & 0xFFFF;
			fifoSecondAvail = true;
			fifoSecondBuffer = fifodata >> 16;
			wordsRemain--;
		}

		finished |= rlDecode(nextData);
	}
	if (finished)
	{
		currentBlock++;
		byte blockCount = 1;
		if ((PSXRegs.MDECStatus >> 26) & 1) blockCount = 6;
		if(currentBlock == blockCount)
		{
			decodeDone = true;
			return true;
		}
	}
	return false;
}

void Mdec::finishMacroBlock()
{
	if (!fifoOut.empty())
	{
		return;
	}
	else
	{
		bool colorMode = (PSXRegs.MDECStatus >> 26) & 1;
		byte blockCount = 1;
		if (colorMode) blockCount = 6;
		IDCT(blockCount);
#ifdef MDECFILEOUT
		for (int b = 0; b < blockCount; b++)
		{
			for (int i = 0; i < 64; i++)
			{
				//MDECOutCapture(5, b*64 + i, blocks[b][i]);
			}
		}
#endif
		currentBlock = 0;
		currentCoeff = 64;
		currentQScale = 0;
		if (colorMode)
		{
			yuvToRGB(2);
			yuvToRGB(3);
			yuvToRGB(4);
			yuvToRGB(5);
#ifdef MDECDEBUGOUT
			FILE* file = fopen("R:\\debug_mdec_COLOR.txt", "a");
			char buf[10];
			_itoa(debugcnt_COLOR, buf, 10);
			fputs(buf, file);
			fputs(" color converted", file);
			fputc('\n', file);
			for (int i = 0; i < 256; i++)
			{
				_itoa(outputBlock[i], buf, 16);
				for (int c = strlen(buf); c < 6; c++) fputc('0', file);
				fputs(buf, file);
				fputc('\n', file);
			}
			fclose(file);
			debugcnt_COLOR++;
			if (debugcnt_COLOR == 22)
			{
				int a = 5;
			}
#endif
		}
		else
		{
			yToMono();
#ifdef MDECDEBUGOUT
			FILE* file = fopen("R:\\debug_mdec_COLOR.txt", "a");
			char buf[10];
			_itoa(debugcnt_COLOR, buf, 10);
			fputs(buf, file);
			fputs(" color converted", file);
			fputc('\n', file);
			for (int i = 0; i < 64; i++)
			{
				_itoa(outputBlock[i], buf, 16);
				for (int c = strlen(buf); c < 6; c++) fputc('0', file);
				fputs(buf, file);
				fputc('\n', file);
			}
			fclose(file);
			debugcnt_COLOR++;
#endif
		}
		decoding = false;
		writing = true;
		switch ((PSXRegs.MDECStatus >> 25) & 3)
		{
		case 0: cmdTicks = 448 * 6; break; // 4 bit
		case 1: cmdTicks = 448 * 6; break; // 8 bit
		case 2: cmdTicks = 448 * 6; break; // 24 bit
		case 3: cmdTicks = 550 * 6; break; // 15 bit
		}
		MDECOutCapture(2, 0, cmdTicks);
		cmdTicks++;
		return;
	}
}

bool Mdec::rlDecode(UInt16 data)
{
	if (currentCoeff == 64)
	{
		for (int i = 0; i < 64; i++) blocks[currentBlock][i] = 0;
		if (data == 0xFE00) return false;
		currentCoeff = 0;
		currentQScale = (data >> 10) & 0x3F;
		Int32 value = (data & 0x1FF);
		if (data & 0x200) value = -512 + value;
		Int32 mul;
		if (currentBlock >= 2) mul = iqY[currentCoeff];
		else mul = iqUV[currentCoeff];
		if (currentQScale == 0) value = value * 2;
		else value = value * mul;
		if (value < -1024) value = -1024;
		if (value > 1023) value = 1023;
		if (currentQScale == 0) blocks[currentBlock][currentCoeff] = (Int16)value;
		else blocks[currentBlock][zigZag[currentCoeff]] = (Int16)value;
	}
	else
	{
		currentCoeff += ((data >> 10) & 0x3F) + 1;
		if (currentCoeff >= 63)
		{
#ifdef MDECFILEOUT
			MDECOutCapture(3, currentBlock, currentCoeff);
			MDECOutCapture(6, 0, (fifoIn.size() * 2) + fifoSecondAvail);

			for (int i = 0; i < 64; i++)
			{
				//MDECOutCapture(4, i, blocks[currentBlock][i]);
			}
#endif
			currentCoeff = 64;
			return true;
		}
		else
		{
			Int32 value = (data & 0x1FF);
			if (data & 0x200) value = -512 + value;
			Int32 mul;
			if (currentBlock >= 2) mul = iqY[currentCoeff];
			else mul = iqUV[currentCoeff];
			if (currentQScale == 0) value = value * 2;
			else
			{
				value *= mul;
				value *= currentQScale;
				value += 4;
				value /= 8;
			}
			if (value < -1024) value = -1024;
			if (value > 1023) value = 1023;
			if (currentQScale == 0) blocks[currentBlock][currentCoeff] = (Int16)value;
			else blocks[currentBlock][zigZag[currentCoeff]] = (Int16)value;
		}
	}
	return false;
}

void Mdec::IDCT(byte blockCount)
{
#ifdef MDECDEBUGOUT
	FILE* file = fopen("R:\\debug_mdec_RL.txt", "a");
	for (int i = 0; i < blockCount; i++)
	{
		char buf[10];
		_itoa(debugcnt_RL, buf, 10);
		fputs(buf, file);
		fputs(" RL decoded block ", file);
		_itoa(i, buf, 10);
		fputs(buf, file);
		fputc('\n', file);
		for (int j = 0; j < 64; j++)
		{
			_itoa(blocks[i][j], buf, 10);
			fputs(buf, file);
			fputc('\n', file);
		}
		debugcnt_RL++;
		if (debugcnt_RL == 66)
		{
			int a = 5;
		}
	}
	fclose(file);
#endif

	Int32 temp[64];
	for (int i = 0; i < blockCount; i++)
	{
		for (int x = 0; x < 8; x++)
		{
			for (int y = 0; y < 8; y++)
			{
				Int32 sum = 0;
				for (int u = 0; u < 8; u++)
				{
					sum += (Int32)(blocks[i][u * 8 + x]) * Int32(scaleTable[u * 8 + y]);
				}
				temp[x + y * 8] = sum;
			}
		}
		for (int x = 0; x < 8; x++)
		{
			for (int y = 0; y < 8; y++)
			{
				Int64 sum = 0;
				for (int u = 0; u < 8; u++)
				{
					sum += (Int64)(temp[u + y * 8]) * Int32(scaleTable[u * 8 + x]);
				}
				
#ifdef FPGACOMPATIBLE
				Int16 value = (sum >> 32);
#else
				Int16 value = (sum >> 32) + ((sum >> 31) & 1);
#endif
				if (value < -128) value = -128;
				if (value > 127) value = 127;
				blocks[i][x + y * 8] = value & 0xFFFF;
			}
		}
	}
#ifdef MDECDEBUGOUT
	file = fopen("R:\\debug_mdec_IDCT.txt", "a");
	for (int i = 0; i < blockCount; i++)
	{
		char buf[10];
		_itoa(debugcnt_IDCT, buf, 10);
		fputs(buf, file);
		fputs(" IDCT converted block ", file);
		_itoa(i, buf, 10);
		fputs(buf, file);
		fputc('\n', file);
		for (int j = 0; j < 64; j++)
		{
			_itoa(blocks[i][j], buf, 10);
			fputs(buf, file);
			fputc('\n', file);
		}
		debugcnt_IDCT++;
	}
	fclose(file);
#endif
}

void Mdec::yuvToRGB(byte block)
{
	for (int y = 0; y < 8; y++)
	{
		for (int x = 0; x < 8; x++)
		{
			byte xBase = 0;
			byte yBase = 0;
			switch (block)
			{
			case 3: xBase = 8; break;
			case 4: yBase = 8; break;
			case 5: xBase = 8; yBase = 8; break;
			}
			Int16 R = blocks[0][(x + xBase) / 2 + ((y + yBase) / 2) * 8];
			Int16 B = blocks[1][(x + xBase) / 2 + ((y + yBase) / 2) * 8];

			// floating point
			//Int16 G = (Int16)((-0.3437f * ((float)B)) + (-0.7143f * ((float)R)));
			//R = (Int16)(1.402f * ((float)R));
			//B = (Int16)(1.772f * ((float)B));

			// fixed point
		    Int16 G = ((-344 * B) + (-714 * R)) >> 10;
			R = (R * 1402) >> 10;
			B = (B * 1772) >> 10;

			Int16 Y = blocks[block][x + y * 8];
			if (Y + R < -128) R = -128; else if (Y + R > 127) R = 127; else R = Y + R;
			if (Y + G < -128) G = -128; else if (Y + G > 127) G = 127; else G = Y + G;
			if (Y + B < -128) B = -128; else if (Y + B > 127) B = 127; else B = Y + B;

			R += 128;
			G += 128;
			B += 128;

			UInt32 color = R | (G << 8) | (B << 16);
			outputBlock[(x + xBase) + (y + yBase) * 16] = color;
#ifdef MDECFILEOUT
			//MDECOutCapture(1, 0, color);
#endif
		}
	}
}

void Mdec::yToMono()
{
	for (int i = 0; i < 64; i++)
	{
		Int16 color = blocks[0][i];
		if (color < -128) color = -128;
		if (color > 127) color = 127;
		color += 128;
		color &= 0xFF;
		outputBlock[i] = color;

#ifdef MDECFILEOUT
		MDECOutCapture(1, i, color);
#endif
	}
}

void Mdec::saveState(UInt32 offset)
{
	psx.savestate_addvalue(offset + 0, 31, 0, PSXRegs.MDECStatus);
	psx.savestate_addvalue(offset + 1, 31, 0, PSXRegs.MDECControl);

	for (int i = 0; i < 16; i++)
	{
		UInt32 dword = iqUV[i * 4];
		dword |= iqUV[i * 4 + 1] << 8;
		dword |= iqUV[i * 4 + 2] << 16;
		dword |= iqUV[i * 4 + 3] << 24;
		psx.savestate[offset + 16 + i] = dword;
	}

	for (int i = 0; i < 16; i++)
	{
		UInt32 dword = iqY[i * 4];
		dword |= iqY[i * 4 + 1] << 8;
		dword |= iqY[i * 4 + 2] << 16;
		dword |= iqY[i * 4 + 3] << 24;
		psx.savestate[offset + 32 + i] = dword;
	}

	for (int i = 0; i < 32; i++)
	{
		UInt32 dword = scaleTable[i * 2];
		dword &= 0xFFFF;
		dword |= ((scaleTable[i * 2 + 1] << 16) & 0xFFFF0000);
		psx.savestate[offset + 64 + i] = dword;
	}
}

void Mdec::loadState(UInt32 offset)
{
	PSXRegs.MDECStatus  = psx.savestate_loadvalue(offset + 0, 31, 0);
	PSXRegs.MDECControl = psx.savestate_loadvalue(offset + 1, 31, 0);

	for (int i = 0; i < 16; i++)
	{
		UInt32 dword = psx.savestate[offset + 16 + i];
		iqUV[i * 4] = dword;
		iqUV[i * 4 + 1] = dword >> 8;
		iqUV[i * 4 + 2] = dword >> 16;
		iqUV[i * 4 + 3] = dword >> 24;
	}

	for (int i = 0; i < 16; i++)
	{
		UInt32 dword = psx.savestate[offset + 32 + i];
		iqY[i * 4] = dword;
		iqY[i * 4 + 1] = dword >> 8;
		iqY[i * 4 + 2] = dword >> 16;
		iqY[i * 4 + 3] = dword >> 24;
	}

	for (int i = 0; i < 32; i++)
	{
		UInt32 dword = psx.savestate[offset + 64 + i];
		scaleTable[i * 2] = dword;
		scaleTable[i * 2 + 1] = dword >> 16;
	}
}

bool Mdec::isSSIdle()
{
	if (busy) return false;
	if (fifoSecondAvail) return false;
	if (!fifoIn.empty()) return false;
	if (!fifoOut.empty()) return false;

	return true;
}

void Mdec::MDECOutWriteFile(bool writeTest)
{
#ifdef MDECFILEOUT
	FILE* file = fopen("R:\\debug_mdec.txt", "w");

	for (int i = 0; i < debug_MDECOutCount; i++)
	{
		if (debug_MDECOutType[i] == 1) fputs("Pixel: ", file);
		if (debug_MDECOutType[i] == 2) fputs("Fifo: ", file);
		if (debug_MDECOutType[i] == 3) fputs("Blockendpos: ", file);
		if (debug_MDECOutType[i] == 4) fputs("Blockresult: ", file);
		if (debug_MDECOutType[i] == 5) fputs("IDCTresult: ", file);
		if (debug_MDECOutType[i] == 6) fputs("FIFOLeft: ", file);
		if (debug_MDECOutType[i] == 7) fputs("CPUREAD: ", file);
		if (debug_MDECOutType[i] == 8) fputs("CPUWRITE: ", file);
		if (debug_MDECOutType[i] == 9) fputs("DMAREAD: ", file);
		if (debug_MDECOutType[i] == 10) fputs("DMAWRITE: ", file);
		if (debug_MDECOutType[i] == 11) fputs("EVENT: ", file);
		if (debug_MDECOutType[i] == 12) fputs("DMA1READ: ", file);
		char buf[10];
		_itoa(debug_MDECOutTime[i], buf, 16);
		for (int c = strlen(buf); c < 8; c++) fputc('0', file);
		fputs(buf, file);
		fputc(' ', file);
		_itoa(debug_MDECOutAddr[i], buf, 16);
		for (int c = strlen(buf); c < 2; c++) fputc('0', file);
		fputs(buf, file);
		fputc(' ', file);
		_itoa(debug_MDECOutData[i], buf, 16);
		for (int c = strlen(buf); c < 8; c++) fputc('0', file);
		fputs(buf, file);

		fputc('\n', file);
	}
	fclose(file);

	if (writeTest)
	{
		file = fopen("R:\\mdec_test_FPSXA.txt", "w");

		for (int i = 0; i < debug_MDECOutCount; i++)
		{
			if (debug_MDECOutType[i] == 7 || debug_MDECOutType[i] == 8 || debug_MDECOutType[i] == 10 || debug_MDECOutType[i] == 12)
			{
				char buf[10];
				_itoa(debug_MDECOutType[i], buf, 16);
				for (int c = strlen(buf); c < 2; c++) fputc('0', file);
				fputs(buf, file);
				fputc(' ', file);
				_itoa(debug_MDECOutTime[i], buf, 16);
				for (int c = strlen(buf); c < 8; c++) fputc('0', file);
				fputs(buf, file);
				fputc(' ', file);
				_itoa(debug_MDECOutAddr[i], buf, 16);
				for (int c = strlen(buf); c < 2; c++) fputc('0', file);
				fputs(buf, file);
				fputc(' ', file);
				_itoa(debug_MDECOutData[i], buf, 16);
				for (int c = strlen(buf); c < 8; c++) fputc('0', file);
				fputs(buf, file);

				fputc('\n', file);
			}
		}
		fclose(file);
	}
#endif
}

void Mdec::MDECOutCapture(Byte type, Byte addr, UInt32 data)
{
#ifdef MDECFILEOUT
	if (debug_MDECOutCount == 797531)
	{
		int a = 5;
	}

	if (debug_MDECOutCount == 2000000) return;

	debug_MDECOutTime[debug_MDECOutCount] = CPU.totalticks;
	debug_MDECOutAddr[debug_MDECOutCount] = addr;
	debug_MDECOutData[debug_MDECOutCount] = data;
	debug_MDECOutType[debug_MDECOutCount] = type;
	debug_MDECOutCount++;
#endif
}

void Mdec::MDECTEST()
{
	//std::ifstream infile("R:\\mdec_test_duck.txt");
	std::ifstream infile("R:\\mdec_test_FPSXA.txt");
	std::string line;
	UInt32 timer = 0;
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
		while (timer < timeI)
		{
			work();
			timer++;
			CPU.totalticks++;
		}
		switch (typeI)
		{
		case 7:  PSXRegs.read_reg_mdec(addrI); break;
		case 8:  PSXRegs.write_reg_mdec(addrI, dataI); break;

		case 9:
		{
			MDECOutCapture(9, 0, dataI);
			for (int i = 0; i < dataI; i++)
			{
				MDEC.fifoRead(true);
				work();
			}
		}
		break;

		case 10:
			MDECOutCapture(10, 0, dataI);
			MDEC.CMDWrite(dataI);
			work();
			break;

#ifndef FPGACOMPATIBLE
		case 11:
			if (writing && cmdTicks > 1)
			{
				cmdTicks = 1;
				work();
			}
			break;
#endif
		}

	}

	MDECOutWriteFile(false);
}