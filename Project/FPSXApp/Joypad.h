#pragma once

#include "types.h"

class JOYPAD
{
public:
	bool KeyLeft;
	bool KeyRight;
	bool KeyUp;
	bool KeyDown;
	bool KeyStart;
	bool KeySelect;
	bool KeyR1;
	bool KeyR2;
	bool KeyR3;
	bool KeyL1;
	bool KeyL2;
	bool KeyL3;
	bool KeyTriangle;
	bool KeyCircle;
	bool KeyCross;
	bool KeySquare;

	bool blockbuttons;

	byte transmitBuffer;
	byte transmitValue;
	bool transmitFilled;
	UInt32 transmitWait;

	bool transmitting;
	bool waitAck;

	byte receiveBuffer;
	bool receiveFilled;

	byte activeDevice;

	enum class CONTROLLERSTATE
	{
		IDLE,
		READY,
		ID,
		BUTTONLSB,
		BUTTONMSB,
		ANALOGRIGHTX,
		ANALOGRIGHTY,
		ANALOGLEFTX,
		ANALOGLEFTY,
		MOUSEBUTTONSLSB,
		MOUSEBUTTONSMSB,
		MOUSEAXISX,
		MOUSEAXISY,
		SETANALOGMODE,
		GETANALOGMODE,
		COMMAND46,
		COMMAND47,
		COMMAND4C,
		UPDATERUMBLE
	};
	CONTROLLERSTATE controllerState;

	enum class  MEMCARDRSTATE
	{
		IDLE,
		COMMAND,

		READID1,
		READID2,
		READADDR1,
		READADDR2,
		READACK1,
		READACK2,
		READCONFADDR1,
		READCONFADDR2,
		READDATA,
		READCHECKSUM,
		READEND,

		WRITEID1,
		WRITEID2,
		WRITEADDR1,
		WRITEADDR2,
		WRITEDATA,
		WRITECHECKSUM,
		WRITEACK1,
		WRITEACK2,
		WRITEEND
	};
	MEMCARDRSTATE memcardState;
	byte memcardFlags;
	UInt16 memcardAddr;
	Byte memcardData[131072];
	UInt32 memcardPos;
	UInt16 memcardCnt;
	byte memcardChecksum;
	byte lastbyte;

	bool ds_analog;
	bool ds_config;
	bool ds_rumble;
	bool ds_analoglocked;
	byte rumble_config[6];
	byte rumbleIndexLarge;
	byte rumbleIndexSmall;

	bool ds_readconfig;
	bool ds_config_now;
	byte ds_buffer[7];
	byte ds_retlength;
	byte ds_retcount;

	void reset();
	void work();
	bool controllerTransfer();
	bool controllerTransferDigital();
	bool controllerTransferAnalog();
	bool controllerTransferMouse();
	bool controllerTransferDS();
	bool memcardTransfer();
	void updateJoyStat();
	void beginTransfer();
	void write_reg(UInt32 adr, UInt32 value, byte writeMask);
	UInt32 read_reg(UInt32 adr);

	void saveState(UInt32 offset);
	void loadState(UInt32 offset);

#if DEBUG
#define PADFILEOUT
#endif
//#define PADFILEOUT
#ifdef PADFILEOUT
	UInt32 debug_PadOutTime[1000000];
	byte debug_PadOutAddr[1000000];
	UInt16 debug_PadOutData[1000000];
	Byte debug_PadOutType[1000000];
	UInt32 debug_PadOutCount;
#endif
	void padCapture(byte addr, UInt16 data, byte type);
	void padOutWriteFile(bool writeTest);
	void padTest();

private:
};
extern JOYPAD Joypad;