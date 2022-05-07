#pragma once
#include "types.h"

class PSXREGS
{
public:
	UInt32 CACHECONTROL;    // 0xFFFE0130

	UInt32 MC_EXP1_BASE;    // 0x1F801000
	UInt32 MC_EXP2_BASE;    // 0x1F801004
	UInt32 MC_EXP1_DELAY;   // 0x1F801008
	UInt32 MC_EXP3_DELAY;   // 0x1F80100C
	UInt32 MC_BIOS_DELAY;   // 0x1F801010
	UInt32 MC_SPU_DELAY;    // 0x1F801014
	UInt32 MC_CDROM_DELAY;  // 0x1F801018
	UInt32 MC_EXP2_DELAY;   // 0x1F80101C
	UInt32 MC_COMMON_DELAY; // 0x1F801020

	UInt16	JOY_STAT;
	UInt16	JOY_MODE;
	UInt16	JOY_CTRL;
	UInt16	JOY_BAUD;

	UInt32 SIO_STAT;      // 0x1F801054
	UInt32 SIO_MODE;      // 0x1F801058
	UInt32 SIO_CTRL;      // 0x1F80105A
	UInt16 SIO_BAUD;      // 0x1F80105E

	UInt32 MC_RAMSIZE; // 0x1F801060

	UInt32 I_STATUS; // 0x1F801070
	UInt32 I_MASK; // 0x1F801074

	UInt32 D_MADR[7]; // 0x1F801080
	UInt32 D_BCR[7];
	UInt32 D_CHCR[7];
	UInt32 DPCR;
	UInt32 DICR;

	UInt16 T_CURRENT[3]; // 0x1F801100
	UInt16 T_MODE[3];
	UInt16 T_TARGET[3];

	Byte CDROM_STATUS; // 0x1F801800
	Byte CDROM_IRQENA;
	Byte CDROM_IRQFLAG;

	UInt32 GPUREAD; // 0x1F801810
	UInt32 GPUSTAT; // 0x1F801814		
	
	UInt32 MDECStatus;  // 0x1F801824	
	UInt32 MDECControl; // 0x1F801824	

	UInt16 SPU_VOICEREGS[192]; // 0x1F801C00..0x1F801D7F

	//1F801C00h + N * 10h 4   0 Voice 0..23 Volume Left + Right -> 2 regs
	//1F801C04h + N * 10h 2   2 Voice 0..23 ADPCM Sample Rate
	//1F801C06h + N * 10h 2   3 Voice 0..23 ADPCM Start Address
	//1F801C08h + N * 10h 4   4 Voice 0..23 ADSR Attack / Decay / Sustain / Release -> 2 regs
	//1F801C0Ch + N * 10h 2   6 Voice 0..23 ADSR Current Volume
	//1F801C0Eh + N * 10h 2   7 Voice 0..23 ADPCM Repeat Address

	Int16 SPU_VOICEVOLUME[24][2]; // 1F801E00h + N * 04h  4 Voice 0..23 Current Volume Left / Right

	UInt16 SPU_VOLUME_LEFT;    // 0x1F801D80
	UInt16 SPU_VOLUME_RIGHT;   // 0x1F801D82

	UInt32 SPU_KEYON;          // 0x1F801D88
	UInt32 SPU_KEYOFF;         // 0x1F801D8C
	UInt32 SPU_PITCHMODENA;    // 0x1F801D90
	UInt32 SPU_NOISEMODE;      // 0x1F801D94
	UInt32 SPU_REVERBON;       // 0x1F801D98
	UInt32 SPU_ENDX;           // 0x1F801D9C

	UInt16 SPU_IRQ_ADDR;       // 0x1F801DA4

	UInt16 SPU_TRANSFERADDR;   // 0x1F801DA6

	UInt16 SPU_CNT;            // 0x1F801DAA
	UInt16 SPU_TRANSFER_CNT;   // 0x1F801DAC
	UInt16 SPU_STAT;           // 0x1F801DAE

