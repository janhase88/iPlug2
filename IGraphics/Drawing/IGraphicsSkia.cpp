#include <cmath>
#include <map>

#include "IGraphicsSkia.h"

#pragma warning(push)
#pragma warning(disable : 4244)
#include "include/core/SkBitmap.h"
#include "include/core/SkBlurTypes.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontMetrics.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkMaskFilter.h"
#include "include/core/SkPathEffect.h"
#include "include/core/SkSwizzle.h"
#include "include/core/SkTypeface.h"
#include "include/core/SkVertices.h"
#include "include/effects/SkDashPathEffect.h"
#include "include/effects/SkGradientShader.h"
#include "include/effects/SkImageFilters.h"

#if !defined IGRAPHICS_NO_SKIA_SKPARAGRAPH
  #include "modules/skparagraph/include/FontCollection.h"
  #include "modules/skparagraph/include/Paragraph.h"
  #include "modules/skparagraph/include/ParagraphBuilder.h"
  #include "modules/skparagraph/include/ParagraphStyle.h"
  #include "modules/skparagraph/include/TextStyle.h"
  #include "modules/skparagraph/include/TypefaceFontProvider.h"
  #include "modules/skshaper/include/SkShaper.h"
  #include "modules/skunicode/include/SkUnicode_icu.h"
#endif
#pragma warning(pop)
#include "include/gpu/GrBackendSurface.h"
#include "include/gpu/ganesh/SkSurfaceGanesh.h"

#if defined OS_MAC || defined OS_IOS
  #include "include/utils/mac/SkCGUtils.h"
  #if defined IGRAPHICS_GL2
    #error SKIA doesn't work correctly with IGRAPHICS_GL2
  #elif defined IGRAPHICS_GL3
    #include <OpenGL/gl3.h>
  #elif defined IGRAPHICS_METAL
    // even though this is a .cpp we are in an objc(pp) compilation unit
    #include "include/gpu/ganesh/mtl/GrMtlBackendContext.h"
    #include "include/gpu/ganesh/mtl/GrMtlBackendSurface.h"
    #include "include/gpu/ganesh/mtl/GrMtlDirectContext.h"
    #import <Metal/Metal.h>
    #import <QuartzCore/CAMetalLayer.h>
  #elif !defined IGRAPHICS_CPU
    #error Define either IGRAPHICS_GL2, IGRAPHICS_GL3, IGRAPHICS_METAL, or IGRAPHICS_CPU for IGRAPHICS_SKIA with OS_MAC
  #endif

  #include "include/ports/SkFontMgr_mac_ct.h"

#elif defined OS_WIN
  #include "include/ports/SkTypeface_win.h"

  #pragma comment(lib, "skia.lib")

  #ifndef IGRAPHICS_NO_SKIA_SKPARAGRAPH
    #pragma comment(lib, "skparagraph.lib")
    #pragma comment(lib, "skshaper.lib")
    #pragma comment(lib, "skunicode_core.lib")
    #pragma comment(lib, "skunicode_icu.lib")
  #endif

#endif

#if defined IGRAPHICS_GL
  #include "include/gpu/ganesh/gl/GrGLBackendSurface.h"
  #include "include/gpu/ganesh/gl/GrGLDirectContext.h"
  #include "include/gpu/gl/GrGLInterface.h"

  #if defined OS_MAC
    #include "include/gpu/ganesh/gl/mac/GrGLMakeMacInterface.h"
  #elif defined OS_WIN
    #include "include/gpu/ganesh/gl/win/GrGLMakeWinInterface.h"
    #pragma comment(lib, "opengl32.lib")
  #endif

#endif

using namespace iplug;
using namespace igraphics;

extern std::map<std::string, MTLTexturePtr> gTextureMap;

#pragma mark - Private Classes and Structs

class IGraphicsSkia::Bitmap : public APIBitmap
{
public:
  Bitmap(sk_sp<SkSurface> surface, int width, int height, float scale, float drawScale);
  Bitmap(const char* path, double sourceScale);
  Bitmap(const void* pData, int size, double sourceScale);
  Bitmap(sk_sp<SkImage>, double sourceScale);

private:
  SkiaDrawable mDrawable;
};

IGraphicsSkia::Bitmap::Bitmap(sk_sp<SkSurface> surface, int width, int height, float scale, float drawScale)
{
  mDrawable.mSurface = surface;
  mDrawable.mIsSurface = true;

  SetBitmap(&mDrawable, width, height, scale, drawScale);
}

IGraphicsSkia::Bitmap::Bitmap(const char* path, double sourceScale)
{
  sk_sp<SkData> data = SkData::MakeFromFileName(path);

  assert(data && "Unable to load file at path");

  auto image = SkImages::DeferredFromEncodedData(data);

#ifdef IGRAPHICS_CPU
  image = image->makeRasterImage();
#endif

  mDrawable.mImage = image;

  mDrawable.mIsSurface = false;
  SetBitmap(&mDrawable, mDrawable.mImage->width(), mDrawable.mImage->height(), sourceScale, 1.f);
}

IGraphicsSkia::Bitmap::Bitmap(const void* pData, int size, double sourceScale)
{
  auto data = SkData::MakeWithoutCopy(pData, size);
  auto image = SkImages::DeferredFromEncodedData(data);

#ifdef IGRAPHICS_CPU
  image = image->makeRasterImage();
#endif

  mDrawable.mImage = image;

  mDrawable.mIsSurface = false;
  SetBitmap(&mDrawable, mDrawable.mImage->width(), mDrawable.mImage->height(), sourceScale, 1.f);
}

IGraphicsSkia::Bitmap::Bitmap(sk_sp<SkImage> image, double sourceScale)
{
#ifdef IGRAPHICS_CPU
  mDrawable.mImage = image->makeRasterImage();
#else
  mDrawable.mImage = image;
#endif

  SetBitmap(&mDrawable, mDrawable.mImage->width(), mDrawable.mImage->height(), sourceScale, 1.f);
}

struct IGraphicsSkia::Font
{
  Font(IFontDataPtr&& data, sk_sp<SkTypeface> typeFace)
    : mData(std::move(data))
    , mTypeface(typeFace)
  {
  }

  IFontDataPtr mData;
  sk_sp<SkTypeface> mTypeface;
};

