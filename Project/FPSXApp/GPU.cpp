#include <algorithm>
using namespace std;
#undef NDEBUG
#include <assert.h>
#include <fstream>
#include <string>
using namespace std;

#include "GPU.h"
#include "GPU_Timing.h"
#include "Polygon.h"
#include "psx.h"
#include "PSXRegs.h"
#include "Memory.h"
#include "CPU.h"
#include "DMA.h"
#include "FileIO.h"

Gpu GPU;

void Gpu::reset()
{
#if DEBUG
	lockSpeed = false;
#endif

	for (int i = 0; i < 1048576; i++) VRAM[i] = 0x00;
	for (int i = 0; i < 1024 * 512; i++) bufferWork[i] = 0;
	for (int i = 0; i < 1024 * 512; i++) buffer[i] = 0;

	intern_frames = 0;
	frametimeleft = FRAMETIME;
	while (!fifo.empty()) fifo.pop_front();
	while (!copyfifo.empty()) copyfifo.pop();
	while (!polyfifo.empty()) polyfifo.pop();
	fifoIdle = true;
	cmdTicks = 0;
	fifoTotalwords = 0;
	pendingPolyline = false;
	pendingCPUtoVRAM = false;
	pendingVRAMtoCPU = false;
	endCopyWait = 0;
	checkMask = false;

	textureWindow_AND_X = 0xFF;
	textureWindow_AND_Y = 0xFF;

	// debug
#ifdef VRAMFILEOUT
	polygonCount = 0;
	objCount = 0;
	polyLineShadedCount = 0;
	debug_VramOutCount = 0;
#endif
}

void Gpu::GP0Write(Uint32 value)
{
	fifo.push_back(value);
	fifoIdle = false;

	if (value == 0x01ff03ff)
	{
		int a = 5;
	}

	//if (fifo.size() > 16)
	//if (fifo.size() > 31)
	if (fifo.size() > 127)
	{
		_wassert(_CRT_WIDE("Fifo size exceeded"), _CRT_WIDE("GPU"), fifo.size());
	}
#if defined(VRAMFILEOUT)
	if (debug_VramOutCount < VramOutCountMAX)
	{
		debug_VramOutTime[debug_VramOutCount] = CPU.totalticks;
		debug_VramOutAddr[debug_VramOutCount] = value;
		debug_VramOutType[debug_VramOutCount] = 2;
		debug_VramOutCount++;
	}

	if (debug_VramOutCount == 611695)
	{
		int a = 5;
	}
#endif
}

void Gpu::GP1Write(Uint32 value)
{
	byte command = (value >> 24) & 0x3F;
	Uint32 param = value & 0xFFFFFF;

	switch (command)
	{
	case 0x00: // reset
		GPU_Timing.softreset();
		drawMode = 0;
		break;

	case 0x01: // clear fifo
#ifdef FPGACOMPATIBLE
		while (!fifo.empty()) fifo.pop_front();
		if (pendingCPUtoVRAM) CopyToVRAM();
		pendingVRAMtoCPU = false;
#else
		if (pendingCPUtoVRAM) CopyToVRAM();
		while (!fifo.empty()) fifo.pop_front();
		while (!copyfifo.empty()) copyfifo.pop();
		while (!polyfifo.empty()) polyfifo.pop();
		fifoIdle = true;
		cmdTicks = 0;
		fifoTotalwords = 0;
		pendingPolyline = false;
		pendingCPUtoVRAM = false;
		pendingVRAMtoCPU = false;
		PSXRegs.GPUSTAT |= 1 << 26;
		endCopyWait = 0;
#endif
		break;

	case 0x02: // ack irq
		PSXRegs.GPUSTAT &= 0xFEFFFFFF;
		break;

	case 0x03: // display on/off
		PSXRegs.GPUSTAT &= 0xFF7FFFFF;
		PSXRegs.GPUSTAT |= (param & 1) << 23;
		break;

	case 0x04: // DMA direction
		PSXRegs.GPUSTAT &= 0x9FFFFFFF;
		PSXRegs.GPUSTAT |= (param & 3) << 29; 
		UpdateDMARequest();
		break;

	case 0x05: // Start of Display area (in VRAM)
		GPU_Timing.vramRange = value & 0x7FFFE;
		GPU_Timing.recalc(false);
		break;

	case 0x06: // horizontal diplay range
		GPU_Timing.hDisplayRange = value & 0xFFFFFF;
		GPU_Timing.recalc(false);
		break;

	case 0x07: // vertical diplay range
		GPU_Timing.vDisplayRange = value & 0xFFFFF;
		GPU_Timing.recalc(false);
		break;

	case 0x08: // Set display mode
	{
		UInt32 newstat = PSXRegs.GPUSTAT;
		newstat &= 0xFF80BFFF;
		newstat |= (param & 3) << 17; // Horizontal Resolution 1 
		newstat |= ((param >> 2) & 1) << 19; // Vertical Resolution
		newstat |= ((param >> 3) & 1) << 20; // Video Mode 
		newstat |= ((param >> 4) & 1) << 21; // Display Area Color Depth
		newstat |= ((param >> 5) & 1) << 22; // Vertical Interlace
		newstat |= ((param >> 6) & 1) << 16; // Horizontal Resolution 2
		newstat |= ((param >> 7) & 1) << 14; // Reverseflag
		if (newstat != PSXRegs.GPUSTAT)
		{
			PSXRegs.GPUSTAT = newstat;
			GPU_Timing.recalc(false);
		}
	}
		break;

	case 0x09: // Set display mode
		textureDisable = param & 1;
		break;

	case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: case 0x15: case 0x16: case 0x17: case 0x18: case 0x19: case 0x1A: case 0x1B: case 0x1C: case 0x1D: case 0x1E: case 0x1F: // GPUInfo
		switch (value & 7)
		{
		case 0: case 1: case 6: case 7: // do nothing
			break;

		case 2: //Get Texture Window
			PSXRegs.GPUREAD = textureWindow;
			break;

		case 3 : //Get Draw Area Top Left
			PSXRegs.GPUREAD = (drawingAreaLeft & 0x3FF) | ((drawingAreaTop & 0x3FF) << 10);
			break;

		case 4: //Get Draw Area Bottom Right
			PSXRegs.GPUREAD = (drawingAreaRight & 0x3FF) | ((drawingAreaBottom & 0x3FF) << 10);
			break;

		case 5: //Get Drawing Offset
			PSXRegs.GPUREAD = (drawingOffsetX & 0x7FF) | ((drawingOffsetY & 0x7FF) << 11);
			break;
		}
		break;

	default:
		_wassert(_CRT_WIDE("GP1 Command not implemented"), _CRT_WIDE("GPU"), command);
	}
}

void Gpu::UpdateDMARequest()
{
	byte direction = (PSXRegs.GPUSTAT >> 29) & 3;
	bool request = false;
	switch (direction)
	{
	case 0: break; //off
	case 1: request = (PSXRegs.GPUSTAT >> 28) & 1; break; //FIFO
	case 2: request = (PSXRegs.GPUSTAT >> 28) & 1; break; //CPU to GP0
	case 3: request = (PSXRegs.GPUSTAT >> 27) & 1; break; //GPUREAD to CPU
	}

	if (request)
	{
		int a = 5;
	}

	PSXRegs.GPUSTAT &= 0xFDFFFFFF;
	PSXRegs.GPUSTAT |= request << 25;
	DMA.requests[2] = request;
}

