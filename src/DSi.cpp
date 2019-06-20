/*
    Copyright 2016-2019 Arisotura

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#include <stdio.h>
#include <string.h>
#include "NDS.h"
#include "DSi.h"
#include "ARM.h"
#include "GPU.h"
#include "Platform.h"

#include "DSi_NDMA.h"
#include "DSi_I2C.h"
#include "DSi_SD.h"
#include "DSi_AES.h"


namespace NDS
{

extern ARMv5* ARM9;
extern ARMv4* ARM7;

}


namespace DSi
{

u32 BootAddr[2];

u32 MBK[2][9];

u8 NWRAM_A[0x40000];
u8 NWRAM_B[0x40000];
u8 NWRAM_C[0x40000];

u8* NWRAMMap_A[2][4];
u8* NWRAMMap_B[3][8];
u8* NWRAMMap_C[3][8];

u32 NWRAMStart[2][3];
u32 NWRAMEnd[2][3];
u32 NWRAMMask[2][3];

u32 NDMACnt[2];
DSi_NDMA* NDMAs[8];

DSi_SDHost* SDMMC;
DSi_SDHost* SDIO;

u64 ConsoleID;
u8 eMMC_CID[16];

u8 ITCMInit[0x8000];


bool Init()
{
    if (!DSi_I2C::Init()) return false;
    if (!DSi_AES::Init()) return false;

    NDMAs[0] = new DSi_NDMA(0, 0);
    NDMAs[1] = new DSi_NDMA(0, 1);
    NDMAs[2] = new DSi_NDMA(0, 2);
    NDMAs[3] = new DSi_NDMA(0, 3);
    NDMAs[4] = new DSi_NDMA(1, 0);
    NDMAs[5] = new DSi_NDMA(1, 1);
    NDMAs[6] = new DSi_NDMA(1, 2);
    NDMAs[7] = new DSi_NDMA(1, 3);

    SDMMC = new DSi_SDHost(0);
    SDIO = new DSi_SDHost(1);

    return true;
}

void DeInit()
{
    DSi_I2C::DeInit();
    DSi_AES::DeInit();

    for (int i = 0; i < 8; i++) delete NDMAs[i];

    delete SDMMC;
    delete SDIO;
}

void Reset()
{
    //NDS::ARM9->CP15Write(0x910, 0x0D00000A);
    //NDS::ARM9->CP15Write(0x911, 0x00000020);
    //NDS::ARM9->CP15Write(0x100, NDS::ARM9->CP15Read(0x100) | 0x00050000);

    NDS::ARM9->JumpTo(BootAddr[0]);
    NDS::ARM7->JumpTo(BootAddr[1]);

    NDMACnt[0] = 0; NDMACnt[1] = 0;
    for (int i = 0; i < 8; i++) NDMAs[i]->Reset();

    memcpy(NDS::ARM9->ITCM, ITCMInit, 0x8000);

    DSi_I2C::Reset();
    DSi_AES::Reset();

    SDMMC->Reset();
    SDIO->Reset();

    // LCD init flag
    GPU::DispStat[0] |= (1<<6);
    GPU::DispStat[1] |= (1<<6);
}

bool LoadBIOS()
{
    FILE* f;
    u32 i;

    f = Platform::OpenLocalFile("bios9i.bin", "rb");
    if (!f)
    {
        printf("ARM9i BIOS not found\n");

        for (i = 0; i < 16; i++)
            ((u32*)NDS::ARM9BIOS)[i] = 0xE7FFDEFF;
    }
    else
    {
        fseek(f, 0, SEEK_SET);
        fread(NDS::ARM9BIOS, 0x10000, 1, f);

        printf("ARM9i BIOS loaded\n");
        fclose(f);
    }

    f = Platform::OpenLocalFile("bios7i.bin", "rb");
    if (!f)
    {
        printf("ARM7i BIOS not found\n");

        for (i = 0; i < 16; i++)
            ((u32*)NDS::ARM7BIOS)[i] = 0xE7FFDEFF;
    }
    else
    {
        // TODO: check if the first 32 bytes are crapoed

        fseek(f, 0, SEEK_SET);
        fread(NDS::ARM7BIOS, 0x10000, 1, f);

        printf("ARM7i BIOS loaded\n");
        fclose(f);
    }

    // herp
    *(u32*)&NDS::ARM9BIOS[0] = 0xEAFFFFFE;
    *(u32*)&NDS::ARM7BIOS[0] = 0xEAFFFFFE;

    return true;
}

bool LoadNAND()
{
    printf("Loading DSi NAND\n");

    memset(NWRAM_A, 0, 0x40000);
    memset(NWRAM_B, 0, 0x40000);
    memset(NWRAM_C, 0, 0x40000);

    memset(MBK, 0, sizeof(MBK));
    memset(NWRAMMap_A, 0, sizeof(NWRAMMap_A));
    memset(NWRAMMap_B, 0, sizeof(NWRAMMap_B));
    memset(NWRAMMap_C, 0, sizeof(NWRAMMap_C));
    memset(NWRAMStart, 0, sizeof(NWRAMStart));
    memset(NWRAMEnd, 0, sizeof(NWRAMEnd));
    memset(NWRAMMask, 0, sizeof(NWRAMMask));

    FILE* f = Platform::OpenLocalFile("nand.bin", "rb");
    if (f)
    {
        u32 bootparams[8];
        fseek(f, 0x220, SEEK_SET);
        fread(bootparams, 4, 8, f);

        printf("ARM9: offset=%08X size=%08X RAM=%08X size_aligned=%08X\n",
               bootparams[0], bootparams[1], bootparams[2], bootparams[3]);
        printf("ARM7: offset=%08X size=%08X RAM=%08X size_aligned=%08X\n",
               bootparams[4], bootparams[5], bootparams[6], bootparams[7]);

        // read and apply new-WRAM settings

        MBK[0][8] = 0;
        MBK[1][8] = 0;

        u32 mbk[12];
        fseek(f, 0x380, SEEK_SET);
        fread(mbk, 4, 12, f);

        MapNWRAM_A(0, mbk[0] & 0xFF);
        MapNWRAM_A(1, (mbk[0] >> 8) & 0xFF);
        MapNWRAM_A(2, (mbk[0] >> 16) & 0xFF);
        MapNWRAM_A(3, mbk[0] >> 24);

        MapNWRAM_B(0, mbk[1] & 0xFF);
        MapNWRAM_B(1, (mbk[1] >> 8) & 0xFF);
        MapNWRAM_B(2, (mbk[1] >> 16) & 0xFF);
        MapNWRAM_B(3, mbk[1] >> 24);
        MapNWRAM_B(4, mbk[2] & 0xFF);
        MapNWRAM_B(5, (mbk[2] >> 8) & 0xFF);
        MapNWRAM_B(6, (mbk[2] >> 16) & 0xFF);
        MapNWRAM_B(7, mbk[2] >> 24);

        MapNWRAM_C(0, mbk[3] & 0xFF);
        MapNWRAM_C(1, (mbk[3] >> 8) & 0xFF);
        MapNWRAM_C(2, (mbk[3] >> 16) & 0xFF);
        MapNWRAM_C(3, mbk[3] >> 24);
        MapNWRAM_C(4, mbk[4] & 0xFF);
        MapNWRAM_C(5, (mbk[4] >> 8) & 0xFF);
        MapNWRAM_C(6, (mbk[4] >> 16) & 0xFF);
        MapNWRAM_C(7, mbk[4] >> 24);

        MapNWRAMRange(0, 0, mbk[5]);
        MapNWRAMRange(0, 1, mbk[6]);
        MapNWRAMRange(0, 2, mbk[7]);

        MapNWRAMRange(1, 0, mbk[8]);
        MapNWRAMRange(1, 1, mbk[9]);
        MapNWRAMRange(1, 2, mbk[10]);

        // TODO: find out why it is 0xFF000000
        mbk[11] &= 0x00FFFF0F;
        MBK[0][8] = mbk[11];
        MBK[1][8] = mbk[11];

        // load binaries
        // TODO: optionally support loading from actual NAND?
        // currently decrypted binaries have to be provided
        // they can be decrypted with twltool

        FILE* bin;

        bin = Platform::OpenLocalFile("boot2_9.bin", "rb");
        if (bin)
        {
            u32 dstaddr = bootparams[2];
            for (u32 i = 0; i < bootparams[1]; i += 4)
            {
                u32 _tmp;
                fread(&_tmp, 4, 1, bin);
                ARM9Write32(dstaddr, _tmp);
                dstaddr += 4;
            }

            fclose(bin);
        }
        else
        {
            printf("ARM9 boot2 not found\n");
        }

        bin = Platform::OpenLocalFile("boot2_7.bin", "rb");
        if (bin)
        {
            u32 dstaddr = bootparams[6];
            for (u32 i = 0; i < bootparams[5]; i += 4)
            {
                u32 _tmp;
                fread(&_tmp, 4, 1, bin);
                ARM7Write32(dstaddr, _tmp);
                dstaddr += 4;
            }

            fclose(bin);
        }
        else
        {
            printf("ARM7 boot2 not found\n");
        }

        // repoint CPUs to the boot2 binaries

        BootAddr[0] = bootparams[2];
        BootAddr[1] = bootparams[6];

#define printhex(str, size) { for (int z = 0; z < (size); z++) printf("%02X", (str)[z]); printf("\n"); }
#define printhex_rev(str, size) { for (int z = (size)-1; z >= 0; z--) printf("%02X", (str)[z]); printf("\n"); }

        fseek(f, 0xF000010, SEEK_SET);
        fread(eMMC_CID, 1, 16, f);
        fread(&ConsoleID, 1, 8, f);

        printf("eMMC CID: "); printhex(eMMC_CID, 16);
        printf("Console ID: %llx\n", ConsoleID);

        fclose(f);
    }

    memset(ITCMInit, 0, 0x8000);

    f = fopen("dsikeys.bin", "rb");
    if (f)
    {
        // first 0x2524 bytes are loaded to 0x01FFC400

        u32 dstaddr = 0x01FFC400;
        fread(&ITCMInit[dstaddr & 0x7FFF], 0x2524, 1, f);
        fclose(f);
    }
    else
    {
        printf("DSi keys not found\n");
    }

    return true;
}


void RunNDMAs(u32 cpu)
{
    // TODO: round-robin mode (requires DMA channels to have a subblock delay set)

    if (cpu == 0)
    {
        if (NDS::ARM9Timestamp >= NDS::ARM9Target) return;

        if (!(NDS::CPUStop & 0x80000000)) NDMAs[0]->Run();
        if (!(NDS::CPUStop & 0x80000000)) NDMAs[1]->Run();
        if (!(NDS::CPUStop & 0x80000000)) NDMAs[2]->Run();
        if (!(NDS::CPUStop & 0x80000000)) NDMAs[3]->Run();
    }
    else
    {
        if (NDS::ARM7Timestamp >= NDS::ARM7Target) return;

        NDMAs[4]->Run();
        NDMAs[5]->Run();
        NDMAs[6]->Run();
        NDMAs[7]->Run();
    }
}

void StallNDMAs()
{
    // TODO
}

bool NDMAsRunning(u32 cpu)
{
    cpu <<= 2;
    if (NDMAs[cpu+0]->IsRunning()) return true;
    if (NDMAs[cpu+1]->IsRunning()) return true;
    if (NDMAs[cpu+2]->IsRunning()) return true;
    if (NDMAs[cpu+3]->IsRunning()) return true;
    return false;
}

void CheckNDMAs(u32 cpu, u32 mode)
{
    cpu <<= 2;
    NDMAs[cpu+0]->StartIfNeeded(mode);
    NDMAs[cpu+1]->StartIfNeeded(mode);
    NDMAs[cpu+2]->StartIfNeeded(mode);
    NDMAs[cpu+3]->StartIfNeeded(mode);
}

void StopNDMAs(u32 cpu, u32 mode)
{
    cpu <<= 2;
    NDMAs[cpu+0]->StopIfNeeded(mode);
    NDMAs[cpu+1]->StopIfNeeded(mode);
    NDMAs[cpu+2]->StopIfNeeded(mode);
    NDMAs[cpu+3]->StopIfNeeded(mode);
}


// new WRAM mapping
// TODO: find out what happens upon overlapping slots!!

void MapNWRAM_A(u32 num, u8 val)
{
    if (MBK[0][8] & (1 << num))
    {
        printf("trying to map NWRAM_A %d to %02X, but it is write-protected (%08X)\n", num, val, MBK[0][8]);
        return;
    }

    int mbkn = 0, mbks = 8*num;

    u8 oldval = (MBK[0][mbkn] >> mbks) & 0xFF;
    if (oldval == val) return;

    MBK[0][mbkn] &= ~(0xFF << mbks);
    MBK[0][mbkn] |= (val << mbks);
    MBK[1][mbkn] = MBK[0][mbkn];

    u8* ptr = &NWRAM_A[num << 16];

    if (oldval & 0x80)
    {
        if (NWRAMMap_A[oldval & 0x01][(oldval >> 2) & 0x3] == ptr)
            NWRAMMap_A[oldval & 0x01][(oldval >> 2) & 0x3] = NULL;
    }

    if (val & 0x80)
    {
        NWRAMMap_A[val & 0x01][(val >> 2) & 0x3] = ptr;
    }
}

void MapNWRAM_B(u32 num, u8 val)
{
    if (MBK[0][8] & (1 << (8+num)))
    {
        printf("trying to map NWRAM_B %d to %02X, but it is write-protected (%08X)\n", num, val, MBK[0][8]);
        return;
    }

    int mbkn = 1+(num>>2), mbks = 8*(num&3);

    u8 oldval = (MBK[0][mbkn] >> mbks) & 0xFF;
    if (oldval == val) return;

    MBK[0][mbkn] &= ~(0xFF << mbks);
    MBK[0][mbkn] |= (val << mbks);
    MBK[1][mbkn] = MBK[0][mbkn];

    u8* ptr = &NWRAM_B[num << 15];

    if (oldval & 0x80)
    {
        if (oldval & 0x02) oldval &= 0xFE;

        if (NWRAMMap_B[oldval & 0x03][(oldval >> 2) & 0x7] == ptr)
            NWRAMMap_B[oldval & 0x03][(oldval >> 2) & 0x7] = NULL;
    }

    if (val & 0x80)
    {
        if (val & 0x02) val &= 0xFE;

        NWRAMMap_B[val & 0x03][(val >> 2) & 0x7] = ptr;
    }
}

void MapNWRAM_C(u32 num, u8 val)
{
    if (MBK[0][8] & (1 << (16+num)))
    {
        printf("trying to map NWRAM_C %d to %02X, but it is write-protected (%08X)\n", num, val, MBK[0][8]);
        return;
    }

    int mbkn = 3+(num>>2), mbks = 8*(num&3);

    u8 oldval = (MBK[0][mbkn] >> mbks) & 0xFF;
    if (oldval == val) return;

    MBK[0][mbkn] &= ~(0xFF << mbks);
    MBK[0][mbkn] |= (val << mbks);
    MBK[1][mbkn] = MBK[0][mbkn];

    u8* ptr = &NWRAM_C[num << 15];

    if (oldval & 0x80)
    {
        if (oldval & 0x02) oldval &= 0xFE;

        if (NWRAMMap_C[oldval & 0x03][(oldval >> 2) & 0x7] == ptr)
            NWRAMMap_C[oldval & 0x03][(oldval >> 2) & 0x7] = NULL;
    }

    if (val & 0x80)
    {
        if (val & 0x02) val &= 0xFE;

        NWRAMMap_C[val & 0x03][(val >> 2) & 0x7] = ptr;
    }
}

void MapNWRAMRange(u32 cpu, u32 num, u32 val)
{
    u32 oldval = MBK[cpu][5+num];
    if (oldval == val) return;

    MBK[cpu][5+num] = val;

    // TODO: what happens when the ranges are 'out of range'????
    if (num == 0)
    {
        u32 start = 0x03000000 + (((val >> 4) & 0xFF) << 16);
        u32 end   = 0x03000000 + (((val >> 20) & 0x1FF) << 16);
        u32 size  = (val >> 12) & 0x3;

        printf("NWRAM-A: ARM%d range %08X-%08X, size %d\n", cpu?7:9, start, end, size);

        NWRAMStart[cpu][num] = start;
        NWRAMEnd[cpu][num] = end;

        switch (size)
        {
        case 0:
        case 1: NWRAMMask[cpu][num] = 0x0; break;
        case 2: NWRAMMask[cpu][num] = 0x1; break; // CHECKME
        case 3: NWRAMMask[cpu][num] = 0x3; break;
        }
    }
    else
    {
        u32 start = 0x03000000 + (((val >> 3) & 0x1FF) << 15);
        u32 end   = 0x03000000 + (((val >> 19) & 0x3FF) << 15);
        u32 size  = (val >> 12) & 0x3;

        printf("NWRAM-%c: ARM%d range %08X-%08X, size %d\n", 'A'+num, cpu?7:9, start, end, size);

        NWRAMStart[cpu][num] = start;
        NWRAMEnd[cpu][num] = end;

        switch (size)
        {
        case 0: NWRAMMask[cpu][num] = 0x0; break;
        case 1: NWRAMMask[cpu][num] = 0x1; break;
        case 2: NWRAMMask[cpu][num] = 0x3; break;
        case 3: NWRAMMask[cpu][num] = 0x7; break;
        }
    }
}


u8 ARM9Read8(u32 addr)
{
    switch (addr & 0xFF000000)
    {
    case 0x03000000:
        if (addr >= NWRAMStart[0][0] && addr < NWRAMEnd[0][0])
        {
            u8* ptr = NWRAMMap_A[0][(addr >> 16) & NWRAMMask[0][0]];
            return ptr ? *(u8*)&ptr[addr & 0xFFFF] : 0;
        }
        if (addr >= NWRAMStart[0][1] && addr < NWRAMEnd[0][1])
        {
            u8* ptr = NWRAMMap_B[0][(addr >> 15) & NWRAMMask[0][1]];
            return ptr ? *(u8*)&ptr[addr & 0x7FFF] : 0;
        }
        if (addr >= NWRAMStart[0][2] && addr < NWRAMEnd[0][2])
        {
            u8* ptr = NWRAMMap_C[0][(addr >> 15) & NWRAMMask[0][2]];
            return ptr ? *(u8*)&ptr[addr & 0x7FFF] : 0;
        }
        return NDS::ARM9Read8(addr);

    case 0x04000000:
        return ARM9IORead8(addr);
    }

    return NDS::ARM9Read8(addr);
}

u16 ARM9Read16(u32 addr)
{
    switch (addr & 0xFF000000)
    {
    case 0x03000000:
        if (addr >= NWRAMStart[0][0] && addr < NWRAMEnd[0][0])
        {
            u8* ptr = NWRAMMap_A[0][(addr >> 16) & NWRAMMask[0][0]];
            return ptr ? *(u16*)&ptr[addr & 0xFFFF] : 0;
        }
        if (addr >= NWRAMStart[0][1] && addr < NWRAMEnd[0][1])
        {
            u8* ptr = NWRAMMap_B[0][(addr >> 15) & NWRAMMask[0][1]];
            return ptr ? *(u16*)&ptr[addr & 0x7FFF] : 0;
        }
        if (addr >= NWRAMStart[0][2] && addr < NWRAMEnd[0][2])
        {
            u8* ptr = NWRAMMap_C[0][(addr >> 15) & NWRAMMask[0][2]];
            return ptr ? *(u16*)&ptr[addr & 0x7FFF] : 0;
        }
        return NDS::ARM9Read16(addr);

    case 0x04000000:
        return ARM9IORead16(addr);
    }

    return NDS::ARM9Read16(addr);
}

u32 ARM9Read32(u32 addr)
{
    switch (addr & 0xFF000000)
    {
    case 0x03000000:
        if (addr >= NWRAMStart[0][0] && addr < NWRAMEnd[0][0])
        {
            u8* ptr = NWRAMMap_A[0][(addr >> 16) & NWRAMMask[0][0]];
            return ptr ? *(u32*)&ptr[addr & 0xFFFF] : 0;
        }
        if (addr >= NWRAMStart[0][1] && addr < NWRAMEnd[0][1])
        {
            u8* ptr = NWRAMMap_B[0][(addr >> 15) & NWRAMMask[0][1]];
            return ptr ? *(u32*)&ptr[addr & 0x7FFF] : 0;
        }
        if (addr >= NWRAMStart[0][2] && addr < NWRAMEnd[0][2])
        {
            u8* ptr = NWRAMMap_C[0][(addr >> 15) & NWRAMMask[0][2]];
            return ptr ? *(u32*)&ptr[addr & 0x7FFF] : 0;
        }
        return NDS::ARM9Read32(addr);

    case 0x04000000:
        return ARM9IORead32(addr);
    }

    return NDS::ARM9Read32(addr);
}

void ARM9Write8(u32 addr, u8 val)
{
    switch (addr & 0xFF000000)
    {
    case 0x03000000:
        if (addr >= NWRAMStart[0][0] && addr < NWRAMEnd[0][0])
        {
            u8* ptr = NWRAMMap_A[0][(addr >> 16) & NWRAMMask[0][0]];
            if (ptr) *(u8*)&ptr[addr & 0xFFFF] = val;
            return;
        }
        if (addr >= NWRAMStart[0][1] && addr < NWRAMEnd[0][1])
        {
            u8* ptr = NWRAMMap_B[0][(addr >> 15) & NWRAMMask[0][1]];
            if (ptr) *(u8*)&ptr[addr & 0x7FFF] = val;
            return;
        }
        if (addr >= NWRAMStart[0][2] && addr < NWRAMEnd[0][2])
        {
            u8* ptr = NWRAMMap_C[0][(addr >> 15) & NWRAMMask[0][2]];
            if (ptr) *(u8*)&ptr[addr & 0x7FFF] = val;
            return;
        }
        return NDS::ARM9Write8(addr, val);

    case 0x04000000:
        ARM9IOWrite8(addr, val);
        return;
    }

    return NDS::ARM9Write8(addr, val);
}

void ARM9Write16(u32 addr, u16 val)
{
    switch (addr & 0xFF000000)
    {
    case 0x03000000:
        if (addr >= NWRAMStart[0][0] && addr < NWRAMEnd[0][0])
        {
            u8* ptr = NWRAMMap_A[0][(addr >> 16) & NWRAMMask[0][0]];
            if (ptr) *(u16*)&ptr[addr & 0xFFFF] = val;
            return;
        }
        if (addr >= NWRAMStart[0][1] && addr < NWRAMEnd[0][1])
        {
            u8* ptr = NWRAMMap_B[0][(addr >> 15) & NWRAMMask[0][1]];
            if (ptr) *(u16*)&ptr[addr & 0x7FFF] = val;
            return;
        }
        if (addr >= NWRAMStart[0][2] && addr < NWRAMEnd[0][2])
        {
            u8* ptr = NWRAMMap_C[0][(addr >> 15) & NWRAMMask[0][2]];
            if (ptr) *(u16*)&ptr[addr & 0x7FFF] = val;
            return;
        }
        return NDS::ARM9Write16(addr, val);

    case 0x04000000:
        ARM9IOWrite16(addr, val);
        return;
    }

    return NDS::ARM9Write16(addr, val);
}

void ARM9Write32(u32 addr, u32 val)
{
    switch (addr & 0xFF000000)
    {
    case 0x03000000:
        if (addr >= NWRAMStart[0][0] && addr < NWRAMEnd[0][0])
        {
            u8* ptr = NWRAMMap_A[0][(addr >> 16) & NWRAMMask[0][0]];
            if (ptr) *(u32*)&ptr[addr & 0xFFFF] = val;
            return;
        }
        if (addr >= NWRAMStart[0][1] && addr < NWRAMEnd[0][1])
        {
            u8* ptr = NWRAMMap_B[0][(addr >> 15) & NWRAMMask[0][1]];
            if (ptr) *(u32*)&ptr[addr & 0x7FFF] = val;
            return;
        }
        if (addr >= NWRAMStart[0][2] && addr < NWRAMEnd[0][2])
        {
            u8* ptr = NWRAMMap_C[0][(addr >> 15) & NWRAMMask[0][2]];
            if (ptr) *(u32*)&ptr[addr & 0x7FFF] = val;
            return;
        }
        return NDS::ARM9Write32(addr, val);

    case 0x04000000:
        ARM9IOWrite32(addr, val);
        return;
    }

    return NDS::ARM9Write32(addr, val);
}

bool ARM9GetMemRegion(u32 addr, bool write, NDS::MemRegion* region)
{
    switch (addr & 0xFF000000)
    {
    case 0x02000000:
        region->Mem = NDS::MainRAM;
        region->Mask = MAIN_RAM_SIZE-1;
        return true;
    }

    if ((addr & 0xFFFF0000) == 0xFFFF0000 && !write)
    {
        region->Mem = NDS::ARM9BIOS;
        region->Mask = 0xFFFF;
        return true;
    }

    region->Mem = NULL;
    return false;
}



u8 ARM7Read8(u32 addr)
{
    switch (addr & 0xFF800000)
    {
    case 0x03000000:
        if (addr >= NWRAMStart[1][0] && addr < NWRAMEnd[1][0])
        {
            u8* ptr = NWRAMMap_A[1][(addr >> 16) & NWRAMMask[1][0]];
            return ptr ? *(u8*)&ptr[addr & 0xFFFF] : 0;
        }
        if (addr >= NWRAMStart[1][1] && addr < NWRAMEnd[1][1])
        {
            u8* ptr = NWRAMMap_B[1][(addr >> 15) & NWRAMMask[1][1]];
            return ptr ? *(u8*)&ptr[addr & 0x7FFF] : 0;
        }
        if (addr >= NWRAMStart[1][2] && addr < NWRAMEnd[1][2])
        {
            u8* ptr = NWRAMMap_C[1][(addr >> 15) & NWRAMMask[1][2]];
            return ptr ? *(u8*)&ptr[addr & 0x7FFF] : 0;
        }
        return NDS::ARM7Read8(addr);

    case 0x04000000:
        return ARM7IORead8(addr);
    }

    return NDS::ARM7Read8(addr);
}

u16 ARM7Read16(u32 addr)
{
    switch (addr & 0xFF800000)
    {
    case 0x03000000:
        if (addr >= NWRAMStart[1][0] && addr < NWRAMEnd[1][0])
        {
            u8* ptr = NWRAMMap_A[1][(addr >> 16) & NWRAMMask[1][0]];
            return ptr ? *(u16*)&ptr[addr & 0xFFFF] : 0;
        }
        if (addr >= NWRAMStart[1][1] && addr < NWRAMEnd[1][1])
        {
            u8* ptr = NWRAMMap_B[1][(addr >> 15) & NWRAMMask[1][1]];
            return ptr ? *(u16*)&ptr[addr & 0x7FFF] : 0;
        }
        if (addr >= NWRAMStart[1][2] && addr < NWRAMEnd[1][2])
        {
            u8* ptr = NWRAMMap_C[1][(addr >> 15) & NWRAMMask[1][2]];
            return ptr ? *(u16*)&ptr[addr & 0x7FFF] : 0;
        }
        return NDS::ARM7Read16(addr);

    case 0x04000000:
        return ARM7IORead16(addr);
    }

    return NDS::ARM7Read16(addr);
}

u32 ARM7Read32(u32 addr)
{
    switch (addr & 0xFF800000)
    {
    case 0x03000000:
        if (addr >= NWRAMStart[1][0] && addr < NWRAMEnd[1][0])
        {
            u8* ptr = NWRAMMap_A[1][(addr >> 16) & NWRAMMask[1][0]];
            return ptr ? *(u32*)&ptr[addr & 0xFFFF] : 0;
        }
        if (addr >= NWRAMStart[1][1] && addr < NWRAMEnd[1][1])
        {
            u8* ptr = NWRAMMap_B[1][(addr >> 15) & NWRAMMask[1][1]];
            return ptr ? *(u32*)&ptr[addr & 0x7FFF] : 0;
        }
        if (addr >= NWRAMStart[1][2] && addr < NWRAMEnd[1][2])
        {
            u8* ptr = NWRAMMap_C[1][(addr >> 15) & NWRAMMask[1][2]];
            return ptr ? *(u32*)&ptr[addr & 0x7FFF] : 0;
        }
        return NDS::ARM7Read32(addr);

    case 0x04000000:
        return ARM7IORead32(addr);
    }

    return NDS::ARM7Read32(addr);
}

void ARM7Write8(u32 addr, u8 val)
{
    switch (addr & 0xFF800000)
    {
    case 0x03000000:
        if (addr >= NWRAMStart[1][0] && addr < NWRAMEnd[1][0])
        {
            u8* ptr = NWRAMMap_A[1][(addr >> 16) & NWRAMMask[1][0]];
            if (ptr) *(u8*)&ptr[addr & 0xFFFF] = val;
            return;
        }
        if (addr >= NWRAMStart[1][1] && addr < NWRAMEnd[1][1])
        {
            u8* ptr = NWRAMMap_B[1][(addr >> 15) & NWRAMMask[1][1]];
            if (ptr) *(u8*)&ptr[addr & 0x7FFF] = val;
            return;
        }
        if (addr >= NWRAMStart[1][2] && addr < NWRAMEnd[1][2])
        {
            u8* ptr = NWRAMMap_C[1][(addr >> 15) & NWRAMMask[1][2]];
            if (ptr) *(u8*)&ptr[addr & 0x7FFF] = val;
            return;
        }
        return NDS::ARM7Write8(addr, val);

    case 0x04000000:
        ARM7IOWrite8(addr, val);
        return;
    }

    return NDS::ARM7Write8(addr, val);
}

void ARM7Write16(u32 addr, u16 val)
{
    switch (addr & 0xFF800000)
    {
    case 0x03000000:
        if (addr >= NWRAMStart[1][0] && addr < NWRAMEnd[1][0])
        {
            u8* ptr = NWRAMMap_A[1][(addr >> 16) & NWRAMMask[1][0]];
            if (ptr) *(u16*)&ptr[addr & 0xFFFF] = val;
            return;
        }
        if (addr >= NWRAMStart[1][1] && addr < NWRAMEnd[1][1])
        {
            u8* ptr = NWRAMMap_B[1][(addr >> 15) & NWRAMMask[1][1]];
            if (ptr) *(u16*)&ptr[addr & 0x7FFF] = val;
            return;
        }
        if (addr >= NWRAMStart[1][2] && addr < NWRAMEnd[1][2])
        {
            u8* ptr = NWRAMMap_C[1][(addr >> 15) & NWRAMMask[1][2]];
            if (ptr) *(u16*)&ptr[addr & 0x7FFF] = val;
            return;
        }
        return NDS::ARM7Write16(addr, val);

    case 0x04000000:
        ARM7IOWrite16(addr, val);
        return;
    }

    return NDS::ARM7Write16(addr, val);
}

void ARM7Write32(u32 addr, u32 val)
{
    switch (addr & 0xFF800000)
    {
    case 0x03000000:
        if (addr >= NWRAMStart[1][0] && addr < NWRAMEnd[1][0])
        {
            u8* ptr = NWRAMMap_A[1][(addr >> 16) & NWRAMMask[1][0]];
            if (ptr) *(u32*)&ptr[addr & 0xFFFF] = val;
            return;
        }
        if (addr >= NWRAMStart[1][1] && addr < NWRAMEnd[1][1])
        {
            u8* ptr = NWRAMMap_B[1][(addr >> 15) & NWRAMMask[1][1]];
            if (ptr) *(u32*)&ptr[addr & 0x7FFF] = val;
            return;
        }
        if (addr >= NWRAMStart[1][2] && addr < NWRAMEnd[1][2])
        {
            u8* ptr = NWRAMMap_C[1][(addr >> 15) & NWRAMMask[1][2]];
            if (ptr) *(u32*)&ptr[addr & 0x7FFF] = val;
            return;
        }
        return NDS::ARM7Write32(addr, val);

    case 0x04000000:
        ARM7IOWrite32(addr, val);
        return;
    }

    return NDS::ARM7Write32(addr, val);
}

bool ARM7GetMemRegion(u32 addr, bool write, NDS::MemRegion* region)
{
    switch (addr & 0xFF800000)
    {
    case 0x02000000:
    case 0x02800000:
        region->Mem = NDS::MainRAM;
        region->Mask = MAIN_RAM_SIZE-1;
        return true;
    }

    // BIOS. ARM7 PC has to be within range.
    /*if (addr < 0x00010000 && !write)
    {
        if (NDS::ARM7->R[15] < 0x00010000 && (addr >= NDS::ARM7BIOSProt || NDS::ARM7->R[15] < NDS::ARM7BIOSProt))
        {
            region->Mem = NDS::ARM7BIOS;
            region->Mask = 0xFFFF;
            return true;
        }
    }*/

    region->Mem = NULL;
    return false;
}




