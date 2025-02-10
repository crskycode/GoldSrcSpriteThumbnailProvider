#include "SpriteFile.h"


#ifdef _DEBUG
#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#else
#include <malloc.h>
#endif


static HRESULT ReadBytes(IStream* stream, PVOID buffer, ULONG count) {
	HRESULT hr;
	ULONG read;

	hr = stream->Read(buffer, count, &read);
	if (FAILED(hr)) {
		return hr;
	}

	return S_OK;
}


static HRESULT ReadUInt8(IStream* stream, BYTE* result) {
	HRESULT hr;
	BYTE buffer;
	ULONG read;

	hr = stream->Read(&buffer, sizeof(BYTE), &read);
	if (FAILED(hr)) {
		return hr;
	}

	*result = buffer;

	return S_OK;
}


static HRESULT ReadInt16(IStream* stream, INT16* result) {
	HRESULT hr;
	INT16 buffer;
	ULONG read;

	hr = stream->Read(&buffer, sizeof(INT16), &read);
	if (FAILED(hr)) {
		return hr;
	}

	*result = buffer;

	return S_OK;
}


static HRESULT ReadInt32(IStream* stream, INT32* result) {
	HRESULT hr;
	INT32 buffer;
	ULONG read;

	hr = stream->Read(&buffer, sizeof(INT32), &read);
	if (FAILED(hr)) {
		return hr;
	}

	*result = buffer;

	return S_OK;
}


static HRESULT ReadFloat(IStream* stream, float* result) {
	HRESULT hr;
	float buffer;
	ULONG read;

	hr = stream->Read(&buffer, sizeof(float), &read);
	if (FAILED(hr)) {
		return hr;
	}

	*result = buffer;

	return S_OK;
}


static HRESULT LoadFrameSingle(IStream* stream, PSPRITE_FRAME_SINGLE* result) {
	HRESULT hr;

	PSPRITE_FRAME_SINGLE frame = (PSPRITE_FRAME_SINGLE)malloc(sizeof(SPRITE_FRAME_SINGLE));

	if (frame == NULL) {
		return E_OUTOFMEMORY;
	}

	memset(frame, 0, sizeof(SPRITE_FRAME_SINGLE));

	hr = ReadInt32(stream, &frame->Header.Origin[0]);
	if (FAILED(hr)) {
		free(frame);
		return hr;
	}

	hr = ReadInt32(stream, &frame->Header.Origin[1]);
	if (FAILED(hr)) {
		free(frame);
		return hr;
	}

	hr = ReadInt32(stream, &frame->Header.Width);
	if (FAILED(hr)) {
		free(frame);
		return hr;
	}

	if (frame->Header.Width < 1) {
		free(frame);
		return E_UNEXPECTED;
	}

	hr = ReadInt32(stream, &frame->Header.Height);
	if (FAILED(hr)) {
		free(frame);
		return hr;
	}

	if (frame->Header.Height < 1) {
		free(frame);
		return E_UNEXPECTED;
	}

	size_t dataSize = (size_t)frame->Header.Width * (size_t)frame->Header.Height;

	frame->Pixels = (PBYTE)malloc(dataSize);

	if (frame->Pixels == NULL) {
		free(frame);
		return E_OUTOFMEMORY;
	}

	memset(frame->Pixels, 0, dataSize);

	hr = ReadBytes(stream, frame->Pixels, (ULONG)dataSize);
	if (FAILED(hr)) {
		free(frame->Pixels);
		free(frame);
		return hr;
	}

	*result = frame;

	return S_OK;
}