void Gpu::work()
{
	bool again = true;
	if (cmdTicks == 1 || cmdTicks == 2)
	{
#ifdef FPGACOMPATIBLE
		again = false;
		if (fifo.empty()) PSXRegs.GPUSTAT |= 1 << 28;
		if (pendingVRAMtoCPU) PSXRegs.GPUSTAT |= 1 << 27;
#endif
		cmdTicks = 0;
		objCount++;
	}
	if (cmdTicks > 2)
	{
		cmdTicks -= 2;
	}

	if (pendingVRAMtoCPU && endCopyWait > 0)
	{
		endCopyWait--;
		if (endCopyWait == 0)
		{
			pendingVRAMtoCPU = false;
			PSXRegs.GPUSTAT &= ~(1 << 27); // not ready to send vram
		}
	}

	while (again)
	{
		if (cmdTicks == 0 && !pendingCPUtoVRAM && !pendingVRAMtoCPU)
		{
			if (fifo.empty()) PSXRegs.GPUSTAT |= 1 << 26;
#ifdef FPGACOMPATIBLE
			if (fifo.empty()) PSXRegs.GPUSTAT |= 1 << 28;
#endif
		}

		again = false;

		if (cmdTicks == 0 && !fifoIdle)
		{
			UInt32 fifodata = fifo.front();
			byte command = fifodata >> 24;
			UInt32 param = fifodata & 0xFFFFFF;

			if (pendingCPUtoVRAM)
			{
				while (!fifo.empty() && copyfifo.size() < copyWordcount)
				{
					fifodata = fifo.front();
					fifo.pop_front();
					fifoIdle = true;
					copyfifo.push(fifodata);
				}
				UInt32 fifoCount = copyfifo.size();
				if (fifoCount >= copyWordcount)
				{
					CopyToVRAM();
					objCount++;
				}
			}
			else if (pendingVRAMtoCPU)
			{
			}
			else if (pendingPolyline)
			{
				bool shading = (polyfifo.front() >> 28) & 1;

				UInt32 count = fifo.size();
				for (int i = 0; i < count; i++)
				{
					uint data = fifo.front(); fifo.pop_front();
					fifoIdle = true;
					polyfifo.push(data);
					if (!shading || shading == (polyfifo.size() & 1))
					{
						if ((data & 0xF000F000) == 0x50005000)
						{
							drawPolyLine();
							break;
						}
					}
				}
			}
			else if (command >= 0x20 && command <= 0x3F) // polygon
			{
#ifdef FPGACOMPATIBLE
				PSXRegs.GPUSTAT &= ~(1 << 28);
#endif
				byte wordsVertex = 1;
				if ((fifodata >> 26) & 1) wordsVertex++; // textured
				if ((fifodata >> 28) & 1) wordsVertex++; // gouroud
				byte vertices = 3;
				if ((fifodata >> 27) & 1) vertices++; // quad
				byte words = wordsVertex * vertices;
				if (!((fifodata >> 28) & 1)) words++; // flat shaded
				if (fifo.size() >= words)
				{
					drawPolygon();
				}
				else
				{
					fifoTotalwords = words;
					fifoIdle = true;
					PSXRegs.GPUSTAT &= ~(1 << 26);
				}
			}
			else if (command >= 0x40 && command <= 0x5F && ((command >> 3) & 1)) // polyline
			{
#ifdef FPGACOMPATIBLE
				PSXRegs.GPUSTAT &= ~(1 << 28);
#endif
				byte words = 4;
				bool shading = (fifodata >> 28) & 1;
				if (shading) words = 3;
				if (fifo.size() >= words)
				{
					pendingPolyline = true;
					for (int i = 0; i < words; i++)
					{
						polyfifo.push(fifo.front());
						fifo.pop_front();
					}
					fifoIdle = fifo.empty();
					fifoTotalwords = 0;
					cmdTicks += 16;
				}
				else
				{
					fifoTotalwords = words;
					fifoIdle = true;
					PSXRegs.GPUSTAT &= ~(1 << 26);
				}
			}
			else if (command >= 0x40 && command <= 0x5F) // line
			{
#ifdef FPGACOMPATIBLE
				PSXRegs.GPUSTAT &= ~(1 << 28);
#endif
				byte words = 3;
				if ((fifodata >> 28) & 1) words = 4; // shading enable
				if (fifo.size() >= words)
				{
					drawLine();
				}
				else
				{
					fifoTotalwords = words;
					fifoIdle = true;
					PSXRegs.GPUSTAT &= ~(1 << 26);
				}
			}
			else if (command >= 0x60 && command <= 0x7F) // rectangle
			{
#ifdef FPGACOMPATIBLE
			PSXRegs.GPUSTAT &= ~(1 << 28);
#endif
			if (drawingAreaLeft == 0x200)
			{
				int a = 5;
			}

				byte words = 2;
				if ((fifodata >> 26) & 1) words = 3; // texture enable
				if (((fifodata >> 27) & 3) == 0) words++; // variable size
				if (fifo.size() >= words)
				{
					drawRectangle();
				}
				else
				{
					fifoTotalwords = words;
					fifoIdle = true;
					PSXRegs.GPUSTAT &= ~(1 << 26);
				}
			}
			else if (command >= 0x80 && command <= 0x9F) // copy rectangle from VRAM to VRAM
			{
				if (fifo.size() >= 4)
				{
					CopyVRAMVRAM();
				}
				else
				{
					fifoTotalwords = 4;
					fifoIdle = true;
					PSXRegs.GPUSTAT &= ~(1 << 26);
				}
			}
			else if (command >= 0xA0 && command <= 0xBF) // copy rectangle from CPU to VRAM
			{
				if (fifo.size() >= 3)
				{
					pendingCPUtoVRAM = true;
					fifo.pop_front();
					copyDstX = fifo.front() & 0x3FF;
					copyDstY = (fifo.front() >> 16) & 0x1FF;
					fifo.pop_front();
					copySizeX = fifo.front() & 0x3FF;
					copySizeY = (fifo.front() >> 16) & 0x1FF;
					fifo.pop_front();
					if (copySizeX == 0) copySizeX = 0x400;
					if (copySizeY == 0) copySizeY = 0x200;
					copyWordcount = ((copySizeX * copySizeY) + 1) / 2;
					fifoIdle = fifo.empty();
					fifoTotalwords = 0;
					PSXRegs.GPUSTAT |= 1 << 28; // ready to receive dma
				}
				else
				{
					fifoTotalwords = 3;
					fifoIdle = true;
					PSXRegs.GPUSTAT &= ~(1 << 26);
				}
			}
			else if (command >= 0xC0 && command <= 0xDF) // copy rectangle from VRAM to CPU
			{
				if (fifo.size() >= 3)
				{
					pendingVRAMtoCPU = true;
					fifo.pop_front();
					copyDstX = fifo.front() & 0x3FF;
					copyDstY = (fifo.front() >> 16) & 0x1FF;
					fifo.pop_front();
					copySizeX = (((fifo.front() & 0xFFFF) - 1) & 0x3FF) + 1;
					copySizeY = (((fifo.front() >> 16) - 1) & 0x1FF) + 1;
					fifo.pop_front();
					copyWordcount = ((copySizeX * copySizeY) + 1) / 2;
					fifoIdle = fifo.empty();
					fifoTotalwords = 0;
					copyCntX = 0;
					copyCntY = 0;
#ifdef FPGACOMPATIBLE
					cmdTicks = 60;
#else
					PSXRegs.GPUSTAT |= 1 << 27; // ready to send vram
#endif
					PSXRegs.GPUSTAT &= ~(1 << 28); // not ready to receive dma
				}
				else
				{
					fifoTotalwords = 3;
					fifoIdle = true;
					PSXRegs.GPUSTAT &= ~(1 << 26);
				}
			}
			else
			{
				switch (command)
				{
				// nop
				case 0x00: case 0x03: case 0x04: case 0x05: case 0x06: case 0x07: case 0x08: case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D: case 0x0E: case 0x0F:
				case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: case 0x15: case 0x16: case 0x17: case 0x18: case 0x19: case 0x1A: case 0x1B: case 0x1C: case 0x1D: case 0x1E:
				case 0xE0: case 0xE7: case 0xE8: case 0xE9: case 0xEA: case 0xEB: case 0xEC: case 0xED: case 0xEE: case 0xEF:
				case 0xFF:
					fifo.pop_front();
					fifoIdle = fifo.empty();
					again = true;
					break;

				case 0x01: // clear cache
					fifo.pop_front();
					fifoIdle = fifo.empty();
					PSXRegs.GPUSTAT &= ~(1 << 26);
					//cmdTicks += 1;
					again = true;
					break;

				case 0x02: // direct vram
#ifdef FPGACOMPATIBLE
					PSXRegs.GPUSTAT &= ~(1 << 28);
#endif
					if (fifo.size() >= 3)
					{
						fillVRAM();
					}
					else
					{
						fifoTotalwords = 3;
						fifoIdle = true;
						PSXRegs.GPUSTAT &= ~(1 << 26);
					}
					break;

				case 0x1F: // irq request
					if (((PSXRegs.GPUSTAT >> 24) & 1) == 0)
					{
						PSXRegs.GPUSTAT |= 1 << 24;
						PSXRegs.setIRQ(1);
					}
					fifo.pop_front();
					fifoIdle = fifo.empty();
					//PSXRegs.GPUSTAT &= ~(1 << 26);
					//cmdTicks += 1;
					again = true;
					break;

				case 0xE1: // Draw Mode setting
					PSXRegs.GPUSTAT &= 0xFFFFF800;
					PSXRegs.GPUSTAT |= param & 0x7FF;
					drawMode = param & 0x3FFF;
					fifo.pop_front();
					fifoIdle = fifo.empty();
					//PSXRegs.GPUSTAT &= ~(1 << 26);
					//cmdTicks += 1;
					again = true;
					break;

				case 0xE2: // Set Texture window
				{
					textureWindow = param;
					byte maskX = param & 0x1F;
					byte maskY = (param >> 5) & 0x1F;
					byte offsetX = (param >> 10) & 0x1F;
					byte offsetY = (param >> 15) & 0x1F;
					textureWindow_AND_X = ~(maskX * 8);
					textureWindow_AND_Y = ~(maskY * 8);
					textureWindow_OR_X = (offsetX & maskX) * 8u;
					textureWindow_OR_Y = (offsetY & maskY) * 8u;
					fifo.pop_front();
					fifoIdle = fifo.empty();
					//PSXRegs.GPUSTAT &= ~(1 << 26);
					//cmdTicks += 1;
					again = true;
				}
					break;

				case 0xE3: // Set Drawing Area top left (X1,Y1)
					drawingAreaLeft = param & 0x3FF;
					drawingAreaTop = (param >> 10) & 0x1FF;
					if (drawingAreaLeft == 0x200)
					{
						int a = 5;
					}
					fifo.pop_front();
					fifoIdle = fifo.empty();
					//PSXRegs.GPUSTAT &= ~(1 << 26);
					//cmdTicks += 1;
					again = true;
					break;

				case 0xE4: // Set Drawing Area bottom right (X2,Y2)
					drawingAreaRight = param & 0x3FF;
					drawingAreaBottom = (param >> 10) & 0x1FF;
					fifo.pop_front();
					fifoIdle = fifo.empty();
					//PSXRegs.GPUSTAT &= ~(1 << 26);
					//cmdTicks += 1;
					again = true;
					break;

				case 0xE5: // Set Drawing Offset (X,Y)
					drawingOffsetX = param & 0x7FF;
					if (drawingOffsetX & 0x400) drawingOffsetX = -1024 + (drawingOffsetX & 0x3FF);
					drawingOffsetY = (param >> 11) & 0x7FF;
					if (drawingOffsetY & 0x400) drawingOffsetY = -1024 + (drawingOffsetY & 0x3FF);
					fifo.pop_front();
					fifoIdle = fifo.empty();
					//PSXRegs.GPUSTAT &= ~(1 << 26);
					//cmdTicks += 1;
					again = true;
					break;

				case 0xE6: // Mask Bit Setting
					PSXRegs.GPUSTAT &= 0xFFFFE7FF;
					PSXRegs.GPUSTAT |= (param & 3) << 11;
					checkMask = (param >> 1) & 1;
					fifo.pop_front();
					fifoIdle = fifo.empty();
					//PSXRegs.GPUSTAT &= ~(1 << 26);
					//cmdTicks += 1;
					again = true;
					break;

				default:
					_wassert(_CRT_WIDE("GP0 Command not implemented"), _CRT_WIDE("GPU"), command);
				}
			}

			cmdTicksLast = cmdTicks;
		}
#ifdef FPGACOMPATIBLE
		again = false;
#endif
	}

#ifdef FPGACOMPATIBLE
	// set ready to receive DMA
	if (!pendingPolyline && !pendingCPUtoVRAM && !pendingVRAMtoCPU)
	{
		if (fifo.empty() || fifo.size() < fifoTotalwords) PSXRegs.GPUSTAT |= 1 << 28;
		else PSXRegs.GPUSTAT &= ~(1 << 28);
	}
#else
	// set ready to receive DMA
	if (!pendingPolyline && !pendingCPUtoVRAM && !pendingVRAMtoCPU)
	{
		if (fifo.empty() || fifo.size() < fifoTotalwords) PSXRegs.GPUSTAT |= 1 << 28;
		else PSXRegs.GPUSTAT &= ~(1 << 28);
	}
#endif

	UpdateDMARequest();
}

