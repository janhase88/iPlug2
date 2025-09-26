/*
 ==============================================================================

 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers.

 See LICENSE.txt for  more info.

 ==============================================================================
*/

#pragma once

#include "IPlugPlatform.h"

#include <windows.h>
#include <cstdint>
#include <cstring>
#include <new>
#include <vector>

#include "IGraphicsPrivate.h"

BEGIN_IPLUG_NAMESPACE
BEGIN_IGRAPHICS_NAMESPACE

// Fonts

class InstalledWinFont
{
public:
  InstalledWinFont(const void* data, int resSize)
  : mFontHandle(nullptr)
  {
    if (data && resSize > 0)
    {
      try
      {
        mFontData.resize(static_cast<size_t>(resSize));
      }
      catch (const std::bad_alloc&)
      {
        mFontData.clear();
      }

      if (!mFontData.empty())
      {
        std::memcpy(mFontData.data(), data, mFontData.size());

        DWORD numFonts = 0;
        mFontHandle = AddFontMemResourceEx(mFontData.data(), resSize, NULL, &numFonts);

        if (!mFontHandle)
        {
          mFontData.clear();
        }
      }
    }
  }

  ~InstalledWinFont()
  {
    if (IsValid())
      RemoveFontMemResourceEx(mFontHandle);
  }

  InstalledWinFont(const InstalledWinFont&) = delete;
  InstalledWinFont& operator=(const InstalledWinFont&) = delete;

  bool IsValid() const { return mFontHandle; }

  const void* GetData() const { return mFontData.empty() ? nullptr : mFontData.data(); }
  size_t GetSize() const { return mFontData.size(); }

private:
  HANDLE mFontHandle;
  std::vector<uint8_t> mFontData;
};

struct HFontHolder
{
  HFontHolder(HFONT hfont) : mHFont(nullptr)
  {
    LOGFONTW lFont = { 0 };
    GetObjectW(hfont, sizeof(LOGFONTW), &lFont);
    mHFont = CreateFontIndirectW(&lFont);
  }
  
  HFONT mHFont;
};

class WinFont : public PlatformFont
{
public:
  WinFont(HFONT font, const char* styleName, bool system)
  : PlatformFont(system), mFont(font), mStyleName(styleName) {}
  ~WinFont()
  {
    DeleteObject(mFont);
  }
  
  FontDescriptor GetDescriptor() override { return mFont; }
  
  IFontDataPtr GetFontData() override
  {
    HDC hdc = CreateCompatibleDC(NULL);
    IFontDataPtr fontData(new IFontData());
      
    if (hdc != NULL)
    {
      SelectObject(hdc, mFont);
      const size_t size = ::GetFontData(hdc, 0, 0, NULL, 0);

      if (size != GDI_ERROR)
      {
        fontData = std::make_unique<IFontData>(size);

        if (fontData->GetSize() == size)
        {
          size_t result = ::GetFontData(hdc, 0x66637474, 0, fontData->Get(), size);
          if (result == GDI_ERROR)
            result = ::GetFontData(hdc, 0, 0, fontData->Get(), size);
          if (result == size)
            fontData->SetFaceIdx(GetFaceIdx(fontData->Get(), fontData->GetSize(), mStyleName.Get()));
        }
      }
        
      DeleteDC(hdc);
    }

    return fontData;
  }
    
private:
  HFONT mFont;
  WDL_String mStyleName;
};

END_IGRAPHICS_NAMESPACE
END_IPLUG_NAMESPACE
