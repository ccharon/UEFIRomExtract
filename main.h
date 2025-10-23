//
//  ma.h
//  UEFIRomExtract
//
//  Created by Andy Vandijck on 18/07/14.
//  Copyright (c) 2014 AnV Software. All rights reserved.
//
//  Linux Build, 2025 Christian Charon.
//

#ifndef UEFIRomExtract_ma_h
#define UEFIRomExtract_ma_h

#include <stdint.h>
#include <assert.h>

#define MAX_ADDRESS   0xFFFFFFFFFFFFFFFFULL

#define ASSERT assert

typedef uint32_t RETURN_STATUS;

#define RETURN_SUCCESS 0
#define RETURN_INVALID_PARAMETER 2

#define  BIT8     0x00000100

//
// Decompression algorithm begs here
//
#define BITBUFSIZ 32
#define MAXMATCH  256
#define THRESHOLD 3
#define CODE_BIT  16
#define BAD_TABLE - 1

//
// C: Char&Len Set; P: Position Set; T: exTra Set
//
#define NC      (0xff + MAXMATCH + 2 - THRESHOLD)
#define CBIT    9
#define MAXPBIT 5
#define TBIT    5
#define MAXNP   ((1U << MAXPBIT) - 1)
#define NT      (CODE_BIT + 3)

#if NT > MAXNP
#define NPT NT
#else
#define NPT MAXNP
#endif

typedef struct {
    uint8_t *mSrcBase; // The starting address of compressed data
    uint8_t *mDstBase; // The starting address of decompressed data
    uint32_t mOutBuf;
    uint32_t mInBuf;

    uint16_t mBitCount;
    uint32_t mBitBuf;
    uint32_t mSubBitBuf;
    uint16_t mBlockSize;
    uint32_t mCompSize;
    uint32_t mOrigSize;

    uint16_t mBadTableFlag;

    uint16_t mLeft[2 * NC - 1];
    uint16_t mRight[2 * NC - 1];
    uint8_t mCLen[NC];
    uint8_t mPTLen[NPT];
    uint16_t mCTable[4096];
    uint16_t mPTTable[256];

    // The length of the field 'Position Set Code Length Array Size' in Block Header.
    // For UEFI 2.0 de/compression algorithm, mPBit = 4.
    uint8_t mPBit;
} SCRATCH_DATA;

typedef struct {
    uint16_t Signature; // 0xaa55
    uint8_t Reserved[0x16];
    uint16_t PcirOffset;
} PCI_EXPANSION_ROM_HEADER;

typedef struct {
    uint16_t Signature; // 0xaa55
    uint16_t InitializationSize;
    uint32_t EfiSignature; // 0x0EF1
    uint16_t EfiSubsystem;
    uint16_t EfiMachineType;
    uint16_t CompressionType;
    uint8_t Reserved[8];
    uint16_t EfiImageHeaderOffset;
    uint16_t PcirOffset;
} EFI_PCI_EXPANSION_ROM_HEADER;

typedef struct {
    uint32_t Signature; ///< "PCIR"
    uint16_t VendorId;
    uint16_t DeviceId;
    uint16_t Reserved0;
    uint16_t Length;
    uint8_t Revision;
    uint8_t ClassCode[3];
    uint16_t ImageLength;
    uint16_t CodeRevision;
    uint8_t CodeType;
    uint8_t Indicator;
    uint16_t Reserved1;
} PCI_DATA_STRUCTURE;

typedef struct {
    uint32_t Signature; // "PCIR"
    uint16_t VendorId;
    uint16_t DeviceId;
    uint16_t DeviceListOffset;
    uint16_t Length;
    uint8_t Revision;
    uint8_t ClassCode[3];
    uint16_t ImageLength;
    uint16_t CodeRevision;
    uint8_t CodeType;
    uint8_t Indicator;
    uint16_t MaxRuntimeImageLength;
    uint16_t ConfigUtilityCodeHeaderOffset;
    uint16_t DMTFCLPEntryPointOffset;
} PCI_3_0_DATA_STRUCTURE;

#define PCI_CODE_TYPE_EFI_IMAGE 0x03
#define EFI_PCI_EXPANSION_ROM_HEADER_COMPRESSED 0x0001
#define INDICATOR_LAST  0x80

void Usage(const char *appname);

uint8_t GetEfiCompressedROM(const char *InFile, uint8_t Pci23, uint32_t *EFIIMGStart);

void *InternalMemSetMem16(void *Buffer, uint32_t Length, uint16_t Value);

void *SetMem16(void *Buffer, uint32_t Length, uint16_t Value);

uint16_t ReadUnaligned16(const uint16_t *Buffer);

uint32_t ReadUnaligned32(const uint32_t *Buffer);


/**
 Read NumOfBit of bits from source to mBitBuf.
 Shift mBitBuf NumOfBits left. Read  NumOfBits of bits from source.

 @param  Sd        The global scratch data.
 @param  NumOfBits The number of bits to shift and read.
 **/
void FillBuf(SCRATCH_DATA *Sd, uint16_t NumOfBits);


/**
 Get NumOfBits of bits  from mBitBuf. Fill mBitBuf with subsequent
 NumOfBits of bits from source. Returns NumOfBits of bits that are
 popped .

 @param  Sd        The global scratch data.
 @param  NumOfBits The number of bits to pop and read.

 @return The bits that are popped .
 **/
uint32_t GetBits(SCRATCH_DATA *Sd, uint16_t NumOfBits);


/**
 Creates Huffman Code mappg table for Extra Set, Char&Len Set
 and Position Set accordg to code length array.
 If TableBits > 16, then ASSERT ().

 @param  Sd        The global scratch data.
 @param  NumOfChar The number of symbols  the symbol set.
 @param  BitLen    Code length array.
 @param  TableBits The width of the mappg table.
 @param  Table     The table to be created.

 @retval  0 OK.
 @retval  BAD_TABLE The table is corrupted.
 **/
uint16_t MakeTable(SCRATCH_DATA *Sd, uint16_t NumOfChar, uint8_t *BitLen, uint16_t TableBits, uint16_t *Table);


/**
 Get a position value accordg to Position Huffman Table.

 @param  Sd The global scratch data.

 @return The position value decoded.
**/
uint32_t DecodeP(SCRATCH_DATA *Sd);


/**
 Read  the Extra Set or Potion Set Length Arrary, then
 generate the Huffman code mappg for them.

 @param  Sd      The global scratch data.
 @param  nn      The number of symbols.
 @param  nbit    The number of bits needed to represent nn.
 @param  Special The special symbol that needs to be taken care of.

 @retval  0 OK.
 @retval  BAD_TABLE Table is corrupted.
**/
uint16_t ReadPTLen(SCRATCH_DATA *Sd, uint16_t nn, uint16_t nbit, uint16_t Special);


/**
 Read  and decode the Char&Len Set Code Length Array, then
 generate the Huffman Code mappg table for the Char&Len Set.

 @param  Sd The global scratch data.
**/
void ReadCLen(SCRATCH_DATA *Sd);


/**
 Decode a character/length value.

 Read one value from mBitBuf, Get one code from mBitBuf. If it is at block boundary, generates
 Huffman code mappg table for Extra Set, Code&Len Set and
 Position Set.

 @param  Sd The global scratch data.

 @return The value decoded.

 **/
uint16_t
DecodeC(SCRATCH_DATA *Sd);


/**
 Decode the source data and put the resultg data to the destation buffer.

 @param  Sd The global scratch data.
 **/
void Decode(SCRATCH_DATA *Sd);

#endif
