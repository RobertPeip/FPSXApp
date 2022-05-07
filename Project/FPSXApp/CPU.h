#pragma once

#include "types.h"

#if DEBUG
class cpustate
{
public:
	UInt32 regs[32];
	UInt32 regs_hi;
	UInt32 regs_lo;
	UInt32 sr;
	UInt32 cause;

	UInt32 opcode;
	UInt32 ticks;
	UInt32 pc;

	UInt16 irq;

	UInt16 gpu_time;
	UInt16 gpu_line;
	UInt32 gpu_stat;
	UInt16 fifocount;
	UInt16 gpu_ticks;
	UInt16 gpu_obj;

	UInt32 mdec_stat;

	UInt16 cd_status;

	UInt16 timer[3];

	unsigned char debug8;
	UInt16 debug16;
	UInt32 debug32;

	void cpustate::update();
};
#endif

enum class CPU_LOADTYPE
{
	SBYTE,
	SWORD,
	LEFT,
	DWORD,
	BYTE,
	WORD,
	RIGHT
};

class Cpu
{
public:
	UInt32 regs[32];
	UInt32 pc;
	UInt32 hi;
	UInt32 lo;

	UInt32 cop0_BPC;
	UInt32 cop0_BDA;
	UInt32 cop0_JUMPDEST;
	UInt32 cop0_DCIC;
	UInt32 cop0_BADVADDR;
	UInt32 cop0_BDAM;
	UInt32 cop0_BPCM;
	UInt32 cop0_SR;
	UInt32 cop0_CAUSE;
	UInt32 cop0_EPC;
	UInt32 cop0_PRID;

	UInt32 newticks;
	UInt32 totalticks;

	UInt32 opcode[5];

	byte stall;
	byte newStall;
	UInt32 pcOld[5];

	byte exception;
	byte newException;

	byte memoryMuxStage;

	UInt32 value1;
	UInt32 value2;

	byte scratchpad[1024];

	bool blockLoadforward;

	// stage 1
	bool fetchReady;
	bool cacheReady;
	bool fetchWaitRead;
	UInt32 cacheData[256][4];
	UInt32 cacheTags[256];
	bool cacheValid[256];

	// stage 2
	UInt32 decodeImmData;
	byte decodeSource1;
	byte decodeSource2;	
	UInt32 decodeValue1;
	UInt32 decodeValue2;
	byte decodeTarget;
	UInt32 decodeJumpTarget;
	byte decodeOP;
	byte decodeRD;
	byte decodeFunct;
	byte decodeShamt;
	UInt32 newPc;
	bool decodeException;
	
	// stage 3
	bool branch;
	UInt32 resultData;
	byte resultTarget;
	bool resultWriteEnable;
	bool executeReadEnable;
	bool executeGTEReadEnable;
	byte executeGTETarget;
	UInt32 executeReadAddress;
	CPU_LOADTYPE executeLoadType;
	UInt32 executeMemWriteData;
	UInt32 executeMemWriteAddress;
	bool executeMemWriteEnable;
	byte executeMemWriteMask;
	bool executeException;
	bool executeCOP0WriteEnable;
	byte executeCOP0WriteDestination;
	UInt32 executeCOP0WriteValue;
	bool executeBranchTaken;
	bool executeBranchdelaySlot;
	UInt32 hi_next;
	UInt32 lo_next;
	byte hilo_wait;
	bool executelastWasLoad;
	byte executelastLoadTarget;
	byte gtecalctime;
	bool stallFromHILO;
	bool stallFromGTE;
	bool execute_gte_writeEna;
	byte execute_gte_writeAddr;
	UInt32 execute_gte_writeData;
	bool execute_gte_cmdEna;
	UInt32 execute_gte_cmdData;
	bool lastReadCOP;

	// stage 4
	UInt32 writebackData;
	byte writebackTarget;
	bool writebackWriteEnable;
	UInt32 writebackMemWriteData;
	UInt32 writebackMemWriteAddress;
	bool writebackMemWriteEnable;
	byte writebackMemWriteMask;
	bool writebackException;
	bool writebackCOP0WriteEnable;
	byte writebackCOP0WriteDestination;
	UInt32 writebackCOP0WriteValue;
	bool writebackBranchTaken;
	bool writebackBranchdelaySlot;
	CPU_LOADTYPE writebackLoadType;
	UInt32 writebackReadAddress;
	bool writebackGTEReadEnable;
	byte writebackGTETarget;
	bool writebackMemOPisRead;

	// stage 5
	byte emptyPipe;
	UInt32 writeDoneData;
	byte writeDoneTarget;
	bool writeDoneWriteEnable;
	bool writeDoneException;
	bool writeDoneBranchTaken;
	bool writeDoneBranchdelaySlot;

	// exception
	UInt32 exception_SR;
	UInt32 exception_CAUSE;
	UInt32 exception_EPC;
	UInt32 exception_JMP;

	byte blockirq;
	bool lastExcIRQ;
	bool irqNext3;
	bool irqNext2;
	bool irqNext1;

	bool paused;

#if DEBUG
	const int Tracelist_Length = 5000000;
	cpustate Tracelist[5000000];
#endif
	UInt32 debug_outdivcnt = 0;
	UInt32 debug_outdiv = 1;
	int traclist_ptr;
	int runmoretrace = -1;

	int additional_steps;
	UInt64 commands;
	bool tracenext;

	UInt32 cop0_SR_next;

	void reset();
	void work();

	void stage1();
	void stage2();
	void stage3();
	void stage4();
	void stage5();

	void stage4DataRead(UInt32 data, byte AddrLow);

	void doException(byte code, UInt32 pcSave, UInt32 instr, bool branchtaken, bool isDelaySlot);

	void updateCache(UInt32 addr, UInt32 data, byte writeMask);

	void saveState(UInt32 offset);
	void loadState(UInt32 offset);

#if DEBUG
	void trace_file_last();
#endif
};
extern Cpu CPU;