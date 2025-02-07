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
#include <vector>
#include <memory>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "Crypt32.lib")
#pragma comment(lib, "msxml6.lib")

#define DDS_MAGIC 0x20534444
#define DDS_DXT5 0x35545844 

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
		DWORD dwSize;
		DWORD dwFlags;
		DWORD dwHeight;
		DWORD dwWidth;
		DWORD dwPitchOrLinearSize;
		DWORD dwDepth;
		DWORD dwMipMapCount;
		DWORD dwReserved1[11];
		DDS_PIXELFORMAT ddspf;
		DWORD dwCaps;
		DWORD dwCaps2;
		DWORD dwCaps3;
		DWORD dwCaps4;
		DWORD dwReserved2;
	};

	HRESULT LoadSpriteV3(IStream* pStream, UINT cx, HBITMAP* phbmp, WTS_ALPHATYPE* pdwAlpha);
	HRESULT GetThumbnailV2(UINT cx, HBITMAP* phbmp, WTS_ALPHATYPE* pdwAlpha);
	void DecompressDXT5(const BYTE* compressedData, BYTE* rgbaData, UINT width, UINT height);
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
	if (_pStream == NULL)
		return E_UNEXPECTED;

	LARGE_INTEGER pos = { 0 };
	_pStream->Seek(pos, STREAM_SEEK_SET, NULL);

	// magic number and version
	DWORD magic;
	INT32 version;
	ULONG bytesRead;

	HRESULT hr = _pStream->Read(&magic, sizeof(DWORD), &bytesRead);
	if (FAILED(hr))
		return hr;

	hr = _pStream->Read(&version, sizeof(INT32), &bytesRead);
	if (FAILED(hr))
		return hr;

	_pStream->Seek(pos, STREAM_SEEK_SET, NULL);

	if (version == 2) {
		return GetThumbnailV2(cx, phbmp, pdwAlpha);
	}
	else if (version == 3) {// for CSO/CSN
		return LoadSpriteV3(_pStream, cx, phbmp, pdwAlpha);
	}

	return E_FAIL;
}

HRESULT CSpriteThumbProvider::GetThumbnailV2(UINT cx, HBITMAP* phbmp, WTS_ALPHATYPE* pdwAlpha)
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

HRESULT CSpriteThumbProvider::LoadSpriteV3(IStream* pStream, UINT cx, HBITMAP* phbmp, WTS_ALPHATYPE* pdwAlpha)
{
	HRESULT hr = S_OK;
	
	DWORD idspMagic;
	ULONG bytesRead;
	hr = pStream->Read(&idspMagic, sizeof(DWORD), &bytesRead);
	if (FAILED(hr) || idspMagic != 0x50534449)
		return E_FAIL;

	INT32 version;
	hr = pStream->Read(&version, sizeof(INT32), &bytesRead);
	if (FAILED(hr) || version != 3)
		return E_FAIL;

	INT32 type, texFormat;
	float boundingRadius;
	INT32 width, height, numFrames;
	float beamLength;
	INT32 syncType;

	hr = pStream->Read(&type, sizeof(INT32), &bytesRead);
	hr = pStream->Read(&texFormat, sizeof(INT32), &bytesRead);
	hr = pStream->Read(&boundingRadius, sizeof(float), &bytesRead);
	hr = pStream->Read(&width, sizeof(INT32), &bytesRead);
	hr = pStream->Read(&height, sizeof(INT32), &bytesRead);
	hr = pStream->Read(&numFrames, sizeof(INT32), &bytesRead);
	hr = pStream->Read(&beamLength, sizeof(float), &bytesRead);
	hr = pStream->Read(&syncType, sizeof(INT32), &bytesRead);

	DWORD signature;
	bool foundDDS = false;
	LARGE_INTEGER offset = {0};

	while (SUCCEEDED(pStream->Read(&signature, sizeof(DWORD), &bytesRead)) && bytesRead == sizeof(DWORD)) {
		if (signature == DDS_MAGIC) {
			foundDDS = true;
			break;
		}
		offset.QuadPart -= sizeof(DWORD);
		pStream->Seek(offset, STREAM_SEEK_CUR, NULL);
	}

	if (!foundDDS)
		return E_FAIL;

	DDS_HEADER ddsHeader; 
	hr = pStream->Read(&ddsHeader, sizeof(DDS_HEADER), &bytesRead);
	if (FAILED(hr))
		return hr;

	
	DWORD blockSize = 16; // DXT5
	DWORD blocksWide = (ddsHeader.dwWidth + 3) / 4;
	DWORD blocksHigh = (ddsHeader.dwHeight + 3) / 4;
	DWORD dataSize = blocksWide * blocksHigh * blockSize;

	std::vector<BYTE> compressedData(dataSize);
	hr = pStream->Read(compressedData.data(), dataSize, &bytesRead);
	if (FAILED(hr))
		return hr;

	std::vector<BYTE> rgbaData(ddsHeader.dwWidth * ddsHeader.dwHeight * 4);
	DecompressDXT5(compressedData.data(), rgbaData.data(), ddsHeader.dwWidth, ddsHeader.dwHeight);

	UINT newHeight = (UINT)((float)cx * ddsHeader.dwHeight / ddsHeader.dwWidth);
	std::vector<BYTE> scaledData(cx * newHeight * 4);

	float xRatio = (float)ddsHeader.dwWidth / cx;
	float yRatio = (float)ddsHeader.dwHeight / newHeight;

	for (UINT y = 0; y < newHeight; y++) {
		for (UINT x = 0; x < cx; x++) {
			float srcX = x * xRatio;
			float srcY = y * yRatio;
			UINT srcIndex = ((UINT)srcY * ddsHeader.dwWidth + (UINT)srcX) * 4;
			UINT dstIndex = (y * cx + x) * 4;

			scaledData[dstIndex + 0] = rgbaData[srcIndex + 0];
			scaledData[dstIndex + 1] = rgbaData[srcIndex + 1];
			scaledData[dstIndex + 2] = rgbaData[srcIndex + 2];
			scaledData[dstIndex + 3] = rgbaData[srcIndex + 3];
		}
	}

	BITMAPINFO bmi = { 0 };
	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth = cx;
	bmi.bmiHeader.biHeight = -(INT)newHeight;
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 32;
	bmi.bmiHeader.biCompression = BI_RGB;

	BYTE* pBits = NULL;
	HBITMAP hBmp = CreateDIBSection(NULL, &bmi, DIB_RGB_COLORS, (void**)&pBits, NULL, 0);
	if (!hBmp)
		return E_FAIL;

	for (UINT i = 0; i < cx * newHeight; i++) {
		pBits[i * 4 + 0] = scaledData[i * 4 + 2]; // B
		pBits[i * 4 + 1] = scaledData[i * 4 + 1]; // G
		pBits[i * 4 + 2] = scaledData[i * 4 + 0]; // R
		pBits[i * 4 + 3] = scaledData[i * 4 + 3]; // A
	}

	*phbmp = hBmp;
	*pdwAlpha = WTSAT_ARGB;

	return S_OK;
}

