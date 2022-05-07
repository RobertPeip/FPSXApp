#include "SDL.h"
#include <fstream>
#include <string>
#include <sstream>

#include "Joypad.h"
#include "PSXRegs.h"
#include "psx.h"
#include "CPU.h"
#include "memory.h"

JOYPAD Joypad;

void JOYPAD::reset()
{
	KeyLeft = false;
	KeyRight = false;
	KeyUp = false;
	KeyDown = false;
	KeyStart = false;
	KeySelect = false;
	KeyR1 = false;
	KeyR2 = false;
	KeyR3 = false;
	KeyL1 = false;
	KeyL2 = false;
	KeyL3 = false;
	KeyTriangle = false;
	KeyCircle = false;
	KeyCross = false;
	KeySquare = false;

	transmitFilled = false;
	receiveFilled = false;

	transmitting = false;
	waitAck = false;

	activeDevice = 0;
	controllerState = CONTROLLERSTATE::IDLE;

	memcardState = MEMCARDRSTATE::IDLE;
	memcardFlags = 0x08; // not written yet

	for (int i = 0; i < 131072; i++)
	{
		memcardData[i] = 0x00;
	}

	ds_analog = false;
	ds_config = false;
	ds_rumble = false;
	ds_analoglocked = false;
	for (int i = 0; i < 6; i++) rumble_config[i] = 0xFF;
	rumbleIndexLarge = 0xFF;
	rumbleIndexSmall = 0xFF;
}

void JOYPAD::work()
{
	if (transmitWait > 0)
	{
		transmitWait--;
		if (transmitWait == 0)
		{
			if (transmitting)
			{
				byte controllerIndex = (PSXRegs.JOY_CTRL >> 13) & 1;

				PSXRegs.JOY_CTRL |= 0b100; // enable rxen

				receiveBuffer = 0xFF;
				bool ack = false;
				bool memoryCardTranfer = false;

				if (controllerIndex == 0)
				{
					switch (activeDevice)
					{
					case 0: // no active device
						if (controllerTransfer())
						{
							activeDevice = 1;
							ack = true;
						}
						else if (memcardTransfer())
						{
							activeDevice = 2;
							memoryCardTranfer = true;
							ack = true;
						}
						break;	

					case 1: // controller
						ack = controllerTransfer();
						break;

					case 2: // memcard
						memoryCardTranfer = true;
						ack = memcardTransfer();
						break;
					}

					padCapture(transmitValue, receiveBuffer, 9);
				}
				//else
				//{
				//	switch (activeDevice)
				//	{
				//	case 0: // no active device
				//		if (controllerTransfer())
				//		{
				//			activeDevice = 1;
				//			ack = true;
				//		}
				//		break;
				//
				//	case 1: // controller
				//		ack = controllerTransfer();
				//		break;
				//
				//	case 2: // memcard
				//		ack = memcardTransfer();
				//		break;
				//	}
				//}
				
				receiveFilled = true;
				lastbyte = transmitValue;

				transmitting = false;
				if (ack)
				{
					waitAck = true;
					if (memoryCardTranfer) transmitWait = 170;
					else transmitWait = 452;
#ifdef FPGACOMPATIBLE
					transmitWait++;
#endif
				}
				else
				{
					activeDevice = 0;
				}

				padCapture(0, receiveBuffer, 3);
			}
			else if (waitAck)
			{
				PSXRegs.JOY_STAT |= 1 << 7; // ackinput

				if ((PSXRegs.JOY_CTRL >> 12) & 1) // irq ena
				{
					PSXRegs.JOY_STAT |= 1 << 9; // intr
					PSXRegs.setIRQ(7);

					padCapture(0, PSXRegs.JOY_STAT, 4);
				}

				waitAck = false;

				if (transmitFilled && ((PSXRegs.JOY_CTRL & 3) == 3))
				{
					beginTransfer();
				}
			}
			updateJoyStat();
		}
	}
}

bool JOYPAD::controllerTransfer()
{
	bool mouse = false;
	bool analog = false;
	bool ds = false;
	if (ds)
	{
		return controllerTransferDS();
	}
	if (mouse)
	{
		return controllerTransferMouse();
	}
	if (analog)
	{
		return controllerTransferAnalog();
	}
	else
	{
		return controllerTransferDigital();
	}
}