static HRESULT LoadFrameGroup(IStream* stream, PSPRITE_FRAME_GROUP* result) {
	HRESULT hr;

	PSPRITE_FRAME_GROUP group = (PSPRITE_FRAME_GROUP)malloc(sizeof(SPRITE_FRAME_GROUP));

	if (group == NULL) {
		return E_OUTOFMEMORY;
	}

	memset(group, 0, sizeof(SPRITE_FRAME_GROUP));

	//
	// Header
	//

	hr = ReadInt32(stream, &group->FrameCount);
	if (FAILED(hr)) {
		free(group);
		return hr;
	}

	if (group->FrameCount < 1) {
		free(group);
		return E_UNEXPECTED;
	}

	//
	// Intervals
	//

	size_t intervalArraySize = sizeof(float) * (size_t)group->FrameCount;

	group->Intervals = (float*)malloc(intervalArraySize);

	if (group->Intervals == NULL) {
		free(group);
		return E_OUTOFMEMORY;
	}

	memset(group->Intervals, 0, intervalArraySize);

	for (int i = 0; i < group->FrameCount; i++) {
		hr = ReadFloat(stream, &group->Intervals[i]);
		if (FAILED(hr)) {
			free(group->Intervals);
			free(group);
			return hr;
		}
	}

	//
	// Frames
	//

	size_t frameArraySize = sizeof(PSPRITE_FRAME_SINGLE) * (size_t)group->FrameCount;

	group->Frames = (PSPRITE_FRAME_SINGLE*)malloc(frameArraySize);

	if (group->Frames == NULL) {
		free(group->Intervals);
		free(group);
		return E_OUTOFMEMORY;
	}

	memset(group->Frames, 0, frameArraySize);

	for (int i = 0; i < group->FrameCount; i++) {
		hr = LoadFrameSingle(stream, &group->Frames[i]);
		if (FAILED(hr)) {
			free(group->Intervals);
			free(group->Frames);
			free(group);
			return hr;
		}
	}

	*result = group;

	return S_OK;
}


static HRESULT LoadSpriteFrame(IStream* stream, PSPRITE_FRAME* result) {
	HRESULT hr;

	PSPRITE_FRAME frame = (PSPRITE_FRAME)malloc(sizeof(SPRITE_FRAME));

	if (frame == NULL) {
		return E_OUTOFMEMORY;
	}

	memset(frame, 0, sizeof(SPRITE_FRAME));

	hr = ReadInt32(stream, &frame->Type);
	if (FAILED(hr)) {
		free(frame);
		return hr;
	}

	if (frame->Type == SPR_SINGLE) {
		hr = LoadFrameSingle(stream, &frame->u.Single);
		if (FAILED(hr)) {
			free(frame);
			return hr;
		}
	}
	else if (frame->Type == SPR_GROUP) {
		hr = LoadFrameGroup(stream, &frame->u.Group);
		if (FAILED(hr)) {
			free(frame);
			return hr;
		}
	}
	else {
		free(frame);
		return E_UNEXPECTED;
	}

	*result = frame;

	return S_OK;
}


VOID FreeSpriteFile(PSPRITE_FILE sprite) {
	if (sprite->Palette.Colors) {
		free(sprite->Palette.Colors);
	}
	if (sprite->Frames) {
		for (INT32 i = 0; i < sprite->Header.FrameCount; i++) {
			PSPRITE_FRAME frame = sprite->Frames[i];
			if (frame) {
				if (frame->Type == SPR_SINGLE) {
					PSPRITE_FRAME_SINGLE single = frame->u.Single;
					if (single) {
						if (single->Pixels) {
							free(single->Pixels);
						}
						free(single);
					}
				}
				else if (frame->Type == SPR_GROUP) {
					PSPRITE_FRAME_GROUP group = frame->u.Group;
					if (group) {
						if (group->Intervals) {
							free(group->Intervals);
						}
						if (group->Frames) {
							for (INT32 i = 0; i < group->FrameCount; i++) {
								PSPRITE_FRAME_SINGLE singleFrame = group->Frames[i];
								if (singleFrame) {
									if (singleFrame->Pixels) {
										free(singleFrame->Pixels);
									}
									free(singleFrame);
								}
							}
							free(group->Frames);
						}
						free(group);
					}
				}
				free(frame);
			}
		}
		free(sprite->Frames);
	}
	free(sprite);
}


