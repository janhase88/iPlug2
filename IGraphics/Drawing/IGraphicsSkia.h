#pragma once

#include "IGraphics.h"
#include "IPlugPlatform.h"

// N.B. - this must be defined according to the skia build, not the iPlug build
#if (defined OS_MAC || defined OS_IOS) && !defined IGRAPHICS_SKIA_NO_METAL
  #define SK_METAL
#endif

#if defined IGRAPHICS_GL
  #define SK_GL
#endif

#pragma warning(push)
#pragma warning(disable : 4244)
#include "include/core/SkCanvas.h"
#include "include/core/SkImage.h"
#include "include/core/SkPath.h"
#include "include/core/SkSurface.h"
#include "include/gpu/GrDirectContext.h"
#pragma warning(pop)

#if !defined IGRAPHICS_NO_SKIA_SKPARAGRAPH
  #include "modules/skparagraph/include/FontCollection.h"
  #include "modules/skparagraph/include/TypefaceFontProvider.h" // <-- ADD THIS LINE
#endif

namespace skia::textlayout
{
class FontCollection;
}

BEGIN_IPLUG_NAMESPACE
BEGIN_IGRAPHICS_NAMESPACE

/** Converts IRECT to a SkRect */
SkRect SkiaRect(const IRECT& r);

/** Converts IBlend to a SkBlendMode */
SkBlendMode SkiaBlendMode(const IBlend* pBlend);

/** Converts IColor to a SkColor */
SkColor SkiaColor(const IColor& color, const IBlend* pBlend);

/** Get SkTileMode for IPattern */
SkTileMode SkiaTileMode(const IPattern& pattern);

/** Converts IPattern to SkPaint */
SkPaint SkiaPaint(const IPattern& pattern, const IBlend* pBlend);

/** IGraphics draw class using Skia
 *   @ingroup DrawClasses */
class IGraphicsSkia : public IGraphics
{
private:
  class Bitmap;
  struct Font;

public:
  IGraphicsSkia(IGEditorDelegate& dlg, int w, int h, int fps, float scale);
  ~IGraphicsSkia();

  const char* GetDrawingAPIStr() override;

  void BeginFrame() override;
  void EndFrame() override;
  void OnViewInitialized(void* pContext) override;
  void OnViewDestroyed() override;
  void DrawResize() override;

  void DrawBitmap(const IBitmap& bitmap, const IRECT& dest, int srcX, int srcY, const IBlend* pBlend) override;

  void PathClear() override { mMainPath.reset(); }
  void PathClose() override { mMainPath.close(); }
  void PathArc(float cx, float cy, float r, float a1, float a2, EWinding winding) override;

  void PathMoveTo(float x, float y) override { mMainPath.moveTo(mMatrix.mapXY(x, y)); }
  void PathLineTo(float x, float y) override { mMainPath.lineTo(mMatrix.mapXY(x, y)); }

  void PathCubicBezierTo(float x1, float y1, float x2, float y2, float x3, float y3) override { mMainPath.cubicTo(mMatrix.mapXY(x1, y1), mMatrix.mapXY(x2, y2), mMatrix.mapXY(x3, y3)); }

  void PathQuadraticBezierTo(float cx, float cy, float x2, float y2) override { mMainPath.quadTo(mMatrix.mapXY(cx, cy), mMatrix.mapXY(x2, y2)); }

  void PathStroke(const IPattern& pattern, float thickness, const IStrokeOptions& options, const IBlend* pBlend) override;
  void PathFill(const IPattern& pattern, const IFillOptions& options, const IBlend* pBlend) override;

#ifdef IGRAPHICS_DRAWFILL_DIRECT
  // void DrawPoint(const IColor& color, float x, float y, const IBlend* pBlend) override;
  // void DrawLine(const IColor& color, float x1, float y1, float x2, float y2, const IBlend* pBlend, float thickness) override;
  // void DrawGrid(const IColor& color, const IRECT& bounds, float gridSizeH, float gridSizeV, const IBlend* pBlend, float thickness) override;
  // void DrawData(const IColor& color, const IRECT& bounds, float* normYPoints, int nPoints, float* normXPoints, const IBlend* pBlend, float thickness) override;
  // void DrawDottedLine(const IColor& color, float x1, float y1, float x2, float y2, const IBlend* pBlend, float thickness, float dashLen) override;
  // void DrawTriangle(const IColor& color, float x1, float y1, float x2, float y2, float x3, float y3, const IBlend* pBlend, float thickness) override;
  void DrawRect(const IColor& color, const IRECT& bounds, const IBlend* pBlend, float thickness) override;
  void DrawRoundRect(const IColor& color, const IRECT& bounds, float cornerRadius, const IBlend* pBlend, float thickness) override;
  // void DrawRoundRect(const IColor& color, const IRECT& bounds, float cRTL, float cRTR, float cRBR, float cRBL, const IBlend* pBlend, float thickness) override;
  // void DrawConvexPolygon(const IColor& color, float* x, float* y, int nPoints, const IBlend* pBlend, float thickness) override;
  void DrawArc(const IColor& color, float cx, float cy, float r, float a1, float a2, const IBlend* pBlend, float thickness) override;
  void DrawCircle(const IColor& color, float cx, float cy, float r, const IBlend* pBlend, float thickness) override;
  // void DrawDottedRect(const IColor& color, const IRECT& bounds, const IBlend* pBlend, float thickness, float dashLen) override;
  void DrawEllipse(const IColor& color, const IRECT& bounds, const IBlend* pBlend, float thickness) override;
  // void DrawEllipse(const IColor& color, float x, float y, float r1, float r2, float angle, const IBlend* pBlend, float thickness) override;