bool JOYPAD::controllerTransferDigital()
{
	switch (controllerState)
	{
	case CONTROLLERSTATE::IDLE:
		if (transmitValue == 0x01)
		{
			controllerState = CONTROLLERSTATE::READY;
			return true;
		}
		break;

	case CONTROLLERSTATE::READY:
		if (transmitValue == 0x42)
		{
			receiveBuffer = 0x41;
			controllerState = CONTROLLERSTATE::ID;
			return true;
		}
		break;

	case CONTROLLERSTATE::ID:
		receiveBuffer = 0x5A;
		controllerState = CONTROLLERSTATE::BUTTONLSB;
		return true;
		break;

	case CONTROLLERSTATE::BUTTONLSB:
		receiveBuffer = 0xFF;
		if (KeySelect) receiveBuffer &= ~(1 << 0);
		if (KeyL3) receiveBuffer &= ~(1 << 1);
		if (KeyR3) receiveBuffer &= ~(1 << 2);
		if (KeyStart) receiveBuffer &= ~(1 << 3);
		if (KeyUp) receiveBuffer &= ~(1 << 4);
		if (KeyRight) receiveBuffer &= ~(1 << 5);
		if (KeyDown) receiveBuffer &= ~(1 << 6);
		if (KeyLeft) receiveBuffer &= ~(1 << 7);
		controllerState = CONTROLLERSTATE::BUTTONMSB;
		return true;
		break;

	case CONTROLLERSTATE::BUTTONMSB:
		receiveBuffer = 0xFF;
		if (KeyL2) receiveBuffer &= ~(1 << 0);
		if (KeyR2) receiveBuffer &= ~(1 << 1);
		if (KeyL1) receiveBuffer &= ~(1 << 2);
		if (KeyR1) receiveBuffer &= ~(1 << 3);
		if (KeyTriangle) receiveBuffer &= ~(1 << 4);
		if (KeyCircle) receiveBuffer &= ~(1 << 5);
		if (KeyCross) receiveBuffer &= ~(1 << 6);
		if (KeySquare) receiveBuffer &= ~(1 << 7);
		controllerState = CONTROLLERSTATE::IDLE;
		return false;
		break;
	}

	return false;
}

bool JOYPAD::controllerTransferAnalog()
{
	switch (controllerState)
	{
	case CONTROLLERSTATE::IDLE:
		if (transmitValue == 0x01)
		{
			controllerState = CONTROLLERSTATE::READY;
			return true;
		}
		break;

	case CONTROLLERSTATE::READY:
		if (transmitValue == 0x42)
		{
			receiveBuffer = 0x73;
			controllerState = CONTROLLERSTATE::ID;
			return true;
		}
		break;

	case CONTROLLERSTATE::ID:
		receiveBuffer = 0x5A;
		controllerState = CONTROLLERSTATE::BUTTONLSB;
		return true;
		break;

	case CONTROLLERSTATE::BUTTONLSB:
		receiveBuffer = 0xFF;
		if (KeySelect) receiveBuffer &= ~(1 << 0);
		if (KeyL3) receiveBuffer &= ~(1 << 1);
		if (KeyR3) receiveBuffer &= ~(1 << 2);
		if (KeyStart) receiveBuffer &= ~(1 << 3);
		if (KeyUp) receiveBuffer &= ~(1 << 4);
		if (KeyRight) receiveBuffer &= ~(1 << 5);
		if (KeyDown) receiveBuffer &= ~(1 << 6);
		if (KeyLeft) receiveBuffer &= ~(1 << 7);
		controllerState = CONTROLLERSTATE::BUTTONMSB;
		return true;
		break;

	case CONTROLLERSTATE::BUTTONMSB:
		receiveBuffer = 0xFF;
		if (KeyL2) receiveBuffer &= ~(1 << 0);
		if (KeyR2) receiveBuffer &= ~(1 << 1);
		if (KeyL1) receiveBuffer &= ~(1 << 2);
		if (KeyR1) receiveBuffer &= ~(1 << 3);
		if (KeyTriangle) receiveBuffer &= ~(1 << 4);
		if (KeyCircle) receiveBuffer &= ~(1 << 5);
		if (KeyCross) receiveBuffer &= ~(1 << 6);
		if (KeySquare) receiveBuffer &= ~(1 << 7);
		controllerState = CONTROLLERSTATE::ANALOGRIGHTX;
		return true;
		break;

	case CONTROLLERSTATE::ANALOGRIGHTX:
		receiveBuffer = 0x80;
		controllerState = CONTROLLERSTATE::ANALOGRIGHTY;
		return true;
		break;

	case CONTROLLERSTATE::ANALOGRIGHTY:
		receiveBuffer = 0x80;
		controllerState = CONTROLLERSTATE::ANALOGLEFTX;
		return true;
		break;

	case CONTROLLERSTATE::ANALOGLEFTX:
		receiveBuffer = 0x80;
		controllerState = CONTROLLERSTATE::ANALOGLEFTY;
		return true;
		break;

	case CONTROLLERSTATE::ANALOGLEFTY:
		receiveBuffer = 0x80;
		controllerState = CONTROLLERSTATE::IDLE;
		return false;
		break;
	}

	return false;
}