// Fonts
StaticStorage<IGraphicsSkia::Font> IGraphicsSkia::sFontCache;

#pragma mark - Utility conversions

BEGIN_IPLUG_NAMESPACE
BEGIN_IGRAPHICS_NAMESPACE

SkColor SkiaColor(const IColor& color, const IBlend* pBlend)
{
  if (pBlend)
    return SkColorSetARGB(Clip(static_cast<int>(pBlend->mWeight * color.A), 0, 255), color.R, color.G, color.B);
  else
    return SkColorSetARGB(color.A, color.R, color.G, color.B);
}

SkRect SkiaRect(const IRECT& r) { return SkRect::MakeLTRB(r.L, r.T, r.R, r.B); }

SkBlendMode SkiaBlendMode(const IBlend* pBlend)
{
  if (!pBlend)
    return SkBlendMode::kSrcOver;

  switch (pBlend->mMethod)
  {
  case EBlend::SrcOver:
    return SkBlendMode::kSrcOver;
  case EBlend::SrcIn:
    return SkBlendMode::kSrcIn;
  case EBlend::SrcOut:
    return SkBlendMode::kSrcOut;
  case EBlend::SrcAtop:
    return SkBlendMode::kSrcATop;
  case EBlend::DstOver:
    return SkBlendMode::kDstOver;
  case EBlend::DstIn:
    return SkBlendMode::kDstIn;
  case EBlend::DstOut:
    return SkBlendMode::kDstOut;
  case EBlend::DstAtop:
    return SkBlendMode::kDstATop;
  case EBlend::Add:
    return SkBlendMode::kPlus;
  case EBlend::XOR:
    return SkBlendMode::kXor;
  }

  return SkBlendMode::kClear;
}

SkTileMode SkiaTileMode(const IPattern& pattern)
{
  switch (pattern.mExtend)
  {
  case EPatternExtend::None:
    return SkTileMode::kDecal;
  case EPatternExtend::Reflect:
    return SkTileMode::kMirror;
  case EPatternExtend::Repeat:
    return SkTileMode::kRepeat;
  case EPatternExtend::Pad:
    return SkTileMode::kClamp;
  }

  return SkTileMode::kClamp;
}

SkPaint SkiaPaint(const IPattern& pattern, const IBlend* pBlend)
{
  SkPaint paint;
  paint.setAntiAlias(true);
  paint.setBlendMode(SkiaBlendMode(pBlend));
  int numStops = pattern.NStops();

  if (pattern.mType == EPatternType::Solid || numStops < 2)
  {
    paint.setColor(SkiaColor(pattern.GetStop(0).mColor, pBlend));
  }
  else
  {
    double x1 = 0.0;
    double y1 = 0.0;
    double x2 = 0.0;
    double y2 = 1.0;

    IMatrix m = pattern.mTransform;
    m.Invert();
    m.TransformPoint(x1, y1);
    m.TransformPoint(x2, y2);

    SkPoint points[2] = {SkPoint::Make(x1, y1), SkPoint::Make(x2, y2)};

    SkColor colors[8];
    SkScalar positions[8];

    assert(numStops <= 8);

    for (int i = 0; i < numStops; i++)
    {
      const IColorStop& stop = pattern.GetStop(i);
      colors[i] = SkiaColor(stop.mColor, pBlend);
      positions[i] = stop.mOffset;
    }

    switch (pattern.mType)
    {
    case EPatternType::Linear:
      paint.setShader(SkGradientShader::MakeLinear(points, colors, positions, numStops, SkiaTileMode(pattern), 0, nullptr));
      break;

    case EPatternType::Radial: {
      float xd = points[0].x() - points[1].x();
      float yd = points[0].y() - points[1].y();
      float radius = std::sqrt(xd * xd + yd * yd);
      paint.setShader(SkGradientShader::MakeRadial(points[0], radius, colors, positions, numStops, SkiaTileMode(pattern), 0, nullptr));
      break;
    }

    case EPatternType::Sweep: {
      SkMatrix matrix = SkMatrix::MakeAll(m.mXX, m.mYX, 0, m.mXY, m.mYY, 0, 0, 0, 1);

      paint.setShader(SkGradientShader::MakeSweep(x1, y1, colors, nullptr, numStops, SkTileMode::kDecal, 0, 360 * positions[numStops - 1], 0, &matrix));

      break;
    }

    default:
      break;
    }
  }

  return paint;
}

END_IGRAPHICS_NAMESPACE
END_IPLUG_NAMESPACE

#pragma mark -

static sk_sp<SkFontMgr> SFontMgrFactory()
{
#if defined OS_MAC || defined OS_IOS
  return SkFontMgr_New_CoreText(nullptr);
#elif defined OS_WIN
  return SkFontMgr_New_DirectWrite();
#else
  #error "Not supported"
#endif
}

bool gFontMgrFactoryCreated = false;

sk_sp<SkFontMgr> SkFontMgrRefDefault()
{
  static std::once_flag flag;
  static sk_sp<SkFontMgr> mgr;
  std::call_once(flag, [] {
    mgr = SFontMgrFactory();
    gFontMgrFactoryCreated = true;
  });
  return mgr;
}

#if !defined IGRAPHICS_NO_SKIA_SKPARAGRAPH

bool gSkUnicodeCreated = false;

sk_sp<SkUnicode> GetUnicode()
{
  static std::once_flag flag;
  static sk_sp<SkUnicode> unicode;
  std::call_once(flag, [] {
    unicode = SkUnicodes::ICU::Make();
    gSkUnicodeCreated = true;
  });

  if (!unicode)
  {
    DBGMSG("Could not load unicode data\n");
    return nullptr;
  }

  return unicode;
}
#endif