void Gpu::fillVRAM()
{
	UInt32 color = fifo.front() & 0xFFFFFF;
	fifo.pop_front();
	UInt16 x1 = fifo.front() & 0x3F0;
	UInt16 y1 = (fifo.front() >> 16) & 0x1FF;
	fifo.pop_front();
	UInt16 width = ((fifo.front() & 0x3FF) + 0xF) & 0xFFF0;
	UInt16 height = (fifo.front() >> 16) & 0x1FF;
	fifo.pop_front();
	cmdTicks = 46 + width * height;
	UInt16 color16 = ((color >> 3) & 0x1F) | (((color >> 11) & 0x1F) << 5) | (((color >> 19) & 0x1F) << 10);

	bool interlaceDrawing = ((PSXRegs.GPUSTAT & 0x00480400) == 0x00480000);
	for (int y = 0; y < height; y++)
	{
		UInt32 row = (y1 + y) & 0x1FF;
		if (interlaceDrawing && (GPU_Timing.activeLineLSB == (row & 1))) continue;
		for (int x = 0; x < width; x++)
		{
			Uint32 col = (x1 + x) & 0x3FF;
			UInt32 addr = (row * 1024 + col) * 2;
			VRAM[addr + 0] = color16;
			VRAM[addr + 1] = color16 >> 8;
		}
	}
	fifoIdle = fifo.empty();
	fifoTotalwords = 0;
}

void Gpu::CopyVRAMVRAM()
{
	fifo.pop_front();
	UInt16 srcX = fifo.front() & 0x3FF;
	UInt16 srcY = (fifo.front() >> 16) & 0x1FF;
	fifo.pop_front();
	UInt16 dstX = fifo.front() & 0x3FF;
	UInt16 dstY = (fifo.front() >> 16) & 0x1FF;
	fifo.pop_front();
	UInt16 width = (fifo.front() & 0x3FF);
	UInt16 height = (fifo.front() >> 16) & 0x1FF;
	fifo.pop_front();
	if (width == 0) width = 0x400;
	if (height == 0) height = 0x200;

	bool drawMask = ((PSXRegs.GPUSTAT >> 11) & 1) == 1;
	bool setMask = ((PSXRegs.GPUSTAT >> 11) & 1) == 1;

#ifndef FPGACOMPATIBLE
	if (width == 0 || height == 0 || (srcX == dstX && srcY == dstY && !((PSXRegs.GPUSTAT >> 11) & 1)))
	{
		// don't draw
	}
	else
#endif
	{
		SByte dir = 1;
		if (srcX < dstX || (((srcX + width - 1) & 0x3FF) < ((dstX + width - 1) & 0x3FF))) dir = -1;

		for (int y = 0; y < height; y++)
		{
			UInt16 rowSrc = (y + srcY) & 0x1FF;
			UInt16 rowDst = (y + dstY) & 0x1FF;

			int x = 0;
			if (dir == -1) x = width - 1;
			while (x >= 0 && x < width)
			{
				UInt16 colSrc = (x + srcX) & 0x3FF;
				UInt16 colDst = (x + dstX) & 0x3FF;
				UInt32 addrSrc = (rowSrc * 1024 + colSrc) * 2;
				UInt32 addrDst = (rowDst * 1024 + colDst) * 2;
				UInt16 dataword = *(UInt16*)&VRAM[addrSrc];

				if (!drawMask || ((dataword >> 15) & 1) == 0)
				{
					if (setMask) dataword |= 0x8000;
					VRAM[addrDst + 0] = dataword;
					VRAM[addrDst + 1] = dataword >> 8;
				}
#ifdef VRAMFILEOUT
				writePixelExport(colDst, rowDst, dataword);
#endif // VRAMFILEOUT

				x += dir;
			}
		}
	}

	cmdTicks = width * height * 2;

	fifoIdle = fifo.empty();
	fifoTotalwords = 0;
}

void Gpu::CopyToVRAM()
{
	pendingCPUtoVRAM = false;

	bool firstWord = true;
	UInt32 dataword;

	// todo: AND/OR masking

	for (int y = 0; y < copySizeY; y++)
	{
		UInt32 row = (copyDstY + y) & 0x1FF;
		for (int x = 0; x < copySizeX; x++)
		{
			Uint32 col = (copyDstX + x) & 0x3FF;
			UInt32 addr = (row * 1024 + col) * 2;

			if (firstWord)
			{
				dataword = copyfifo.front(); copyfifo.pop();
				VRAM[addr + 0] = dataword;
				VRAM[addr + 1] = dataword >> 8;
#ifdef VRAMFILEOUT
				writePixelExport(col, row, dataword);
#endif // VRAMFILEOUT
			}
			else
			{
				VRAM[addr + 0] = dataword >> 16;
				VRAM[addr + 1] = dataword >> 24;
#ifdef VRAMFILEOUT
				writePixelExport(col, row, dataword >> 16);
#endif // VRAMFILEOUT
				if (copyfifo.empty()) break;
			}
			firstWord = !firstWord;
		}
		if (copyfifo.empty() && firstWord) break;
	}
	fifoIdle = fifo.empty();
	if (fifo.empty()) PSXRegs.GPUSTAT |= 1 << 26;
	fifoTotalwords = 0;
}

UInt32 Gpu::ReadVRAMtoCPU()
{
	if (!pendingVRAMtoCPU) return 0xFFFFFFFF;

	UInt32 value = 0;
	for (int i = 0; i < 2; i++)
	{
		UInt16 xPos = (copyDstX + copyCntX) & 0x3FF;
		UInt16 yPos = (copyDstY + copyCntY) & 0x1FF;

		UInt32 addr = (yPos * 1024 + xPos) * 2;
		value |= (*(UInt16*)&VRAM[addr]) << (i * 16);

		copyCntX++;
		if (copyCntX == copySizeX)
		{
			copyCntX = 0;
			copyCntY++;
			if (copyCntY == copySizeY)
			{
				endCopyWait = 2;
				copyCntY--;
				copyCntX++;
				copyCntX & 0x3FF;
			}
		}
	}

	PSXRegs.GPUREAD = value;

#if defined(VRAMFILEOUT)
	if (debug_VramOutCount < VramOutCountMAX)
	{
		debug_VramOutTime[debug_VramOutCount] = CPU.commands;
		debug_VramOutAddr[debug_VramOutCount] = value;
		debug_VramOutData[debug_VramOutCount] = 0;
		debug_VramOutType[debug_VramOutCount] = 4;
		debug_VramOutCount++;
		pixeldrawn++;
	}
#endif

	return value;
}

