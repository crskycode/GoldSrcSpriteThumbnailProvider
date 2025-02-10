#pragma once

#include <Windows.h>


struct SPRITE_FRAME_HEADER_V3 {
	INT32 Width;
	INT32 Height;
	DWORD Format;
};


struct SPRITE_FRAME_V3 {
	SPRITE_FRAME_HEADER_V3 Header;
	PBYTE Pixels;
};

typedef SPRITE_FRAME_V3* PSPRITE_FRAME_V3;


struct SPRITE_FILE_HEADER_V3 {
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


struct SPRITE_FILE_V3 {
	SPRITE_FILE_HEADER_V3 Header;
	PSPRITE_FRAME_V3* Frames;
};

typedef SPRITE_FILE_V3* PSPRITE_FILE_V3;


VOID FreeSpriteFileV3(PSPRITE_FILE_V3 sprite);

HRESULT LoadSpriteFileV3(IStream* stream, PSPRITE_FILE_V3* result);