HRESULT LoadSpriteFile(IStream* stream, PSPRITE_FILE* result) {
	HRESULT hr;

	PSPRITE_FILE sprite = (PSPRITE_FILE)malloc(sizeof(SPRITE_FILE));

	if (sprite == NULL) {
		return E_OUTOFMEMORY;
	}

	memset(sprite, 0, sizeof(SPRITE_FILE));

	//
	// File Header
	//

	hr = ReadInt32(stream, &sprite->Header.ID);
	if (FAILED(hr)) {
		FreeSpriteFile(sprite);
		return hr;
	}

	if (sprite->Header.ID != 0x50534449) {
		FreeSpriteFile(sprite);
		return E_UNEXPECTED;
	}

	hr = ReadInt32(stream, &sprite->Header.Version);
	if (FAILED(hr)) {
		FreeSpriteFile(sprite);
		return hr;
	}

	if (sprite->Header.Version != 2) {
		FreeSpriteFile(sprite);
		return E_UNEXPECTED;
	}

	hr = ReadInt32(stream, &sprite->Header.Type);
	if (FAILED(hr)) {
		FreeSpriteFile(sprite);
		return hr;
	}

	hr = ReadInt32(stream, &sprite->Header.TexFormat);
	if (FAILED(hr)) {
		FreeSpriteFile(sprite);
		return hr;
	}

	hr = ReadFloat(stream, &sprite->Header.BoundingRadius);
	if (FAILED(hr)) {
		FreeSpriteFile(sprite);
		return hr;
	}

	hr = ReadInt32(stream, &sprite->Header.Width);
	if (FAILED(hr)) {
		FreeSpriteFile(sprite);
		return hr;
	}

	hr = ReadInt32(stream, &sprite->Header.Height);
	if (FAILED(hr)) {
		FreeSpriteFile(sprite);
		return hr;
	}

	hr = ReadInt32(stream, &sprite->Header.FrameCount);
	if (FAILED(hr)) {
		FreeSpriteFile(sprite);
		return hr;
	}

	if (sprite->Header.FrameCount < 1) {
		FreeSpriteFile(sprite);
		return E_UNEXPECTED;
	}

	hr = ReadFloat(stream, &sprite->Header.BeamLength);
	if (FAILED(hr)) {
		FreeSpriteFile(sprite);
		return hr;
	}

	hr = ReadInt32(stream, &sprite->Header.SyncType);
	if (FAILED(hr)) {
		FreeSpriteFile(sprite);
		return hr;
	}

	//
	// Palette
	//

	hr = ReadInt16(stream, &sprite->Palette.Count);
	if (FAILED(hr)) {
		FreeSpriteFile(sprite);
		return hr;
	}

	if (sprite->Palette.Count < 1 || sprite->Palette.Count > 256) {
		FreeSpriteFile(sprite);
		return E_UNEXPECTED;
	}

	size_t paletteSize = sprite->Palette.Count * sizeof(COLOR24);

	sprite->Palette.Colors = (PCOLOR24)malloc(paletteSize);

	if (sprite->Palette.Colors == NULL) {
		FreeSpriteFile(sprite);
		return E_OUTOFMEMORY;
	}

	hr = ReadBytes(stream, sprite->Palette.Colors, (ULONG)paletteSize);
	if (FAILED(hr)) {
		FreeSpriteFile(sprite);
		return hr;
	}

	//
	// Frames
	//

	size_t frameArraySize = sizeof(PSPRITE_FRAME) * (size_t)sprite->Header.FrameCount;

	sprite->Frames = (PSPRITE_FRAME*)malloc(frameArraySize);

	if (sprite->Frames == NULL) {
		FreeSpriteFile(sprite);
		return E_OUTOFMEMORY;
	}

	for (INT32 i = 0; i < sprite->Header.FrameCount; i++) {
		hr = LoadSpriteFrame(stream, &sprite->Frames[i]);
		if (FAILED(hr)) {
			FreeSpriteFile(sprite);
			return hr;
		}
	}

	*result = sprite;

	return S_OK;
}