void Gpu::drawRectangle()
{
	// first dword
	UInt32 fifodata = fifo.front(); fifo.pop_front();

	bool textured = (fifodata >> 26) & 1;
	byte size = (fifodata >> 27) & 3;
	bool transparency = (fifodata >> 25) & 1;
	bool rawTexture = (fifodata >> 24) & 1;
	UInt32 firstColor = fifodata & 0xFFFFFF;

	Int16 posX = fifo.front() & 0x7FF;
	Int16 posY = (fifo.front() >> 16) & 0x7FF;
	//if (posX & 0x400) posX = -1024 + (posX & 0x3FF);
	//if (posY & 0x400) posY = -1024 + (posY & 0x3FF);
	posX = drawingOffsetX + posX;
	posY = drawingOffsetY + posY;
	if (posX & 0x400) posX |= 0xF800; else posX &= 0x3FF;
	if (posY & 0x400) posY |= 0xF800; else posY &= 0x3FF;


	if (posX == 816 && posY == 96)
	{
		int a = 5;
	}

	fifo.pop_front();

	if (textured)
	{
		UInt32 data = fifo.front(); fifo.pop_front();
		vertices[0].u = data & 0xFF;
		vertices[0].v = (data >> 8) & 0xFF;
		texturePaletteX = ((data >> 16) & 0x3F) << 4;
		texturePaletteY = (data >> 22) & 0x1FF;
	}

	UInt16 width;
	UInt16 height;
	switch (size)
	{
	case 0: 
		width = fifo.front() & 0x3ff;
		height = (fifo.front() >> 16) & 0x1FF;
		fifo.pop_front();
		break;

	case 1:
		width = 1;
		height = 1;
		break;

	case 2:
		width = 8;
		height = 8;
		break;

	case 3:
		width = 16;
		height = 16;
		break;
	}

	fifoIdle = fifo.empty();
	//if (!fifoIdle) psx.log("Fifo not empty after Rectangle CMD");
	fifoTotalwords = 0;

	if (drawingAreaLeft > drawingAreaRight || drawingAreaTop > drawingAreaBottom) return;

	Int16 clipLeft = posX;
	Int16 clipRight = posX + width;
	Int16 clipTop = posY;
	Int16 clipBottom = posY + height;

	if (clipLeft   < drawingAreaLeft) clipLeft   = drawingAreaLeft; if (clipLeft   > drawingAreaRight)  clipLeft   = drawingAreaRight;
	if (clipRight  < drawingAreaLeft) clipRight  = drawingAreaLeft; if (clipRight  > drawingAreaRight)  clipRight  = drawingAreaRight;
	if (clipTop    < drawingAreaTop)  clipTop    = drawingAreaTop;  if (clipTop    > drawingAreaBottom) clipTop    = drawingAreaBottom;
	if (clipBottom < drawingAreaTop)  clipBottom = drawingAreaTop;  if (clipBottom > drawingAreaBottom) clipBottom = drawingAreaBottom;

	clipRight++;
	clipBottom++;

	UInt16 sizeX = clipRight - clipLeft;
	UInt16 sizeY = clipBottom - clipTop;

#ifdef REPRODUCIBLEGPUTIMING
	cmdTicks = 50;
	byte mul = 2;
	if (transparency || textured) mul = 4;
	cmdTicks += (width * height) * mul;
#else
	if (textured)
	{
		if (transparency || ((PSXRegs.GPUSTAT >> 12) & 1))
		{
			cmdTicks = (sizeX * 2 + (sizeX + 1) / 2) * sizeY;
		}
		else
		{
			cmdTicks = sizeX * 2 * sizeY;
		}
	}
	else
	{
		if (transparency || ((PSXRegs.GPUSTAT >> 12) & 1))
		{
			cmdTicks = (sizeX + (sizeX + 1) / 2) * sizeY;
		}
		else
		{
			cmdTicks = sizeX * sizeY;
		}
	}
	if ((PSXRegs.GPUSTAT & 0x00480400) == 0x00480000 && sizeY > 1)
	{
		cmdTicks /= 2;
	}
	cmdTicks += 16;
#endif

	vertices[0].color = firstColor;
	vertices[0].r = firstColor;
	vertices[0].g = (firstColor >> 8) & 0xFF;
	vertices[0].b = (firstColor >> 16);
	vertices[0].x = posX;
	vertices[0].y = posY;

	drawRectangleBase(vertices[0], width, height, textured, rawTexture, transparency);
}

void Gpu::drawRectangleBase(Vertex v, UInt16 width, UInt16 height, bool textured, bool rawTexture, bool transparent)
{
	bool interlaceDrawing = ((PSXRegs.GPUSTAT & 0x00480400) == 0x00480000);

	for (int y = 0; y < height; y++)
	{
		UInt16 posY = y + v.y;
		if (!interlaceDrawing || (GPU_Timing.activeLineLSB != (posY & 1)))
		{
			if (posY >= drawingAreaTop && posY <= drawingAreaBottom)
			{
				for (int x = 0; x < width; x++)
				{
					UInt16 posX = x + v.x;
					if (posX >= drawingAreaLeft && posX <= drawingAreaRight)
					{
						shadePixel(posX, posY, v.r, v.g, v.b, v.u + x, v.v + y, textured, transparent, rawTexture, false, 0, 0, false);
					}
				}
			}
		}
	}
}

void Gpu::drawPolyLine()
{
	pendingPolyline = false;

	UInt32 fifodata = polyfifo.front(); polyfifo.pop();
	bool shading = (fifodata >> 28) & 1;
	bool transparency = (fifodata >> 25) & 1;

	if (shading)
	{
		vertices[0].color = fifodata & 0xFFFFFF;
		vertices[0].r = vertices[0].color;
		vertices[0].g = (vertices[0].color >> 8) & 0xFF;
		vertices[0].b = (vertices[0].color >> 16);
		UInt32 count = polyfifo.size() / 2;
		Int16 posX = polyfifo.front() & 0x7FF;
		Int16 posY = (polyfifo.front() >> 16) & 0x7FF;
		if (posX & 0x400) posX = -1024 + (posX & 0x3FF);
		if (posY & 0x400) posY = -1024 + (posY & 0x3FF);
		posX += drawingOffsetX;
		posY += drawingOffsetY;
		vertices[0].x = posX;
		vertices[0].y = posY;
		polyfifo.pop();
		for (int i = 1; i < count; i++)
		{
			vertices[1].color = polyfifo.front() & 0xFFFFFF;
			vertices[1].r = vertices[1].color;
			vertices[1].g = (vertices[1].color >> 8) & 0xFF;
			vertices[1].b = (vertices[1].color >> 16);
			polyfifo.pop();
			posX = polyfifo.front() & 0x7FF;
			posY = (polyfifo.front() >> 16) & 0x7FF;
			if (posX & 0x400) posX = -1024 + (posX & 0x3FF);
			if (posY & 0x400) posY = -1024 + (posY & 0x3FF);
			posX += drawingOffsetX;
			posY += drawingOffsetY;
			vertices[1].x = posX;
			vertices[1].y = posY;
			polyfifo.pop();
			//if (polyLineShadedCount < 1)
			{
				polyLineShadedCount++;
				drawLineBase(vertices[0], vertices[1], shading, transparency);
			}
			vertices[0].x = vertices[1].x;
			vertices[0].y = vertices[1].y;
			vertices[0].r = vertices[1].r;
			vertices[0].g = vertices[1].g;
			vertices[0].b = vertices[1].b;
		}
	}
	else
	{
		vertices[0].color = fifodata & 0xFFFFFF;
		vertices[1].color = fifodata & 0xFFFFFF;
		for (int i = 0; i < 2; i++)
		{
			vertices[i].r = vertices[i].color;
			vertices[i].g = (vertices[i].color >> 8) & 0xFF;
			vertices[i].b = (vertices[i].color >> 16);
		}

		UInt32 count = polyfifo.size() - 1;

		Int16 posX = polyfifo.front() & 0x7FF;
		Int16 posY = (polyfifo.front() >> 16) & 0x7FF;
		if (posX & 0x400) posX = -1024 + (posX & 0x3FF);
		if (posY & 0x400) posY = -1024 + (posY & 0x3FF);
		posX += drawingOffsetX;
		posY += drawingOffsetY;
		vertices[0].x = posX;
		vertices[0].y = posY;
		polyfifo.pop();

		for (int i = 1; i < count; i++)
		{
			posX = polyfifo.front() & 0x7FF;
			posY = (polyfifo.front() >> 16) & 0x7FF;
			if (posX & 0x400) posX = -1024 + (posX & 0x3FF);
			if (posY & 0x400) posY = -1024 + (posY & 0x3FF);
			posX += drawingOffsetX;
			posY += drawingOffsetY;
			vertices[1].x = posX;
			vertices[1].y = posY;
			polyfifo.pop();
			drawLineBase(vertices[0], vertices[1], shading, transparency);
			vertices[0].x = vertices[1].x;
			vertices[0].y = vertices[1].y;
		}
	}

	polyfifo.pop();
	fifoIdle = fifo.empty();
	if (!fifoIdle) psx.log("Fifo not empty after PolyLine CMD");
	fifoTotalwords = 0;
}

void Gpu::drawLine()
{
	UInt32 fifodata = fifo.front(); fifo.pop_front();
	bool shading = (fifodata >> 28) & 1;
	bool transparency = (fifodata >> 25) & 1;

	if (shading)
	{
		vertices[0].color = fifodata & 0xFFFFFF;

		Int16 posX = fifo.front() & 0x7FF;
		Int16 posY = (fifo.front() >> 16) & 0x7FF;
		if (posX & 0x400) posX = -1024 + (posX & 0x3FF);
		if (posY & 0x400) posY = -1024 + (posY & 0x3FF);
		posX += drawingOffsetX;
		posY += drawingOffsetY;
		vertices[0].x = posX;
		vertices[0].y = posY;
		fifo.pop_front();

		vertices[1].color = fifo.front() & 0xFFFFFF;
		fifo.pop_front();

		posX = fifo.front() & 0x7FF;
		posY = (fifo.front() >> 16) & 0x7FF;
		if (posX & 0x400) posX = -1024 + (posX & 0x3FF);
		if (posY & 0x400) posY = -1024 + (posY & 0x3FF);
		posX += drawingOffsetX;
		posY += drawingOffsetY;
		vertices[1].x = posX;
		vertices[1].y = posY;
		fifo.pop_front();

		for (int i = 0; i < 2; i++)
		{
			vertices[i].r = vertices[i].color;
			vertices[i].g = (vertices[i].color >> 8) & 0xFF;
			vertices[i].b = (vertices[i].color >> 16);
		}
	}
	else
	{
		vertices[0].color = fifodata & 0xFFFFFF;
		vertices[1].color = fifodata & 0xFFFFFF;
		for (int i = 0; i < 2; i++)
		{
			vertices[i].r = vertices[i].color;
			vertices[i].g = (vertices[i].color >> 8) & 0xFF;
			vertices[i].b = (vertices[i].color >> 16);
			Int16 posX = fifo.front() & 0x7FF;
			Int16 posY = (fifo.front() >> 16) & 0x7FF;
			if (posX & 0x400) posX = -1024 + (posX & 0x3FF);
			if (posY & 0x400) posY = -1024 + (posY & 0x3FF);
			posX += drawingOffsetX;
			posY += drawingOffsetY;
			vertices[i].x = posX;
			vertices[i].y = posY;
			fifo.pop_front();
		}
	}

	drawLineBase(vertices[0], vertices[1], shading, transparency);
	fifoIdle = fifo.empty();
	//if (!fifoIdle) psx.log("Fifo not empty after Line CMD");
	fifoTotalwords = 0;
}

