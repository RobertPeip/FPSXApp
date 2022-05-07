#pragma once
#include "types.h"
#include <queue>
#include <string>
using namespace std;

class CDROM
{
public:

#define RAW_SECTOR_SIZE 2352
#define DATA_SECTOR_SIZE 2048
#define PREGAPSIZE 0x96
#define SECTOR_SYNC_SIZE 12
#define SECTOR_HEADER_SIZE 4
#define FRAMES_PER_SECOND 75
#define SECONDS_PER_MINUTE 60
#define FRAMES_PER_MINUTE FRAMES_PER_SECOND * SECONDS_PER_MINUTE
#define SUBCHANNEL_BYTES_PER_FRAME 12
#define LEAD_OUT_SECTOR_COUNT 6750
#define NUM_SECTOR_BUFFERS 8
#define LEAD_OUT_TRACK_NUMBER 0xAA
#define RAW_SECTOR_OUTPUT_SIZE RAW_SECTOR_SIZE - SECTOR_SYNC_SIZE

	std::deque<byte> fifoParam;
	std::deque<byte> fifoData;
	std::deque<byte> fifoResponse;

	bool hasCD;
	UInt32 lbaCount;

	byte irqDelay = 0;

	byte internalStatus;

	byte modeReg;

	bool busy;
	Int32 delay;
	byte nextCmd;
	bool cmdPending;

	byte pendingDriveIRQ;
	byte pendingDriveResponse;

	byte workCommand;
	bool working;
	Int32 workDelay;

	bool setLocActive;
	byte setLocMinute;
	byte setLocSecond;
	byte setLocFrame;

	UInt32 seekStartLBA;
	UInt32 seekEndLBA;
	UInt32 currentLBA;
	SByte fastForwardRate;

	enum class DRIVESTATE
	{
		IDLE,
		SEEKPHYSICAL,
		SEEKLOGICAL,
		SEEKIMPLICIT,
		READING,
		PLAYING,
		SPEEDCHANGEORTOCREAD,
		SPINNINGUP,
		CHANGESESSION,
		OPENING
	};
	DRIVESTATE driveState;
	bool driveBusy;
	Int32 driveDelay;
	Int32 driveDelayNext;

	bool readAfterSeek;
	bool playAfterSeek;

	byte sectorBuffer[RAW_SECTOR_SIZE];
	byte sectorBuffers[NUM_SECTOR_BUFFERS][RAW_SECTOR_SIZE];
	UInt16 sectorBufferSizes[NUM_SECTOR_BUFFERS];
	byte writeSectorPointer;
	byte readSectorPointer;
	UInt32 lastReadSector;

	UInt32 startLBA;
	UInt32 positionInIndex;

	UInt32 physicalLBA;
	UInt32 physicalLBATick;
	UInt32 physicalLBAcarry;

	bool lastSectorHeaderValid;
	byte header[4];
	byte subheader[4];

	byte subdata[12];
	byte nextSubdata[12];

	byte session;

	// XA

	SByte filtertablePos[4] = { 0, 60, 115, 98 };
	SByte filtertableNeg[4] = { 0, 0, -52, -55 };
	Int16 zigzagTable[7][29] = 
	{
		{0,       0x0,     0x0,     0x0,    0x0,     -0x0002, 0x000A,  -0x0022, 0x0041, -0x0054, 0x0034, 0x0009,  -0x010A, 0x0400, -0x0A78, 0x234C,  0x6794,  -0x1780, 0x0BCD, -0x0623, 0x0350, -0x016D, 0x006B,  0x000A, -0x0010, 0x0011,  -0x0008, 0x0003,  -0x0001},
		{0,       0x0,    0x0,     -0x0002, 0x0,    0x0003,  -0x0013, 0x003C,  -0x004B, 0x00A2, -0x00E3, 0x0132, -0x0043, -0x0267, 0x0C9D, 0x74BB,  -0x11B4, 0x09B8,  -0x05BF, 0x0372, -0x01A8, 0x00A6, -0x001B, 0x0005,  0x0006, -0x0008, 0x0003,  -0x0001, 0x0},
		{0,       0x0,     -0x0001, 0x0003,  -0x0002, -0x0005, 0x001F,  -0x004A, 0x00B3, -0x0192, 0x02B1, -0x039E, 0x04F8,  -0x05A6, 0x7939,  -0x05A6, 0x04F8,  -0x039E, 0x02B1, -0x0192, 0x00B3, -0x004A, 0x001F,  -0x0005, -0x0002, 0x0003,  -0x0001, 0x0,     0x0},
		{0,       -0x0001, 0x0003,  -0x0008, 0x0006, 0x0005,  -0x001B, 0x00A6, -0x01A8, 0x0372, -0x05BF, 0x09B8,  -0x11B4, 0x74BB,  0x0C9D, -0x0267, -0x0043, 0x0132, -0x00E3, 0x00A2, -0x004B, 0x003C,  -0x0013, 0x0003,  0x0,    -0x0002, 0x0,     0x0,    0x0},
		{-0x0001, 0x0003,  -0x0008, 0x0011,  -0x0010, 0x000A, 0x006B,  -0x016D, 0x0350, -0x0623, 0x0BCD,  -0x1780, 0x6794,  0x234C,  -0x0A78, 0x0400, -0x010A, 0x0009,  0x0034, -0x0054, 0x0041,  -0x0022, 0x000A,  -0x0001, 0x0,     0x0001, 0x0,     0x0,     0x0},
		{0x0002,  -0x0008, 0x0010,  -0x0023, 0x002B, 0x001A,  -0x00EB, 0x027B,  -0x0548, 0x0AFA, -0x16FA, 0x53E0,  0x3C07,  -0x1249, 0x080E, -0x0347, 0x015B,  -0x0044, -0x0017, 0x0046, -0x0023, 0x0011,  -0x0005, 0x0,     0x0,    0x0,     0x0,     0x0,     0x0},
		{-0x0005, 0x0011,  -0x0023, 0x0046, -0x0017, -0x0044, 0x015B,  -0x0347, 0x080E, -0x1249, 0x3C07,  0x53E0,  -0x16FA, 0x0AFA, -0x0548, 0x027B,  -0x00EB, 0x001A,  0x002B, -0x0023, 0x0010,  -0x0008, 0x0002,  0x0,    0x0,     0x0,     0x0,     0x0,     0x0}
	};
	
