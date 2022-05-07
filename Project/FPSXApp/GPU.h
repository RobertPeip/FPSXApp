#pragma once
#include <queue>
#include "SDL.h"
#include "types.h"

struct Vertex
{
	Int16 x;
	Int16 y;
	UInt32 color;
	byte r;
	byte g;
	byte b;
	byte u;
	byte v;
};

struct ColorDeltas
{
	UInt32 duDx;
	UInt32 dvDx;
	UInt32 drDx;
	UInt32 dgDx;
	UInt32 dbDx;

	UInt32 duDy;
	UInt32 dvDy;
	UInt32 drDy;
	UInt32 dgDy;
	UInt32 dbDy;
};

struct ColorGroup
{
	UInt32 r;
	UInt32 g;
	UInt32 b;
	UInt32 u;
	UInt32 v;
};

class Gpu
{
public:

	// fifo
	std::deque<UInt32> fifo;
	bool fifoIdle;
	Int32 cmdTicks;
	UInt32 fifoTotalwords;

	// exchange
	Vertex vertices[4];

	// state
	bool pendingPolyline;
	bool pendingCPUtoVRAM;
	bool pendingVRAMtoCPU;

	// memory
	Byte VRAM[1048576];
	unsigned int bufferWork[1024 * 512];
	unsigned int buffer[1024 * 512];

	// registers
	UInt16 drawMode;
	UInt32 textureWindow;

	byte textureWindow_AND_X;
	byte textureWindow_AND_Y;
	byte textureWindow_OR_X;
	byte textureWindow_OR_Y;

	// data copy
	UInt16 copyDstX;
	UInt16 copyDstY;
	UInt16 copySizeX;
	UInt16 copySizeY;
	UInt32 copyWordcount;
	UInt32 copyCntX;
	UInt32 copyCntY;
	byte endCopyWait;
	std::queue<UInt32> copyfifo;

	// polyline
	std::queue<UInt32> polyfifo;

	// textures
	UInt16 texturePaletteX;
	UInt16 texturePaletteY;
	bool checkMask;
	bool textureDisable;

	// drawing area
	UInt16 drawingAreaLeft;
	UInt16 drawingAreaTop;
	UInt16 drawingAreaRight;
	UInt16 drawingAreaBottom;
	Int16 drawingOffsetX;
	Int16 drawingOffsetY;

	byte minU;
	byte maxU;
	byte minV;
	byte maxV;

	SByte DITHER_MATRIX[4][4] = {
		{-4, +0, -3, +1},
		{+2, -2, +3, -1},
		{-3, +1, -4, +0},
		{+3, -1, +2, -2}
	};

	// hacks
	bool skipnext;

	// debug
	bool autotest;
	UInt64 hash;
	UInt32 polygonCount;
	UInt32 polyLineShadedCount;
	Int32 cmdTicksLast;
	UInt16 objCount;

#if DEBUG
	#define VRAMFILEOUT
    #define VRAMPIXELOUT
#endif
#ifdef VRAMFILEOUT
	#define VramOutCountMAX 3000000
	UInt32 debug_VramOutTime[VramOutCountMAX];
	UInt32 debug_VramOutAddr[VramOutCountMAX];
	UInt16 debug_VramOutData[VramOutCountMAX];
	Byte debug_VramOutType[VramOutCountMAX];
	UInt32 debug_VramOutCount;
#endif
	void VramOutWriteFile(bool writeTest);
	void GPUTEST();

	// frametime
	bool lockSpeed = true;
	int speedmult = 1;
	int frameskip = 0;
	int frameskip_counter = 0;
	UInt64 intern_frames;
	SDL_mutex* drawlock;

	UInt32 pixeldrawn;

	const long FRAMETIME = (1000000 / 75);
	long frametimeleft;
	Uint64 lastTime_frame;

	void reset();

	void GP0Write(Uint32 value);
	void GP1Write(Uint32 value);

	void UpdateDMARequest();

	void work();
	void fillVRAM();
	void CopyVRAMVRAM();
	void CopyToVRAM();
	UInt32 ReadVRAMtoCPU();
	void drawRectangle();
	void drawRectangleBase(Vertex v, UInt16 width, UInt16 height, bool textured, bool rawTexture, bool transparent);
	void drawLine();
	void drawPolyLine();
	void drawLineBase(Vertex v0, Vertex v1, bool shaded, bool transparent);
	void drawPolygon();
	void addColorDx(ColorDeltas cD, ColorGroup& cG, UInt32 mul, bool shaded, bool textured);
	void addColorDy(ColorDeltas cD, ColorGroup& cG, UInt32 mul, bool shaded, bool textured);
	void shadePixel(UInt32 x, UInt32 y, byte cRed, byte cGreen, byte cBlue, byte u, byte v, bool textured, bool transparent, bool rawTexture, bool dithering, UInt32 uFull, UInt32 vFull, bool useBilinear);
	void writePixelExport(Int32 x, Int32 y, UInt16 color);
	void writePixelVRAM16(Int32 x, Int32 y, UInt16 color);
	void writePixelVRAM24(Int32 x, Int32 y, UInt32 color);
	void writeColor24VRAM16(UInt32 color, UInt32 address);

	void finishFrame();
	void draw_game();

	void saveState(UInt32 offset);
	void loadState(UInt32 offset);
	bool isSSIdle();
};
extern Gpu GPU;