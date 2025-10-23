//
//  main.c
//  UEFIRomExtract
//
//  Created by Andy Vandijck on 18/07/14.
//  Copyright (c) 2014 AnV Software. All rights reserved.
//
//  Linux Build, 2025 Christian Charon.
//
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "main.h"

void *InternalMemSetMem16(void *Buffer, uint32_t Length, uint16_t Value) {
    do {
        ((uint16_t *) Buffer)[--Length] = Value;
    } while (Length != 0);

    return Buffer;
}

void *SetMem16(void *Buffer, uint32_t Length, uint16_t Value) {
    if (Length == 0) {
        return Buffer;
    }

    ASSERT(Buffer != NULL);

    uintptr_t buf_addr = (uintptr_t)Buffer;
    uintptr_t avail = (uintptr_t)MAX_ADDRESS - buf_addr;

    ASSERT((uintptr_t)(Length - 1) <= avail);
    ASSERT((buf_addr & (sizeof(Value) - 1)) == 0);
    ASSERT((Length & (sizeof(Value) - 1)) == 0);

    uint32_t Count = Length / sizeof(Value);

    return InternalMemSetMem16(Buffer, Count, Value);
}

uint16_t ReadUnaligned16(const uint16_t *Buffer) {
    ASSERT(Buffer != NULL);

    return (uint16_t) (((uint8_t *) Buffer)[0] | (((uint8_t *) Buffer)[1] << 8));
}

uint32_t ReadUnaligned32(const uint32_t *Buffer) {
    ASSERT(Buffer != NULL);

    uint16_t LowerBytes = ReadUnaligned16((uint16_t *) Buffer);
    uint16_t HigherBytes = ReadUnaligned16((uint16_t *) Buffer + 1);

    return (uint32_t) (LowerBytes | (HigherBytes << 16));
}

/**
 Read NumOfBit of bits from source into mBitBuf.

 Shift mBitBuf NumOfBits left. Read NumOfBits of bits from source.

 @param  Sd        The global scratch data.
 @param  NumOfBits The number of bits to shift and read.
 **/
void FillBuf(SCRATCH_DATA *Sd, uint16_t NumOfBits) {
    // Left shift NumOfBits of bits advance
    Sd->mBitBuf = (uint32_t) (Sd->mBitBuf << NumOfBits);

    // Copy data needed bytes into mSbuBitBuf
    while (NumOfBits > Sd->mBitCount) {
        Sd->mBitBuf |= (uint32_t) (Sd->mSubBitBuf << (NumOfBits = (uint16_t) (NumOfBits - Sd->mBitCount)));

        if (Sd->mCompSize > 0) {
            // Get 1 byte into SubBitBuf
            Sd->mCompSize--;
            Sd->mSubBitBuf = Sd->mSrcBase[Sd->mInBuf++];
            Sd->mBitCount = 8;
        } else {
            // No more bits from the source, just pad zero bit.
            Sd->mSubBitBuf = 0;
            Sd->mBitCount = 8;
        }
    }

    // Calculate additional bit count read to update mBitCount
    Sd->mBitCount = (uint16_t) (Sd->mBitCount - NumOfBits);

    // Copy NumOfBits of bits from mSubBitBuf into mBitBuf
    Sd->mBitBuf |= Sd->mSubBitBuf >> Sd->mBitCount;
}

/**
 Get NumOfBits of bits from mBitBuf. Fill mBitBuf with subsequent
 NumOfBits of bits from source. Returns NumOfBits of bits that are
 popped out.

 @param  Sd        The global scratch data.
 @param  NumOfBits The number of bits to pop and read.

 @return The bits that are popped out.
 **/
uint32_t GetBits(SCRATCH_DATA *Sd, uint16_t NumOfBits) {
    // Pop NumOfBits of Bits from Left
    uint32_t OutBits = (uint32_t) (Sd->mBitBuf >> (BITBUFSIZ - NumOfBits));

    // Fill up mBitBuf from source
    FillBuf(Sd, NumOfBits);

    return OutBits;
}