IGraphicsSkia::IGraphicsSkia(IGEditorDelegate& dlg, int w, int h, int fps, float scale)
  : IGraphics(dlg, w, h, fps, scale)
{
  mMainPath.setIsVolatile(true);

#if defined IGRAPHICS_CPU
  DBGMSG("IGraphics Skia CPU @ %i FPS\n", fps);
#elif defined IGRAPHICS_METAL
  DBGMSG("IGraphics Skia METAL @ %i FPS\n", fps);
#elif defined IGRAPHICS_GL
  DBGMSG("IGraphics Skia GL @ %i FPS\n", fps);
#endif
  StaticStorage<Font>::Accessor storage(sFontCache);
  storage.Retain();

#if !defined IGRAPHICS_NO_SKIA_SKPARAGRAPH
   if (!mFontCollection)
  {
     mFontMgr = SParagraphFontMgr();
     mTypefaceProvider = sk_make_sp<skia::textlayout::TypefaceFontProvider>();
     mTypefaceProvider->ref();// <-- CHANGED THIS LINE
     mFontCollection = sk_make_sp<skia::textlayout::FontCollection>();
     mFontCollection->setAssetFontManager(mTypefaceProvider);
     mFontCollection->setDefaultFontManager(mFontMgr);
     mFontCollection->enableFontFallback();
   }
  #endif
}

IGraphicsSkia::~IGraphicsSkia()
{
#if !defined IGRAPHICS_NO_SKIA_SKPARAGRAPH
  if (mFontCollection)
    mFontCollection->clearCaches();

  {
    StaticStorage<Font>::Accessor storage(sFontCache);
    storage.Release();
  }

  if (mTypefaceProvider)
    mTypefaceProvider->unref(); // pair with the manual ref

  mFontCollection.reset();
  mTypefaceProvider.reset();
  mFontMgr.reset();
#endif
}

bool IGraphicsSkia::BitmapExtSupported(const char* ext)
{
  char extLower[32];
  ToLower(extLower, ext);
  return (strstr(extLower, "png") != nullptr) || (strstr(extLower, "jpg") != nullptr) || (strstr(extLower, "jpeg") != nullptr);
}

APIBitmap* IGraphicsSkia::LoadAPIBitmap(const char* fileNameOrResID, int scale, EResourceLocation location, const char* ext)
{
// #ifdef OS_IOS
//   if (location == EResourceLocation::kPreloadedTexture)
//   {
//     assert(0 && "SKIA does not yet load KTX textures");
//     GrMtlTextureInfo textureInfo;
//     textureInfo.fTexture.retain((void*)(gTextureMap[fileNameOrResID]));
//     id<MTLTexture> texture = (id<MTLTexture>) textureInfo.fTexture.get();
//
//     MTLPixelFormat pixelFormat = texture.pixelFormat;
//
//     auto grBackendTexture = GrBackendTexture(texture.width, texture.height, GrMipMapped::kNo, textureInfo);
//
//     sk_sp<SkImage> image = SkImage::MakeFromTexture(mGrContext.get(), grBackendTexture, kTopLeft_GrSurfaceOrigin, kBGRA_8888_SkColorType, kOpaque_SkAlphaType, nullptr);
//     return new Bitmap(image, scale);
//   }
//   else
// #endif
#ifdef OS_WIN
  if (location == EResourceLocation::kWinBinary)
  {
    int size = 0;
    const void* pData = LoadWinResource(fileNameOrResID, ext, size, GetWinModuleHandle());
    return new Bitmap(pData, size, scale);
  }
  else
#endif
    return new Bitmap(fileNameOrResID, scale);
}

APIBitmap* IGraphicsSkia::LoadAPIBitmap(const char* name, const void* pData, int dataSize, int scale) { return new Bitmap(pData, dataSize, scale); }

void IGraphicsSkia::OnViewInitialized(void* pContext)
{
#if defined IGRAPHICS_GL
  #if defined OS_MAC
  auto glInterface = GrGLInterfaces::MakeMac();
  #elif defined OS_WIN
  auto glInterface = GrGLInterfaces::MakeWin();
  #endif
  mGrContext = GrDirectContexts::MakeGL(glInterface);
#elif defined IGRAPHICS_METAL
  CAMetalLayer* pMTLLayer = (CAMetalLayer*)pContext;
  id<MTLDevice> device = pMTLLayer.device;
  id<MTLCommandQueue> commandQueue = [device newCommandQueue];
  GrMtlBackendContext backendContext = {};
  backendContext.fDevice.retain((__bridge GrMTLHandle)device);
  backendContext.fQueue.retain((__bridge GrMTLHandle)commandQueue);
  mGrContext = GrDirectContexts::MakeMetal(backendContext);
  mMTLDevice = (void*)device;
  mMTLCommandQueue = (void*)commandQueue;
  mMTLLayer = pContext;
#endif

  DrawResize();
}

void IGraphicsSkia::OnViewDestroyed()
{
  RemoveAllControls();

#if defined IGRAPHICS_GL
  mSurface = nullptr;
  mScreenSurface = nullptr;
  mGrContext = nullptr;
#elif defined IGRAPHICS_METAL
  [(id<MTLCommandQueue>)mMTLCommandQueue release];
  mMTLCommandQueue = nullptr;
  mMTLLayer = nullptr;
  mMTLDevice = nullptr;
#endif
}

void IGraphicsSkia::DrawResize()
{
  ScopedGLContext scopedGLContext{this};
  auto w = static_cast<int>(std::ceil(static_cast<float>(WindowWidth()) * GetScreenScale()));
  auto h = static_cast<int>(std::ceil(static_cast<float>(WindowHeight()) * GetScreenScale()));

#if defined IGRAPHICS_GL || defined IGRAPHICS_METAL
  if (mGrContext.get())
  {
    SkImageInfo info = SkImageInfo::MakeN32Premul(w, h);
    mSurface = SkSurfaces::RenderTarget(mGrContext.get(), skgpu::Budgeted::kYes, info);
  }
#else
  #ifdef OS_WIN
  mSurface.reset();

  const size_t bmpSize = sizeof(BITMAPINFOHEADER) + (w * h * sizeof(uint32_t));
  mSurfaceMemory.Resize(bmpSize);
  BITMAPINFO* bmpInfo = reinterpret_cast<BITMAPINFO*>(mSurfaceMemory.Get());
  ZeroMemory(bmpInfo, sizeof(BITMAPINFO));
  bmpInfo->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmpInfo->bmiHeader.biWidth = w;
  bmpInfo->bmiHeader.biHeight = -h; // negative means top-down bitmap. Skia draws top-down.
  bmpInfo->bmiHeader.biPlanes = 1;
  bmpInfo->bmiHeader.biBitCount = 32;
  bmpInfo->bmiHeader.biCompression = BI_RGB;
  void* pixels = bmpInfo->bmiColors;

  SkImageInfo info = SkImageInfo::Make(w, h, kN32_SkColorType, kPremul_SkAlphaType, nullptr);
  mSurface = SkSurfaces::WrapPixels(info, pixels, sizeof(uint32_t) * w);
  #else
  SkImageInfo info = SkImageInfo::MakeN32Premul(w, h);
  mSurface = SkSurfaces::Raster(info);
  #endif
#endif
  if (mSurface)
  {
    mCanvas = mSurface->getCanvas();
    mCanvas->save();
  }
}