  // void FillTriangle(const IColor& color, float x1, float y1, float x2, float y2, float x3, float y3, const IBlend* pBlend) override;
  void FillRect(const IColor& color, const IRECT& bounds, const IBlend* pBlend) override;
  void FillRoundRect(const IColor& color, const IRECT& bounds, float cornerRadius, const IBlend* pBlend) override;
  // void FillRoundRect(const IColor& color, const IRECT& bounds, float cRTL, float cRTR, float cRBR, float cRBL, const IBlend* pBlend) override;
  // void FillConvexPolygon(const IColor& color, float* x, float* y, int nPoints, const IBlend* pBlend) override;
  void FillArc(const IColor& color, float cx, float cy, float r, float a1, float a2, const IBlend* pBlend) override;
  void FillCircle(const IColor& color, float cx, float cy, float r, const IBlend* pBlend) override;
  void FillEllipse(const IColor& color, const IRECT& bounds, const IBlend* pBlend) override;
  // void FillEllipse(const IColor& color, float x, float y, float r1, float r2, float angle, const IBlend* pBlend) override;
#endif
  void DrawFastDropShadow(const IRECT& innerBounds, const IRECT& outerBounds, float xyDrop, float roundness, float blur, IBlend* pBlend) override;

  IColor GetPoint(int x, int y) override;
  void* GetDrawContext() override { return (void*)mCanvas; }

  bool BitmapExtSupported(const char* ext) override;
  int AlphaChannel() const override { return 3; }
  bool FlippedBitmap() const override { return false; }

  APIBitmap* CreateAPIBitmap(int width, int height, float scale, double drawScale, bool cacheable = false, int MSAASampleCount = 0) override;

  void ApplyLayerDropShadow(ILayerPtr& layer, const IShadow& shadow) override;
  void GetLayerBitmapData(const ILayerPtr& layer, RawBitmapData& data) override;
  void ApplyShadowMask(ILayerPtr& layer, RawBitmapData& mask, const IShadow& shadow) override;

  void UpdateLayer() override;

  void DrawMultiLineText(const IText& text, const char* str, const IRECT& bounds, const IBlend* pBlend) override;

protected:
  void CleanUpSkiaStatics();
  float DoMeasureText(const IText& text, const char* str, IRECT& bounds) const override;
  void DoDrawText(const IText& text, const char* str, const IRECT& bounds, const IBlend* pBlend) override;

  bool LoadAPIFont(const char* fontID, const PlatformFontPtr& font) override;

  APIBitmap* LoadAPIBitmap(const char* fileNameOrResID, int scale, EResourceLocation location, const char* ext) override;
  APIBitmap* LoadAPIBitmap(const char* name, const void* pData, int dataSize, int scale) override;

private:
  void PrepareAndMeasureText(const IText& text, const char* str, IRECT& r, double& x, double& y, SkFont& font) const;

  void PathTransformSetMatrix(const IMatrix& m) override;
  void SetClipRegion(const IRECT& r) override;

  void RenderPath(SkPaint& paint);

  sk_sp<SkSurface> mSurface;
  SkCanvas* mCanvas = nullptr;
  SkPath mMainPath;
  SkMatrix mMatrix;
  SkMatrix mClipMatrix;
  SkMatrix mFinalMatrix;

#if defined OS_WIN && defined IGRAPHICS_CPU
  WDL_TypedBuf<uint8_t> mSurfaceMemory;
#endif

#ifndef IGRAPHICS_CPU
  sk_sp<GrDirectContext> mGrContext;
  sk_sp<SkSurface> mScreenSurface;
#endif

#if !defined IGRAPHICS_NO_SKIA_SKPARAGRAPH
  sk_sp<skia::textlayout::FontCollection> mFontCollection;
  sk_sp<skia::textlayout::TypefaceFontProvider> mTypefaceProvider;
  sk_sp<SkFontMgr> mFontMgr;
  static sk_sp<SkFontMgr> SParagraphFontMgr();
#endif

#ifdef IGRAPHICS_METAL
  void* mMTLDevice;
  void* mMTLCommandQueue;
  void* mMTLDrawable;
  void* mMTLLayer;
#endif

  static StaticStorage<Font> sFontCache;
};

END_IGRAPHICS_NAMESPACE
END_IPLUG_NAMESPACE