/**
 Creates Huffman Code mapping table for Extra Set, Char&Len Set
 and Position Set according to code length array.
 If TableBits > 16, then ASSERT ().

 @param  Sd        The global scratch data.
 @param  NumOfChar The number of symbols the symbol set.
 @param  BitLen    Code length array.
 @param  TableBits The width of the mapping table.
 @param  Table     The table to be created.

 @retval  0 OK.
 @retval  BAD_TABLE The table is corrupted.
 **/
uint16_t MakeTable(SCRATCH_DATA *Sd, uint16_t NumOfChar, uint8_t *BitLen, uint16_t TableBits, uint16_t *Table) {
    uint16_t Count[17];
    uint16_t Weight[17];
    uint16_t Start[18];
    uint16_t Index3;
    uint16_t Index;

    //
    // The maximum mapping table width supported by this internal
    // working function is 16.
    //
    ASSERT(TableBits <= 16);

    for (Index = 0; Index <= 16; Index++) {
        Count[Index] = 0;
    }

    for (Index = 0; Index < NumOfChar; Index++) {
        Count[BitLen[Index]]++;
    }

    Start[0] = 0;
    Start[1] = 0;

    for (Index = 1; Index <= 16; Index++) {
        uint16_t WordOfStart = Start[Index];
        uint16_t WordOfCount = Count[Index];
        Start[Index + 1] = (uint16_t) (WordOfStart + (WordOfCount << (16 - Index)));
    }

    if (Start[17] != 0) {
        /*(1U << 16)*/
        return (uint16_t) BAD_TABLE;
    }

    uint16_t JuBits = (uint16_t) (16 - TableBits);

    Weight[0] = 0;
    for (Index = 1; Index <= TableBits; Index++) {
        Start[Index] >>= JuBits;
        Weight[Index] = (uint16_t) (1U << (TableBits - Index));
    }

    while (Index <= 16) {
        Weight[Index] = (uint16_t) (1U << (16 - Index));
        Index++;
    }

    Index = (uint16_t) (Start[TableBits + 1] >> JuBits);

    if (Index != 0) {
        Index3 = (uint16_t) (1U << TableBits);
        if (Index < Index3) {
            SetMem16(Table + Index, (Index3 - Index) * sizeof (*Table), 0);
        }
    }

    uint16_t Avail = NumOfChar;
    uint16_t Mask = (uint16_t) (1U << (15 - TableBits));

    for (uint16_t Char = 0; Char < NumOfChar; Char++) {
        uint16_t Len = BitLen[Char];
        if (Len == 0 || Len >= 17) {
            continue;
        }

        uint16_t NextCode = (uint16_t) (Start[Len] + Weight[Len]);

        if (Len <= TableBits) {
            for (Index = Start[Len]; Index < NextCode; Index++) {
                Table[Index] = Char;
            }
        } else {
            Index3 = Start[Len];
            uint16_t *Pointer = &Table[Index3 >> JuBits];
            Index = (uint16_t) (Len - TableBits);

            while (Index != 0) {
                if (*Pointer == 0 && Avail < (2 * NC - 1)) {
                    Sd->mRight[Avail] = Sd->mLeft[Avail] = 0;
                    *Pointer = Avail++;
                }

                if (*Pointer < (2 * NC - 1)) {
                    if ((Index3 & Mask) != 0) {
                        Pointer = &Sd->mRight[*Pointer];
                    } else {
                        Pointer = &Sd->mLeft[*Pointer];
                    }
                }

                Index3 <<= 1;
                Index--;
            }

            *Pointer = Char;
        }

        Start[Len] = NextCode;
    }
    //
    // Succeeds
    //
    return 0;
}

/**
 Get a position value according to Position Huffman Table.

 @param  Sd The global scratch data.

 @return The position value decoded.
 **/
uint32_t DecodeP(SCRATCH_DATA *Sd) {
    uint16_t Val = Sd->mPTTable[Sd->mBitBuf >> (BITBUFSIZ - 8)];

    if (Val >= MAXNP) {
        uint32_t Mask = 1U << (BITBUFSIZ - 1 - 8);

        do {
            if ((Sd->mBitBuf & Mask) != 0) {
                Val = Sd->mRight[Val];
            } else {
                Val = Sd->mLeft[Val];
            }

            Mask >>= 1;
        } while (Val >= MAXNP);
    }

    // Advance what we have read
    FillBuf(Sd, Sd->mPTLen[Val]);

    uint32_t Pos = Val;
    if (Val > 1) {
        Pos = (uint32_t) ((1U << (Val - 1)) + GetBits(Sd, (uint16_t) (Val - 1)));
    }

    return Pos;
}