void IGraphicsSkia::BeginFrame()
{
#if defined IGRAPHICS_GL
  if (mGrContext.get())
  {
    int width = WindowWidth() * GetScreenScale();
    int height = WindowHeight() * GetScreenScale();

    // Bind to the current main framebuffer
    int fbo = 0, samples = 0, stencilBits = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
    glGetIntegerv(GL_SAMPLES, &samples);
  #ifdef IGRAPHICS_GL3
    glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, GL_STENCIL, GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE, &stencilBits);
  #else
    glGetIntegerv(GL_STENCIL_BITS, &stencilBits);
  #endif

    GrGLFramebufferInfo fbInfo;
    fbInfo.fFBOID = fbo;
    fbInfo.fFormat = 0x8058;

    auto backendRT = GrBackendRenderTargets::MakeGL(width, height, samples, stencilBits, fbInfo);

    mScreenSurface = SkSurfaces::WrapBackendRenderTarget(mGrContext.get(), backendRT, kBottomLeft_GrSurfaceOrigin, kRGBA_8888_SkColorType, nullptr, nullptr);
    assert(mScreenSurface);
  }
#elif defined IGRAPHICS_METAL
  if (mGrContext.get())
  {
    int width = WindowWidth() * GetScreenScale();
    int height = WindowHeight() * GetScreenScale();

    id<CAMetalDrawable> drawable = [(CAMetalLayer*)mMTLLayer nextDrawable];

    GrMtlTextureInfo fbInfo;
    fbInfo.fTexture.retain((const void*)(drawable.texture));
    auto backendRT = GrBackendRenderTargets::MakeMtl(width, height, fbInfo);
    mScreenSurface = SkSurfaces::WrapBackendRenderTarget(mGrContext.get(), backendRT, kTopLeft_GrSurfaceOrigin, kBGRA_8888_SkColorType, nullptr, nullptr);

    mMTLDrawable = (void*)drawable;
    assert(mScreenSurface);
  }
#endif

  IGraphics::BeginFrame();
}

void IGraphicsSkia::EndFrame()
{
#ifdef IGRAPHICS_CPU
  #if defined OS_MAC || defined OS_IOS
  SkPixmap pixmap;
  mSurface->peekPixels(&pixmap);
  SkBitmap bmp;
  bmp.installPixels(pixmap);
  CGContext* pCGContext = (CGContextRef)GetPlatformContext();
  CGContextSaveGState(pCGContext);
  CGContextScaleCTM(pCGContext, 1.0 / GetScreenScale(), 1.0 / GetScreenScale());
  SkCGDrawBitmap(pCGContext, bmp, 0, 0);
  CGContextRestoreGState(pCGContext);
  #elif defined OS_WIN
  auto w = WindowWidth() * GetScreenScale();
  auto h = WindowHeight() * GetScreenScale();
  BITMAPINFO* bmpInfo = reinterpret_cast<BITMAPINFO*>(mSurfaceMemory.Get());
  HWND hWnd = (HWND)GetWindow();
  PAINTSTRUCT ps;
  HDC hdc = BeginPaint(hWnd, &ps);
  StretchDIBits(hdc, 0, 0, w, h, 0, 0, w, h, bmpInfo->bmiColors, bmpInfo, DIB_RGB_COLORS, SRCCOPY);
  ReleaseDC(hWnd, hdc);
  EndPaint(hWnd, &ps);
  #else
    #error NOT IMPLEMENTED
  #endif
#else // GPU
  mSurface->draw(mScreenSurface->getCanvas(), 0.0, 0.0, nullptr);

  if (auto dContext = GrAsDirectContext(mScreenSurface->getCanvas()->recordingContext()))
  {
    dContext->flushAndSubmit();
  }

  #ifdef IGRAPHICS_METAL
  id<MTLCommandBuffer> commandBuffer = [(id<MTLCommandQueue>)mMTLCommandQueue commandBuffer];
  commandBuffer.label = @"Present";

  [commandBuffer presentDrawable:(id<CAMetalDrawable>)mMTLDrawable];
  [commandBuffer commit];
  #endif
#endif
}

void IGraphicsSkia::DrawBitmap(const IBitmap& bitmap, const IRECT& dest, int srcX, int srcY, const IBlend* pBlend)
{
  SkPaint p;

  p.setAntiAlias(true);
  p.setBlendMode(SkiaBlendMode(pBlend));
  if (pBlend)
    p.setAlpha(Clip(static_cast<int>(pBlend->mWeight * 255), 0, 255));

  SkiaDrawable* image = bitmap.GetAPIBitmap()->GetBitmap();

  double scale1 = 1.0 / (bitmap.GetScale() * bitmap.GetDrawScale());
  double scale2 = bitmap.GetScale() * bitmap.GetDrawScale();

  mCanvas->save();
  mCanvas->clipRect(SkiaRect(dest));
  mCanvas->translate(dest.L, dest.T);
  mCanvas->scale(scale1, scale1);
  mCanvas->translate(-srcX * scale2, -srcY * scale2);

  auto samplingOptions = SkSamplingOptions(SkFilterMode::kLinear, SkMipmapMode::kLinear);

  if (image->mIsSurface)
    image->mSurface->draw(mCanvas, 0.0, 0.0, samplingOptions, &p);
  else
    mCanvas->drawImage(image->mImage, 0.0, 0.0, samplingOptions, &p);

  mCanvas->restore();
}