bool JOYPAD::controllerTransferMouse()
{
	switch (controllerState)
	{
	case CONTROLLERSTATE::IDLE:
		if (transmitValue == 0x01)
		{
			controllerState = CONTROLLERSTATE::READY;
			return true;
		}
		break;

	case CONTROLLERSTATE::READY:
		if (transmitValue == 0x42)
		{
			receiveBuffer = 0x12;
			controllerState = CONTROLLERSTATE::ID;
			return true;
		}
		break;

	case CONTROLLERSTATE::ID:
		receiveBuffer = 0x5A;
		controllerState = CONTROLLERSTATE::MOUSEBUTTONSLSB;
		return true;
		break;

	case CONTROLLERSTATE::MOUSEBUTTONSLSB:
		receiveBuffer = 0xFF;
		controllerState = CONTROLLERSTATE::MOUSEBUTTONSMSB;
		return true;
		break;

	case CONTROLLERSTATE::MOUSEBUTTONSMSB:
		receiveBuffer = 0xFF;
		if (KeyCircle) receiveBuffer &= ~(1 << 2);
		if (KeyCross) receiveBuffer &= ~(1 << 3);
		controllerState = CONTROLLERSTATE::MOUSEAXISX;
		return true;
		break;

	case CONTROLLERSTATE::MOUSEAXISX:
		receiveBuffer = 0x00;
		if (KeyRight) receiveBuffer = 1;
		if (KeyLeft) receiveBuffer = -1;
		controllerState = CONTROLLERSTATE::MOUSEAXISY;
		return true;
		break;

	case CONTROLLERSTATE::MOUSEAXISY:
		receiveBuffer = 0x00;
		if (KeyUp) receiveBuffer = -1;
		if (KeyDown) receiveBuffer = 1;
		return false;
		break;
	}

	return false;
}