/**
 Read the Extra Set or Position Set Length Array, then
 generate the Huffman code mapping for them.

 @param  Sd      The global scratch data.
 @param  nn      The number of symbols.
 @param  nbit    The number of bits needed to represent nn.
 @param  Special The special symbol that needs to be taken care of.

 @retval  0 OK.
 @retval  BAD_TABLE Table is corrupted.
 **/
uint16_t ReadPTLen(SCRATCH_DATA *Sd, uint16_t nn, uint16_t nbit, uint16_t Special) {
    uint16_t CharC;

    // Read Extra Set Code Length Array size
    uint16_t Number = (uint16_t) GetBits(Sd, nbit);

    if (Number == 0) {
        // This represents only Huffman code used
        CharC = (uint16_t) GetBits(Sd, nbit);
        SetMem16(&Sd->mPTTable[0], sizeof (Sd->mPTTable), CharC);
        memset(Sd->mPTLen, 0, nn);

        return 0;
    }

    uint16_t Index = 0;

    while (Index < Number && Index < NPT) {
        CharC = (uint16_t) (Sd->mBitBuf >> (BITBUFSIZ - 3));

        // If a code length is less than 7, then it is encoded as a 3-bit
        // value. Or it is encoded as a series of "1"s followed by a
        // terminating "0". The number of "1"s = Code length - 4.
        if (CharC == 7) {
            uint32_t Mask = 1U << (BITBUFSIZ - 1 - 3);
            while (Mask & Sd->mBitBuf) {
                Mask >>= 1;
                CharC += 1;
            }
        }

        FillBuf(Sd, (uint16_t) ((CharC < 7) ? 3 : CharC - 3));

        Sd->mPTLen[Index++] = (uint8_t) CharC;

        // For Code&Len Set,
        // After the third length of the code length concatenation,
        // a 2-bit value is used to indicated the number of consecutive
        // zero lengths after the third length.
        if (Index == Special) {
            CharC = (uint16_t) GetBits(Sd, 2);
            while ((int16_t) (--CharC) >= 0 && Index < NPT) {
                Sd->mPTLen[Index++] = 0;
            }
        }
    }

    while (Index < nn && Index < NPT) {
        Sd->mPTLen[Index++] = 0;
    }

    return MakeTable(Sd, nn, Sd->mPTLen, 8, Sd->mPTTable);
}

/**
 Read and decode the Char&Len Set Code Length Array, then
 generate the Huffman Code mapping table for the Char&Len Set.

 @param  Sd The global scratch data.
 **/
void ReadCLen(SCRATCH_DATA *Sd) {
    uint16_t CharC;

    uint16_t Number = (uint16_t) GetBits(Sd, CBIT);

    if (Number == 0) {
        // This represents only Huffman code used
        CharC = (uint16_t) GetBits(Sd, CBIT);

        memset(Sd->mCLen, 0, NC);
        SetMem16(&Sd->mCTable[0], sizeof (Sd->mCTable), CharC);

        return;
    }

    uint16_t Index = 0;
    while (Index < Number && Index < NC) {
        CharC = Sd->mPTTable[Sd->mBitBuf >> (BITBUFSIZ - 8)];
        if (CharC >= NT) {
            uint32_t Mask = 1U << (BITBUFSIZ - 1 - 8);

            do {
                if (Mask & Sd->mBitBuf) {
                    CharC = Sd->mRight[CharC];
                } else {
                    CharC = Sd->mLeft[CharC];
                }

                Mask >>= 1;
            } while (CharC >= NT);
        }

        // Advance what we have read
        FillBuf(Sd, Sd->mPTLen[CharC]);

        if (CharC <= 2) {
            if (CharC == 0) {
                CharC = 1;
            } else if (CharC == 1) {
                CharC = (uint16_t) (GetBits(Sd, 4) + 3);
            } else if (CharC == 2) {
                CharC = (uint16_t) (GetBits(Sd, CBIT) + 20);
            }

            while ((int16_t) (--CharC) >= 0 && Index < NC) {
                Sd->mCLen[Index++] = 0;
            }
        } else {
            Sd->mCLen[Index++] = (uint8_t) (CharC - 2);
        }
    }

    memset(Sd->mCLen + Index, 0, NC - Index);

    MakeTable(Sd, NC, Sd->mCLen, 12, Sd->mCTable);
}