	UInt16 SPU_CDAUDIO_VOL_L;  // 0x1F801DB0	
	UInt16 SPU_CDAUDIO_VOL_R;  // 0x1F801DB2	
	UInt16 SPU_EXT_VOL_L;      // 0x1F801DB4	
	UInt16 SPU_EXT_VOL_R;      // 0x1F801DB6	
	UInt16 SPU_CURVOL_L;       // 0x1F801DB8	
	UInt16 SPU_CURVOL_R;       // 0x1F801DBA	

	Int16  SPU_REVERB_vLOUT  ;  // 1F801D84h spu   vLOUT   volume  Reverb Output Volume Left					  // ss
	Int16  SPU_REVERB_vROUT  ;  // 1F801D86h spu   vROUT   volume  Reverb Output Volume Right					  // ss
	UInt16 SPU_REVERB_mBASE  ;  // 1F801DA2h spu   mBASE   base    Reverb Work Area Start Address in Sound RAM	  // ss
	Int16  SPU_REVERB_dAPF1  ;  // 1F801DC0h rev00 dAPF1   disp    Reverb APF Offset 1							  // ss
	Int16  SPU_REVERB_dAPF2  ;  // 1F801DC2h rev01 dAPF2   disp    Reverb APF Offset 2							  // ss
	Int16  SPU_REVERB_vIIR   ;  // 1F801DC4h rev02 vIIR    volume  Reverb Reflection Volume 1					  // ss
	Int16  SPU_REVERB_vCOMB1 ;  // 1F801DC6h rev03 vCOMB1  volume  Reverb Comb Volume 1							  // ss
	Int16  SPU_REVERB_vCOMB2 ;  // 1F801DC8h rev04 vCOMB2  volume  Reverb Comb Volume 2							  // ss
	Int16  SPU_REVERB_vCOMB3 ;  // 1F801DCAh rev05 vCOMB3  volume  Reverb Comb Volume 3							  // ss
	Int16  SPU_REVERB_vCOMB4 ;  // 1F801DCCh rev06 vCOMB4  volume  Reverb Comb Volume 4							  // ss
	Int16  SPU_REVERB_vWALL  ;  // 1F801DCEh rev07 vWALL   volume  Reverb Reflection Volume 2					  // ss
	Int16  SPU_REVERB_vAPF1  ;  // 1F801DD0h rev08 vAPF1   volume  Reverb APF Volume 1							  // ss
	Int16  SPU_REVERB_vAPF2  ;  // 1F801DD2h rev09 vAPF2   volume  Reverb APF Volume 2							  // ss
	UInt16 SPU_REVERB_mLSAME ;  // 1F801DD4h rev0A mLSAME  src / dst Reverb Same Side Reflection Address 1 Left	  // ss
	UInt16 SPU_REVERB_mRSAME ;  // 1F801DD6h rev0B mRSAME  src / dst Reverb Same Side Reflection Address 1 Right  // ss
	UInt16 SPU_REVERB_mLCOMB1;  // 1F801DD8h rev0C mLCOMB1 src     Reverb Comb Address 1 Left					  // ss
	UInt16 SPU_REVERB_mRCOMB1;  // 1F801DDAh rev0D mRCOMB1 src     Reverb Comb Address 1 Right					  // ss
	UInt16 SPU_REVERB_mLCOMB2;  // 1F801DDCh rev0E mLCOMB2 src     Reverb Comb Address 2 Left					  // ss
	UInt16 SPU_REVERB_mRCOMB2;  // 1F801DDEh rev0F mRCOMB2 src     Reverb Comb Address 2 Right					  // ss
	UInt16 SPU_REVERB_dLSAME ;  // 1F801DE0h rev10 dLSAME  src     Reverb Same Side Reflection Address 2 Left	  // ss
	UInt16 SPU_REVERB_dRSAME ;  // 1F801DE2h rev11 dRSAME  src     Reverb Same Side Reflection Address 2 Right	  // ss
	UInt16 SPU_REVERB_mLDIFF ;  // 1F801DE4h rev12 mLDIFF  src / dst Reverb Different Side Reflect Address 1 Left // ss
	UInt16 SPU_REVERB_mRDIFF ;  // 1F801DE6h rev13 mRDIFF  src / dst Reverb Different Side Reflect Address 1 Righ // ss
	UInt16 SPU_REVERB_mLCOMB3;  // 1F801DE8h rev14 mLCOMB3 src     Reverb Comb Address 3 Left					  // ss
	UInt16 SPU_REVERB_mRCOMB3;  // 1F801DEAh rev15 mRCOMB3 src     Reverb Comb Address 3 Right					  // ss
	UInt16 SPU_REVERB_mLCOMB4;  // 1F801DECh rev16 mLCOMB4 src     Reverb Comb Address 4 Left					  // ss
	UInt16 SPU_REVERB_mRCOMB4;  // 1F801DEEh rev17 mRCOMB4 src     Reverb Comb Address 4 Right					  // ss
	UInt16 SPU_REVERB_dLDIFF ;  // 1F801DF0h rev18 dLDIFF  src     Reverb Different Side Reflect Address 2 Left	  // ss
	UInt16 SPU_REVERB_dRDIFF ;  // 1F801DF2h rev19 dRDIFF  src     Reverb Different Side Reflect Address 2 Right  // ss
	UInt16 SPU_REVERB_mLAPF1 ;  // 1F801DF4h rev1A mLAPF1  src / dst Reverb APF Address 1 Left					  // ss
	UInt16 SPU_REVERB_mRAPF1 ;  // 1F801DF6h rev1B mRAPF1  src / dst Reverb APF Address 1 Right					  // ss
	UInt16 SPU_REVERB_mLAPF2 ;  // 1F801DF8h rev1C mLAPF2  src / dst Reverb APF Address 2 Left					  // ss
	UInt16 SPU_REVERB_mRAPF2 ;  // 1F801DFAh rev1D mRAPF2  src / dst Reverb APF Address 2 Right					  // ss
	Int16  SPU_REVERB_vLIN   ;  // 1F801DFCh rev1E vLIN    volume  Reverb Input Volume Left						  // ss
	Int16  SPU_REVERB_vRIN   ;  // 1F801DFEh rev1F vRIN    volume  Reverb Input Volume Right					  // ss
	