void Gpu::drawLineBase(Vertex v0, Vertex v1, bool shaded, bool transparent)
{
	// cycle count
	UInt16 minX;
	UInt16 minY;
	UInt16 maxX;
	UInt16 maxY;
	if (v0.x < v1.x)
	{
		minX = v0.x;
		maxX = v1.x;
	}
	else
	{
		minX = v1.x;
		maxX = v0.x;
	}
	if (v0.y < v1.y)
	{
		minY = v0.y;
		maxY = v1.y;
	}
	else
	{
		minY = v1.y;
		maxY = v0.y;
	}

	if ((maxX - minX) >= 0x400 || (maxY - minY) >=  0x200) return;

	// todo: correct clip/clamp
	UInt16 clipLeft = minX;
	UInt16 clipRight = maxX + 1;
	UInt16 clipTop = minY;
	UInt16 clipBottom = maxY + 1;

	UInt16 sizeX = clipRight - clipLeft;
	UInt16 sizeY = clipBottom - clipTop;
	if ((PSXRegs.GPUSTAT & 0x00480400) == 0x00480000 && sizeY > 1) sizeY /= 2;
#ifdef REPRODUCIBLEGPUTIMING
	cmdTicks += 1000;
#else
	if (sizeX > sizeY) cmdTicks += sizeX;
	else cmdTicks += sizeY;
#endif

	// drawing
	Int32 dx = std::abs(v0.x - v1.x);
	Int32 dy = std::abs(v0.y - v1.y);
	Int32 points = dy;
	if (dx > dy) points = dx;

	if (dx >= 0x400 || dy >= 0x200) return;

	if (v0.x >= v1.x && points > 0)
	{
		std::swap(v0, v1);
	}

	Int64 stepDx = 0;
	Int64 stepDy = 0;
	Int32 stepDr = 0;
	Int32 stepDg = 0;
	Int32 stepDb = 0;
	if (points > 0)
	{
		stepDx = ((Int64)(v1.x - v0.x)) << 32;
		if (stepDx < 0) stepDx -= points - 1;
		else if (stepDx > 0) stepDx += points - 1;
		stepDx /= points;

		stepDy = ((Int64)(v1.y - v0.y)) << 32;
		if (stepDy < 0) stepDy -= points - 1;
		else if (stepDy > 0) stepDy += points - 1;
		stepDy /= points;

		if (shaded)
		{
			Int32 test = (v1.r - v0.r);
			test = test << 12;
			test = test / points;

			stepDr = (Int32)((UInt32)(v1.r - v0.r) << 12) / points;
			stepDg = (Int32)((UInt32)(v1.g - v0.g) << 12) / points;
			stepDb = (Int32)((UInt32)(v1.b - v0.b) << 12) / points;
		}
	}

	UInt64 workX = (((UInt64)v0.x) << 32) | (((UInt32)1) << 31);
	UInt64 workY = ((UInt64)v0.y << 32) | (((UInt32)1) << 31);
	UInt32 workR;
	UInt32 workG;
	UInt32 workB;
	if (shaded)
	{
		workR = ((UInt32)v0.r << 12) | (1 << 11);
		workG = ((UInt32)v0.g << 12) | (1 << 11);
		workB = ((UInt32)v0.b << 12) | (1 << 11);
	}

	workX -= 1024;
	if (stepDy < 0) workY -= 1024;

	bool interlaceDrawing = ((PSXRegs.GPUSTAT & 0x00480400) == 0x00480000);

	bool dithering = ((drawMode >> 9) & 1);

	for (int i = 0; i <= points; i++)
	{
		Int32 x = (workX >> 32) & 2047;
		Int32 y = (workY >> 32) & 2047;

		if (!interlaceDrawing || (GPU_Timing.activeLineLSB != (y & 1)))
		{
			if (x >= drawingAreaLeft && x <= drawingAreaRight && y >= drawingAreaTop && y <= drawingAreaBottom)
			{
				if (shaded)
				{
					shadePixel(x, y, workR >> 12, workG >> 12, workB >> 12, 0, 0, false, transparent, false, dithering, 0, 0, false);
				}
				else
				{
					shadePixel(x, y, v0.r, v0.g, v0.b, 0, 0, false, transparent, false, dithering, 0, 0, false);
				}
			}
		}

		workX += stepDx;
		workY += stepDy;
		if (shaded)
		{
			workR += stepDr;
			workG += stepDg;
			workB += stepDb;
		}
	}

}

void Gpu::drawPolygon()
{
	// first dword
	UInt32 fifodata = fifo.front(); fifo.pop_front();
	byte vertexCount = 3; 
	if ((fifodata >> 27) & 1) vertexCount = 4;
	UInt32 firstColor = fifodata & 0xFFFFFF;
	bool shaded = (fifodata >> 28) & 1;
	bool textured = (fifodata >> 26) & 1;
	bool transparency = (fifodata >> 25) & 1;
	bool rawTexture = (fifodata >> 24) & 1;
	bool dithering = (shaded || (textured && !rawTexture)) && ((drawMode >> 9) & 1);

#ifdef REPRODUCIBLEGPUTIMING
	cmdTicks = 500;
#else
	if (vertexCount == 3)
	{
		if (shaded)
		{
			if (textured) cmdTicks = 496;
			else cmdTicks = 334;
		}
		else
		{
			if (textured) cmdTicks = 226;
			else cmdTicks = 46;
		}
	}
	else
	{
		if (shaded)
		{
			if (textured) cmdTicks = 532;
			else cmdTicks = 370;
		}
		else
		{
			if (textured) cmdTicks = 262;
			else cmdTicks = 82;
		}
	}
#endif

	polygonCount++;
	//if (polygonCount > 2) return;

	minU = 0xFF;
	maxU = 0;
	minV = 0xFF;
	maxV = 0x0;

	for (int i = 0; i < vertexCount; i++)
	{
		// color
		if (shaded && i > 0)
		{
			vertices[i].color = fifo.front() & 0xFFFFFF; fifo.pop_front();
		}
		else
		{
			vertices[i].color = firstColor;
		}
		vertices[i].r = vertices[i].color;
		vertices[i].g = (vertices[i].color >> 8) & 0xFF;
		vertices[i].b = (vertices[i].color >> 16);

		if (drawingOffsetX < 0)
		{
			int a = 5;
		}

		// position
		Int16 posX = fifo.front() & 0x7FF;
		Int16 posY = (fifo.front() >> 16) & 0x7FF;
		if (posX & 0x400) posX |= 0xF800; else posX &= 0x3FF;
		if (posY & 0x400) posY |= 0xF800; else posY &= 0x3FF;
		posX = drawingOffsetX + posX;
		posY = drawingOffsetY + posY;
		if (drawingOffsetX < 0)
		{
			if (posX & 0x400) posX |= 0xF800; else posX &= 0x3FF;
		}
		if (drawingOffsetY < 0)
		{
			if (posY & 0x400) posY |= 0xF800; else posY &= 0x3FF;
		}
		vertices[i].x = posX;
		vertices[i].y = posY;

		fifo.pop_front();
		// texture
		if (textured)
		{
			UInt32 data = fifo.front();
			vertices[i].u = data & 0xFF;
			vertices[i].v = (data >> 8) & 0xFF;

			if (vertices[i].u < minU) minU = vertices[i].u;
			if (vertices[i].u > maxU) maxU = vertices[i].u;
			if (vertices[i].v < minV) minV = vertices[i].v;
			if (vertices[i].v > maxV) maxV = vertices[i].v;

			if (i == 0)
			{
				texturePaletteX = ((data >> 16) & 0x3F) << 4;
				texturePaletteY = (data >> 22) & 0x1FF;
			}
			if (i == 1)
			{
				GPU.drawMode &= ~(0x9FF);
				GPU.drawMode |= (data >> 16) & 0x9FF;
			}
			fifo.pop_front();
		}
	}

	fifoIdle = fifo.empty();
	//if (!fifoIdle) psx.log("Fifo not empty after Polygon CMD");
	fifoTotalwords = 0;

	if (drawingAreaLeft > drawingAreaRight || drawingAreaTop > drawingAreaBottom) return;
	
	// todo: large polygon culling

	byte bilinearfilter = 0;

	// tick calculation
	{
		Int16 x0 = vertices[0].x;
		Int16 x1 = vertices[1].x;
		Int16 x2 = vertices[2].x;
		Int16 y0 = vertices[0].y;
		Int16 y1 = vertices[1].y;
		Int16 y2 = vertices[2].y;

		if (x0 < drawingAreaLeft) x0 = drawingAreaLeft; if (x0 >= drawingAreaRight) x0 = drawingAreaRight - 1;
		if (x1 < drawingAreaLeft) x1 = drawingAreaLeft; if (x1 >= drawingAreaRight) x1 = drawingAreaRight - 1;
		if (x2 < drawingAreaLeft) x2 = drawingAreaLeft; if (x2 >= drawingAreaRight) x2 = drawingAreaRight - 1;

		if (y0 < drawingAreaTop) y0 = drawingAreaTop; if (y0 >= drawingAreaBottom) y0 = drawingAreaBottom - 1;
		if (y1 < drawingAreaTop) y1 = drawingAreaTop; if (y1 >= drawingAreaBottom) y1 = drawingAreaBottom - 1;
		if (y2 < drawingAreaTop) y2 = drawingAreaTop; if (y2 >= drawingAreaBottom) y2 = drawingAreaBottom - 1;

#ifdef REPRODUCIBLEGPUTIMING
		int maxX = std::max(vertices[0].x, std::max(vertices[1].x, vertices[2].x));
		int minX = std::min(vertices[0].x, std::min(vertices[1].x, vertices[2].x));
		int maxY = std::max(vertices[0].y, std::max(vertices[1].y, vertices[2].y));
		int minY = std::min(vertices[0].y, std::min(vertices[1].y, vertices[2].y));
		byte mul = 2;
		if (textured || transparency) mul = 4;
		cmdTicks += (maxY + 1 - minY) * (maxX - minX) * mul;
#else
		Int32 polyticks = std::abs(x0 * y1 + x1 * y2 + x2 * y0 - x0 * y2 - x1 * y0 - x2 * y1) / 2;
		if (textured) polyticks *= 2;
		if (transparency || ((PSXRegs.GPUSTAT >> 12) & 1)) polyticks += (polyticks + 1) / 2;
		if ((PSXRegs.GPUSTAT & 0x00480400) == 0x00480000) polyticks /= 2;
		cmdTicks += polyticks;
#endif

		bool useBilinear = false;
#ifdef BILINEAR
		int maxX = std::max(vertices[0].x, std::max(vertices[1].x, vertices[2].x));
		int minX = std::min(vertices[0].x, std::min(vertices[1].x, vertices[2].x));
		int maxY = std::max(vertices[0].y, std::max(vertices[1].y, vertices[2].y));
		int minY = std::min(vertices[0].y, std::min(vertices[1].y, vertices[2].y));
		if ((maxX - minX) >= bilinearfilter && (maxY - minY) >= bilinearfilter)
		{
			useBilinear = dithering;
		}
#endif;
		POLYGON.drawPolygon(vertices[0], vertices[1], vertices[2], shaded, textured, transparency, rawTexture, dithering, useBilinear);
	}

	if (vertexCount == 4)
	{
		// todo : for quads, add second culling check
		Int16 x0 = vertices[2].x;
		Int16 x1 = vertices[1].x;
		Int16 x2 = vertices[3].x;
		Int16 y0 = vertices[2].y;
		Int16 y1 = vertices[1].y;
		Int16 y2 = vertices[3].y;

		if (x0 < drawingAreaLeft) x0 = drawingAreaLeft; if (x0 >= drawingAreaRight) x0 = drawingAreaRight - 1;
		if (x1 < drawingAreaLeft) x1 = drawingAreaLeft; if (x1 >= drawingAreaRight) x1 = drawingAreaRight - 1;
		if (x2 < drawingAreaLeft) x2 = drawingAreaLeft; if (x2 >= drawingAreaRight) x2 = drawingAreaRight - 1;

		if (y0 < drawingAreaTop) y0 = drawingAreaTop; if (y0 >= drawingAreaBottom) y0 = drawingAreaBottom - 1;
		if (y1 < drawingAreaTop) y1 = drawingAreaTop; if (y1 >= drawingAreaBottom) y1 = drawingAreaBottom - 1;
		if (y2 < drawingAreaTop) y2 = drawingAreaTop; if (y2 >= drawingAreaBottom) y2 = drawingAreaBottom - 1;

#ifdef REPRODUCIBLEGPUTIMING
		int maxX = std::max(vertices[2].x, std::max(vertices[1].x, vertices[3].x));
		int minX = std::min(vertices[2].x, std::min(vertices[1].x, vertices[3].x));
		int maxY = std::max(vertices[2].y, std::max(vertices[1].y, vertices[3].y));
		int minY = std::min(vertices[2].y, std::min(vertices[1].y, vertices[3].y));
		byte mul = 2;
		if (textured || transparency) mul = 4;
		cmdTicks += (maxY + 1 - minY) * (maxX - minX) * mul;
#else
		Int32 polyticks = std::abs(x0 * y1 + x1 * y2 + x2 * y0 - x0 * y2 - x1 * y0 - x2 * y1) / 2;
		if (textured) polyticks *= 2;
		if (transparency || ((PSXRegs.GPUSTAT >> 12) & 1)) polyticks += (polyticks + 1) / 2;
		if ((PSXRegs.GPUSTAT & 0x00480400) == 0x00480000) polyticks /= 2;
		cmdTicks += polyticks;
#endif

		bool useBilinear = false;
#ifdef BILINEAR
		int maxX = std::max(vertices[0].x, std::max(vertices[1].x, vertices[2].x));
		int minX = std::min(vertices[0].x, std::min(vertices[1].x, vertices[2].x));
		int maxY = std::max(vertices[0].y, std::max(vertices[1].y, vertices[2].y));
		int minY = std::min(vertices[0].y, std::min(vertices[1].y, vertices[2].y));
		if ((maxX - minX) >= bilinearfilter && (maxY - minY) >= bilinearfilter)
		{
			useBilinear = dithering;
		}
#endif;
		POLYGON.drawPolygon(vertices[2], vertices[1], vertices[3], shaded, textured, transparency, rawTexture, dithering, useBilinear);
	}
}