#define CASE_READ8_16BIT(addr, val) \
    case (addr): return (val) & 0xFF; \
    case (addr+1): return (val) >> 8;

#define CASE_READ8_32BIT(addr, val) \
    case (addr): return (val) & 0xFF; \
    case (addr+1): return ((val) >> 8) & 0xFF; \
    case (addr+2): return ((val) >> 16) & 0xFF; \
    case (addr+3): return (val) >> 24;

#define CASE_READ16_32BIT(addr, val) \
    case (addr): return (val) & 0xFFFF; \
    case (addr+2): return (val) >> 16;

u8 ARM9IORead8(u32 addr)
{
    switch (addr)
    {
    case 0x04004000: return 1;

    CASE_READ8_32BIT(0x04004040, MBK[0][0])
    CASE_READ8_32BIT(0x04004044, MBK[0][1])
    CASE_READ8_32BIT(0x04004048, MBK[0][2])
    CASE_READ8_32BIT(0x0400404C, MBK[0][3])
    CASE_READ8_32BIT(0x04004050, MBK[0][4])
    CASE_READ8_32BIT(0x04004054, MBK[0][5])
    CASE_READ8_32BIT(0x04004058, MBK[0][6])
    CASE_READ8_32BIT(0x0400405C, MBK[0][7])
    CASE_READ8_32BIT(0x04004060, MBK[0][8])
    }

    return NDS::ARM9IORead8(addr);
}

