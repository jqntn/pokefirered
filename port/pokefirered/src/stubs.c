#include "global.h"
#include "gba/gba.h"
#include "pfr/stubs.h"

#include "gba/types.h"
#include "gba/m4a_internal.h"
#include "link_rfu.h"
#include "load_save.h"
#include "m4a.h"
#include "save.h"
#include "sound.h"
#include "scanline_effect.h"
#include "sprite.h"
#include "window.h"
#include "bg.h"
#include "decompress.h"

// Audio
void PlayCry_ByMode(u16 species, s8 pan, u8 mode) { (void)species; (void)pan; (void)mode; }
void PlaySE(u16 songNum) { (void)songNum; }
void SetPokemonCryStereo(u32 val) { (void)val; }
void m4aSongNumStart(u16 n) { (void)n; }

// Multiboot & Serial
void ScanlineEffect_Stop(void) {}
void GameCubeMultiBoot_Main(void) {}
void GameCubeMultiBoot_ExecuteProgram(void) {}
void GameCubeMultiBoot_Init(void) {}
void GameCubeMultiBoot_HandleSerialInterrupt(void) {}
void GameCubeMultiBoot_Quit(void) {}
void ResetSerial(void) {}
void SerialCB(void) {}

// Intro.c dependencies from game logic
void Save_ResetSaveCounters(void) {}
u8 LoadGameSave(u8 saveType) { (void)saveType; return 0; }
void ResetMenuAndMonGlobals(void) {}
void Sav2_ClearSetDefault(void) {}
void CB2_InitTitleScreen(void) {}

void ResetBgPositions(void) {
    ChangeBgX(0, 0, 0); ChangeBgX(1, 0, 0); ChangeBgX(2, 0, 0); ChangeBgX(3, 0, 0);
    ChangeBgY(0, 0, 0); ChangeBgY(1, 0, 0); ChangeBgY(2, 0, 0); ChangeBgY(3, 0, 0);
}

void StartBlendTask(s8 startY, s8 targetY, s8 deltaY, u8 delay, u8 submode, u32 selectedPalettes) {
    (void)startY; (void)targetY; (void)deltaY; (void)delay; (void)submode; (void)selectedPalettes;
}

bool8 IsBlendTaskActive(void) {
    return FALSE;
}

void ResetTempTileDataBuffers(void) {}

bool8 FreeTempTileDataBuffersIfPossible(void) { 
    return FALSE; 
}

void *DecompressAndCopyTileDataToVram(u8 bgId, const void *src, u32 size, u16 offset, u8 mode) {
    u32 sizeOut = 0;
    u8 *sizeAsBytes = (u8 *)&sizeOut;
    const u8 *srcAsBytes = (const u8*)src;

    (void)mode;
    
    sizeAsBytes[0] = srcAsBytes[1];
    sizeAsBytes[1] = srcAsBytes[2];
    sizeAsBytes[2] = srcAsBytes[3];
    sizeAsBytes[3] = 0;

    LZ77UnCompWram(src, gDecompressionBuffer);
    
    if (size == 0) {
        size = sizeOut;
    }

    LoadBgTiles(bgId, gDecompressionBuffer, (u16)size, offset);
    return NULL; 
}

// Missing util.c stuff
void StoreWordInTwoHalfwords(u16 *dest, u32 data)
{
    dest[0] = (u16)data;
    dest[1] = (u16)(data >> 16);
}

u32 LoadWordFromTwoHalfwords(u16 *src)
{
    return src[0] | (src[1] << 16);
}

// Variables missing
u8 gHeap[0x1C000]; // 112KB buffer as per original game
u16 gSaveFileStatus = 0;
const s16 gSineTable[256] = {0};
const u32 gMultiBootProgram_PokemonColosseum_Start[1] = {0};

// gfx utils missing
void BlendPalette(u16 palOffset, u16 numEntries, u8 coeff, u16 blendColor) {
    (void)palOffset; (void)numEntries; (void)coeff; (void)blendColor;
}

struct Bitmap;
void BlitBitmapRect4Bit(const struct Bitmap *src, struct Bitmap *dest, u16 srcX, u16 srcY, u16 destX, u16 destY, u16 width, u16 height, u16 colorKey) {
    (void)src; (void)dest; (void)srcX; (void)srcY; (void)destX; (void)destY; (void)width; (void)height; (void)colorKey;
}
void FillBitmapRect4Bit(struct Bitmap *dest, u16 x, u16 y, u16 width, u16 height, u8 colorIndex) {
    (void)dest; (void)x; (void)y; (void)width; (void)height; (void)colorIndex;
}
void DrawSpindaSpots(u16 species, u32 personality, u8 *dest, bool8 isFrontPic) {
    (void)species; (void)personality; (void)dest; (void)isFrontPic;
}

u8 gDecompressionBuffer[0x4000]; // 16KB buffer
void * gMonFrontPicTable[1];
void * gMonBackPicTable[1];