void Gpu::addColorDx(ColorDeltas cD, ColorGroup& cG, UInt32 mul, bool shaded, bool textured)
{
	if (shaded)
	{
		cG.r += cD.drDx * mul;
		cG.g += cD.dgDx * mul;
		cG.b += cD.dbDx * mul;
	}

	if (textured)
	{
		cG.u += cD.duDx * mul;
		cG.v += cD.dvDx * mul;
	}
}

void Gpu::addColorDy(ColorDeltas cD, ColorGroup& cG, UInt32 mul, bool shaded, bool textured)
{
	if (shaded)
	{
		cG.r += cD.drDy * mul;
		cG.g += cD.dgDy * mul;
		cG.b += cD.dbDy * mul;
	}

	if (textured)
	{
		cG.u += cD.duDy * mul;
		cG.v += cD.dvDy * mul;
	}
}

void Gpu::shadePixel(UInt32 x, UInt32 y, byte cRed, byte cGreen, byte cBlue, byte u, byte v, bool textured, bool transparent, bool rawTexture, bool dithering, UInt32 uFull, UInt32 vFull, bool useBilinear)
{
	UInt16 color16 = 0x7C1F; // purple as default
	bool alphaCheck;
	bool alphaBit;

	if (x == 507 && y == 80)
	{
		//writePixelVRAM16(x, y, 0xFFFF);

		int a = 5;
		if (textured && u == 0x59)
		{
			a = 6;
		}
	}

	if (textured)
	{
		UInt16 textureColor;

		if (useBilinear)
		{
			// unreal method
			bool x_low = x & 1;
			bool y_low = y & 1;

			if (!x_low && !y_low) { uFull += 0x400; vFull += 0x000; }
			else if (x_low && !y_low) { uFull += 0x800; vFull += 0xC00; }
			else if (!x_low && y_low) { uFull += 0xC00; vFull += 0x800; }
			else if (x_low && y_low) { uFull += 0x000; vFull += 0x400; }

			//byte x3 = x & 3;
			//byte y3 = y & 3;
			//
			//if      (x3 == 0 && y3 == 0) { uFull += 0x200; vFull += 0x000; }
			//else if (x3 == 1 && y3 == 0) { uFull += 0x400; vFull += 0x400; }
			//else if (x3 == 2 && y3 == 0) { uFull += 0x600; vFull += 0x800; }
			//else if (x3 == 3 && y3 == 0) { uFull += 0x800; vFull += 0xC00; }
			//else if (x3 == 0 && y3 == 1) { uFull += 0xC00; vFull += 0x800; }
			//else if (x3 == 1 && y3 == 1) { uFull += 0x800; vFull += 0x600; }
			//else if (x3 == 2 && y3 == 1) { uFull += 0x400; vFull += 0x400; }
			//else if (x3 == 3 && y3 == 1) { uFull += 0x000; vFull += 0x200; }
			//else if (x3 == 0 && y3 == 2) { uFull += 0xC00; vFull += 0x800; }
			//else if (x3 == 1 && y3 == 2) { uFull += 0x800; vFull += 0x600; }
			//else if (x3 == 2 && y3 == 2) { uFull += 0x400; vFull += 0x400; }
			//else if (x3 == 3 && y3 == 2) { uFull += 0x000; vFull += 0x200; }
			//else if (x3 == 0 && y3 == 3) { uFull += 0x000; vFull += 0x200; }
			//else if (x3 == 1 && y3 == 3) { uFull += 0x400; vFull += 0x400; }
			//else if (x3 == 2 && y3 == 3) { uFull += 0x800; vFull += 0x600; }
			//else if (x3 == 3 && y3 == 3) { uFull += 0xC00; vFull += 0x800; }

			UInt32 unew = uFull >> 12;
			UInt32 vnew = vFull >> 12;

			if (unew < minU)
				unew = u;
			if (unew > maxU)
				unew = u;
			if (vnew < minV)
				vnew = v;
			if (vnew > maxV)
				vnew = v;

			//if ((u & 0xFFFFFFE0) != (unew & 0xFFFFFFE0))
			//	unew = u;
			//if ((v & 0xFFFFFFE0) != (vnew & 0xFFFFFFE0)) 
			//	vnew = v;

			//if ((u & 0xFFFFFFF8) != (unew & 0xFFFFFFF8)) unew = u;
			//if ((v & 0xFFFFFFF8) != (vnew & 0xFFFFFFF8)) vnew = v;



			u = unew;
			v = vnew;
		}

		u = (u & textureWindow_AND_X) | textureWindow_OR_X;
		v = (v & textureWindow_AND_Y) | textureWindow_OR_Y;

		switch ((GPU.drawMode >> 7) & 3)
		{
		case 0: // Palette4Bit
		{
			UInt16 texturepageX = (((GPU.drawMode & 0xF) * 64) + u / 4) & 0x3FF;
			UInt16 texturepageY = ((((GPU.drawMode >> 4) & 0x1) * 256) + v) & 0x1FF;
			UInt32 address = (texturepageY * 1024 + texturepageX) * 2;
			UInt16 paletteIndex = *(UInt16*)&GPU.VRAM[address];
			switch (u & 3)
			{
			case 0: paletteIndex = paletteIndex & 0xF; break;
			case 1: paletteIndex = (paletteIndex >> 4) & 0xF; break;
			case 2: paletteIndex = (paletteIndex >> 8) & 0xF; break;
			case 3: paletteIndex = (paletteIndex >> 12) & 0xF; break;
			}
			paletteIndex = (texturePaletteX + paletteIndex) & 0x3FF;
			address = (texturePaletteY * 1024 + paletteIndex) * 2;
			textureColor = *(UInt16*)&GPU.VRAM[address];
		}
			break;

		case 1: // Palette8Bit
		{
			UInt16 texturepageX = (((GPU.drawMode & 0xF) * 64) + u / 2) & 0x3FF;
			UInt16 texturepageY = ((((GPU.drawMode >> 4) & 0x1) * 256) + v) & 0x1FF;
			UInt32 address = (texturepageY * 1024 + texturepageX) * 2;
			UInt16 paletteIndex = *(UInt16*)&GPU.VRAM[address];
			if (u & 1) paletteIndex = paletteIndex >> 8;
			else paletteIndex &= 0xFF;

			if (texturePaletteX + paletteIndex >= 1024)
			{
				int a = 5;
			}

			paletteIndex = (texturePaletteX + paletteIndex) & 0x3FF;
			address = (texturePaletteY * 1024 + paletteIndex) * 2;
			textureColor = *(UInt16*)&GPU.VRAM[address];
		}
			break;


		default: // 15bit
		{
			UInt16 texturepageX = (((GPU.drawMode & 0xF) * 64) + u) & 0x3FF;
			UInt16 texturepageY = ((((GPU.drawMode >> 4) & 0x1) * 256) + v) & 0x1FF;
			UInt32 address = (texturepageY * 1024 + texturepageX) * 2;
			textureColor = *(UInt16*)&GPU.VRAM[address];
		}
			break;

		}

		//if (useBilinear)
		//{
		//	textureColor = 0xFFFF;
		//}

		if (textureColor == 0) return;

		alphaCheck = textureColor >> 15;
		alphaBit = alphaCheck;

		if (rawTexture)
		{
			cRed = textureColor & 0x1F;
			cGreen = (textureColor >> 5) & 0x1F;
			cBlue = (textureColor >> 10) & 0x1F;
		}
		else
		{
			byte tRed = textureColor & 0x1F;
			byte tGreen = (textureColor >> 5) & 0x1F;
			byte tBlue = (textureColor >> 10) & 0x1F;

			UInt16 bRed = (tRed * cRed) >> 4;
			UInt16 bGreen = (tGreen * cGreen) >> 4;
			UInt16 bBlue = (tBlue * cBlue) >> 4;

			if (dithering)
			{
				byte ditherX = x & 3;
				byte ditherY = y & 3;
				SByte ditherAdd = DITHER_MATRIX[ditherY][ditherX];
				if (bRed + ditherAdd < 0) bRed = 0; else if (bRed + ditherAdd > 255) bRed = 255; else bRed = bRed + ditherAdd;
				if (bGreen + ditherAdd < 0) bGreen = 0; else if (bGreen + ditherAdd > 255) bGreen = 255; else bGreen = bGreen + ditherAdd;
				if (bBlue + ditherAdd < 0) bBlue = 0; else if (bBlue + ditherAdd > 255) bBlue = 255; else bBlue = bBlue + ditherAdd;
			}

			cRed = bRed >> 3;
			cGreen = bGreen >> 3;
			cBlue = bBlue >> 3;

			if (cRed   > 0x1F) cRed = 0x1F;
			if (cGreen > 0x1F) cGreen = 0x1F;
			if (cBlue  > 0x1F) cBlue = 0x1F;
		}
	}
	else
	{
		if (dithering)
		{
			byte ditherX = x & 3;
			byte ditherY = y & 3;
			SByte ditherAdd = DITHER_MATRIX[ditherY][ditherX];
			if (cRed + ditherAdd < 0) cRed = 0; else if (cRed + ditherAdd > 255) cRed = 255; else cRed = cRed + ditherAdd;
			if (cGreen + ditherAdd < 0) cGreen = 0; else if (cGreen + ditherAdd > 255) cGreen = 255; else cGreen = cGreen + ditherAdd;
			if (cBlue + ditherAdd < 0) cBlue = 0; else if (cBlue + ditherAdd > 255) cBlue = 255; else cBlue = cBlue + ditherAdd;
		}

		cRed = cRed >> 3;
		cGreen = cGreen >> 3;
		cBlue = cBlue >> 3;
		alphaCheck = true;
		alphaBit = 0;
	}

	// todo: over/underflow check

	UInt16 colorBG;
	if ((transparent && alphaCheck) || checkMask)
	{
		UInt32 address = (y * 1024 + x) * 2;
		colorBG = *(UInt16*)&GPU.VRAM[address];
	}

	if (transparent && alphaCheck)
	{
		byte colorBGr = (colorBG & 0x1F);
		byte colorBGg = ((colorBG >> 5) & 0x1F);
		byte colorBGb = ((colorBG >> 10) & 0x1F);
		switch ((GPU.drawMode >> 5) & 3)
		{
		case 0: // B/2+F/2
			cRed = cRed / 2 + colorBGr / 2;
			cGreen = cGreen / 2 + colorBGg / 2;
			cBlue = cBlue / 2 + colorBGb / 2;
			break;

		case 1: // B+F
			cRed = cRed + colorBGr;
			cGreen = cGreen + colorBGg;
			cBlue = cBlue + colorBGb;
			if (cRed > 0x1F) cRed = 0x1F;
			if (cGreen > 0x1F) cGreen = 0x1F;
			if (cBlue > 0x1F) cBlue = 0x1F;
			break;

		case 2: // B-F
			cRed = colorBGr - cRed;
			cGreen = colorBGg - cGreen;
			cBlue = colorBGb - cBlue;
			if (cRed > 0x1F) cRed = 0;
			if (cGreen > 0x1F) cGreen = 0;
			if (cBlue > 0x1F) cBlue = 0;
			break;

		case 3: // B+F/4
			cRed = cRed / 4 + colorBGr;
			cGreen = cGreen / 4 + colorBGg;
			cBlue = cBlue / 4 + colorBGb;
			if (cRed > 0x1F) cRed = 0x1F;
			if (cGreen > 0x1F) cGreen = 0x1F;
			if (cBlue > 0x1F) cBlue = 0x1F;
			break;
		}
	}
	if ((PSXRegs.GPUSTAT >> 11) & 1) alphaBit = 1;

	color16 = cRed | (cGreen << 5) | (cBlue << 10) | (alphaBit << 15);

	if (checkMask)
	{
		if (colorBG & 0x8000) return;
	}

	writePixelVRAM16(x, y, color16);
}

