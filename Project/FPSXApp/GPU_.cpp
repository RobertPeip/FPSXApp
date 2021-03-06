#include <algorithm>
using namespace std;
#undef NDEBUG
#include <assert.h>

#include "GPU.h"
#include "GPU_Timing.h"
#include "Polygon.h"
#include "psx.h"
#include "PSXRegs.h"
#include "Memory.h"
#include "CPU.h"
#include "DMA.h"

Gpu GPU;

void Gpu::reset()
{
#if DEBUG
	lockSpeed = false;
#endif

	for (int i = 0; i < 1048576; i++) VRAM[i] = 0x00;
	for (int i = 0; i < 640 * 512; i++) bufferWork[i] = 0;
	for (int i = 0; i < 640 * 512; i++) buffer[i] = 0;

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

	// debug
#ifdef VRAMFILEOUT
	polygonCount = 0;
	polyLineShadedCount = 0;
	debug_VramOutCount = 0;
#endif
}

void Gpu::GP0Write(Uint32 value)
{
	fifo.push_back(value);
	fifoIdle = false;
#if defined(VRAMFILEOUT)
	debug_VramOutTime[debug_VramOutCount] = CPU.commands;
	debug_VramOutAddr[debug_VramOutCount] = value;
	debug_VramOutType[debug_VramOutCount] = 2;
	debug_VramOutCount++;

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
		GPU_Timing.recalc();
		break;

	case 0x06: // horizontal diplay range
		GPU_Timing.hDisplayRange = value & 0xFFFFFF;
		GPU_Timing.recalc();
		break;

	case 0x07: // vertical diplay range
		GPU_Timing.vDisplayRange = value & 0xFFFFF;
		GPU_Timing.recalc();
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
			GPU_Timing.recalc();
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
			PSXRegs.GPUREAD = (drawingOffsetX & 0x3FF) | ((drawingOffsetY & 0x3FF) << 10);
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

	PSXRegs.GPUSTAT &= 0xFDFFFFFF;
	PSXRegs.GPUSTAT |= request << 25;
	DMA.requests[2] = request;
}