/**
 Read one value from mBitBuf, Get one code from mBitBuf. If it is at block boundary, generates
 Huffman code mapping table for Extra Set, Code&Len Set and
 Position Set.

 @param  Sd The global scratch data.

 @return The value decoded.

 **/
uint16_t
DecodeC(
    SCRATCH_DATA *Sd
) {
    if (Sd->mBlockSize == 0) {
        // Starting a new block
        // Read BlockSize from block header
        Sd->mBlockSize = (uint16_t) GetBits(Sd, 16);

        // Read the Extra Set Code Length Arrary,
        // Generate the Huffman code mapping table for Extra Set.
        Sd->mBadTableFlag = ReadPTLen(Sd, NT, TBIT, 3);

        if (Sd->mBadTableFlag != 0) {
            return 0;
        }

        // Read and decode the Char&Len Set Code Length Arrary,
        // Generate the Huffman code mapping table for Char&Len Set.
        ReadCLen(Sd);

        // Read the Position Set Code Length Arrary,
        // Generate the Huffman code mapping table for the Position Set.
        Sd->mBadTableFlag = ReadPTLen(Sd, MAXNP, Sd->mPBit, (uint16_t) (-1));

        if (Sd->mBadTableFlag != 0) {
            return 0;
        }
    }

    // Get one code according to Code&Set Huffman Table
    Sd->mBlockSize--;
    uint16_t Index2 = Sd->mCTable[Sd->mBitBuf >> (BITBUFSIZ - 12)];

    if (Index2 >= NC) {
        uint32_t Mask = 1U << (BITBUFSIZ - 1 - 12);

        do {
            if ((Sd->mBitBuf & Mask) != 0) {
                Index2 = Sd->mRight[Index2];
            } else {
                Index2 = Sd->mLeft[Index2];
            }

            Mask >>= 1;
        } while (Index2 >= NC);
    }

    // Advance what we have read
    FillBuf(Sd, Sd->mCLen[Index2]);

    return Index2;
}

/**
 Decode the source data and put the resulting data into the destination buffer.

 @param  Sd The global scratch data.
 **/
void Decode(SCRATCH_DATA *Sd) {
    uint16_t CharC;

    uint16_t BytesRemain = (uint16_t) (-1);

    uint32_t DataIdx = 0;

    for (;;) {
        // Get one code from mBitBuf
        CharC = DecodeC(Sd);
        if (Sd->mBadTableFlag != 0) {
            goto Done;
        }

        if (CharC < 256) {
            // Process an Original character
            if (Sd->mOutBuf >= Sd->mOrigSize) {
                goto Done;
            }

            // Write orignal character into mDstBase
            Sd->mDstBase[Sd->mOutBuf++] = (uint8_t) CharC;

        } else {
            // Process a Pointer
            CharC = (uint16_t) (CharC - (BIT8 - THRESHOLD));

            // Get string length
            BytesRemain = CharC;

            // Locate string position
            DataIdx = Sd->mOutBuf - DecodeP(Sd) - 1;

            // Write BytesRemaof bytes into mDstBase
            BytesRemain--;
            while ((int16_t) (BytesRemain) >= 0) {
                Sd->mDstBase[Sd->mOutBuf++] = Sd->mDstBase[DataIdx++];
                if (Sd->mOutBuf >= Sd->mOrigSize) {
                    goto Done;
                }

                BytesRemain--;
            }
        }
    }

Done:
    return;
}