u16 ARM9IORead16(u32 addr)
{
    switch (addr)
    {
    case 0x04004004: return 0; // TODO

    CASE_READ16_32BIT(0x04004040, MBK[0][0])
    CASE_READ16_32BIT(0x04004044, MBK[0][1])
    CASE_READ16_32BIT(0x04004048, MBK[0][2])
    CASE_READ16_32BIT(0x0400404C, MBK[0][3])
    CASE_READ16_32BIT(0x04004050, MBK[0][4])
    CASE_READ16_32BIT(0x04004054, MBK[0][5])
    CASE_READ16_32BIT(0x04004058, MBK[0][6])
    CASE_READ16_32BIT(0x0400405C, MBK[0][7])
    CASE_READ16_32BIT(0x04004060, MBK[0][8])
    }

    return NDS::ARM9IORead16(addr);
}

u32 ARM9IORead32(u32 addr)
{
    switch (addr)
    {
    case 0x04004008: return 0x8307F100;
    case 0x04004010: return 1; // todo

    case 0x04004040: return MBK[0][0];
    case 0x04004044: return MBK[0][1];
    case 0x04004048: return MBK[0][2];
    case 0x0400404C: return MBK[0][3];
    case 0x04004050: return MBK[0][4];
    case 0x04004054: return MBK[0][5];
    case 0x04004058: return MBK[0][6];
    case 0x0400405C: return MBK[0][7];
    case 0x04004060: return MBK[0][8];

    case 0x04004100: return NDMACnt[0];
    case 0x04004104: return NDMAs[0]->SrcAddr;
    case 0x04004108: return NDMAs[0]->DstAddr;
    case 0x0400410C: return NDMAs[0]->TotalLength;
    case 0x04004110: return NDMAs[0]->BlockLength;
    case 0x04004114: return NDMAs[0]->SubblockTimer;
    case 0x04004118: return NDMAs[0]->FillData;
    case 0x0400411C: return NDMAs[0]->Cnt;
    case 0x04004120: return NDMAs[1]->SrcAddr;
    case 0x04004124: return NDMAs[1]->DstAddr;
    case 0x04004128: return NDMAs[1]->TotalLength;
    case 0x0400412C: return NDMAs[1]->BlockLength;
    case 0x04004130: return NDMAs[1]->SubblockTimer;
    case 0x04004134: return NDMAs[1]->FillData;
    case 0x04004138: return NDMAs[1]->Cnt;
    case 0x0400413C: return NDMAs[2]->SrcAddr;
    case 0x04004140: return NDMAs[2]->DstAddr;
    case 0x04004144: return NDMAs[2]->TotalLength;
    case 0x04004148: return NDMAs[2]->BlockLength;
    case 0x0400414C: return NDMAs[2]->SubblockTimer;
    case 0x04004150: return NDMAs[2]->FillData;
    case 0x04004154: return NDMAs[2]->Cnt;
    case 0x04004158: return NDMAs[3]->SrcAddr;
    case 0x0400415C: return NDMAs[3]->DstAddr;
    case 0x04004160: return NDMAs[3]->TotalLength;
    case 0x04004164: return NDMAs[3]->BlockLength;
    case 0x04004168: return NDMAs[3]->SubblockTimer;
    case 0x0400416C: return NDMAs[3]->FillData;
    case 0x04004170: return NDMAs[3]->Cnt;
    }

    return NDS::ARM9IORead32(addr);
}