void IGraphicsSkia::PathArc(float cx, float cy, float r, float a1, float a2, EWinding winding)
{
  SkPath arc;
  arc.setIsVolatile(true);

  float sweep = (a2 - a1);

  if (sweep >= 360.f || sweep <= -360.f)
  {
    arc.addCircle(cx, cy, r);
    mMainPath.addPath(arc, mMatrix, SkPath::kAppend_AddPathMode);
  }
  else
  {
    if (winding == EWinding::CW)
    {
      while (sweep < 0)
        sweep += 360.f;
    }
    else
    {
      while (sweep > 0)
        sweep -= 360.f;
    }

    arc.arcTo(SkRect::MakeLTRB(cx - r, cy - r, cx + r, cy + r), a1 - 90.f, sweep, false);
    mMainPath.addPath(arc, mMatrix, SkPath::kExtend_AddPathMode);
  }
}

IColor IGraphicsSkia::GetPoint(int x, int y)
{
  SkBitmap bitmap;
  bitmap.allocPixels(SkImageInfo::MakeN32Premul(1, 1));
  mCanvas->readPixels(bitmap, x, y);
  auto color = bitmap.getColor(0, 0);
  return IColor(SkColorGetA(color), SkColorGetR(color), SkColorGetG(color), SkColorGetB(color));
}

sk_sp<SkFontMgr> IGraphicsSkia::SParagraphFontMgr()
{
  static std::once_flag flag;
  static sk_sp<SkFontMgr> mgr;
  std::call_once(flag, [] {
    mgr = SFontMgrFactory(); // SFontMgrFactory is already defined in your code
  });
  return mgr;
}

bool IGraphicsSkia::LoadAPIFont(const char* fontID, const PlatformFontPtr& font)
{
  StaticStorage<Font>::Accessor storage(sFontCache);
  Font* cached = storage.Find(fontID);

  if (cached)
    return true;

  IFontDataPtr data = font->GetFontData();

  if (data->IsValid())
  {
    auto wrappedData = SkData::MakeWithCopy(data->Get(), data->GetSize());

#if !defined IGRAPHICS_NO_SKIA_SKPARAGRAPH

    //if (!mFontCollection)
    //{
    //  mFontMgr = SParagraphFontMgr(); 
    //  mTypefaceProvider = sk_make_sp<skia::textlayout::TypefaceFontProvider>();
    //  mTypefaceProvider->ref();// <-- CHANGED THIS LINE
    //  mFontCollection = sk_make_sp<skia::textlayout::FontCollection>();
    //  mFontCollection->setAssetFontManager(mTypefaceProvider);
    //  mFontCollection->setDefaultFontManager(mFontMgr);
    //  mFontCollection->enableFontFallback();
    //}

    // Create the typeface using our private font manager instance.
    auto typeFace = mFontMgr->makeFromData(wrappedData);
#else
    auto typeFace = SkFontMgrRefDefault()->makeFromData(wrappedData);
#endif

    if (typeFace)
    {
      storage.Add(new Font(std::move(data), typeFace), fontID);

#if !defined IGRAPHICS_NO_SKIA_SKPARAGRAPH
      mTypefaceProvider->registerTypeface(typeFace, SkString(fontID));
#endif
      return true;
    }
  }
  return false;
}

void IGraphicsSkia::PrepareAndMeasureText(const IText& text, const char* str, IRECT& r, double& x, double& y, SkFont& font) const
{
  using namespace skia::textlayout;

  StaticStorage<Font>::Accessor storage(sFontCache);
  Font* pFont = storage.Find(text.mFont);
  assert(pFont && "No font found - did you forget to load it?");

  ParagraphStyle paraStyle;
  paraStyle.setMaxLines(1);

  TextStyle txtStyle;
  txtStyle.setFontSize(text.mSize);
  txtStyle.setTypeface(pFont->mTypeface); // This tells the builder to prefer this font.
  txtStyle.setColor(SK_ColorBLACK);

  txtStyle.setFontFamilies({SkString(text.mFont)});

  // Build the paragraph using the stable, pre-configured member font collection.
  auto builder = ParagraphBuilder::make(paraStyle, mFontCollection);
  builder->pushStyle(txtStyle);
  builder->addText(str);
  builder->pop();
  auto paragraph = builder->Build();
  paragraph->layout(10000.f);

  const double measuredWidth = paragraph->getLongestLine();
  const double measuredHeight = paragraph->getHeight();

  switch (text.mAlign)
  {
  case EAlign::Near:
    x = r.L;
    break;
  case EAlign::Center:
    x = r.MW() - (measuredWidth / 2.0);
    break;
  case EAlign::Far:
    x = r.R - measuredWidth;
    break;
  }

  switch (text.mVAlign)
  {
  case EVAlign::Top:
    y = r.T;
    break;
  case EVAlign::Middle:
    y = r.MH() - (measuredHeight / 2.0);
    break;
  case EVAlign::Bottom:
    y = r.B - measuredHeight;
    break;
  }

  r = IRECT((float)x, (float)y, (float)(x + measuredWidth), (float)(y + measuredHeight));
}

float IGraphicsSkia::DoMeasureText(const IText& text, const char* str, IRECT& bounds) const
{
  SkFont font;
  font.setEdging(SkFont::Edging::kSubpixelAntiAlias);

  IRECT r = bounds;
  double x, y;
  PrepareAndMeasureText(text, str, bounds, x, y, font);
  DoMeasureTextRotation(text, r, bounds);
  return bounds.W();
}