bool JOYPAD::controllerTransferDS()
{
	switch (controllerState)
	{
	case CONTROLLERSTATE::IDLE:
		if (transmitValue == 0x01)
		{
			controllerState = CONTROLLERSTATE::READY;
			return true;
		}
		break;

	case CONTROLLERSTATE::READY:
		ds_retcount = 0;
		ds_retlength = 7;

		if (transmitValue == 0x42 || transmitValue == 0x43)
		{
			receiveBuffer = 0x41;

			if (ds_config) receiveBuffer = 0xF3;
			else if (ds_analog) receiveBuffer = 0x73;

			ds_config_now = ds_config;
			ds_readconfig = false;
			if (transmitValue == 0x43) ds_readconfig = true;

			controllerState = CONTROLLERSTATE::ID;
			return true;
		}
		else if (transmitValue == 0x44 && ds_config)
		{
			receiveBuffer = 0xF3;
			controllerState = CONTROLLERSTATE::SETANALOGMODE;
			byte ds_buffer_new[] = { 0x5A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }; std::copy(std::begin(ds_buffer_new), std::end(ds_buffer_new), std::begin(ds_buffer));
			for (int i = 0; i < 6; i++) rumble_config[i] = 0xFF;
			rumbleIndexLarge = 0xFF;
			rumbleIndexSmall = 0xFF;
		}
		else if (transmitValue == 0x45 && ds_config)
		{
			receiveBuffer = 0xF3;
			controllerState = CONTROLLERSTATE::GETANALOGMODE;
			byte ds_buffer_new[] = { 0x5A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }; std::copy(std::begin(ds_buffer_new), std::end(ds_buffer_new), std::begin(ds_buffer));
		}
		else if (transmitValue == 0x46 && ds_config)
		{
			receiveBuffer = 0xF3;
			controllerState = CONTROLLERSTATE::COMMAND46;
			byte ds_buffer_new[] = { 0x5A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }; std::copy(std::begin(ds_buffer_new), std::end(ds_buffer_new), std::begin(ds_buffer));
		}
		else if (transmitValue == 0x47 && ds_config)
		{
			receiveBuffer = 0xF3;
			controllerState = CONTROLLERSTATE::COMMAND47;
			byte ds_buffer_new[] = { 0x5A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }; std::copy(std::begin(ds_buffer_new), std::end(ds_buffer_new), std::begin(ds_buffer));
		}
		else if (transmitValue == 0x4C && ds_config)
		{
			receiveBuffer = 0xF3;
			controllerState = CONTROLLERSTATE::COMMAND4C;
			byte ds_buffer_new[] = { 0x5A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }; std::copy(std::begin(ds_buffer_new), std::end(ds_buffer_new), std::begin(ds_buffer));
		}
		else if (transmitValue == 0x4D && ds_config)
		{
			receiveBuffer = 0xF3;
			controllerState = CONTROLLERSTATE::UPDATERUMBLE;
			byte ds_buffer_new[] = { 0x5A, rumble_config[0], rumble_config[1], rumble_config[2], rumble_config[3], rumble_config[4], rumble_config[5] }; std::copy(std::begin(ds_buffer_new), std::end(ds_buffer_new), std::begin(ds_buffer));
			rumbleIndexLarge = 0xFF;
			rumbleIndexSmall = 0xFF;
		}
		else
		{
			int a = 5;
		}
		break;

	case CONTROLLERSTATE::ID:
		receiveBuffer = 0x5A;
		controllerState = CONTROLLERSTATE::BUTTONLSB;
		return true;
		break;

	case CONTROLLERSTATE::BUTTONLSB:
		receiveBuffer = 0xFF;
		if (KeySelect) receiveBuffer &= ~(1 << 0);
		if (KeyL3) receiveBuffer &= ~(1 << 1);
		if (KeyR3) receiveBuffer &= ~(1 << 2);
		if (KeyStart) receiveBuffer &= ~(1 << 3);
		if (KeyUp) receiveBuffer &= ~(1 << 4);
		if (KeyRight) receiveBuffer &= ~(1 << 5);
		if (KeyDown) receiveBuffer &= ~(1 << 6);
		if (KeyLeft) receiveBuffer &= ~(1 << 7);
		if (ds_readconfig)
		{
			if (ds_config_now) receiveBuffer = 0;
			ds_config = false;
			if (transmitValue == 0x01) ds_config = true;
			ds_rumble = true;
		}
		controllerState = CONTROLLERSTATE::BUTTONMSB;
		return true;
		break;

	case CONTROLLERSTATE::BUTTONMSB:
		receiveBuffer = 0xFF;
		if (KeyL2) receiveBuffer &= ~(1 << 0);
		if (KeyR2) receiveBuffer &= ~(1 << 1);
		if (KeyL1) receiveBuffer &= ~(1 << 2);
		if (KeyR1) receiveBuffer &= ~(1 << 3);
		if (KeyTriangle) receiveBuffer &= ~(1 << 4);
		if (KeyCircle) receiveBuffer &= ~(1 << 5);
		if (KeyCross) receiveBuffer &= ~(1 << 6);
		if (KeySquare) receiveBuffer &= ~(1 << 7);
		if (ds_readconfig && ds_config_now) receiveBuffer = 0;
		if (ds_analog || ds_config)
		{
			controllerState = CONTROLLERSTATE::ANALOGRIGHTX;
			return true;
		}
		else
		{
			controllerState = CONTROLLERSTATE::IDLE;
			return false;
		}
		break;

	case CONTROLLERSTATE::ANALOGRIGHTX:
		receiveBuffer = 0x80;
		if (ds_readconfig && ds_config_now) receiveBuffer = 0;
		controllerState = CONTROLLERSTATE::ANALOGRIGHTY;
		return true;
		break;

	case CONTROLLERSTATE::ANALOGRIGHTY:
		receiveBuffer = 0x80;
		if (ds_readconfig && ds_config_now) receiveBuffer = 0;
		controllerState = CONTROLLERSTATE::ANALOGLEFTX;
		return true;
		break;

	case CONTROLLERSTATE::ANALOGLEFTX:
		receiveBuffer = 0x80;
		if (ds_readconfig && ds_config_now) receiveBuffer = 0;
		controllerState = CONTROLLERSTATE::ANALOGLEFTY;
		return true;
		break;

	case CONTROLLERSTATE::ANALOGLEFTY:
		receiveBuffer = 0x80;
		if (ds_readconfig && ds_config_now) receiveBuffer = 0;
		controllerState = CONTROLLERSTATE::IDLE;
		return false;
		break;

	case CONTROLLERSTATE::SETANALOGMODE:
		receiveBuffer = ds_buffer[ds_retcount];
		if (ds_retcount == 1)
		{
			if (transmitValue == 0x00) ds_analog = false;
			if (transmitValue == 0x01) ds_analog = true;
		}
		if (ds_retcount == 2)
		{
			if (transmitValue == 0x02) ds_analoglocked = false;
			if (transmitValue == 0x03) ds_analoglocked = true;
		}
		if (ds_retcount + 1 < ds_retlength)
		{
			ds_retcount++;
			return true;
		}
		else
		{
			controllerState = CONTROLLERSTATE::IDLE;
			return false;
		}	
	
	case CONTROLLERSTATE::GETANALOGMODE:
		receiveBuffer = ds_buffer[ds_retcount];
		if (ds_retcount + 1 < ds_retlength)
		{
			ds_retcount++;
			return true;
		}
		else
		{
			controllerState = CONTROLLERSTATE::IDLE;
			return false;
		}

	case CONTROLLERSTATE::COMMAND46:
		if (ds_retcount == 1)
		{
			if (transmitValue == 0x00)
			{
				ds_buffer[3] = 0x01;
				ds_buffer[4] = 0x02;
				ds_buffer[5] = 0x00;
				ds_buffer[6] = 0x0A;
			}
			else if (transmitValue == 0x01)
			{
				ds_buffer[3] = 0x01;
				ds_buffer[4] = 0x01;
				ds_buffer[5] = 0x01;
				ds_buffer[6] = 0x14;
			}
		}
		receiveBuffer = ds_buffer[ds_retcount];
		if (ds_retcount + 1 < ds_retlength)
		{
			ds_retcount++;
			return true;
		}
		else
		{
			controllerState = CONTROLLERSTATE::IDLE;
			return false;
		}

	case CONTROLLERSTATE::COMMAND47:
		if (ds_retcount == 1 && transmitValue != 0x00)
		{
			ds_buffer[3] = 0x00;
			ds_buffer[4] = 0x00;
			ds_buffer[5] = 0x00;
			ds_buffer[6] = 0x00;
		}
		receiveBuffer = ds_buffer[ds_retcount];
		if (ds_retcount + 1 < ds_retlength)
		{
			ds_retcount++;
			return true;
		}
		else
		{
			controllerState = CONTROLLERSTATE::IDLE;
			return false;
		}

	case CONTROLLERSTATE::COMMAND4C:
		if (ds_retcount == 1 )
		{
			if (transmitValue == 0x00)
			{
				ds_buffer[4] = 0x04;
			}
			else if (transmitValue == 0x01)
			{
				ds_buffer[4] = 0x07;
			}
		}
		receiveBuffer = ds_buffer[ds_retcount];
		if (ds_retcount + 1 < ds_retlength)
		{
			ds_retcount++;
			return true;
		}
		else
		{
			controllerState = CONTROLLERSTATE::IDLE;
			return false;
		}

	case CONTROLLERSTATE::UPDATERUMBLE:
		receiveBuffer = ds_buffer[ds_retcount];
		if (ds_retcount > 0)
		{
			rumble_config[ds_retcount - 1] = transmitValue;
			if (transmitValue == 0x00) rumbleIndexSmall = ds_retcount;
			if (transmitValue == 0x01) rumbleIndexLarge = ds_retcount;
		}
		if (ds_retcount + 1 < ds_retlength)
		{
			ds_retcount++;
			return true;
		}
		else
		{
			controllerState = CONTROLLERSTATE::IDLE;
			return false;
		}

	}

	return false;
}