void ARM9IOWrite8(u32 addr, u8 val)
{
    switch (addr)
    {
    case 0x04004040: MapNWRAM_A(0, val); return;
    case 0x04004041: MapNWRAM_A(1, val); return;
    case 0x04004042: MapNWRAM_A(2, val); return;
    case 0x04004043: MapNWRAM_A(3, val); return;
    case 0x04004044: MapNWRAM_B(0, val); return;
    case 0x04004045: MapNWRAM_B(1, val); return;
    case 0x04004046: MapNWRAM_B(2, val); return;
    case 0x04004047: MapNWRAM_B(3, val); return;
    case 0x04004048: MapNWRAM_B(4, val); return;
    case 0x04004049: MapNWRAM_B(5, val); return;
    case 0x0400404A: MapNWRAM_B(6, val); return;
    case 0x0400404B: MapNWRAM_B(7, val); return;
    case 0x0400404C: MapNWRAM_C(0, val); return;
    case 0x0400404D: MapNWRAM_C(1, val); return;
    case 0x0400404E: MapNWRAM_C(2, val); return;
    case 0x0400404F: MapNWRAM_C(3, val); return;
    case 0x04004050: MapNWRAM_C(4, val); return;
    case 0x04004051: MapNWRAM_C(5, val); return;
    case 0x04004052: MapNWRAM_C(6, val); return;
    case 0x04004053: MapNWRAM_C(7, val); return;
    }

    return NDS::ARM9IOWrite8(addr, val);
}