	void reset();

	void write_reg_irq(UInt32 adr, UInt32 value);
	UInt32 read_reg_irq(UInt32 adr);
	void setIRQ(UInt16 index);
	void checkIRQ(bool instant);
	void saveStateIRQ(UInt32 offset);
	void loadStateIRQ(UInt32 offset);

	void write_reg_dma(UInt32 adr, UInt32 value);
	UInt32 read_reg_dma(UInt32 adr);

	void write_reg_timer(UInt32 adr, UInt32 value);
	UInt32 read_reg_timer(UInt32 adr);

	void write_reg_gpu(UInt32 adr, UInt32 value);
	UInt32 read_reg_gpu(UInt32 adr);	
	
	void write_reg_mdec(UInt32 adr, UInt32 value);
	UInt32 read_reg_mdec(UInt32 adr);		
	
	void write_reg_memctrl(UInt32 adr, UInt32 value);
	UInt32 read_reg_memctrl(UInt32 adr);	

	void write_reg_memctrl2(UInt32 adr, UInt32 value);
	UInt32 read_reg_memctrl2(UInt32 adr);

	void write_reg_sio(UInt32 adr, UInt16 value);
	UInt32 read_reg_sio(UInt32 adr);
	void saveState_sio(UInt32 offset);
	void loadState_sio(UInt32 offset);
};
extern PSXREGS PSXRegs;
