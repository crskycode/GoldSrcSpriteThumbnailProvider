// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved

#include <shlwapi.h>
#include <Wincrypt.h>   // For CryptStringToBinary.
#include <thumbcache.h> // For IThumbnailProvider.
#include <wincodec.h>   // Windows Imaging Codecs
#include <msxml6.h>
#include <new>
#include "SpriteFile.h"
#include "stb_image_resize2.h"

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "Crypt32.lib")
#pragma comment(lib, "msxml6.lib")

// this thumbnail provider implements IInitializeWithStream to enable being hosted
// in an isolated process for robustness

class CSpriteThumbProvider
	: public IInitializeWithStream
	, public IThumbnailProvider
{
public:
	CSpriteThumbProvider()
		: _cRef(1)
		, _pStream(NULL)
	{
	}

	virtual ~CSpriteThumbProvider()
	{
		if (_pStream)
		{
			_pStream->Release();
		}
	}

	// IUnknown
	IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv)
	{
		static const QITAB qit[] =
		{
			QITABENT(CSpriteThumbProvider, IInitializeWithStream),
			QITABENT(CSpriteThumbProvider, IThumbnailProvider),
			{ 0 },
		};
		return QISearch(this, qit, riid, ppv);
	}

	IFACEMETHODIMP_(ULONG) AddRef()
	{
		return InterlockedIncrement(&_cRef);
	}

	IFACEMETHODIMP_(ULONG) Release()
	{
		ULONG cRef = InterlockedDecrement(&_cRef);
		if (!cRef)
		{
			delete this;
		}
		return cRef;
	}

	// IInitializeWithStream
	IFACEMETHODIMP Initialize(IStream* pStream, DWORD grfMode);

	// IThumbnailProvider
	IFACEMETHODIMP GetThumbnail(UINT cx, HBITMAP* phbmp, WTS_ALPHATYPE* pdwAlpha);

private:
	long _cRef;
	IStream* _pStream;     // provided during initialization.
};

HRESULT CSpriteThumbProvider_CreateInstance(REFIID riid, void** ppv)
{
	CSpriteThumbProvider* pNew = new (std::nothrow) CSpriteThumbProvider();
	HRESULT hr = pNew ? S_OK : E_OUTOFMEMORY;
	if (SUCCEEDED(hr))
	{
		hr = pNew->QueryInterface(riid, ppv);
		pNew->Release();
	}
	return hr;
}

// IInitializeWithStream
IFACEMETHODIMP CSpriteThumbProvider::Initialize(IStream* pStream, DWORD)
{
	HRESULT hr = E_UNEXPECTED;  // can only be inited once
	if (_pStream == NULL)
	{
		// take a reference to the stream if we have not been inited yet
		hr = pStream->QueryInterface(&_pStream);
	}
	return hr;
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

static HRESULT ScaleImage(int nNewWidth, int nNewHeight, PSPRITE_FRAME_SINGLE pFrame, const BYTE* pPixels, BYTE** ppResult)
{
	int nWidth = pFrame->Header.Width;
	int nHeight = pFrame->Header.Height;

	size_t nSize = (size_t)nNewWidth * (size_t)nNewHeight * 3;

	BYTE* pBuffer = (BYTE*)malloc(nSize);

	if (pBuffer == NULL)
	{
		return E_OUTOFMEMORY;
	}

	memset(pBuffer, 0, nSize);

	stbir_resize(pPixels, nWidth, nHeight, nWidth * 3, pBuffer, nNewWidth, nNewHeight, nNewWidth * 3,
		STBIR_RGB, STBIR_TYPE_UINT8, STBIR_EDGE_ZERO, STBIR_FILTER_DEFAULT);

	*ppResult = pBuffer;

	return S_OK;
}

static HRESULT CreateDIB(BYTE* pPixels, int nWidth, int nHeight, HBITMAP* ppResult)
{
	BITMAPINFO bmi;
	memset(&bmi, 0, sizeof(BITMAPINFO));

	bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
	bmi.bmiHeader.biWidth = nWidth;
	bmi.bmiHeader.biHeight = -nHeight;
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 32;
	bmi.bmiHeader.biCompression = BI_RGB;

	BYTE* pBits = NULL;

	HBITMAP hBmp = CreateDIBSection(NULL, &bmi, DIB_RGB_COLORS, (void**)&pBits, NULL, 0);

	if (hBmp == NULL)
	{
		return E_OUTOFMEMORY;
	}

	size_t nSize = (size_t)nWidth * (size_t)nHeight * 4;

	memset(pBits, 0, nSize);

	for (int Y = 0; Y < nHeight; Y++)
	{
		for (int X = 0; X < nWidth; X++)
		{
			BYTE* pSrc = &pPixels[(Y * nWidth + X) * 3];
			BYTE* pDst = &pBits[(Y * nWidth + X) * 4];

			pDst[0] = pSrc[2]; // B
			pDst[1] = pSrc[1]; // G
			pDst[2] = pSrc[0]; // R
			pDst[3] = 0xFF;
		}
	}

	*ppResult = hBmp;

	return S_OK;
}

// IThumbnailProvider
IFACEMETHODIMP CSpriteThumbProvider::GetThumbnail(UINT cx, HBITMAP* phbmp, WTS_ALPHATYPE* pdwAlpha)
{
	HRESULT hr;

	if (_pStream == NULL)
	{
		return E_UNEXPECTED;
	}

	// Start loading

	LARGE_INTEGER pos;
	memset(&pos, 0, sizeof(LARGE_INTEGER));

	_pStream->Seek(pos, STREAM_SEEK_SET, NULL);

	// Load SPR file

	PSPRITE_FILE pSprite;

	hr = LoadSpriteFile(_pStream, &pSprite);
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

	BYTE* pOriginalImagePixels;

	hr = ConvertFrameToRGB(pSprite, pFrame, &pOriginalImagePixels);
	if (FAILED(hr)) {
		FreeSpriteFile(pSprite);
		return hr;
	}

	// Scale image

	BYTE* pScaledImagePixels;

	int nNewWidth = cx;
	int nNewHeight = (int)((float)cx / ((float)pFrame->Header.Width / (float)pFrame->Header.Height));

	hr = ScaleImage(nNewWidth, nNewHeight, pFrame, pOriginalImagePixels, &pScaledImagePixels);

	free(pOriginalImagePixels);

	FreeSpriteFile(pSprite);

	if (FAILED(hr)) {
		return hr;
	}

	// Create Bitmap Object

	hr = CreateDIB(pScaledImagePixels, nNewWidth, nNewHeight, phbmp);

	free(pScaledImagePixels);

	if (FAILED(hr)) {
		return hr;
	}

	// Finish

	*pdwAlpha = WTSAT_ARGB;

	return S_OK;
}
