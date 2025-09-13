/*
 ==============================================================================

 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers.

 See LICENSE.txt for  more info.

 ==============================================================================
*/

#pragma once

#include "IGraphicsStructs.h"
#include "IGraphics_select.h"
#include <map>
#include <string>
#ifdef IPLUG_SEPARATE_FONTDESC_CACHE
  #include "IGraphicsCoreText.h"
#endif
#ifdef __OBJC__
@class NSArray;
#endif

BEGIN_IPLUG_NAMESPACE
BEGIN_IGRAPHICS_NAMESPACE

extern void GetScreenDimensions(int& width, int& height);

extern float GetScaleForScreen(int width, int height);

/** IGraphics platform class for IOS
 *   @ingroup PlatformClasses */
class IGraphicsIOS final : public IGRAPHICS_DRAW_CLASS
{
public:
  IGraphicsIOS(IGEditorDelegate& dlg, int w, int h, int fps, float scale);
  virtual ~IGraphicsIOS();

  void SetBundleID(const char* bundleID) { mBundleID.Set(bundleID); }
  void SetAppGroupID(const char* appGroupID) { mAppGroupID.Set(appGroupID); }

  void* OpenWindow(void* pWindow) override;
  void CloseWindow() override;
  bool WindowIsOpen() override;
  void PlatformResize(bool parentHasResized) override;
  void AttachPlatformView(const IRECT& r, void* pView) override;
  void RemovePlatformView(void* pView) override;
  void HidePlatformView(void* pView, bool hide) override;

  void GetMouseLocation(float& x, float& y) const override;

  EMsgBoxResult ShowMessageBox(const char* str, const char* title, EMsgBoxType type, IMsgBoxCompletionHandlerFunc completionHandler) override;
  void ForceEndUserEdit() override;

  const char* GetPlatformAPIStr() override;

  void UpdateTooltips() override {};

  void PromptForFile(WDL_String& fileName, WDL_String& path, EFileAction action, const char* ext, IFileDialogCompletionHandlerFunc completionHandler) override;
  void PromptForDirectory(WDL_String& dir, IFileDialogCompletionHandlerFunc completionHandler) override;
  bool PromptForColor(IColor& color, const char* str, IColorPickerHandlerFunc func) override;

  void HideMouseCursor(bool hide, bool lock) override {}; // NOOP
  void MoveMouseCursor(float x, float y) override {};     // NOOP

  bool OpenURL(const char* url, const char* msgWindowTitle, const char* confirmMsg, const char* errMsgOnFailure) override;

  void* GetWindow() override;

  const char* GetBundleID() const override { return mBundleID.Get(); }
  const char* GetAppGroupID() const override { return mAppGroupID.Get(); }

  static int GetUserOSVersion();

  bool GetTextFromClipboard(WDL_String& str) override;
  bool SetTextInClipboard(const char* str) override;

  void LaunchBluetoothMidiDialog(float x, float y);

  void AttachGestureRecognizer(EGestureType type) override;

  bool PlatformSupportsMultiTouch() const override { return true; }

  EUIAppearance GetUIAppearance() const override;
#ifdef IPLUG_SEPARATE_FONTDESC_CACHE
  StaticStorage<CoreTextFontDescriptor>& GetFontDescriptorCache() { return mFontDescriptorCache; }
#endif
#if defined IGRAPHICS_METAL && IPLUG_SEPARATE_IOS_TEXTURE_CACHE
  std::map<std::string, MTLTexturePtr>& GetTextureMap() { return mTextureMap; }
#endif

protected:
  PlatformFontPtr LoadPlatformFont(const char* fontID, const char* fileNameOrResID) override;
  PlatformFontPtr LoadPlatformFont(const char* fontID, const char* fontName, ETextStyle style) override;
  PlatformFontPtr LoadPlatformFont(const char* fontID, void* pData, int dataSize) override;
  void CachePlatformFont(const char* fontID, const PlatformFontPtr& font) override;

  IPopupMenu* CreatePlatformPopupMenu(IPopupMenu& menu, const IRECT bounds, bool& isAsync) override;
  void CreatePlatformTextEntry(int paramIdx, const IText& text, const IRECT& bounds, int length, const char* str) override;

private:
#ifdef IPLUG_SEPARATE_FONTDESC_CACHE
  StaticStorage<CoreTextFontDescriptor> mFontDescriptorCache;
#endif
  void* mView = nullptr;
  WDL_String mBundleID;
  WDL_String mAppGroupID;
#if defined IGRAPHICS_METAL && IPLUG_SEPARATE_IOS_TEXTURE_CACHE
  std::map<std::string, MTLTexturePtr> mTextureMap;
  NSArray* mTextures = nullptr;
#endif
};

END_IGRAPHICS_NAMESPACE
END_IPLUG_NAMESPACE
