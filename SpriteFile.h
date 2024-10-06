#pragma once

#include <Windows.h>


struct SPRITE_FILE_HEADER {
	INT32 ID;
	INT32 Version;
	INT32 Type;
	INT32 TexFormat;
	float BoundingRadius;
	INT32 Width;
	INT32 Height;
	INT32 FrameCount;
	float BeamLength;
	INT32 SyncType;
};


struct COLOR24 {
	BYTE R;
	BYTE G;
	BYTE B;
};

typedef COLOR24* PCOLOR24;


struct SPRITE_PALETTE {
	INT16 Count;
	PCOLOR24 Colors;
};


struct SPRITE_FRAME_HEADER {
	INT32 Origin[2];
	INT32 Width;
	INT32 Height;
};


struct SPRITE_FRAME_SINGLE {
	SPRITE_FRAME_HEADER Header;
	PBYTE Pixels;
};

typedef SPRITE_FRAME_SINGLE* PSPRITE_FRAME_SINGLE;


struct SPRITE_FRAME_GROUP {
	INT32 FrameCount;
	float* Intervals;
	PSPRITE_FRAME_SINGLE* Frames;
};

typedef SPRITE_FRAME_GROUP* PSPRITE_FRAME_GROUP;


enum {
	SPR_SINGLE = 0,
	SPR_GROUP
};


struct SPRITE_FRAME {
	INT32 Type;
	union {
		PSPRITE_FRAME_SINGLE Single;
		PSPRITE_FRAME_GROUP Group;
	} u;
};

typedef SPRITE_FRAME* PSPRITE_FRAME;


struct SPRITE_FILE {
	SPRITE_FILE_HEADER Header;
	SPRITE_PALETTE Palette;
	PSPRITE_FRAME* Frames;
};

typedef SPRITE_FILE* PSPRITE_FILE;


VOID FreeSpriteFile(PSPRITE_FILE sprite);

HRESULT LoadSpriteFile(IStream* stream, PSPRITE_FILE* result);