/**
 Given a compressed source buffer, this function retrieves the size of
 the uncompressed buffer and the size of the scratch buffer required
 to decompress the compressed source buffer.

 Retrieves the size of the uncompressed buffer and the temporary scratch buffer
 required to decompress the buffer specified by Source and SourceSize.
 If the size of the uncompressed buffer or the size of the scratch buffer cannot
 be determined from the compressed data specified by Source and SourceData,
 then RETURN_INVALID_PARAMETER is returned.  Otherwise, the size of the uncompressed
 buffer is returned DestinationSize, the size of the scratch buffer is returned
 ScratchSize, and RETURN_SUCCESS is returned.
 This function does not have scratch buffer available to perform a thorough
 checking of the validity of the source data.  It just retrieves the "Original Size"
 field from the beginning bytes of the source data and output it as DestinationSize.
 And ScratchSize is specific to the decompression implementation.

 If Source is NULL, then ASSERT().
 If DestinationSize is NULL, then ASSERT().
 If ScratchSize is NULL, then ASSERT().

 @param  Source          The source buffer containing the compressed data.
 @param  SourceSize      The size, bytes, of the source buffer.
 @param  DestinationSize A pointer to the size, bytes, of the uncompressed buffer
 that will be generated when the compressed buffer specified
 by Source and SourceSize is decompressed.
 @param  ScratchSize     A pointer to the size, bytes, of the scratch buffer that
 is required to decompress the compressed buffer specified
 by Source and SourceSize.

 @retval  RETURN_SUCCESS The size of the uncompressed data was returned
 DestinationSize, and the size of the scratch
 buffer was returned ScratchSize.
 @retval  RETURN_INVALID_PARAMETER
 The size of the uncompressed data or the size of
 the scratch buffer cannot be determined from
 the compressed data specified by Source
 and SourceSize.
 **/
RETURN_STATUS UefiDecompressGetInfo(const void *Source, uint32_t SourceSize, uint32_t *DestinationSize, uint32_t *ScratchSize) {
    ASSERT(Source != NULL);
    ASSERT(DestinationSize != NULL);
    ASSERT(ScratchSize != NULL);

    if (SourceSize < 8) {
        return RETURN_INVALID_PARAMETER;
    }

    const uint8_t *s = Source;

    /* Lese die ersten 8 Bytes byteweise (klein-endian erwartet) */
    uint32_t CompressedSize = (uint32_t) s[0] | ((uint32_t) s[1] << 8) |
                              ((uint32_t) s[2] << 16) | ((uint32_t) s[3] << 24);

    if (SourceSize < (CompressedSize + 8)) {
        return RETURN_INVALID_PARAMETER;
    }

    *ScratchSize = (uint32_t) sizeof(SCRATCH_DATA);
    *DestinationSize = (uint32_t) s[4] | ((uint32_t) s[5] << 8) |
                       ((uint32_t) s[6] << 16) | ((uint32_t) s[7] << 24);

    return RETURN_SUCCESS;
}

/**
 Decompresses a compressed source buffer.

 Extracts decompressed data to its original form.
 This function is designed so that the decompression algorithm can be implemented
 withusing any memory services.  As a result, this function is not allowed to
 call any memory allocation services its implementation.  It is the caller's
 responsibility to allocate and free the Destination and Scratch buffers.
 If the compressed source data specified by Source is successfully decompressed
 into Destination, then RETURN_SUCCESS is returned.  If the compressed source data
 specified by Source is not a valid compressed data format,
 then RETURN_INVALID_PARAMETER is returned.

 If Source is NULL, then ASSERT().
 If Destination is NULL, then ASSERT().
 If the required scratch buffer size > 0 and Scratch is NULL, then ASSERT().

 @param  Source      The source buffer containing the compressed data.
 @param  Destination The destination buffer to store the decompressed data.
 @param  Scratch     A temporary scratch buffer that is used to perform the decompression.
 This is an optional parameter that may be NULL if the
 required scratch buffer size is 0.

 @retval  RETURN_SUCCESS Decompression completed successfully, and
 the uncompressed buffer is returned Destination.
 @retval  RETURN_INVALID_PARAMETER
 The source buffer specified by Source is corrupted
 (not a valid compressed format).
 **/