void Gpu::work()
{
	if (cmdTicks == 1) cmdTicks = 0;
	if (cmdTicks > 1)
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

	bool again = true;
	while (again)
	{
		if (cmdTicks == 0 && !pendingCPUtoVRAM && !pendingVRAMtoCPU)
		{
			if (fifo.empty()) PSXRegs.GPUSTAT |= 1 << 26;
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
					PSXRegs.GPUSTAT |= 1 << 27; // ready to send vram
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
					textureWindow = param;
					fifo.pop_front();
					fifoIdle = fifo.empty();
					//PSXRegs.GPUSTAT &= ~(1 << 26);
					//cmdTicks += 1;
					again = true;
					break;

				case 0xE3: // Set Drawing Area top left (X1,Y1)
					drawingAreaLeft = param & 0x3FF;
					drawingAreaTop = (param >> 10) & 0x1FF;
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
	}

	// set ready to receive DMA
	if (!pendingPolyline && !pendingCPUtoVRAM && !pendingVRAMtoCPU)
	{
		PSXRegs.GPUSTAT &= ~(1 << 28);
		if (fifo.empty() || fifo.size() < fifoTotalwords) PSXRegs.GPUSTAT |= 1 << 28;
	}

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
	cmdTicks = 46 + ((width / 8) + 9) * height;
	UInt16 color16 = ((color >> 3) & 0x1F) | (((color >> 11) & 0x1F) << 5) | (((color >> 19) & 0x1F) << 10);

	// todo: interlaced
	for (int y = 0; y < height; y++)
	{
		UInt32 row = (y1 + y) & 0x1FF;
		for (int x = 0; x < width; x++)
		{
			Uint32 col = (x1 + x) & 0x3FF;
			UInt32 addr = (row * 1024 + col) * 2;
			VRAM[addr + 0] = color16;
			VRAM[addr + 1] = color16 >> 8;
			pixeldrawn++;
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

	if (width == 0 || height == 0 || (srcX == dstX && srcY == dstY && !((PSXRegs.GPUSTAT >> 11) & 1)))
	{
		// don't draw
	}
	else
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
				VRAM[addrDst + 0] = dataword;
				VRAM[addrDst + 1] = dataword >> 8;
				pixeldrawn++;

#ifdef VRAMFILEOUT
				if (debug_VramOutCount == 135388)
				{
					int a = 5;
				}
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
			}
			else
			{
				VRAM[addr + 0] = dataword >> 16;
				VRAM[addr + 1] = dataword >> 24;
				if (copyfifo.empty()) break;
			}
			pixeldrawn++;
			firstWord = !firstWord;
		}
		if (copyfifo.empty()) break;
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
			}
		}
	}
	PSXRegs.GPUREAD = value;
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
	if (posX & 0x400) posX = -1024 + (posX & 0x3FF);
	if (posY & 0x400) posY = -1024 + (posY & 0x3FF);
	posX += drawingOffsetX;
	posY += drawingOffsetY;

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

	cmdTicks = 16;

	if (drawingAreaLeft > drawingAreaRight || drawingAreaTop > drawingAreaBottom) return;

	Int16 clipLeft = posX;
	Int16 clipRight = posX + width;
	Int16 clipTop = posY;
	Int16 clipBottom = posY + height;

	if (clipLeft   < drawingAreaLeft) clipLeft   = drawingAreaLeft; if (clipLeft   > drawingAreaRight)  clipLeft   = drawingAreaRight;
	if (clipRight  < drawingAreaLeft) clipRight  = drawingAreaLeft; if (clipRight  > drawingAreaRight)  clipRight  = drawingAreaRight;
	if (clipTop    < drawingAreaTop)  clipTop    = drawingAreaTop;  if (clipTop    > drawingAreaBottom) clipTop    = drawingAreaBottom;
	if (clipBottom < drawingAreaLeft) clipBottom = drawingAreaLeft; if (clipBottom > drawingAreaBottom) clipBottom = drawingAreaBottom;

	clipRight++;
	clipBottom++;

	UInt16 sizeX = clipRight - clipLeft;
	UInt16 sizeY = clipBottom - clipTop;

	if (textured)
	{
		if (transparency || ((PSXRegs.GPUSTAT >> 12) & 1))
		{
			cmdTicks += (sizeX * 2 + (sizeX + 1) / 2) * sizeY;
		}
		else
		{
			cmdTicks += sizeX * 2 * sizeY;
		}
	}
	else
	{
		if (transparency || ((PSXRegs.GPUSTAT >> 12) & 1))
		{
			cmdTicks += (sizeX + (sizeX + 1) / 2) * sizeY;
		}
		else
		{
			cmdTicks += sizeX * sizeY;
		}
	}

	vertices[0].color = firstColor;
	vertices[0].r = firstColor;
	vertices[0].g = (firstColor >> 8) & 0xFF;
	vertices[0].b = (firstColor >> 16);
	vertices[0].x = posX;
	vertices[0].y = posY;

	drawRectangleBase(vertices[0], width, height, textured, rawTexture, transparency);

	fifoIdle = fifo.empty();
	if (!fifoIdle) psx.log("Fifo not empty after Rectangle CMD");
	fifoTotalwords = 0;
}