	byte XaFilterFile;
	byte XaFilterChannel;

	byte XaCurrentFile;
	byte XaCurrentChannel;
	bool XaCurrentSet;

	Int32 AdpcmLast0_L;
	Int32 AdpcmLast1_L;	
	Int32 AdpcmLast0_R;
	Int32 AdpcmLast1_R;

	Int16 XASamples[28 * 8 * 18][2];

	Int16 XaRing_buffer[32][2];
	byte XaRing_pointer;
	byte XaSixStep;

	bool xa_muted;

	std::deque<UInt32> XaFifo;

	// sound related
	bool muted;

	byte cdvol_next00;
	byte cdvol_next01;
	byte cdvol_next10;
	byte cdvol_next11;
	byte cdvol_00;
	byte cdvol_01;
	byte cdvol_10;
	byte cdvol_11;

	struct Trackinfo
	{
		string filename;
		bool isAudio;
		int lbaStart;
		int lbaEnd;
		byte minutesBCD;
		byte secondsBCD;
	};
	Trackinfo trackinfos[99];
	int trackcount = 1;
	int totalLBAs;
	byte totalMinutesBCD;
	byte totalSecondsBCD;
	byte currentTrackBCD;
	byte lastreportCDDA;
	byte trackNumber;
	byte trackNumberBCD;
	Byte* AudioRom;
	UInt32 AudioRom_max;

	bool swapdisk = false;
	UInt32 LIDcount;

	void reset();
	void softReset();

	byte BCDBIN(byte bcd);
	byte BINBCD(byte bin);
	void readCue(string filename);

	void writeReg(byte index, byte value);
	byte readReg(byte index);
	byte readDMA();

	void updateStatus();
	void setIRQ(byte value);
	void errorResponse(byte error, byte reason);
	void ack();
	void ackDrive(byte value);
	void ackRead();
	void startMotor();
	void updatePositionWhileSeeking();
	void updatePhysicalPosition(bool logical);
	Int32 getSeekTicks();
	void seekOnDisk(bool useLBA, UInt32 lba);
	bool completeSeek();
	void readOnDisk(UInt32 sector);
	void readSubchannel(UInt32 sector);
	void startReading(bool afterSeek);
	void startPlaying(byte track, bool afterSeek);
	void processDataSector();
	void processCDDASector();

	void resetXaDecoder();
	void processXAADPCMSector();

	void beginCommand(byte value);
	void handleCommand();
	void handleDrive();
	void work();

	void saveState(UInt32 offset);
	void loadState(UInt32 offset);
	bool isSSIdle();

#if DEBUG
#define CDFILEOUT
#endif
#ifdef CDFILEOUT
	UInt32 debug_CDOutTime[4000000];
	Byte debug_CDOutAddr[4000000];
	UInt32 debug_CDOutData[4000000];
	Byte debug_CDOutType[4000000];
	UInt32 debug_CDOutCount;

	UInt32 debug_XAOutData[1000000];
	Byte debug_XAOutType[1000000];
	UInt32 debug_XAOutCount;
#endif
	void CDOutWriteFile(bool writeTest);
	void CDOutCapture(Byte type, Byte addr, UInt32 data);
	void XAOutCapture(Byte type, UInt32 data);
	void CDTEST();
	bool testaudio = false;
};
extern CDROM CDRom;