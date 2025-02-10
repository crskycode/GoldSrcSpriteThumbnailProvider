#pragma once

#include <Windows.h>

HRESULT LoadSpriteToRGB(IStream* pStream, INT32* pWidth, INT32* pHeight, PVOID* ppRgb);
