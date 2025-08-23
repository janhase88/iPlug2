/*
 ==============================================================================

 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers.

 See LICENSE.txt for  more info.

 ==============================================================================
*/

#pragma once

#include "IPlugLogger.h"
#include <Ole2.h>

BEGIN_IPLUG_NAMESPACE
BEGIN_IGRAPHICS_NAMESPACE

class IGraphicsWin;

namespace DragAndDropHelpers 
{ 
class DropSource : public IDropSource 
{
public:
  DropSource() {}
  ~DropSource() 
  {
    assert(mRefCount == 0);
  }

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID refiid, void** resultHandle) override 
  {
    if (refiid == IID_IDropSource)
    {
      *resultHandle = this;
      AddRef();
      return S_OK;
    }
    *resultHandle = nullptr;
    return E_NOINTERFACE;
  }

  ULONG STDMETHODCALLTYPE AddRef () override { return ++mRefCount; }
  ULONG STDMETHODCALLTYPE Release() override 
  {
    int refCount = --mRefCount;
    assert(refCount >= 0);
    if (refCount <= 0) 
    {
      delete this;
    }
    return refCount;
  }

  // IDropSource methods
  HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escapeKeyPressed, DWORD grfKeyState) override 
  {
    if (escapeKeyPressed == TRUE) 
    {
      DBGMSG("DropSource: escapeKeyPressed, abort dnd\n");
      return DRAGDROP_S_CANCEL;
    }

    if ((grfKeyState & MK_LBUTTON) == 0) 
    {
      return DRAGDROP_S_DROP;
    }
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD /*dwEffect*/) override 
  {
    return DRAGDROP_S_USEDEFAULTCURSORS;
  }
private:
  int mRefCount = 1;
};

static void deepFormatCopy(FORMATETC& dest, const FORMATETC& source) 
{
  dest = source;
  if (source.ptd != nullptr) 
  {
    dest.ptd = (DVTARGETDEVICE*) CoTaskMemAlloc (sizeof (DVTARGETDEVICE));
    if (dest.ptd != nullptr) 
    {
      *(dest.ptd) = *(source.ptd);
    }
  }
}

// Enum class, seems necessary for some reason
struct EnumFORMATETC final : public IEnumFORMATETC
{
  EnumFORMATETC (const FORMATETC* f) : mFormatPtr (f) {}
  ~EnumFORMATETC() {}