RETURN_STATUS UefiDecompress(const void *Source, void *Destination, void *Scratch) {
    ASSERT(Source != NULL);
    ASSERT(Destination != NULL);
    ASSERT(Scratch != NULL);

    const uint8_t *Src = Source;
    uint8_t *Dst = Destination;

    SCRATCH_DATA *Sd = (SCRATCH_DATA *) Scratch;

    uint32_t CompSize = Src[0] + (Src[1] << 8) + (Src[2] << 16) + (Src[3] << 24);
    uint32_t OrigSize = Src[4] + (Src[5] << 8) + (Src[6] << 16) + (Src[7] << 24);

    // If compressed file size is 0, return
    if (OrigSize == 0) {
        return RETURN_SUCCESS;
    }

    Src = Src + 8;
    memset(Sd, 0, sizeof(SCRATCH_DATA));

    // The length of the field 'Position Set Code Length Array Size' Block Header.
    // For UEFI 2.0 de/compression algorithm(Version 1), mPBit = 4
    Sd->mPBit = 4;
    Sd->mSrcBase = (uint8_t *) Src;
    Sd->mDstBase = Dst;

    // CompSize and OrigSize are caculated bytes
    Sd->mCompSize = CompSize;
    Sd->mOrigSize = OrigSize;

    // Fill the first BITBUFSIZ bits
    FillBuf(Sd, BITBUFSIZ);

    // Decompress it
    Decode(Sd);

    if (Sd->mBadTableFlag != 0) {
        // Something wrong with the source
        return RETURN_INVALID_PARAMETER;
    }

    return RETURN_SUCCESS;
}

void Usage(const char *appname) {
    printf("UEFI option ROM extractor and decompressor V1.0\n");
    printf("This program extracts and decompresses UEFI .rom files in their .efi files\n");
    printf("Usage: %s <In_File> <Out_File>\n\n", appname);
    printf("Copyright (C) 2014 - AnV Software, all rights reserved\n");
}

uint8_t GetEfiCompressedROM(const char *InFile, uint8_t Pci23, uint32_t *EFIIMGStart) {
    PCI_EXPANSION_ROM_HEADER PciRomHdr;
    FILE *InFptr;
    uint32_t ImageStart;
    EFI_PCI_EXPANSION_ROM_HEADER EfiRomHdr;
    PCI_DATA_STRUCTURE PciDs23;
    PCI_3_0_DATA_STRUCTURE PciDs30;

    // Open the input file
    if ((InFptr = fopen(InFile, "rb")) == NULL) {
        printf("Error opening file %s!\n", InFile);

        return 0;
    }
    // Go through the image and dump the header stuff for each
    uint32_t ImageCount = 0;
    for (;;) {
        // Save position in the file, since offsets in the headers are relative to the particular image.
        ImageStart = (uint32_t) ftell(InFptr);
        ImageCount++;

        // Read the option ROM header. Have to assume a raw binary image for now.
        if (fread(&PciRomHdr, sizeof (PciRomHdr), 1, InFptr) != 1) {
            printf("Failed to read PCI ROM header from file!\n");
            goto BailOut;
        }

        // Find PCI data structure
        if (fseek(InFptr, ImageStart + PciRomHdr.PcirOffset, SEEK_SET)) {
            printf("Failed to seek to PCI data structure!\n");
            goto BailOut;
        }

        // Read and dump the PCI data structure
        memset(&PciDs23, 0, sizeof (PciDs23));
        memset(&PciDs30, 0, sizeof (PciDs30));

        if (Pci23 == 1) {
            if (fread(&PciDs23, sizeof (PciDs23), 1, InFptr) != 1) {
                printf("Failed to read PCI data structure from file %s!\n", InFile);
                goto BailOut;
            }
        } else {
            if (fread(&PciDs30, sizeof (PciDs30), 1, InFptr) != 1) {
                printf("Failed to read PCI data structure from file %s!\n", InFile);
                goto BailOut;
            }
        }
        if ((PciDs23.CodeType == PCI_CODE_TYPE_EFI_IMAGE) || (PciDs30.CodeType == PCI_CODE_TYPE_EFI_IMAGE)) {
            // Re-read the header as an EFI ROM header, then dump more info
            if (fseek(InFptr, ImageStart, SEEK_SET)) {
                printf("Failed to re-seek to ROM header structure!\n");
                goto BailOut;
            }

            if (fread(&EfiRomHdr, sizeof (EfiRomHdr), 1, InFptr) != 1) {
                printf("Failed to read EFI PCI ROM header from file!\n");
                goto BailOut;
            }
        
            if (EfiRomHdr.CompressionType == EFI_PCI_EXPANSION_ROM_HEADER_COMPRESSED) {
                EFIIMGStart[0] = EfiRomHdr.EfiImageHeaderOffset + (unsigned) ImageStart;

                printf("Found compressed EFI ROM start at 0x%x\n", EFIIMGStart[0]);
                fclose(InFptr);
                return 1;

            }
            
            fclose(InFptr);
            printf("Found non-compressed EFI ROM start at 0x%x, exiting...\n", EfiRomHdr.EfiImageHeaderOffset + (unsigned) ImageStart);

            exit(-1);
        }

        if ((PciDs23.Indicator == INDICATOR_LAST) || (PciDs30.Indicator == INDICATOR_LAST)) {
            goto BailOut;
        }

        // Seek to the start of the next image
        if (Pci23 == 1) {
            if (fseek(InFptr, ImageStart + (PciDs23.ImageLength * 512), SEEK_SET)) {
                printf("Failed to seek to next image!\n");
                goto BailOut;
            }
        } else {
            if (fseek(InFptr, ImageStart + (PciDs30.ImageLength * 512), SEEK_SET)) {
                printf("Failed to seek to next image!\n");
                goto BailOut;
            }
        }
    }

BailOut:
    printf("No compressed EFI ROM found!\n");
    fclose(InFptr);
    EFIIMGStart[0] = 0;
    return 0;
}