void ARM9IOWrite16(u32 addr, u16 val)
{
    switch (addr)
    {
    case 0x04004040:
        MapNWRAM_A(0, val & 0xFF);
        MapNWRAM_A(1, val >> 8);
        return;
    case 0x04004042:
        MapNWRAM_A(2, val & 0xFF);
        MapNWRAM_A(3, val >> 8);
        return;
    case 0x04004044:
        MapNWRAM_B(0, val & 0xFF);
        MapNWRAM_B(1, val >> 8);
        return;
    case 0x04004046:
        MapNWRAM_B(2, val & 0xFF);
        MapNWRAM_B(3, val >> 8);
        return;
    case 0x04004048:
        MapNWRAM_B(4, val & 0xFF);
        MapNWRAM_B(5, val >> 8);
        return;
    case 0x0400404A:
        MapNWRAM_B(6, val & 0xFF);
        MapNWRAM_B(7, val >> 8);
        return;
    case 0x0400404C:
        MapNWRAM_C(0, val & 0xFF);
        MapNWRAM_C(1, val >> 8);
        return;
    case 0x0400404E:
        MapNWRAM_C(2, val & 0xFF);
        MapNWRAM_C(3, val >> 8);
        return;
    case 0x04004050:
        MapNWRAM_C(4, val & 0xFF);
        MapNWRAM_C(5, val >> 8);
        return;
    case 0x04004052:
        MapNWRAM_C(6, val & 0xFF);
        MapNWRAM_C(7, val >> 8);
        return;
    }

    return NDS::ARM9IOWrite16(addr, val);
}