void CSpriteThumbProvider::DecompressDXT5(const BYTE* compressedData, BYTE* rgbaData, UINT width, UINT height)
{
	for (UINT y = 0; y < height; y += 4) {
		for (UINT x = 0; x < width; x += 4) {
			const BYTE* block = compressedData + ((y/4) * ((width+3)/4) + (x/4)) * 16;
			
			BYTE alpha[8];
			alpha[0] = block[0];
			alpha[1] = block[1];
			
			UINT64 alphaBits = 
				((UINT64)block[2]) | 
				((UINT64)block[3] << 8) | 
				((UINT64)block[4] << 16) |
				((UINT64)block[5] << 24) |
				((UINT64)block[6] << 32) |
				((UINT64)block[7] << 40);

			UINT16 color0 = *(UINT16*)(block + 8);
			UINT16 color1 = *(UINT16*)(block + 10);
			
			BYTE r0 = ((color0 >> 11) & 0x1F) << 3;
			BYTE g0 = ((color0 >> 5) & 0x3F) << 2;
			BYTE b0 = (color0 & 0x1F) << 3;
			
			BYTE r1 = ((color1 >> 11) & 0x1F) << 3;
			BYTE g1 = ((color1 >> 5) & 0x3F) << 2;
			BYTE b1 = (color1 & 0x1F) << 3;
			
			UINT32 colorBits = *(UINT32*)(block + 12);

			// 4x4
			for (UINT by = 0; by < 4; by++) {
				for (UINT bx = 0; bx < 4; bx++) {
					if ((x + bx) < width && (y + by) < height) {
						UINT index = ((y + by) * width + (x + bx)) * 4;
						
						UINT alphaIndex = (alphaBits >> ((by * 4 + bx) * 3)) & 0x7;
						rgbaData[index + 3] = alpha[alphaIndex];
						
						UINT colorIndex = (colorBits >> ((by * 4 + bx) * 2)) & 0x3;
						switch(colorIndex) {
							case 0:
								rgbaData[index + 0] = r0;
								rgbaData[index + 1] = g0;
								rgbaData[index + 2] = b0;
								break;
							case 1:
								rgbaData[index + 0] = r1;
								rgbaData[index + 1] = g1;
								rgbaData[index + 2] = b1;
								break;
							case 2:
								rgbaData[index + 0] = (2 * r0 + r1) / 3;
								rgbaData[index + 1] = (2 * g0 + g1) / 3;
								rgbaData[index + 2] = (2 * b0 + b1) / 3;
								break;
							case 3:
								rgbaData[index + 0] = (r0 + 2 * r1) / 3;
								rgbaData[index + 1] = (g0 + 2 * g1) / 3;
								rgbaData[index + 2] = (b0 + 2 * b1) / 3;
								break;
						}
					}
				}
			}
		}
	}
}