bool JOYPAD::memcardTransfer()
{
	switch (memcardState)
	{
	case MEMCARDRSTATE::IDLE:
		if (transmitValue == 0x81)
		{
			memcardState = MEMCARDRSTATE::COMMAND;
			return true;
		}
		break;

	case MEMCARDRSTATE::COMMAND:
		if (transmitValue == 0x52)
		{
			receiveBuffer = memcardFlags;
			memcardState = MEMCARDRSTATE::READID1;
			return true;
		}
		else if (transmitValue == 0x57)
		{
			receiveBuffer = memcardFlags;
			memcardState = MEMCARDRSTATE::WRITEID1;
			return true;
		}
		else
		{
			receiveBuffer = memcardFlags;
			memcardState = MEMCARDRSTATE::IDLE;
			return false;
		}
		break;

	// reading
	case MEMCARDRSTATE::READID1: receiveBuffer = 0x5A; memcardState = MEMCARDRSTATE::READID2; return true;
	case MEMCARDRSTATE::READID2: receiveBuffer = 0x5D; memcardState = MEMCARDRSTATE::READADDR1; return true;
	case MEMCARDRSTATE::READADDR1: receiveBuffer = 0x00; memcardAddr = transmitValue << 8; memcardState = MEMCARDRSTATE::READADDR2; return true;
	case MEMCARDRSTATE::READADDR2: receiveBuffer = memcardAddr >> 8; memcardAddr = (memcardAddr & 0xFF00) | transmitValue; memcardAddr &= 0x3FF; memcardState = MEMCARDRSTATE::READACK1; return true;
	case MEMCARDRSTATE::READACK1: receiveBuffer = 0x5C; memcardState = MEMCARDRSTATE::READACK2; memcardPos = (UInt32)memcardAddr * 128; memcardCnt = 0; return true;
	case MEMCARDRSTATE::READACK2: receiveBuffer = 0x5D; memcardState = MEMCARDRSTATE::READCONFADDR1; return true;
	case MEMCARDRSTATE::READCONFADDR1: receiveBuffer = memcardAddr >> 8; memcardState = MEMCARDRSTATE::READCONFADDR2; return true;
	case MEMCARDRSTATE::READCONFADDR2: receiveBuffer = memcardAddr & 0xFF; memcardState = MEMCARDRSTATE::READDATA; 
		memcardChecksum = (memcardAddr >> 8) ^ (memcardAddr & 0xFF); 
		padCapture(0, memcardAddr, 6);
		if (memcardAddr == 0xFF)
		{
			int a = 5;
		}
		return true;

	case MEMCARDRSTATE::READDATA:
		receiveBuffer = memcardData[memcardPos];
		memcardChecksum ^= receiveBuffer;
		if (memcardCnt < 127)
		{
			memcardCnt++;
			memcardPos++;
		}
		else
		{
			memcardState = MEMCARDRSTATE::READCHECKSUM;
		}
		padCapture(receiveBuffer, 0, 7);
		return true;

	case MEMCARDRSTATE::READCHECKSUM: receiveBuffer = memcardChecksum; memcardState = MEMCARDRSTATE::READEND; return true;
	case MEMCARDRSTATE::READEND: receiveBuffer = 0x47; memcardState = MEMCARDRSTATE::IDLE; return true;

	// writing
	case MEMCARDRSTATE::WRITEID1: receiveBuffer = 0x5A; memcardState = MEMCARDRSTATE::WRITEID2; return true;
	case MEMCARDRSTATE::WRITEID2: receiveBuffer = 0x5D; memcardState = MEMCARDRSTATE::WRITEADDR1; return true;
	case MEMCARDRSTATE::WRITEADDR1: receiveBuffer = 0x00; memcardAddr = transmitValue << 8; memcardState = MEMCARDRSTATE::WRITEADDR2; return true;
	case MEMCARDRSTATE::WRITEADDR2: 
		receiveBuffer = memcardAddr >> 8; 
		memcardAddr = (memcardAddr & 0xFF00) | transmitValue; 
		memcardAddr &= 0x3FF;
		memcardState = MEMCARDRSTATE::WRITEDATA; 
		memcardPos = (UInt32)memcardAddr * 128; 
		memcardCnt = 0;
		memcardChecksum = (memcardAddr >> 8) ^ (memcardAddr & 0xFF);
		return true;

	case MEMCARDRSTATE::WRITEDATA:
		receiveBuffer = lastbyte;
		memcardFlags &= 0xF7; // clear no-write-yet
		memcardData[memcardPos] = transmitValue;
		memcardChecksum ^= transmitValue;
		if (memcardCnt < 127)
		{
			memcardCnt++;
			memcardPos++;
		}
		else
		{
			memcardState = MEMCARDRSTATE::WRITECHECKSUM;
		}
		return true;

	case MEMCARDRSTATE::WRITECHECKSUM: receiveBuffer = memcardChecksum; memcardState = MEMCARDRSTATE::WRITEACK1; return true;
	case MEMCARDRSTATE::WRITEACK1: receiveBuffer = 0x5C; memcardState = MEMCARDRSTATE::WRITEACK2; return true;
	case MEMCARDRSTATE::WRITEACK2: receiveBuffer = 0x5D; memcardState = MEMCARDRSTATE::WRITEEND; return true;
	case MEMCARDRSTATE::WRITEEND: receiveBuffer = 0x47; memcardState = MEMCARDRSTATE::IDLE; return false;
	}
	return false;
}

