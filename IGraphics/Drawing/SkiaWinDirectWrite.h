#pragma once

#if defined(OS_WIN)

#include "include/core/SkRefCnt.h"

class SkFontMgr;
struct IDWriteFactory;
struct IDWriteFontCollection;
struct IDWriteFontFallback;

sk_sp<SkFontMgr> SkFontMgr_New_DirectWrite(IDWriteFactory* factory = nullptr,
                                           IDWriteFontCollection* collection = nullptr);
sk_sp<SkFontMgr> SkFontMgr_New_DirectWrite(IDWriteFactory* factory,
                                           IDWriteFontCollection* collection,
                                           IDWriteFontFallback* fallback);

#endif // defined(OS_WIN)