void Gpu::writePixelExport(Int32 x, Int32 y, UInt16 color16)
{
	if (x == 0 && y == 202 && color16 == 0)
	{
		int a = 5;
	}

	if (pixeldrawn == 47461)
	{
		int a = 5;
	}

#if defined(VRAMPIXELOUT)
	if (debug_VramOutCount < VramOutCountMAX)
	{
		debug_VramOutTime[debug_VramOutCount] = CPU.commands;
		debug_VramOutAddr[debug_VramOutCount] = (y * 1024 + x) * 2;
		debug_VramOutData[debug_VramOutCount] = color16;
		debug_VramOutType[debug_VramOutCount] = 1;
		debug_VramOutCount++;
		pixeldrawn++;
	}
	else
	{
		int a = 5;
	}
#endif
}

void Gpu::writePixelVRAM16(Int32 x, Int32 y, UInt16 color16)
{
	if (x == 95 && y == 68)	
	{
		int a = 5;
	}

	UInt32 address = (y * 1024 + x) * 2;
	GPU.VRAM[address + 0] = color16;
	GPU.VRAM[address + 1] = color16 >> 8;

#ifdef VRAMFILEOUT
	writePixelExport(x, y, color16);
#endif // VRAMFILEOUT
}

void Gpu::writePixelVRAM24(Int32 x, Int32 y, UInt32 color)
{
	UInt16 color16 = ((color >> 3) & 0x1F) | (((color >> 11) & 0x1F) << 5) | (((color >> 19) & 0x1F) << 10);
	UInt32 address = (y * 1024 + x) * 2;
	GPU.VRAM[address + 0] = color16;
	GPU.VRAM[address + 1] = color16 >> 8;
}

void Gpu::writeColor24VRAM16(UInt32 color, UInt32 address)
{
	UInt16 color16 = ((color >> 3) & 0x1F) | (((color >> 11) & 0x1F) << 5) | (((color >> 19) & 0x1F) << 10);
	GPU.VRAM[address + 0] = color16;
	GPU.VRAM[address + 1] = color16 >> 8;
}

