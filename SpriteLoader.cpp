#include <Windows.h>

#include "SpriteFile.h"
#include "SpriteFileV3.h"

#include "dxt.hpp"


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


static PSPRITE_FRAME_SINGLE SelectFirstFrame(PSPRITE_FILE pSprite)
{
	for (INT32 i = 0; i < pSprite->Header.FrameCount; i++)
	{
		PSPRITE_FRAME pFrame = pSprite->Frames[i];

		if (pFrame->Type == SPR_SINGLE)
		{
			return pFrame->u.Single;
		}

		if (pFrame->Type == SPR_GROUP)
		{
			PSPRITE_FRAME_GROUP pGroup = pFrame->u.Group;

			if (pGroup->Frames)
			{
				for (INT32 j = 0; j < pGroup->FrameCount; j++)
				{
					return pGroup->Frames[j];
				}
			}
		}
	}

	return NULL;
}


static HRESULT ConvertFrameToRGB(PSPRITE_FILE pSprite, PSPRITE_FRAME_SINGLE frame, PBYTE* ppResult)
{
	int nWidth = frame->Header.Width;
	int nHeight = frame->Header.Height;

	size_t nSize = (size_t)nWidth * (size_t)nHeight * 3;

	BYTE* pBuffer = (BYTE*)malloc(nSize);

	if (pBuffer == NULL)
	{
		return E_OUTOFMEMORY;
	}

	COLOR24* pColors = pSprite->Palette.Colors;

	BYTE* pSrc = frame->Pixels;
	BYTE* pDst = pBuffer;

	int nCount = nWidth * nHeight;

	for (int i = 0; i < nCount; i++)
	{
		BYTE index = *pSrc;
		COLOR24* color = &pColors[index];

		*(pDst + 0) = color->R;
		*(pDst + 1) = color->G;
		*(pDst + 2) = color->B;

		pSrc += 1;
		pDst += 3;
	}

	*ppResult = pBuffer;

	return S_OK;
}


static HRESULT LoadSpriteV2(IStream* pStream, INT32* pWidth, INT32* pHeight, PVOID* ppRgb) {
	HRESULT hr;

	// Load SPR file

	PSPRITE_FILE pSprite;

	hr = LoadSpriteFile(pStream, &pSprite);
	if (FAILED(hr)) {
		return hr;
	}

	// Get first frame

	PSPRITE_FRAME_SINGLE pFrame = SelectFirstFrame(pSprite);
	if (pFrame == NULL) {
		FreeSpriteFile(pSprite);
		return E_UNEXPECTED;
	}

	// Convert to RGB

	BYTE* pRgb;

	hr = ConvertFrameToRGB(pSprite, pFrame, &pRgb);

	if (FAILED(hr)) {
		FreeSpriteFile(pSprite);
		return hr;
	}

	*pWidth = pFrame->Header.Width;
	*pHeight = pFrame->Header.Height;
	*ppRgb = pRgb;

	FreeSpriteFile(pSprite);

	return S_OK;
}


static HRESULT ConvertDXT5(INT32 nWidth, INT32 nHeight, PVOID pInput, PVOID* pOutput) {
	INT32 nBufferWidth = max(1, ((nWidth + 3) / 4)) * 4;
	INT32 nBufferHeight = max(1, ((nHeight + 3) / 4)) * 4;

	size_t nBufferSize = (size_t)nBufferWidth * (size_t)nBufferHeight * 3;

	PBYTE pBuffer = (PBYTE)malloc(nBufferSize);

	if (pBuffer == NULL) {
		return E_OUTOFMEMORY;
	}

	DecompressDXT5((uint8_t*)pInput, nWidth, nHeight, (RGB24*)pBuffer);

	size_t nOutputBufferSize = (size_t)nWidth * (size_t)nHeight * 3;

	PBYTE pOutputBuffer = (PBYTE)malloc(nOutputBufferSize);

	if (pOutputBuffer == NULL) {
		free(pBuffer);
		return E_OUTOFMEMORY;
	}

	for (INT32 Y = 0; Y < nHeight; Y++) {
		for (INT32 X = 0; X < nWidth; X++) {
			PBYTE pSrc = &pBuffer[(Y * nBufferWidth + X) * 3];
			PBYTE pDst = &pOutputBuffer[(Y * nWidth + X) * 3];

			pDst[0] = pSrc[0]; // R
			pDst[1] = pSrc[1]; // G
			pDst[2] = pSrc[2]; // B
		}
	}

	free(pBuffer);

	*pOutput = pOutputBuffer;

	return S_OK;
}


static HRESULT LoadSpriteV3(IStream* pStream, INT32* pWidth, INT32* pHeight, PVOID* ppRgb) {
	HRESULT hr;

	// Load SPR file

	PSPRITE_FILE_V3 pSprite;

	hr = LoadSpriteFileV3(pStream, &pSprite);
	if (FAILED(hr)) {
		return hr;
	}

	// Get first frame
	PSPRITE_FRAME_V3 pFrame = pSprite->Frames[0];

	// Convert to RGB

	PVOID pRgb = NULL;

	switch (pFrame->Header.Format) {
		// DXT5
		case 0x35545844: {
			hr = ConvertDXT5(pFrame->Header.Width, pFrame->Header.Height, pFrame->Pixels, &pRgb);
			break;
		}
		default: {
			hr = E_NOTIMPL;
			break;
		}
	}

	if (FAILED(hr)) {
		FreeSpriteFileV3(pSprite);
		return hr;
	}

	*pWidth = pFrame->Header.Width;
	*pHeight = pFrame->Header.Height;
	*ppRgb = pRgb;

	FreeSpriteFileV3(pSprite);

	return S_OK;
}


HRESULT LoadSpriteToRGB(IStream* pStream, INT32* pWidth, INT32* pHeight, PVOID* ppRgb) {
	HRESULT hr;

	LARGE_INTEGER pos;
	memset(&pos, 0, sizeof(LARGE_INTEGER));

	pStream->Seek(pos, STREAM_SEEK_SET, NULL);

	DWORD magic;

	hr = ReadDword(pStream, &magic);
	if (FAILED(hr)) {
		return hr;
	}

	// IDSP
	if (magic != 0x50534449) {
		return hr;
	}

	DWORD version;

	hr = ReadDword(pStream, &version);
	if (FAILED(hr)) {
		return hr;
	}

	pStream->Seek(pos, STREAM_SEEK_SET, NULL);

	switch (version) {
		case 2: {
			return LoadSpriteV2(pStream, pWidth, pHeight, ppRgb);
		}
		case 3: {
			return LoadSpriteV3(pStream, pWidth, pHeight, ppRgb);
		}
	}

	return E_NOTIMPL;
}