void IGraphicsSkia::DoDrawText(const IText& text, const char* str, const IRECT& bounds, const IBlend* pBlend)
{
  using namespace skia::textlayout;

  IRECT measured = bounds;
  double x, y;
  SkFont font; // Dummy, not used by new implementation but required by signature.
  PrepareAndMeasureText(text, str, measured, x, y, font);

  StaticStorage<Font>::Accessor storage(sFontCache);
  Font* pFont = storage.Find(text.mFont);
  assert(pFont && "No font found - did you forget to load it?");

  ParagraphStyle paraStyle;
  paraStyle.setMaxLines(1);

  TextStyle txtStyle;
  txtStyle.setColor(SkiaColor(text.mFGColor, pBlend));
  txtStyle.setFontSize(text.mSize);
  txtStyle.setTypeface(pFont->mTypeface);

  txtStyle.setFontFamilies({SkString(text.mFont)});

  // Build the paragraph for drawing, again using the stable member collection.
  auto builder = ParagraphBuilder::make(paraStyle, mFontCollection);
  builder->pushStyle(txtStyle);
  builder->addText(str);
  builder->pop();
  auto paragraph = builder->Build();
  paragraph->layout(10000.f);

  PathTransformSave();
  PathTransformTranslate(measured.L, measured.T);
  if (text.mAngle != 0.f)
    DoTextRotation(text, measured, measured);

  paragraph->paint(mCanvas, 0, 0);
  PathTransformRestore();
}
void IGraphicsSkia::PathStroke(const IPattern& pattern, float thickness, const IStrokeOptions& options, const IBlend* pBlend)
{
  SkPaint paint = SkiaPaint(pattern, pBlend);
  paint.setStyle(SkPaint::kStroke_Style);

  switch (options.mCapOption)
  {
  case ELineCap::Butt:
    paint.setStrokeCap(SkPaint::kButt_Cap);
    break;
  case ELineCap::Round:
    paint.setStrokeCap(SkPaint::kRound_Cap);
    break;
  case ELineCap::Square:
    paint.setStrokeCap(SkPaint::kSquare_Cap);
    break;
  }

  switch (options.mJoinOption)
  {
  case ELineJoin::Miter:
    paint.setStrokeJoin(SkPaint::kMiter_Join);
    break;
  case ELineJoin::Round:
    paint.setStrokeJoin(SkPaint::kRound_Join);
    break;
  case ELineJoin::Bevel:
    paint.setStrokeJoin(SkPaint::kBevel_Join);
    break;
  }

  if (options.mDash.GetCount())
  {
    // N.B. support odd counts by reading the array twice
    int dashCount = options.mDash.GetCount();
    int dashMax = dashCount & 1 ? dashCount * 2 : dashCount;
    float dashArray[16];

    for (int i = 0; i < dashMax; i += 2)
    {
      dashArray[i + 0] = options.mDash.GetArray()[i % dashCount];
      dashArray[i + 1] = options.mDash.GetArray()[(i + 1) % dashCount];
    }

    paint.setPathEffect(SkDashPathEffect::Make(dashArray, dashMax, options.mDash.GetOffset()));
  }

  paint.setStrokeWidth(thickness);
  paint.setStrokeMiter(options.mMiterLimit);

  RenderPath(paint);

  if (!options.mPreserve)
    mMainPath.reset();
}

void IGraphicsSkia::PathFill(const IPattern& pattern, const IFillOptions& options, const IBlend* pBlend)
{
  SkPaint paint = SkiaPaint(pattern, pBlend);
  paint.setStyle(SkPaint::kFill_Style);

  if (options.mFillRule == EFillRule::Winding)
    mMainPath.setFillType(SkPathFillType::kWinding);
  else
    mMainPath.setFillType(SkPathFillType::kEvenOdd);

  RenderPath(paint);

  if (!options.mPreserve)
    mMainPath.reset();
}

#ifdef IGRAPHICS_DRAWFILL_DIRECT
void IGraphicsSkia::DrawRect(const IColor& color, const IRECT& bounds, const IBlend* pBlend, float thickness)
{
  auto paint = SkiaPaint(color, pBlend);
  paint.setStyle(SkPaint::Style::kStroke_Style);
  paint.setStrokeWidth(thickness);
  mCanvas->drawRect(SkiaRect(bounds), paint);
}

void IGraphicsSkia::DrawRoundRect(const IColor& color, const IRECT& bounds, float cornerRadius, const IBlend* pBlend, float thickness)
{
  auto paint = SkiaPaint(color, pBlend);
  paint.setStyle(SkPaint::Style::kStroke_Style);
  paint.setStrokeWidth(thickness);
  mCanvas->drawRoundRect(SkiaRect(bounds), cornerRadius, cornerRadius, paint);
}

void IGraphicsSkia::DrawArc(const IColor& color, float cx, float cy, float r, float a1, float a2, const IBlend* pBlend, float thickness)
{
  auto paint = SkiaPaint(color, pBlend);
  paint.setStyle(SkPaint::Style::kStroke_Style);
  paint.setStrokeWidth(thickness);
  mCanvas->drawArc(SkRect::MakeLTRB(cx - r, cy - r, cx + r, cy + r), a1 - 90.f, (a2 - a1), false, paint);
}

void IGraphicsSkia::DrawCircle(const IColor& color, float cx, float cy, float r, const IBlend* pBlend, float thickness)
{
  auto paint = SkiaPaint(color, pBlend);
  paint.setStyle(SkPaint::Style::kStroke_Style);
  paint.setStrokeWidth(thickness);
  mCanvas->drawCircle(cx, cy, r, paint);
}

void IGraphicsSkia::DrawEllipse(const IColor& color, const IRECT& bounds, const IBlend* pBlend, float thickness)
{
  auto paint = SkiaPaint(color, pBlend);
  paint.setStyle(SkPaint::Style::kStroke_Style);
  paint.setStrokeWidth(thickness);
  mCanvas->drawOval(SkiaRect(bounds), paint);
}

void IGraphicsSkia::FillRect(const IColor& color, const IRECT& bounds, const IBlend* pBlend)
{
  auto paint = SkiaPaint(color, pBlend);
  paint.setStyle(SkPaint::Style::kFill_Style);
  mCanvas->drawRect(SkiaRect(bounds), paint);
}

void IGraphicsSkia::FillRoundRect(const IColor& color, const IRECT& bounds, float cornerRadius, const IBlend* pBlend)
{
  auto paint = SkiaPaint(color, pBlend);
  paint.setStyle(SkPaint::Style::kFill_Style);
  mCanvas->drawRoundRect(SkiaRect(bounds), cornerRadius, cornerRadius, paint);
}

void IGraphicsSkia::FillArc(const IColor& color, float cx, float cy, float r, float a1, float a2, const IBlend* pBlend)
{
  auto paint = SkiaPaint(color, pBlend);
  paint.setStyle(SkPaint::Style::kFill_Style);
  mCanvas->drawArc(SkRect::MakeLTRB(cx - r, cy - r, cx + r, cy + r), a1 - 90.f, (a2 - a1), true, paint);
}