void ARM9IOWrite32(u32 addr, u32 val)
{
    switch (addr)
    {
    case 0x04004040:
        MapNWRAM_A(0, val & 0xFF);
        MapNWRAM_A(1, (val >> 8) & 0xFF);
        MapNWRAM_A(2, (val >> 16) & 0xFF);
        MapNWRAM_A(3, val >> 24);
        return;
    case 0x04004044:
        MapNWRAM_B(0, val & 0xFF);
        MapNWRAM_B(1, (val >> 8) & 0xFF);
        MapNWRAM_B(2, (val >> 16) & 0xFF);
        MapNWRAM_B(3, val >> 24);
        return;
    case 0x04004048:
        MapNWRAM_B(4, val & 0xFF);
        MapNWRAM_B(5, (val >> 8) & 0xFF);
        MapNWRAM_B(6, (val >> 16) & 0xFF);
        MapNWRAM_B(7, val >> 24);
        return;
    case 0x0400404C:
        MapNWRAM_C(0, val & 0xFF);
        MapNWRAM_C(1, (val >> 8) & 0xFF);
        MapNWRAM_C(2, (val >> 16) & 0xFF);
        MapNWRAM_C(3, val >> 24);
        return;
    case 0x04004050:
        MapNWRAM_C(4, val & 0xFF);
        MapNWRAM_C(5, (val >> 8) & 0xFF);
        MapNWRAM_C(6, (val >> 16) & 0xFF);
        MapNWRAM_C(7, val >> 24);
        return;
    case 0x04004054: MapNWRAMRange(0, 0, val); return;
    case 0x04004058: MapNWRAMRange(0, 1, val); return;
    case 0x0400405C: MapNWRAMRange(0, 2, val); return;

    case 0x04004100: NDMACnt[0] = val & 0x800F0000; return;
    case 0x04004104: NDMAs[0]->SrcAddr = val & 0xFFFFFFFC; return;
    case 0x04004108: NDMAs[0]->DstAddr = val & 0xFFFFFFFC; return;
    case 0x0400410C: NDMAs[0]->TotalLength = val & 0x0FFFFFFF; return;
    case 0x04004110: NDMAs[0]->BlockLength = val & 0x00FFFFFF; return;
    case 0x04004114: NDMAs[0]->SubblockTimer = val & 0x0003FFFF; return;
    case 0x04004118: NDMAs[0]->FillData = val; return;
    case 0x0400411C: NDMAs[0]->WriteCnt(val); return;
    case 0x04004120: NDMAs[1]->SrcAddr = val & 0xFFFFFFFC; return;
    case 0x04004124: NDMAs[1]->DstAddr = val & 0xFFFFFFFC; return;
    case 0x04004128: NDMAs[1]->TotalLength = val & 0x0FFFFFFF; return;
    case 0x0400412C: NDMAs[1]->BlockLength = val & 0x00FFFFFF; return;
    case 0x04004130: NDMAs[1]->SubblockTimer = val & 0x0003FFFF; return;
    case 0x04004134: NDMAs[1]->FillData = val; return;
    case 0x04004138: NDMAs[1]->WriteCnt(val); return;
    case 0x0400413C: NDMAs[2]->SrcAddr = val & 0xFFFFFFFC; return;
    case 0x04004140: NDMAs[2]->DstAddr = val & 0xFFFFFFFC; return;
    case 0x04004144: NDMAs[2]->TotalLength = val & 0x0FFFFFFF; return;
    case 0x04004148: NDMAs[2]->BlockLength = val & 0x00FFFFFF; return;
    case 0x0400414C: NDMAs[2]->SubblockTimer = val & 0x0003FFFF; return;
    case 0x04004150: NDMAs[2]->FillData = val; return;
    case 0x04004154: NDMAs[2]->WriteCnt(val); return;
    case 0x04004158: NDMAs[3]->SrcAddr = val & 0xFFFFFFFC; return;
    case 0x0400415C: NDMAs[3]->DstAddr = val & 0xFFFFFFFC; return;
    case 0x04004160: NDMAs[3]->TotalLength = val & 0x0FFFFFFF; return;
    case 0x04004164: NDMAs[3]->BlockLength = val & 0x00FFFFFF; return;
    case 0x04004168: NDMAs[3]->SubblockTimer = val & 0x0003FFFF; return;
    case 0x0400416C: NDMAs[3]->FillData = val; return;
    case 0x04004170: NDMAs[3]->WriteCnt(val); return;
    }

    return NDS::ARM9IOWrite32(addr, val);
}