  ULONG STDMETHODCALLTYPE AddRef(void) { return ++mRefCount; }
  ULONG STDMETHODCALLTYPE Release(void) 
  {
    int refCount = --mRefCount;
    assert(refCount >= 0);
    if(refCount <= 0) {
      delete this;
    }
    return refCount;
  }

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID refiid, void **resultHandle) 
  {
    if (refiid == IID_IEnumFORMATETC) 
    {
      AddRef();
      *resultHandle = this;
      return S_OK;
    }
    *resultHandle = nullptr;
    return E_NOINTERFACE;
  }

  HRESULT STDMETHODCALLTYPE Clone (IEnumFORMATETC** resultHandle) override
  {
    if (resultHandle == nullptr) 
    {
      return E_POINTER;
    }
    EnumFORMATETC* copyObj = new EnumFORMATETC(mFormatPtr);
    copyObj->mCounter = mCounter;
    *resultHandle = copyObj;
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE Next (ULONG celt, FORMATETC *pFormat, ULONG* pceltFetched) override
  {
    if (pceltFetched != nullptr) 
    {
      *pceltFetched = 0;
    } 
    else if (celt != 1)
    {
      return S_FALSE;
    }

    if (mCounter == 0 && celt > 0 && pFormat != nullptr) 
    {
      deepFormatCopy(pFormat[0], *mFormatPtr);
      mCounter++;
      if (pceltFetched != nullptr) 
      {
        *pceltFetched = 1;
      }
      return S_OK;
    }
    return S_FALSE;
  }

  HRESULT STDMETHODCALLTYPE Skip (ULONG celt) override
  {
    if (mCounter + (int) celt >= 1) 
    {
      return S_FALSE;
    }
    mCounter += (int) celt;
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE Reset() override
  {
    mCounter = 0;
    return S_OK;
  }

private:
  int mRefCount = 1;
  const FORMATETC* const mFormatPtr;
  int mCounter = 0;
};

// Object that carries the DnD payload. TODO: also support text/string?
class DataObject : public IDataObject 
{
public:
  DataObject(const FORMATETC* f, const char *filePath) : mFormatPtr (f) 
  {
    mFilePath = filePath;
  }
  
  virtual ~DataObject() 
  {
    assert(mRefCount == 0);
  }
  
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID refiid, void **resultHandle) 
  {
    if (refiid == IID_IDataObject || refiid == IID_IUnknown) 
    { // note: it seems that IUnknown must be supported here, don't know why
      AddRef();
      *resultHandle=this;
      return S_OK;
    }
    *resultHandle = NULL;
    return E_NOINTERFACE;
  }

  ULONG STDMETHODCALLTYPE AddRef() { return ++mRefCount; }
  ULONG STDMETHODCALLTYPE Release() 
  {
    int refCount = --mRefCount;
    assert(mRefCount>=0);
    if (mRefCount<=0) 
    {
      delete this;
    }
    return refCount;
  }

  bool acceptFormat(FORMATETC *f) 
  {
    return (f->dwAspect & DVASPECT_CONTENT) && (f->tymed & TYMED_HGLOBAL) && (f->cfFormat == CF_HDROP);
  }
  
  HRESULT STDMETHODCALLTYPE GetData(FORMATETC *pFormat, STGMEDIUM *pMedium) 
  {
    if (pFormat == nullptr) 
    {
      return E_INVALIDARG;
    }

    if (acceptFormat(pFormat) == false) 
    {
      return DV_E_FORMATETC;
    }

    UTF8AsUTF16 pathWide(mFilePath.c_str());
    // GHND ensures that the memory is zeroed
    HDROP hGlobal = (HDROP)GlobalAlloc(GHND, sizeof(DROPFILES) + (sizeof(wchar_t) * (pathWide.GetLength() + 2)));

    if (!hGlobal) 
    {
      DBGMSG("DataObject::GetData ERROR: GlobalAlloc returned null, aborting.\n");
      return false;
    }

    DROPFILES* pDropFiles = (DROPFILES*) GlobalLock(hGlobal);
    if (!pDropFiles) 
    {
      DBGMSG("DataObject::GetData ERROR: GlobalLock returned null, aborting.\n");
      return false;
    }
    // Populate the dropfile structure and copy the file path
    pDropFiles->pFiles = sizeof(DROPFILES);
    pDropFiles->fWide = true;
    std::copy_n(pathWide.Get(), pathWide.GetLength(), reinterpret_cast<wchar_t*>(&pDropFiles[1]));

    // not sure if this is necessary to set the medium but why not
    pMedium->tymed = TYMED_HGLOBAL;
    pMedium->hGlobal = hGlobal;
    pMedium->pUnkForRelease = nullptr;

    GlobalUnlock(hGlobal);

    return S_OK;
  }
  
  HRESULT STDMETHODCALLTYPE QueryGetData( FORMATETC *pFormat ) 
  {
    if (pFormat == nullptr) 
    {
      return E_INVALIDARG;
    }

    if (acceptFormat(pFormat) == false) 
    {
      return DV_E_FORMATETC;
    }
  
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*, FORMATETC* pFormatOut) override
  {
    pFormatOut->ptd = nullptr;
    return E_NOTIMPL;
  }

  HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD direction, IEnumFORMATETC** resultHandle) override 
  {
    if (!resultHandle) 
    {
      return E_POINTER;
    }
    if (direction == DATADIR_GET) 
    {
      *resultHandle = new EnumFORMATETC(mFormatPtr);
      return S_OK;
    }
    *resultHandle = nullptr;
    return E_NOTIMPL;
  }

  // unimplemented (unnecessary?) functions
  HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*, STGMEDIUM*) { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE SetData(FORMATETC*, STGMEDIUM*, BOOL) { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**) { return E_NOTIMPL; }
private:
  int mRefCount = 1;
  std::string mFilePath;
  const FORMATETC* const mFormatPtr;
};