void IGraphicsSkia::FillCircle(const IColor& color, float cx, float cy, float r, const IBlend* pBlend)
{
  auto paint = SkiaPaint(color, pBlend);
  paint.setStyle(SkPaint::Style::kFill_Style);
  mCanvas->drawCircle(cx, cy, r, paint);
}

void IGraphicsSkia::FillEllipse(const IColor& color, const IRECT& bounds, const IBlend* pBlend)
{
  auto paint = SkiaPaint(color, pBlend);
  paint.setStyle(SkPaint::Style::kFill_Style);
  mCanvas->drawOval(SkiaRect(bounds), paint);
}
#endif

void IGraphicsSkia::RenderPath(SkPaint& paint)
{
  SkMatrix invMatrix;

  if (!mMatrix.isIdentity() && mMatrix.invert(&invMatrix))
  {
    SkPath path;
    path.setIsVolatile(true);
    mMainPath.transform(invMatrix, &path);
    mCanvas->drawPath(path, paint);
  }
  else
  {
    mCanvas->drawPath(mMainPath, paint);
  }
}

void IGraphicsSkia::PathTransformSetMatrix(const IMatrix& m)
{
  double xTranslate = 0.0;
  double yTranslate = 0.0;

  if (!mCanvas)
    return;

  if (!mLayers.empty())
  {
    IRECT bounds = mLayers.top()->Bounds();

    xTranslate = -bounds.L;
    yTranslate = -bounds.T;
  }

  mMatrix = SkMatrix::MakeAll(m.mXX, m.mXY, m.mTX, m.mYX, m.mYY, m.mTY, 0, 0, 1);
  auto scale = GetTotalScale();
  SkMatrix globalMatrix = SkMatrix::Scale(scale, scale);
  mClipMatrix = SkMatrix();
  mFinalMatrix = mMatrix;
  globalMatrix.preTranslate(xTranslate, yTranslate);
  mClipMatrix.postConcat(globalMatrix);
  mFinalMatrix.postConcat(globalMatrix);
  mCanvas->setMatrix(mFinalMatrix);
}

void IGraphicsSkia::SetClipRegion(const IRECT& r)
{
  mCanvas->restoreToCount(0);
  mCanvas->save();
  mCanvas->setMatrix(mClipMatrix);
  mCanvas->clipRect(SkiaRect(r));
  mCanvas->setMatrix(mFinalMatrix);
}

APIBitmap* IGraphicsSkia::CreateAPIBitmap(int width, int height, float scale, double drawScale, bool cacheable, int MSAASampleCount)
{
  sk_sp<SkSurface> surface;
  SkImageInfo info = SkImageInfo::MakeN32Premul(width, height);

#ifndef IGRAPHICS_CPU
  if (cacheable)
  {
    surface = SkSurfaces::Raster(info);
  }
  else
  {
    bool supported = (MSAASampleCount != 0) && (mGrContext->maxSurfaceSampleCountForColorType(info.colorType()) >= MSAASampleCount);

    if (supported)
    {
      // SkDebugf("IGraphicsSkia: MSAA x4 reported as supported. Attempting new RenderTarget.\n");
      SkSurfaceProps surfaceProps(0, kUnknown_SkPixelGeometry);
      surface = SkSurfaces::RenderTarget(mGrContext.get(), skgpu::Budgeted::kNo, info, MSAASampleCount, kTopLeft_GrSurfaceOrigin, &surfaceProps);

      if (!surface) // Budgeted MSAA
      {
        surface = SkSurfaces::RenderTarget(mGrContext.get(), skgpu::Budgeted::kYes, info, MSAASampleCount, kTopLeft_GrSurfaceOrigin, &surfaceProps);
      }

      if (!surface) // <-- DEFAULT ORIGINAL DRAWING, THIS DOES NOT FAIL
      {
        surface = SkSurfaces::RenderTarget(mGrContext.get(), skgpu::Budgeted::kYes, info);
      }

      if (!surface) // If all GPU attempts (MSAA and non-MSAA) failed
      {
        surface = SkSurfaces::Raster(info);
      }
    }
    else
    {
      // SkDebugf("IGraphicsSkia: MSAA x4 reported as NOT supported. Attempting original RenderTarget.\n");
      surface = SkSurfaces::RenderTarget(mGrContext.get(), skgpu::Budgeted::kYes, info);
    }
  }
#else
  surface = SkSurfaces::Raster(info);
#endif

  surface->getCanvas()->save();

  return new Bitmap(std::move(surface), width, height, scale, drawScale);
}

void IGraphicsSkia::UpdateLayer() { mCanvas = mLayers.empty() ? mSurface->getCanvas() : mLayers.top()->GetAPIBitmap()->GetBitmap()->mSurface->getCanvas(); }

static size_t CalcRowBytes(int width)
{
  width = ((width + 7) & (-8));
  return width * sizeof(uint32_t);
}

void IGraphicsSkia::ApplyLayerDropShadow(ILayerPtr& layer, const IShadow& shadow)
{
#ifdef IGRAPHICS_CPU
  bool useFilter = false;
#else
  bool useFilter = shadow.mPattern.mNStops <= 1;
#endif

  if (useFilter)
  {
    auto makeFilter = [&shadow](float scale) {
      // The constant of 3.f matches the IGraphics scaling of blur
      const auto dx = shadow.mXOffset * scale;
      const auto dy = shadow.mYOffset * scale;
      const auto r = shadow.mBlurSize * scale / 3.f;
      const IBlend blend(EBlend::Default, shadow.mOpacity);
      const auto color = SkiaColor(shadow.mPattern.GetStop(0).mColor, &blend);

      if (shadow.mDrawForeground)
        return SkImageFilters::DropShadow(dx, dy, r, r, color, nullptr);
      else
        return SkImageFilters::DropShadowOnly(dx, dy, r, r, color, nullptr);
    };

    auto shadowFilter = makeFilter(layer->GetAPIBitmap()->GetScale());

    SkPaint p;
    SkMatrix m;
    m.reset();

    p.setAntiAlias(true);
    p.setImageFilter(shadowFilter);

    sk_sp<SkSurface> surface = layer->GetAPIBitmap()->GetBitmap()->mSurface;
    SkCanvas* pCanvas = surface->getCanvas();
    sk_sp<SkImage> contents = surface->makeImageSnapshot();

    pCanvas->setMatrix(m);
    pCanvas->clear(SK_ColorTRANSPARENT);
    pCanvas->drawImage(contents.get(), 0.0, 0.0, SkSamplingOptions(), &p);
  }
  else
  {
    IGraphics::ApplyLayerDropShadow(layer, shadow);
  }
}