u8 ARM7IORead8(u32 addr)
{
    switch (addr)
    {
    case 0x04004000: return 0x01;
    case 0x04004001: return 0x01;

    CASE_READ8_32BIT(0x04004040, MBK[1][0])
    CASE_READ8_32BIT(0x04004044, MBK[1][1])
    CASE_READ8_32BIT(0x04004048, MBK[1][2])
    CASE_READ8_32BIT(0x0400404C, MBK[1][3])
    CASE_READ8_32BIT(0x04004050, MBK[1][4])
    CASE_READ8_32BIT(0x04004054, MBK[1][5])
    CASE_READ8_32BIT(0x04004058, MBK[1][6])
    CASE_READ8_32BIT(0x0400405C, MBK[1][7])
    CASE_READ8_32BIT(0x04004060, MBK[1][8])

    case 0x04004500: return DSi_I2C::ReadData();
    case 0x04004501: printf("read I2C CNT %02X\n", DSi_I2C::Cnt); return DSi_I2C::Cnt;

    case 0x04004D00: return ConsoleID & 0xFF;
    case 0x04004D01: return (ConsoleID >> 8) & 0xFF;
    case 0x04004D02: return (ConsoleID >> 16) & 0xFF;
    case 0x04004D03: return (ConsoleID >> 24) & 0xFF;
    case 0x04004D04: return (ConsoleID >> 32) & 0xFF;
    case 0x04004D05: return (ConsoleID >> 40) & 0xFF;
    case 0x04004D06: return (ConsoleID >> 48) & 0xFF;
    case 0x04004D07: return ConsoleID >> 56;
    case 0x04004D08: return 0;
    }

    return NDS::ARM7IORead8(addr);
}

u16 ARM7IORead16(u32 addr)
{
    switch (addr)
    {
    case 0x04000218: return NDS::IE2;
    case 0x0400021C: return NDS::IF2;

    case 0x04004004: return 0x0187;
    case 0x04004006: return 0; // JTAG register

    CASE_READ16_32BIT(0x04004040, MBK[1][0])
    CASE_READ16_32BIT(0x04004044, MBK[1][1])
    CASE_READ16_32BIT(0x04004048, MBK[1][2])
    CASE_READ16_32BIT(0x0400404C, MBK[1][3])
    CASE_READ16_32BIT(0x04004050, MBK[1][4])
    CASE_READ16_32BIT(0x04004054, MBK[1][5])
    CASE_READ16_32BIT(0x04004058, MBK[1][6])
    CASE_READ16_32BIT(0x0400405C, MBK[1][7])
    CASE_READ16_32BIT(0x04004060, MBK[1][8])

    case 0x04004D00: return ConsoleID & 0xFFFF;
    case 0x04004D02: return (ConsoleID >> 16) & 0xFFFF;
    case 0x04004D04: return (ConsoleID >> 32) & 0xFFFF;
    case 0x04004D06: return ConsoleID >> 48;
    case 0x04004D08: return 0;
    }

    if (addr >= 0x04004800 && addr < 0x04004A00)
    {
        return SDMMC->Read(addr);
    }
    if (addr >= 0x04004A00 && addr < 0x04004C00)
    {
        return SDIO->Read(addr);
    }

    return NDS::ARM7IORead16(addr);
}

u32 ARM7IORead32(u32 addr)
{
    switch (addr)
    {
    case 0x04000218: return NDS::IE2;
    case 0x0400021C: return NDS::IF2;

    case 0x04004008: return 0x80000000; // HAX

    case 0x04004040: return MBK[1][0];
    case 0x04004044: return MBK[1][1];
    case 0x04004048: return MBK[1][2];
    case 0x0400404C: return MBK[1][3];
    case 0x04004050: return MBK[1][4];
    case 0x04004054: return MBK[1][5];
    case 0x04004058: return MBK[1][6];
    case 0x0400405C: return MBK[1][7];
    case 0x04004060: return MBK[1][8];

    case 0x04004100: return NDMACnt[1];
    case 0x04004104: return NDMAs[4]->SrcAddr;
    case 0x04004108: return NDMAs[4]->DstAddr;
    case 0x0400410C: return NDMAs[4]->TotalLength;
    case 0x04004110: return NDMAs[4]->BlockLength;
    case 0x04004114: return NDMAs[4]->SubblockTimer;
    case 0x04004118: return NDMAs[4]->FillData;
    case 0x0400411C: return NDMAs[4]->Cnt;
    case 0x04004120: return NDMAs[5]->SrcAddr;
    case 0x04004124: return NDMAs[5]->DstAddr;
    case 0x04004128: return NDMAs[5]->TotalLength;
    case 0x0400412C: return NDMAs[5]->BlockLength;
    case 0x04004130: return NDMAs[5]->SubblockTimer;
    case 0x04004134: return NDMAs[5]->FillData;
    case 0x04004138: return NDMAs[5]->Cnt;
    case 0x0400413C: return NDMAs[6]->SrcAddr;
    case 0x04004140: return NDMAs[6]->DstAddr;
    case 0x04004144: return NDMAs[6]->TotalLength;
    case 0x04004148: return NDMAs[6]->BlockLength;
    case 0x0400414C: return NDMAs[6]->SubblockTimer;
    case 0x04004150: return NDMAs[6]->FillData;
    case 0x04004154: return NDMAs[6]->Cnt;
    case 0x04004158: return NDMAs[7]->SrcAddr;
    case 0x0400415C: return NDMAs[7]->DstAddr;
    case 0x04004160: return NDMAs[7]->TotalLength;
    case 0x04004164: return NDMAs[7]->BlockLength;
    case 0x04004168: return NDMAs[7]->SubblockTimer;
    case 0x0400416C: return NDMAs[7]->FillData;
    case 0x04004170: return NDMAs[7]->Cnt;

    case 0x04004400: return DSi_AES::ReadCnt();
    case 0x0400440C: return DSi_AES::ReadOutputFIFO();

    case 0x04004D00: return ConsoleID & 0xFFFFFFFF;
    case 0x04004D04: return ConsoleID >> 32;
    case 0x04004D08: return 0;
    }

    if (addr >= 0x04004800 && addr < 0x04004A00)
    {
        if (addr == 0x0400490C) return SDMMC->ReadFIFO32();
        return SDMMC->Read(addr) | (SDMMC->Read(addr+2) << 16);
    }
    if (addr >= 0x04004A00 && addr < 0x04004C00)
    {
        if (addr == 0x04004B0C) return SDIO->ReadFIFO32();
        return SDIO->Read(addr) | (SDIO->Read(addr+2) << 16);
    }

    return NDS::ARM7IORead32(addr);
}