void JOYPAD::updateJoyStat()
{
	PSXRegs.JOY_STAT &= 0xFFFFFFF8;
	PSXRegs.JOY_STAT |= !(transmitFilled);
	PSXRegs.JOY_STAT |= (receiveFilled << 1);
	PSXRegs.JOY_STAT |= ((!transmitFilled && !transmitting) << 2);
}

void JOYPAD::beginTransfer()
{
	PSXRegs.JOY_CTRL |= 0b100; // enable rxen
	transmitValue = transmitBuffer;
	transmitFilled = false;
	transmitting = true;
	transmitWait = (PSXRegs.JOY_BAUD * 8) - 1;
	if (PSXRegs.JOY_BAUD == 0)
	{
		transmitWait = 1;
	}

	updateJoyStat();

#ifdef FPGACOMPATIBLE
	transmitWait+=3;
#endif

	padCapture(0, transmitValue, 5);
}

void JOYPAD::write_reg(UInt32 adr, UInt32 value, byte writeMask)
{
	if (adr == 0x8 && writeMask >= 0b0100)
	{
		adr = 0xA;
		value >>= 16;
	}
	if (adr == 0xC && writeMask >= 0b0100)
	{
		value >>= 16;
		adr = 0xE;
	}

	padCapture(adr, value, 1);

	switch (adr)
	{
	case 0x0: // JOY_DATA
		transmitFilled = true;
		transmitBuffer = value & 0xFF;
		
		if (!transmitting && !waitAck && ((PSXRegs.JOY_CTRL & 3) == 3))
		{
			beginTransfer();
		}
		break;

	case 0x8: // JOY_MODE
		PSXRegs.JOY_MODE = value & 0xFFFF;
		break;

	case 0xA: // JOY_CTRL
		PSXRegs.JOY_CTRL = value & 0xFFFF;
		
		if ((PSXRegs.JOY_CTRL >> 6) & 1) // reset
		{
			transmitFilled = false;
			receiveFilled = false;
			PSXRegs.JOY_CTRL = 0;
			PSXRegs.JOY_STAT = 0;
			PSXRegs.JOY_MODE = 0;
			transmitting = false;
		}	

		if ((PSXRegs.JOY_CTRL >> 4) & 1) // ack
		{
			PSXRegs.JOY_STAT &= ~(1 << 9); // clear intr
		}
		
		if (((PSXRegs.JOY_CTRL >> 1) & 1) == 0) // not selected
		{
			activeDevice = 0;
		}

		if ((PSXRegs.JOY_CTRL & 3) == 3) // selected and txen
		{
			// todo start transfer
			if (!transmitting && !waitAck && transmitFilled)
			{
				beginTransfer();
			}
		}
		else
		{
			controllerState = CONTROLLERSTATE::IDLE;
			memcardState = MEMCARDRSTATE::IDLE;
			transmitWait = 0;
			transmitting = false;
			waitAck = false;
			padCapture(0, 0, 8);
		}
		updateJoyStat();
		break;

	case 0xE: // JOY_BAUD
		PSXRegs.JOY_BAUD = value & 0xFFFF;
		break;
	}
}