void IGraphicsSkia::GetLayerBitmapData(const ILayerPtr& layer, RawBitmapData& data)
{
  SkiaDrawable* pDrawable = layer->GetAPIBitmap()->GetBitmap();
  size_t rowBytes = CalcRowBytes(pDrawable->mSurface->width());
  int size = pDrawable->mSurface->height() * static_cast<int>(rowBytes);

  data.Resize(size);

  if (data.GetSize() >= size)
  {
    SkImageInfo info = SkImageInfo::MakeN32Premul(pDrawable->mSurface->width(), pDrawable->mSurface->height());
    pDrawable->mSurface->readPixels(info, data.Get(), rowBytes, 0, 0);
  }
}

void IGraphicsSkia::ApplyShadowMask(ILayerPtr& layer, RawBitmapData& mask, const IShadow& shadow)
{
  SkiaDrawable* pDrawable = layer->GetAPIBitmap()->GetBitmap();
  int width = pDrawable->mSurface->width();
  int height = pDrawable->mSurface->height();
  size_t rowBytes = CalcRowBytes(width);
  double scale = layer->GetAPIBitmap()->GetDrawScale() * layer->GetAPIBitmap()->GetScale();

  SkCanvas* pCanvas = pDrawable->mSurface->getCanvas();

  SkMatrix m;
  m.reset();

  SkImageInfo info = SkImageInfo::MakeN32Premul(width, height);
  SkPixmap pixMap(info, mask.Get(), rowBytes);
  sk_sp<SkImage> image = SkImages::RasterFromPixmap(pixMap, nullptr, nullptr);
  sk_sp<SkImage> foreground;

  // Copy the foreground if needed

  if (shadow.mDrawForeground)
    foreground = pDrawable->mSurface->makeImageSnapshot();

  pCanvas->clear(SK_ColorTRANSPARENT);

  IBlend blend(EBlend::Default, shadow.mOpacity);
  pCanvas->setMatrix(m);
  pCanvas->drawImage(image.get(), shadow.mXOffset * scale, shadow.mYOffset * scale);
  m = SkMatrix::Scale(scale, scale);
  pCanvas->setMatrix(m);
  pCanvas->translate(-layer->Bounds().L, -layer->Bounds().T);
  SkPaint p = SkiaPaint(shadow.mPattern, &blend);
  p.setBlendMode(SkBlendMode::kSrcIn);
  pCanvas->drawPaint(p);

  if (shadow.mDrawForeground)
  {
    m.reset();
    pCanvas->setMatrix(m);
    pCanvas->drawImage(foreground.get(), 0.0, 0.0);
  }
}

void IGraphicsSkia::DrawFastDropShadow(const IRECT& innerBounds, const IRECT& outerBounds, float xyDrop, float roundness, float blur, IBlend* pBlend)
{
  SkRect r = SkiaRect(innerBounds.GetTranslated(xyDrop, xyDrop));

  SkPaint paint = SkiaPaint(COLOR_BLACK_DROP_SHADOW, pBlend);
  paint.setStyle(SkPaint::Style::kFill_Style);

  paint.setMaskFilter(SkMaskFilter::MakeBlur(kSolid_SkBlurStyle, blur * 0.5)); // 0.5 seems to match nanovg
  mCanvas->drawRoundRect(r, roundness, roundness, paint);
}

void IGraphicsSkia::DrawMultiLineText(const IText& text, const char* str, const IRECT& bounds, const IBlend* pBlend)
{
#if !defined IGRAPHICS_NO_SKIA_SKPARAGRAPH
  using namespace skia::textlayout;

  auto ConvertTextAlign = [](EAlign align) {
    switch (align)
    {
    case EAlign::Near:
      return TextAlign::kLeft;
    case EAlign::Center:
      return TextAlign::kCenter;
    case EAlign::Far:
      return TextAlign::kRight;
    default:
      return TextAlign::kLeft;
    }
  };

  ParagraphStyle paragraphStyle;
  paragraphStyle.setTextAlign(ConvertTextAlign(text.mAlign));

  auto builder = ParagraphBuilder::make(paragraphStyle, mFontCollection, GetUnicode());

  assert(builder && "Paragraph Builder couldn't be created");

  TextStyle textStyle;
  textStyle.setColor(SkiaColor(text.mFGColor, pBlend));

  std::string fontFamily = text.mFont;

  size_t pos = fontFamily.find('-');
  if (pos != std::string::npos)
  {
    fontFamily = fontFamily.substr(0, pos);
  }

  textStyle.setFontFamilies({SkString(fontFamily)});
  textStyle.setFontSize(text.mSize * 0.9);
  textStyle.setFontStyle(SkFontStyle(SkFontStyle::kNormal_Weight, SkFontStyle::kNormal_Width, SkFontStyle::kUpright_Slant));

  builder->pushStyle(textStyle);
  builder->addText(str);
  builder->pop();

  auto paragraph = builder->Build();

  paragraph->layout(bounds.W());

  float yOffset = 0;
  switch (text.mVAlign)
  {
  case EVAlign::Top:
    yOffset = 0;
    break;
  case EVAlign::Middle:
    yOffset = (bounds.H() - paragraph->getHeight()) / 2;
    break;
  case EVAlign::Bottom:
    yOffset = bounds.H() - paragraph->getHeight();
    break;
  }

  mCanvas->save();
  mCanvas->translate(bounds.L, bounds.T + yOffset);
  paragraph->paint(mCanvas, 0, 0);
  mCanvas->restore();
#else
  DrawText(text, str, bounds, pBlend);
#endif
}

const char* IGraphicsSkia::GetDrawingAPIStr()
{
#ifdef IGRAPHICS_CPU
  return "SKIA | CPU";
#elif defined IGRAPHICS_GL2
  return "SKIA | GL2";
#elif defined IGRAPHICS_GL3
  return "SKIA | GL3";
#elif defined IGRAPHICS_METAL
  return "SKIA | Metal";
#endif
}