void Gpu::drawRectangleBase(Vertex v, UInt16 width, UInt16 height, bool textured, bool rawTexture, bool transparent)
{
	for (int y = 0; y < height; y++)
	{
		// todo add interlacing check

		UInt16 posY = y + v.y;
		if (posY >= drawingAreaTop && posY <= drawingAreaBottom)
		{
			for (int x = 0; x < width; x++)
			{
				UInt16 posX = x + v.x;
				if (posX >= drawingAreaLeft && posX <= drawingAreaRight)
				{
					shadePixel(posX, posY, v.r, v.g, v.b, v.u + x, v.v + y, textured, transparent, rawTexture, false);
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
				drawLineBase(vertices[0], vertices[1], shading, transparency, false);
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
			drawLineBase(vertices[0], vertices[1], shading, transparency, false);
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

	drawLineBase(vertices[0], vertices[1], shading, transparency, false);
	fifoIdle = fifo.empty();
	if (!fifoIdle) psx.log("Fifo not empty after Line CMD");
	fifoTotalwords = 0;
}

void Gpu::drawLineBase(Vertex v0, Vertex v1, bool shaded, bool transparent, bool dithering)
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

	// todo: culling

	// todo: correct clip/clamp
	UInt16 clipLeft = minX;
	UInt16 clipRight = maxX + 1;
	UInt16 clipTop = minY;
	UInt16 clipBottom = maxY + 1;

	UInt16 sizeX = clipRight - clipLeft;
	UInt16 sizey = clipBottom - clipTop;
	if (sizeX > sizey) cmdTicks += sizeX;
	else cmdTicks += sizey;

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

	for (int i = 0; i <= points; i++)
	{
		Int32 x = (workX >> 32) & 2047;
		Int32 y = (workY >> 32) & 2047;

		// todo: interlace check
		
		if (x >= drawingAreaLeft && x <= drawingAreaRight && y >= drawingAreaTop && y <= drawingAreaBottom)
		{
			if (shaded)
			{
				shadePixel(x, y, workR >> 12, workG >> 12, workB >> 12, 0, 0, false, transparent, false, dithering);
				workR += stepDr;
				workG += stepDg;
				workB += stepDb;
			}
			else
			{
				shadePixel(x, y, v0.r, v0.g, v0.b, 0, 0, false, transparent, false, dithering);
			}
		}

		workX += stepDx;
		workY += stepDy;
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

	polygonCount++;
	//if (polygonCount > 2) return;

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
		// position
		Int16 posX = fifo.front() & 0x7FF;
		Int16 posY = (fifo.front() >> 16) & 0x7FF;
		if (posX & 0x400) posX = -1024 + (posX & 0x3FF);
		if (posY & 0x400) posY = -1024 + (posY & 0x3FF);
		posX += drawingOffsetX;
		posY += drawingOffsetY;
		vertices[i].x = posX;
		vertices[i].y = posY;

		fifo.pop_front();
		// texture
		if (textured)
		{
			UInt32 data = fifo.front();
			vertices[i].u = data & 0xFF;
			vertices[i].v = (data >> 8) & 0xFF;
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

	if (drawingAreaLeft > drawingAreaRight || drawingAreaTop > drawingAreaBottom) return;
	
	// todo: large polygon culling

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

		Int32 polyticks = std::abs(x0 * y1 + x1 * y2 + x2 * y0 - x0 * y2 - x1 * y0 - x2 * y1) / 2;
		if (textured) polyticks *= 2;
		if (transparency || ((PSXRegs.GPUSTAT >> 12) & 1)) polyticks += (polyticks + 1) / 2;
		if ((PSXRegs.GPUSTAT & 0x00480400) == 0x00480000) polyticks /= 2;
		cmdTicks += polyticks;
		POLYGON.drawPolygon(vertices[0], vertices[1], vertices[2], shaded, textured, transparency, rawTexture, false);
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

		Int32 polyticks = std::abs(x0 * y1 + x1 * y2 + x2 * y0 - x0 * y2 - x1 * y0 - x2 * y1) / 2;
		if (textured) polyticks *= 2;
		if (transparency || ((PSXRegs.GPUSTAT >> 12) & 1)) polyticks += (polyticks + 1) / 2;
		if ((PSXRegs.GPUSTAT & 0x00480400) == 0x00480000) polyticks /= 2;
		cmdTicks += polyticks;
		POLYGON.drawPolygon(vertices[2], vertices[1], vertices[3], shaded, textured, transparency, rawTexture, false);
	}

	fifoIdle = fifo.empty();
	if (!fifoIdle) psx.log("Fifo not empty after Polygon CMD");
	fifoTotalwords = 0;
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

void Gpu::shadePixel(UInt32 x, UInt32 y, byte cRed, byte cGreen, byte cBlue, byte u, byte v, bool textured, bool transparent, bool rawTexture, bool dithering)
{
	UInt16 color16 = 0x7C1F; // purple as default
	bool alphaCheck;
	bool alphaBit;
	if (textured)
	{
		// todo texture window
		UInt16 textureColor;
		switch ((GPU.drawMode >> 7) & 3)
		{
		case 0: // Palette4Bit
		{
			UInt16 texturepageX = (((GPU.drawMode & 0x1F) * 64) + u / 4) & 0x3FF;
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
			UInt16 texturepageX = (((GPU.drawMode & 0x1F) * 64) + u / 2) & 0x3FF;
			UInt16 texturepageY = ((((GPU.drawMode >> 4) & 0x1) * 256) + v) & 0x1FF;
			UInt32 address = (texturepageY * 1024 + texturepageX) * 2;
			UInt16 paletteIndex = *(UInt16*)&GPU.VRAM[address];
			if (u & 1) paletteIndex = paletteIndex >> 8;
			else paletteIndex &= 0xFF;
			paletteIndex = (texturePaletteX + paletteIndex) & 0x3FF;
			address = (texturePaletteY * 1024 + paletteIndex) * 2;
			textureColor = *(UInt16*)&GPU.VRAM[address];
		}
			break;


		default: // 15bit
		{
			UInt16 texturepageX = (((GPU.drawMode & 0x1F) * 64) + u) & 0x3FF;
			UInt16 texturepageY = ((((GPU.drawMode >> 4) & 0x1) * 256) + v) & 0x1FF;
			UInt32 address = (texturepageY * 1024 + texturepageX) * 2;
			textureColor = *(UInt16*)&GPU.VRAM[address];
		}
			break;

		}

		if (textureColor == 0) return;

		// todo raw color/dithering

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
	color16 = cRed | (cGreen << 5) | (cBlue << 10) | (alphaBit << 15);

	if (checkMask)
	{
		if (colorBG & 0x8000) return;
	}

	writePixelVRAM16(x, y, color16);
}

void Gpu::writePixelExport(Int32 x, Int32 y, UInt16 color16)
{
#if defined(VRAMPIXELOUT)
	debug_VramOutTime[debug_VramOutCount] = CPU.commands;
	debug_VramOutAddr[debug_VramOutCount] = (y * 1024 + x) * 2;
	debug_VramOutData[debug_VramOutCount] = color16;
	debug_VramOutType[debug_VramOutCount] = 1;
	debug_VramOutCount++;
#endif
}

void Gpu::writePixelVRAM16(Int32 x, Int32 y, UInt16 color16)
{
	UInt32 address = (y * 1024 + x) * 2;
	GPU.VRAM[address + 0] = color16;
	GPU.VRAM[address + 1] = color16 >> 8;

	pixeldrawn++;

#ifdef VRAMFILEOUT
	if (debug_VramOutCount > 135388)
	{
		int a = 5;
	}
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

		if ((PSXRegs.GPUSTAT >> 21) & 1) // 24 bit color in VRAM
		{
			for (int y = 0; y < height; y++)
			{
				UInt32 addr = (y + src_y) * 2048 + src_x * 3;
				for (int x = 0; x < width; x++)
				{
					UInt32 color = *(UInt32*)&VRAM[addr];
					UInt32 color32 = ((color & 0xFF) << 16) | (((color >> 8) & 0xFF) << 8) | ((color >> 16) & 0xFF);
					bufferWork[y * 640 + x] = color32;
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
					UInt32 addr = ((y + src_y) * 1024 + x + src_x) * 2;
					UInt16 color = *(UInt16*)&VRAM[addr];
					UInt32 color32 = ((color & 0x1F) << 19) | (((color >> 5) & 0x1F) << 11) | ((color >> 10) << 3);
					bufferWork[y * 640 + x] = color32;
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

	for (int y = 0; y < height; y++)
	{
		for (int x = 0; x < width; x++)
		{
			buffer[y * 640 + x] = bufferWork[y * 640 + x];
		}
	}	
	
	if (autotest)
	{
		hash = 0;
		for (int y = 0; y < height; y++)
		{
			for (int x = 0; x < width; x++)
			{
				hash += buffer[y * 640 + x];
			}
		}
	}
}

void Gpu::VramOutWriteFile()
{
#ifdef VRAMFILEOUT
	FILE* file = fopen("R:\\debug_gpu.txt", "w");

	for (int i = 0; i < debug_VramOutCount; i++)
	{
		if (debug_VramOutType[i] == 1) fputs("Pixel: ", file);
		if (debug_VramOutType[i] == 2) fputs("Fifo: ", file);
		if (debug_VramOutType[i] == 3) fputs("LinkedList: ", file);
		char buf[10];
		_itoa(debug_VramOutTime[i], buf, 16);
		for (int c = strlen(buf); c < 8; c++) fputc('0', file);
		fputs(buf, file);
		fputc(' ', file);
		_itoa(debug_VramOutAddr[i], buf, 16);
		for (int c = strlen(buf); c < 5; c++) fputc('0', file);
		fputs(buf, file);
		fputc(' ', file);
		_itoa(debug_VramOutData[i], buf, 16);
		for (int c = strlen(buf); c < 4; c++) fputc('0', file);
		fputs(buf, file);

		fputc('\n', file);
	}
	fclose(file);
#endif
}