// IDropTarget implementation for inbound drag & drop (files) using OLE
class DropTarget : public IDropTarget
{
public:
  DropTarget(HWND hwnd, IGraphicsWin* owner)
  : mHwnd(hwnd), mOwner(owner) {}

  // IUnknown
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
  {
    if (!ppv) return E_POINTER;
    if (riid == IID_IDropTarget || riid == IID_IUnknown)
    {
      *ppv = static_cast<IDropTarget*>(this);
      AddRef();
      return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG) ++mRefCount; }
  ULONG STDMETHODCALLTYPE Release() override
  {
    long c = --mRefCount;
    if (c == 0) delete this;
    return (ULONG) (c < 0 ? 0 : c);
  }

  // IDropTarget
  HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* pDataObj, DWORD /*grfKeyState*/, POINTL pt, DWORD* pdwEffect) override
  {
    mHasFiles = CanAccept(pDataObj);
    if (pdwEffect) *pdwEffect = mHasFiles ? DROPEFFECT_COPY : DROPEFFECT_NONE;
    mLastPt = pt;
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE DragOver(DWORD /*grfKeyState*/, POINTL pt, DWORD* pdwEffect) override
  {
    if (pdwEffect) *pdwEffect = mHasFiles ? DROPEFFECT_COPY : DROPEFFECT_NONE;
    mLastPt = pt;
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE DragLeave() override
  {
    mHasFiles = false;
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE Drop(IDataObject* pDataObj, DWORD /*grfKeyState*/, POINTL pt, DWORD* pdwEffect) override
  {
    if (pdwEffect) *pdwEffect = DROPEFFECT_NONE;
    if (!mHasFiles || !pDataObj || !mOwner) return DRAGDROP_S_CANCEL;

    FORMATETC fmt = { CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    STGMEDIUM stg = {};
    if (FAILED(pDataObj->GetData(&fmt, &stg)))
      return DRAGDROP_S_CANCEL;

    std::vector<std::wstring> filesW;
    if (stg.tymed == TYMED_HGLOBAL && stg.hGlobal)
    {
      HDROP hdrop = (HDROP) stg.hGlobal;
      UINT count = DragQueryFileW(hdrop, 0xFFFFFFFF, nullptr, 0);
      filesW.reserve(count ? count : 1);
      wchar_t buf[2048];
      for (UINT i = 0; i < count; ++i)
      {
        UINT n = DragQueryFileW(hdrop, i, buf, (UINT)(sizeof(buf)/sizeof(buf[0])));
        if (n > 0) filesW.emplace_back(buf, buf + n);
      }
    }

    ReleaseStgMedium(&stg);

    if (!filesW.empty())
    {
      mOwner->OnOLEDropFiles(filesW, pt.x, pt.y);
      if (pdwEffect) *pdwEffect = DROPEFFECT_COPY;
      return S_OK;
    }
    return DRAGDROP_S_CANCEL;
  }

private:
  bool CanAccept(IDataObject* pDataObj)
  {
    if (!pDataObj) return false;
    FORMATETC fmt = { CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    return pDataObj->QueryGetData(&fmt) == S_OK;
  }

  std::atomic<long> mRefCount{1};
  HWND mHwnd = nullptr;
  IGraphicsWin* mOwner = nullptr;
  bool mHasFiles = false;
  POINTL mLastPt{};
};

} // end of DragAndDropHelpers namespace

END_IGRAPHICS_NAMESPACE
END_IPLUG_NAMESPACE