UInt32 JOYPAD::read_reg(UInt32 adr)
{
	UInt32 retval = 0xCBAD;

	switch (adr)
	{
	case 0x0: // JOY_DATA
		if (receiveFilled)
		{
			receiveFilled = false;
			updateJoyStat();
			retval = (receiveBuffer | (receiveBuffer << 8) | (receiveBuffer << 16) | (receiveBuffer << 24));
		}
		else
		{
			retval = 0xFFFFFFFF;
		}
		break;

	case 0x4: // JOY_STAT
		// todo early transmit?
		retval = PSXRegs.JOY_STAT;
		PSXRegs.JOY_STAT &= ~(1 << 7); // clear ackinput
		break;

	case 0x8: // JOY_MODE
		retval = PSXRegs.JOY_MODE;
		break;

	case 0xA: // JOY_CTRL
		retval = PSXRegs.JOY_CTRL;
		break;

	case 0xE: // JOY_BAUD
		retval = PSXRegs.JOY_BAUD;
		break;
	}

	padCapture(adr, retval, 2);

	return retval;
}

void JOYPAD::padCapture(byte addr, UInt16 data, byte type)
{
	if (type < 8) return;

#if defined(PADFILEOUT)
	if (debug_PadOutCount < 1000000)
	{
		debug_PadOutTime[debug_PadOutCount] = CPU.totalticks + 1;
		debug_PadOutAddr[debug_PadOutCount] = addr;
		debug_PadOutData[debug_PadOutCount] = data;
		debug_PadOutType[debug_PadOutCount] = type;
		debug_PadOutCount++;
	}
#endif
}

void JOYPAD::saveState(UInt32 offset)
{
	psx.savestate_addvalue(offset + 0, 31,  0, transmitWait);
	psx.savestate_addvalue(offset + 1, 15,  0, PSXRegs.JOY_STAT);
	psx.savestate_addvalue(offset + 1, 31, 16, PSXRegs.JOY_MODE);
	psx.savestate_addvalue(offset + 2, 15,  0, PSXRegs.JOY_CTRL);
	psx.savestate_addvalue(offset + 2, 31, 16, PSXRegs.JOY_BAUD);
	psx.savestate_addvalue(offset + 3,  7,  0, transmitBuffer);
	psx.savestate_addvalue(offset + 3, 15,  8, transmitValue);
	psx.savestate_addvalue(offset + 3, 23, 16, receiveBuffer);
	psx.savestate_addvalue(offset + 3, 31, 24, activeDevice);
	//psx.savestate_addvalue(offset + 4,  7,  0, controllerData);
	psx.savestate_addvalue(offset + 4, 15,  8, (byte)controllerState);
	psx.savestate_addvalue(offset + 4, 16, 16, transmitFilled);
	psx.savestate_addvalue(offset + 4, 17, 17, transmitting);
	psx.savestate_addvalue(offset + 4, 18, 18, waitAck);
	psx.savestate_addvalue(offset + 4, 19, 19, receiveFilled);
}