void Gpu::finishFrame()
{
	GPU.frameskip_counter++;
	if (GPU.frameskip_counter > GPU.frameskip)
	{
		GPU.frameskip_counter = 0;
	}
	Uint64 currentTime;
	double micros = 0;
	if (lockSpeed)
	{
		while (micros < frametimeleft)
		{
			currentTime = SDL_GetPerformanceCounter();
			micros = (double)((currentTime - lastTime_frame) * 1000000 / (double)SDL_GetPerformanceFrequency());
			if (frametimeleft - micros > 1000)
			{
				//Thread.Sleep(1);
			}
		}
		lastTime_frame = SDL_GetPerformanceCounter();
		//frametimeleft = max(1000, frametimeleft - micros + (FRAMETIME / speedmult));
	}
	if (SDL_LockMutex(drawlock) == 0)
	{
		intern_frames++;
		SDL_UnlockMutex(drawlock);
	}

	if (GPU.frameskip_counter == 0)
	{
		UInt32 src_x = GPU_Timing.vramDisplayLeft;
		UInt32 src_y = GPU_Timing.vramDisplayTop;
		UInt32 height = GPU_Timing.vramDisplayHeight;
		UInt32 width = GPU_Timing.vramDisplayWidth;

		bool vramdisplay = false;
		if (vramdisplay)
		{
			for (int y = 0; y < 512; y++)
			{
				for (int x = 0; x < 1024; x++)
				{
					UInt32 addr = (y * 1024 + x) * 2;
					UInt16 color = *(UInt16*)&VRAM[addr];
					UInt32 color32 = ((color & 0x1F) << 19) | (((color >> 5) & 0x1F) << 11) | ((color >> 10) << 3);
					bufferWork[y * 1024 + x] = color32;
				}
			}
		}
		else if ((PSXRegs.GPUSTAT >> 21) & 1) // 24 bit color in VRAM
		{
			for (int y = 0; y < height; y++)
			{
				UInt32 addr = (y + src_y) * 2048 + src_x * 2;
				for (int x = 0; x < width; x++)
				{
					UInt32 color = *(UInt32*)&VRAM[addr];
					UInt32 color32 = ((color & 0xFF) << 16) | (((color >> 8) & 0xFF) << 8) | ((color >> 16) & 0xFF);
					bufferWork[y * 1024 + x] = color32;
					addr += 3;
				}
			}
		}
		else // 16 bit color in VRAM
		{
			for (int y = 0; y < height; y++)
			{
				for (int x = 0; x < width; x++)
				{
					UInt32 addr = (((y + src_y) % 512) * 1024 + x + src_x) * 2;
					UInt16 color = *(UInt16*)&VRAM[addr];
					UInt32 color32 = ((color & 0x1F) << 19) | (((color >> 5) & 0x1F) << 11) | ((color >> 10) << 3);
					bufferWork[y * 1024 + x] = color32;
				}
			}
		}
	}
}

void Gpu::draw_game()
{
	UInt32 height = GPU_Timing.vramDisplayHeight;
	UInt32 width = GPU_Timing.vramDisplayWidth;

	if (height > 0x200) height = 0x200;

	bool vramdisplay = false;
	if (vramdisplay)
	{
		for (int y = 0; y < 512; y++)
		{
			for (int x = 0; x < 1024; x++)
			{
				buffer[y * 1024 + x] = bufferWork[y * 1024 + x];
			}
		}
	}
	else
	{
		for (int y = 0; y < height; y++)
		{
			for (int x = 0; x < width; x++)
			{
				buffer[y * 1024 + x] = bufferWork[y * 1024 + x];
			}
		}
	}
	
	if (autotest)
	{
		hash = 0;
		for (int y = 0; y < height; y++)
		{
			for (int x = 0; x < width; x++)
			{
				hash += buffer[y * 1024 + x];
			}
		}
	}
}

void Gpu::saveState(UInt32 offset)
{
	psx.savestate_addvalue(offset + 0, 31,  0, PSXRegs.GPUREAD);
	psx.savestate_addvalue(offset + 1, 31,  0, PSXRegs.GPUSTAT);
	psx.savestate_addvalue(offset + 2, 31,  0, textureWindow);
	psx.savestate_addvalue(offset + 3, 15,  0, drawMode);
	psx.savestate_addvalue(offset + 3, 16, 16, checkMask);
	psx.savestate_addvalue(offset + 3, 17, 17, textureDisable);
	psx.savestate_addvalue(offset + 4, 15,  0, drawingAreaLeft);
	psx.savestate_addvalue(offset + 4, 31, 16, drawingAreaTop);
	psx.savestate_addvalue(offset + 5, 15, 0, drawingAreaRight);
	psx.savestate_addvalue(offset + 5, 31, 16, drawingAreaBottom);
	psx.savestate_addvalue(offset + 6, 15, 0, drawingOffsetX);
	psx.savestate_addvalue(offset + 6, 31, 16, drawingOffsetY);
}

void Gpu::loadState(UInt32 offset)
{
	PSXRegs.GPUREAD   = psx.savestate_loadvalue(offset + 0, 31, 0);   
	PSXRegs.GPUSTAT	  = psx.savestate_loadvalue(offset + 1, 31, 0);   
	textureWindow	  = psx.savestate_loadvalue(offset + 2, 31, 0);   
	drawMode		  = psx.savestate_loadvalue(offset + 3, 15, 0);   
	checkMask		  = psx.savestate_loadvalue(offset + 3, 16, 16);  
	textureDisable	  = psx.savestate_loadvalue(offset + 3, 17, 17);  
	drawingAreaLeft	  = psx.savestate_loadvalue(offset + 4, 15, 0);   
	drawingAreaTop	  = psx.savestate_loadvalue(offset + 4, 31, 16);  
	drawingAreaRight  = psx.savestate_loadvalue(offset + 5, 15, 0);   
	drawingAreaBottom = psx.savestate_loadvalue(offset + 5, 31, 16);  
	drawingOffsetX	  = psx.savestate_loadvalue(offset + 6, 15, 0);   
	drawingOffsetY	  = psx.savestate_loadvalue(offset + 6, 31, 16);  

	byte maskX = textureWindow & 0x1F;
	byte maskY = (textureWindow >> 5) & 0x1F;
	byte offsetX = (textureWindow >> 10) & 0x1F;
	byte offsetY = (textureWindow >> 15) & 0x1F;
	textureWindow_AND_X = ~(maskX * 8);
	textureWindow_AND_Y = ~(maskY * 8);
	textureWindow_OR_X = (offsetX & maskX) * 8u;
	textureWindow_OR_Y = (offsetY & maskY) * 8u;
}

bool Gpu::isSSIdle()
{
	if (pendingPolyline) return false;
	if (pendingCPUtoVRAM) return false;
	if (pendingVRAMtoCPU) return false;
	if (!fifoIdle) return false;
	if (cmdTicks > 0) return false;
	if (!fifo.empty()) return false;

	return true;
}


void Gpu::VramOutWriteFile(bool writeTest)
{
#ifdef VRAMFILEOUT
	FILE* file = fopen("R:\\debug_gpu.txt", "w");

	for (int i = 0; i < debug_VramOutCount; i++)
	{
		//if (debug_VramOutType[i] == 2)
		{
			if (debug_VramOutType[i] == 1) fputs("Pixel: ", file);
			if (debug_VramOutType[i] == 2) fputs("Fifo: ", file);
			if (debug_VramOutType[i] == 3) fputs("LinkedList: ", file);
			if (debug_VramOutType[i] == 4) fputs("VRAM2CPU: ", file);
			char buf[10];
			_itoa(debug_VramOutTime[i], buf, 16);
			for (int c = strlen(buf); c < 8; c++) fputc('0', file);
			fputs(buf, file);
			fputc(' ', file);
			_itoa(debug_VramOutAddr[i], buf, 16);
			for (int c = strlen(buf); c < 8; c++) fputc('0', file);
			fputs(buf, file);
			fputc(' ', file);
			_itoa(debug_VramOutData[i], buf, 16);
			for (int c = strlen(buf); c < 4; c++) fputc('0', file);
			fputs(buf, file);

			fputc('\n', file);
		}
	}
	fclose(file);

	if (writeTest)
	{
		file = fopen("R:\\gpu_test_FPSXA.txt", "w");

		for (int i = 0; i < debug_VramOutCount; i++)
		{
			if (debug_VramOutType[i] == 2)
			{
				char buf[10];
				_itoa(debug_VramOutType[i], buf, 16);
				for (int c = strlen(buf); c < 2; c++) fputc('0', file);
				fputs(buf, file);
				fputc(' ', file);
				_itoa(debug_VramOutTime[i], buf, 16);
				for (int c = strlen(buf); c < 8; c++) fputc('0', file);
				fputs(buf, file);
				fputc(' ', file);
				_itoa(debug_VramOutAddr[i], buf, 16);
				for (int c = strlen(buf); c < 8; c++) fputc('0', file);
				fputs(buf, file);

				fputc('\n', file);
			}
		}
		fclose(file);
	}

	file = fopen("R:\\debug_pixel.txt", "w");

	for (int i = 0; i < debug_VramOutCount; i++)
	{
		if (debug_VramOutType[i] == 1)
		{
			char buf[10];
			UInt16 x = (debug_VramOutAddr[i] >> 1) & 0x3FF;
			UInt16 y = (debug_VramOutAddr[i] >> 11) & 0x1FF;
			_itoa(x, buf, 10);
			fputs(buf, file);
			fputc(' ', file);
			_itoa(y, buf, 10);
			fputs(buf, file);
			fputc(' ', file);
			_itoa(debug_VramOutData[i], buf, 10);
			fputs(buf, file);
			fputc('\n', file);
		}
	}
	fclose(file);

	file = fopen("R:\\debug_vram2cpu.txt", "w");

	int count = 0;
	for (int i = 0; i < debug_VramOutCount; i++)
	{
		if (debug_VramOutType[i] == 4)
		{
			char buf[10];
			_itoa(count, buf, 16);
			for (int c = strlen(buf); c < 6; c++) fputc('0', file);
			fputs(buf, file);
			fputc(' ', file);
			_itoa(debug_VramOutAddr[i], buf, 16);
			for (int c = strlen(buf); c < 8; c++) fputc('0', file);
			fputs(buf, file);
			fputc('\n', file);
			count++;
		}
	}
	fclose(file);
#endif
}

void Gpu::GPUTEST()
{
	std::ifstream infile("R:\\gpu_test_FPSXA.txt");
	std::string line;
	UInt32 timer = 0;
	int cmdCount = 0;
	while (std::getline(infile, line))
	{
		std::string type = line.substr(0, 2);
		std::string time = line.substr(3, 8);
		std::string data = line.substr(12, 8);
		byte typeI = std::stoul(type, nullptr, 16);
		UInt32 timeI = std::stoul(time, nullptr, 16);
		UInt32 dataI = std::stoul(data, nullptr, 16);
		while (timer < timeI)
		{
			work();
			timer++;
			CPU.totalticks++;
		}
		if (cmdCount == 150900)
		{
			int a = 5;
		}
		switch (typeI)
		{
			case 2:
			{
				GP0Write(dataI);
			}
		}
		cmdCount++;
	}

	VramOutWriteFile(false);
}