void ARM7IOWrite8(u32 addr, u8 val)
{
    switch (addr)
    {
    case 0x04004500: DSi_I2C::WriteData(val); return;
    case 0x04004501: DSi_I2C::WriteCnt(val); return;
    }

    return NDS::ARM7IOWrite8(addr, val);
}

void ARM7IOWrite16(u32 addr, u16 val)
{
    switch (addr)
    {
    case 0x04000218: NDS::IE2 = (val & 0x7FF7); NDS::UpdateIRQ(1); return;
    case 0x0400021C: NDS::IF2 &= ~(val & 0x7FF7); NDS::UpdateIRQ(1); return;
    }

    if (addr >= 0x04004800 && addr < 0x04004A00)
    {
        SDMMC->Write(addr, val);
        return;
    }
    if (addr >= 0x04004A00 && addr < 0x04004C00)
    {
        SDIO->Write(addr, val);
        return;
    }

    return NDS::ARM7IOWrite16(addr, val);
}

void ARM7IOWrite32(u32 addr, u32 val)
{
    switch (addr)
    {
    case 0x04000218: NDS::IE2 = (val & 0x7FF7); NDS::UpdateIRQ(1); return;
    case 0x0400021C: NDS::IF2 &= ~(val & 0x7FF7); NDS::UpdateIRQ(1); return;

    case 0x04004054: MapNWRAMRange(1, 0, val); return;
    case 0x04004058: MapNWRAMRange(1, 1, val); return;
    case 0x0400405C: MapNWRAMRange(1, 2, val); return;
    case 0x04004060: val &= 0x00FFFF0F; MBK[0][8] = val; MBK[1][8] = val; return;

    case 0x04004100: NDMACnt[1] = val & 0x800F0000; return;
    case 0x04004104: NDMAs[4]->SrcAddr = val & 0xFFFFFFFC; return;
    case 0x04004108: NDMAs[4]->DstAddr = val & 0xFFFFFFFC; return;
    case 0x0400410C: NDMAs[4]->TotalLength = val & 0x0FFFFFFF; return;
    case 0x04004110: NDMAs[4]->BlockLength = val & 0x00FFFFFF; return;
    case 0x04004114: NDMAs[4]->SubblockTimer = val & 0x0003FFFF; return;
    case 0x04004118: NDMAs[4]->FillData = val; return;
    case 0x0400411C: NDMAs[4]->WriteCnt(val); return;
    case 0x04004120: NDMAs[5]->SrcAddr = val & 0xFFFFFFFC; return;
    case 0x04004124: NDMAs[5]->DstAddr = val & 0xFFFFFFFC; return;
    case 0x04004128: NDMAs[5]->TotalLength = val & 0x0FFFFFFF; return;
    case 0x0400412C: NDMAs[5]->BlockLength = val & 0x00FFFFFF; return;
    case 0x04004130: NDMAs[5]->SubblockTimer = val & 0x0003FFFF; return;
    case 0x04004134: NDMAs[5]->FillData = val; return;
    case 0x04004138: NDMAs[5]->WriteCnt(val); return;
    case 0x0400413C: NDMAs[6]->SrcAddr = val & 0xFFFFFFFC; return;
    case 0x04004140: NDMAs[6]->DstAddr = val & 0xFFFFFFFC; return;
    case 0x04004144: NDMAs[6]->TotalLength = val & 0x0FFFFFFF; return;
    case 0x04004148: NDMAs[6]->BlockLength = val & 0x00FFFFFF; return;
    case 0x0400414C: NDMAs[6]->SubblockTimer = val & 0x0003FFFF; return;
    case 0x04004150: NDMAs[6]->FillData = val; return;
    case 0x04004154: NDMAs[6]->WriteCnt(val); return;
    case 0x04004158: NDMAs[7]->SrcAddr = val & 0xFFFFFFFC; return;
    case 0x0400415C: NDMAs[7]->DstAddr = val & 0xFFFFFFFC; return;
    case 0x04004160: NDMAs[7]->TotalLength = val & 0x0FFFFFFF; return;
    case 0x04004164: NDMAs[7]->BlockLength = val & 0x00FFFFFF; return;
    case 0x04004168: NDMAs[7]->SubblockTimer = val & 0x0003FFFF; return;
    case 0x0400416C: NDMAs[7]->FillData = val; return;
    case 0x04004170: NDMAs[7]->WriteCnt(val); return;

    case 0x04004400: DSi_AES::WriteCnt(val); return;
    case 0x04004404: DSi_AES::WriteBlkCnt(val); return;
    case 0x04004408: DSi_AES::WriteInputFIFO(val); return;
    }

    if (addr >= 0x04004420 && addr < 0x04004430)
    {
        addr -= 0x04004420;
        DSi_AES::WriteIV(addr, val, 0xFFFFFFFF);
        return;
    }
    if (addr >= 0x04004430 && addr < 0x04004440)
    {
        addr -= 0x04004430;
        DSi_AES::WriteMAC(addr, val, 0xFFFFFFFF);
        return;
    }
    if (addr >= 0x04004440 && addr < 0x04004500)
    {
        addr -= 0x04004440;
        int n = 0;
        while (addr > 0x30) { addr -= 0x30; n++; }

        switch (addr >> 4)
        {
        case 0: DSi_AES::WriteKeyNormal(n, addr&0xF, val, 0xFFFFFFFF); return;
        case 1: DSi_AES::WriteKeyX(n, addr&0xF, val, 0xFFFFFFFF); return;
        case 2: DSi_AES::WriteKeyY(n, addr&0xF, val, 0xFFFFFFFF); return;
        }
    }

    if (addr >= 0x04004800 && addr < 0x04004A00)
    {
        if (addr == 0x0400490C) { SDMMC->WriteFIFO32(val); return; }
        SDMMC->Write(addr, val & 0xFFFF);
        SDMMC->Write(addr+2, val >> 16);
        return;
    }
    if (addr >= 0x04004A00 && addr < 0x04004C00)
    {
        if (addr == 0x04004B0C) { SDIO->WriteFIFO32(val); return; }
        SDIO->Write(addr, val & 0xFFFF);
        SDIO->Write(addr+2, val >> 16);
        return;
    }

    return NDS::ARM7IOWrite32(addr, val);
}

}