void JOYPAD::loadState(UInt32 offset)
{
	transmitWait      = psx.savestate_loadvalue(offset + 0, 31,  0);
	PSXRegs.JOY_STAT  = psx.savestate_loadvalue(offset + 1, 15,  0);
	PSXRegs.JOY_MODE  = psx.savestate_loadvalue(offset + 1, 31, 16);
	PSXRegs.JOY_CTRL  = psx.savestate_loadvalue(offset + 2, 15,  0);
	PSXRegs.JOY_BAUD  = psx.savestate_loadvalue(offset + 2, 31, 16);
	transmitBuffer	  = psx.savestate_loadvalue(offset + 3,  7,  0);
	transmitValue	  = psx.savestate_loadvalue(offset + 3, 15,  8);
	receiveBuffer	  = psx.savestate_loadvalue(offset + 3, 23, 16);
	activeDevice	  = psx.savestate_loadvalue(offset + 3, 31, 24);
	//controllerData	  = psx.savestate_loadvalue(offset + 4,  7,  0);
	controllerState   = (CONTROLLERSTATE)psx.savestate_loadvalue(offset + 4, 15, 8);
	transmitFilled    = psx.savestate_loadvalue(offset + 4, 16, 16);
	transmitting	  = psx.savestate_loadvalue(offset + 4, 17, 17);
	waitAck		      = psx.savestate_loadvalue(offset + 4, 18, 18);
	receiveFilled     = psx.savestate_loadvalue(offset + 4, 19, 19);
}

void JOYPAD::padOutWriteFile(bool writeTest)
{
#ifdef PADFILEOUT
	FILE* file = fopen("R:\\debug_pad.txt", "w");

	for (int i = 0; i < debug_PadOutCount; i++)
	{
		if (debug_PadOutType[i] == 1) fputs("WRITE: ", file);
		if (debug_PadOutType[i] == 2) fputs("READ: ", file);
		if (debug_PadOutType[i] == 3) fputs("TRANSMIT: ", file);
		if (debug_PadOutType[i] == 4) fputs("IRQ: ", file);
		if (debug_PadOutType[i] == 5) fputs("BEGINTRANSFER: ", file);
		if (debug_PadOutType[i] == 6) fputs("READMEMBLOCK: ", file);
		if (debug_PadOutType[i] == 7) fputs("READMEMDATA: ", file);
		if (debug_PadOutType[i] == 8) fputs("RESETCONTROLLER: ", file);
		if (debug_PadOutType[i] == 9) fputs("TRANSFER: ", file);
		char buf[10];
		//_itoa(debug_PadOutTime[i], buf, 16);
		//for (int c = strlen(buf); c < 8; c++) fputc('0', file);
		//fputs(buf, file);
		//fputc(' ', file);
		_itoa(debug_PadOutAddr[i], buf, 16);
		for (int c = strlen(buf); c < 2; c++) fputc('0', file);
		fputs(buf, file);
		fputc(' ', file);
		_itoa(debug_PadOutData[i], buf, 16);
		for (int c = strlen(buf); c < 4; c++) fputc('0', file);
		fputs(buf, file);

		fputc('\n', file);
	}
	fclose(file);
#endif
}

void JOYPAD::padTest()
{
#ifdef FPGACOMPATIBLE
	std::ifstream infile("R:\\pad_test_FPSXA.txt");
#else
	std::ifstream infile("R:\\pad_test_duck.txt");
#endif
	std::string line;
	while (std::getline(infile, line))
	{
		std::string type = line.substr(0, 2);
		std::string addr = line.substr(3, 2);
		std::string data = line.substr(6, 2);
		byte typeI = std::stoul(type, nullptr, 16);
		byte addrI = std::stoul(addr, nullptr, 16);
		byte dataI = std::stoul(data, nullptr, 16);

		switch (typeI)
		{

		case 8: 
			controllerState = CONTROLLERSTATE::IDLE; 
			padCapture(0, 0, 8);
			break;
		case 9:
		{
			transmitValue = addrI;
			receiveBuffer = 0xFF;
			controllerTransfer();
			padCapture(transmitValue, receiveBuffer, 9);
		}
		break;
		}
	}

	padOutWriteFile(false);
}