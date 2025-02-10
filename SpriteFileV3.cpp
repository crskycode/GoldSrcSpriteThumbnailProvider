#include "SpriteFileV3.h"


#ifdef _DEBUG
#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#else
#include <malloc.h>
#endif


struct DDS_PIXELFORMAT {
	DWORD dwSize;
	DWORD dwFlags;
	DWORD dwFourCC;
	DWORD dwRGBBitCount;
	DWORD dwRBitMask;
	DWORD dwGBitMask;
	DWORD dwBBitMask;
	DWORD dwABitMask;
};


struct DDS_HEADER {
	DWORD           dwSize;
	DWORD           dwFlags;
	DWORD           dwHeight;
	DWORD           dwWidth;
	DWORD           dwPitchOrLinearSize;
	DWORD           dwDepth;
	DWORD           dwMipMapCount;
	DWORD           dwReserved1[11];
	DDS_PIXELFORMAT ddspf;
	DWORD           dwCaps;
	DWORD           dwCaps2;
	DWORD           dwCaps3;
	DWORD           dwCaps4;
	DWORD           dwReserved2;
};


static HRESULT ReadBytes(IStream* stream, PVOID buffer, ULONG count) {
	HRESULT hr;
	ULONG read;

	hr = stream->Read(buffer, count, &read);
	if (FAILED(hr)) {
		return hr;
	}

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


static HRESULT ReadDword(IStream* stream, DWORD* result) {
	HRESULT hr;
	DWORD buffer;
	ULONG read;

	hr = stream->Read(&buffer, sizeof(DWORD), &read);
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


static HRESULT ReadDdsHeader(IStream* stream, DDS_HEADER* header) {
	HRESULT hr;

	hr = ReadDword(stream, &header->dwSize);
	if (FAILED(hr)) {
		return hr;
	}

	if (header->dwSize != 0x7C) {
		return E_NOTIMPL;
	}

	hr = ReadDword(stream, &header->dwFlags);
	if (FAILED(hr)) {
		return hr;
	}

	hr = ReadDword(stream, &header->dwHeight);
	if (FAILED(hr)) {
		return hr;
	}

	hr = ReadDword(stream, &header->dwWidth);
	if (FAILED(hr)) {
		return hr;
	}

	hr = ReadDword(stream, &header->dwPitchOrLinearSize);
	if (FAILED(hr)) {
		return hr;
	}

	hr = ReadDword(stream, &header->dwDepth);
	if (FAILED(hr)) {
		return hr;
	}

	hr = ReadDword(stream, &header->dwMipMapCount);
	if (FAILED(hr)) {
		return hr;
	}

	for (int i = 0; i < 11; i++) {
		hr = ReadDword(stream, &header->dwReserved1[i]);
		if (FAILED(hr)) {
			return hr;
		}
	}

	hr = ReadDword(stream, &header->ddspf.dwSize);
	if (FAILED(hr)) {
		return hr;
	}

	if (header->ddspf.dwSize != 0x20) {
		return E_NOTIMPL;
	}

	hr = ReadDword(stream, &header->ddspf.dwFlags);
	if (FAILED(hr)) {
		return hr;
	}

	hr = ReadDword(stream, &header->ddspf.dwFourCC);
	if (FAILED(hr)) {
		return hr;
	}

	hr = ReadDword(stream, &header->ddspf.dwRGBBitCount);
	if (FAILED(hr)) {
		return hr;
	}

	hr = ReadDword(stream, &header->ddspf.dwRBitMask);
	if (FAILED(hr)) {
		return hr;
	}

	hr = ReadDword(stream, &header->ddspf.dwGBitMask);
	if (FAILED(hr)) {
		return hr;
	}

	hr = ReadDword(stream, &header->ddspf.dwBBitMask);
	if (FAILED(hr)) {
		return hr;
	}

	hr = ReadDword(stream, &header->ddspf.dwABitMask);
	if (FAILED(hr)) {
		return hr;
	}

	hr = ReadDword(stream, &header->dwCaps);
	if (FAILED(hr)) {
		return hr;
	}

	hr = ReadDword(stream, &header->dwCaps2);
	if (FAILED(hr)) {
		return hr;
	}

	hr = ReadDword(stream, &header->dwCaps3);
	if (FAILED(hr)) {
		return hr;
	}

	hr = ReadDword(stream, &header->dwCaps4);
	if (FAILED(hr)) {
		return hr;
	}

	hr = ReadDword(stream, &header->dwReserved2);
	if (FAILED(hr)) {
		return hr;
	}

	return S_OK;
}


static HRESULT LoadSpriteFrameV3(IStream* stream, PSPRITE_FRAME_V3* result) {
	HRESULT hr;

	PSPRITE_FRAME_V3 frame = (PSPRITE_FRAME_V3)malloc(sizeof(SPRITE_FRAME_V3));

	if (frame == NULL) {
		return E_OUTOFMEMORY;
	}

	memset(frame, 0, sizeof(SPRITE_FRAME_V3));

	DWORD ddsMagic;

	hr = ReadDword(stream, &ddsMagic);
	if (FAILED(hr)) {
		free(frame);
		return hr;
	}

	// DDS
	if (ddsMagic != 0x20534444) {
		free(frame);
		return E_NOTIMPL;
	}

	DDS_HEADER ddsHeader;

	hr = ReadDdsHeader(stream, &ddsHeader);
	if (FAILED(hr)) {
		free(frame);
		return hr;
	}

	if (ddsHeader.dwWidth < 1 || ddsHeader.dwWidth > 0x7FFFFFFF) {
		free(frame);
		return E_UNEXPECTED;
	}

	if (ddsHeader.dwHeight < 1 || ddsHeader.dwHeight > 0x7FFFFFFF) {
		free(frame);
		return E_UNEXPECTED;
	}

	if (ddsHeader.dwMipMapCount != 1) {
		free(frame);
		return E_NOTIMPL;
	}

	frame->Header.Width = (INT32)ddsHeader.dwWidth;
	frame->Header.Height = (INT32)ddsHeader.dwHeight;
	frame->Header.Format = ddsHeader.ddspf.dwFourCC;

	DWORD dataSize = 0;

	switch (ddsHeader.ddspf.dwFourCC) {
		// DXT5
		case 0x35545844: {
			dataSize = max(1, ((ddsHeader.dwHeight + 3) / 4)) * max(1, ((ddsHeader.dwWidth + 3) / 4)) * 16;
			break;
		}
		default: {
			free(frame);
			return E_NOTIMPL;
		}
	}

	frame->Pixels = (PBYTE)malloc(dataSize);

	if (frame->Pixels == NULL) {
		free(frame);
		return E_OUTOFMEMORY;
	}

	hr = ReadBytes(stream, frame->Pixels, dataSize);
	if (FAILED(hr)) {
		free(frame->Pixels);
		free(frame);
		return hr;
	}

	*result = frame;

	return S_OK;
}


VOID FreeSpriteFileV3(PSPRITE_FILE_V3 sprite) {
	if (sprite->Frames) {
		for (INT32 i = 0; i < sprite->Header.FrameCount; i++) {
			PSPRITE_FRAME_V3 frame = sprite->Frames[i];
			if (frame) {
				if (frame->Pixels) {
					free(frame->Pixels);
				}
				free(frame);
			}
		}
		free(sprite->Frames);
	}
	free(sprite);
}


HRESULT LoadSpriteFileV3(IStream* stream, PSPRITE_FILE_V3* result) {
	HRESULT hr;

	PSPRITE_FILE_V3 sprite = (PSPRITE_FILE_V3)malloc(sizeof(SPRITE_FILE_V3));

	if (sprite == NULL) {
		return E_OUTOFMEMORY;
	}

	memset(sprite, 0, sizeof(SPRITE_FILE_V3));

	//
	// File Header
	//

	hr = ReadInt32(stream, &sprite->Header.ID);
	if (FAILED(hr)) {
		FreeSpriteFileV3(sprite);
		return hr;
	}

	if (sprite->Header.ID != 0x50534449) {
		FreeSpriteFileV3(sprite);
		return E_UNEXPECTED;
	}

	hr = ReadInt32(stream, &sprite->Header.Version);
	if (FAILED(hr)) {
		FreeSpriteFileV3(sprite);
		return hr;
	}

	if (sprite->Header.Version != 3) {
		FreeSpriteFileV3(sprite);
		return E_UNEXPECTED;
	}

	hr = ReadInt32(stream, &sprite->Header.Type);
	if (FAILED(hr)) {
		FreeSpriteFileV3(sprite);
		return hr;
	}

	hr = ReadInt32(stream, &sprite->Header.TexFormat);
	if (FAILED(hr)) {
		FreeSpriteFileV3(sprite);
		return hr;
	}

	hr = ReadFloat(stream, &sprite->Header.BoundingRadius);
	if (FAILED(hr)) {
		FreeSpriteFileV3(sprite);
		return hr;
	}

	hr = ReadInt32(stream, &sprite->Header.Width);
	if (FAILED(hr)) {
		FreeSpriteFileV3(sprite);
		return hr;
	}

	if (sprite->Header.Width < 1) {
		FreeSpriteFileV3(sprite);
		return E_UNEXPECTED;
	}

	hr = ReadInt32(stream, &sprite->Header.Height);
	if (FAILED(hr)) {
		FreeSpriteFileV3(sprite);
		return hr;
	}

	if (sprite->Header.Height < 1) {
		FreeSpriteFileV3(sprite);
		return E_UNEXPECTED;
	}

	hr = ReadInt32(stream, &sprite->Header.FrameCount);
	if (FAILED(hr)) {
		FreeSpriteFileV3(sprite);
		return hr;
	}

	if (sprite->Header.FrameCount < 1) {
		FreeSpriteFileV3(sprite);
		return E_UNEXPECTED;
	}

	hr = ReadFloat(stream, &sprite->Header.BeamLength);
	if (FAILED(hr)) {
		FreeSpriteFileV3(sprite);
		return hr;
	}

	hr = ReadInt32(stream, &sprite->Header.SyncType);
	if (FAILED(hr)) {
		FreeSpriteFileV3(sprite);
		return hr;
	}

	//
	// Frames
	//

	size_t frameArraySize = sizeof(PSPRITE_FRAME_V3) * (size_t)sprite->Header.FrameCount;

	sprite->Frames = (PSPRITE_FRAME_V3*)malloc(frameArraySize);

	if (sprite->Frames == NULL) {
		FreeSpriteFileV3(sprite);
		return E_OUTOFMEMORY;
	}

	for (INT32 i = 0; i < sprite->Header.FrameCount; i++) {
		hr = LoadSpriteFrameV3(stream, &sprite->Frames[i]);
		if (FAILED(hr)) {
			FreeSpriteFileV3(sprite);
			return hr;
		}
	}

	*result = sprite;

	return S_OK;
}