int main(int argc, const char *argv[]) {
    long fInSize = 0;
    long fROMStart = 0;
    long fOutSize = 0;
    long ScratchSize = 0;

    if (argc != 3) {
        Usage(argv[0]);
        return 1;
    }

    FILE *fIn = fopen(argv[1], "rb");
    fseek(fIn, 0, SEEK_END);
    fInSize = ftell(fIn);
    fseek(fIn, 0, SEEK_SET);

    void *Buffer = malloc(fInSize);

    if (Buffer == NULL) {
        printf("Input buffer allocation failed!\n");

        return -1;
    }

    fread(Buffer, fInSize, 1, fIn);
    fclose(fIn);

    if (!GetEfiCompressedROM(argv[1], 0, (uint32_t *) &fROMStart)) {
        if (!GetEfiCompressedROM(argv[1], 1, (uint32_t *) &fROMStart)) {
            printf("Not an EFI ROM file, attempting decompression of data directly...\n");
        }
    }

    if (fROMStart > 0) {
        void *ROMBuffer = malloc(fInSize - fROMStart);

        if (ROMBuffer == NULL) {
            printf("Could not allocation new ROM buffer!\n");
            free(Buffer);

            return -2;
        }

        memcpy(ROMBuffer, (uint8_t *) Buffer + fROMStart, fInSize - fROMStart);
        free(Buffer);

        Buffer = ROMBuffer;
        fInSize -= fROMStart;
    }

    if (UefiDecompressGetInfo(Buffer, (uint32_t) fInSize, (uint32_t *) &fOutSize, (uint32_t *) &ScratchSize)) {
        printf("get UEFI decompression info failed!\n");
        free(Buffer);

        return -3;
    }

    printf("Input size: %lu, Output size: %lu, Scratch size: %lu\n", fInSize, fOutSize, ScratchSize);

    if (fOutSize <= 0) {
        printf("Incorrect output size!\n");
        free(Buffer);

        return -4;
    }

    if (ScratchSize <= 0) {
        printf("Incorrect scratch buffer size!\n");
        free(Buffer);

        return -5;
    }

    void *ScratchBuffer = malloc(ScratchSize);

    if (ScratchBuffer == NULL) {
        printf("Scratch buffer allocation failed!\n");
        free(Buffer);

        return -6;
    }

    void *OutBuffer = malloc(fOutSize);

    if (OutBuffer == NULL) {
        printf("Output buffer buffer allocation failed!\n");
        free(Buffer);
        free(ScratchBuffer);

        return -7;
    }

    if (UefiDecompress(Buffer, OutBuffer, ScratchBuffer)) {
        printf("UEFI decompression failed!\n");
        free(Buffer);
        free(OutBuffer);
        free(ScratchBuffer);

        return -8;
    }

    FILE *fOut = fopen(argv[2], "wb");
    fwrite(OutBuffer, fOutSize, 1, fOut);
    fclose(fOut);

    free(Buffer);
    free(OutBuffer);
    free(ScratchBuffer);

    return 0;
}
