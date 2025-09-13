/*
 ==============================================================================

 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers.

 See LICENSE.txt for  more info.

 ==============================================================================
*/

#pragma once

/**
 * @file
 * @brief IPlug logging a.k.a tracing functionality
 *
 * To trace some arbitrary data:                 Trace(TRACELOC, "inst=%p %s:%d", pInst, myStr, myInt);
 * To simply create a trace entry in the log:    TRACE
 * To time a block using labels:                 TRACE_START("mytask"); ... TRACE_END("mytask");
 * Predefined helpers are available for window creation, cache queries and resource loads.
 * No need to wrap tracer calls in #ifdef TRACER_BUILD because Trace is a no-op unless TRACER_BUILD is defined.
 *
 * From 2024 onwards each plug-in instance writes to its own log file.  The log
 * files are created by \ref LogFileManager using the plug-in name and an
* incrementing index such that the nth instance of "MyPlugin" will produce a
* file like `MyPlugin_n.txt` in the platform specific log folder.  For example
* on Windows the path will be `C:\\temp\\MyPlugin_1.txt`.  The per-instance
* FILE* can be retrieved via IPluginBase::GetLogFile() and passed to the TRACEF
* family of macros in order to direct output to a particular instance.
*/

#include <cassert>
#include <cctype>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>

#include <chrono>
#include <string>
#include <unordered_map>


#include "mutex.h"
#include "wdlstring.h"

#include "IPlugConstants.h"
#include "IPlugUtilities.h"

BEGIN_IPLUG_NAMESPACE

#ifdef NDEBUG
  #define DBGMSG(...)                                                                                                                                                                                  \
    do                                                                                                                                                                                                 \
    {                                                                                                                                                                                                  \
    } while (0) // should be optimized away
#else
  #if defined(OS_MAC) || defined(OS_LINUX) || defined(OS_WEB) || defined(OS_IOS)
    #define DBGMSG(...) printf(__VA_ARGS__)
  #elif defined OS_WIN
static void DBGMSG(const char* format, ...)
{
  char buf[4096];
  va_list args;
  int n;

  va_start(args, format);
  n = vsnprintf_s(buf, sizeof buf, sizeof buf - 1, format, args);
  va_end(args);

  OutputDebugStringW(UTF8AsUTF16(buf).Get());
}
  #endif
#endif

#if defined TRACER_BUILD
  #define TRACE Trace(LogFileManager::GetInstance().GetInstance(nullptr, "IPlugLog"), TRACELOC, "");

  #if defined OS_WIN
    #define SYS_THREAD_ID (intptr_t)GetCurrentThreadId()
  #elif defined(OS_MAC) || defined(OS_LINUX) || defined(OS_WEB)
    #define SYS_THREAD_ID (intptr_t)pthread_self()
  #endif

#else
  #define TRACE
#endif

#define TRACELOC __FUNCTION__, __LINE__
static void Trace(FILE* logFile, const char* funcName, int line, const char* fmtStr, ...);
static void Trace(const char* funcName, int line, const char* fmtStr, ...);
static void Trace(FILE* logFile, const char* funcName, int line, const char* id, bool start);

#define APPEND_TIMESTAMP(str) AppendTimestamp(__DATE__, __TIME__, str)

/**
 * Manager class responsible for creating and returning FILE handles for each
 * plug-in instance.  The key is typically the address of the instance.  Files
 * are created lazily and named using the plug-in name and an incrementing
 * index, e.g. `MyPlugin_1.txt` for the first instance of "MyPlugin".
 */
class LogFileManager
{
public:
  static LogFileManager& GetInstance()
  {
    static LogFileManager sManager;
    return sManager;
  }

  FILE* GetInstance(void* instanceKey, const char* pluginName)
  {
    WDL_MutexLock lock(&mMutex);
    auto it = mFiles.find(instanceKey);
    if (it != mFiles.end())
      return it->second;

    int idx = ++mInstanceCounts[pluginName];
#ifdef OS_WIN
    char logFilePath[MAX_WIN32_PATH_LEN];
    snprintf(logFilePath, MAX_WIN32_PATH_LEN, "C:\\temp\\%s_%d.txt", pluginName, idx);
#else
    char logFilePath[MAX_MACOS_PATH_LEN];
    snprintf(logFilePath, MAX_MACOS_PATH_LEN, "%s/%s_%d.txt", getenv("HOME"), pluginName, idx);
#endif
    FILE* fp = fopenUTF8(logFilePath, "w");
    assert(fp);
    DBGMSG("Logging to %s\n", logFilePath);
    mFiles[instanceKey] = fp;
    return fp;
  }

  void ReleaseInstance(void* instanceKey)
  {
    WDL_MutexLock lock(&mMutex);
    auto it = mFiles.find(instanceKey);
    if (it != mFiles.end())
    {
      fclose(it->second);
      mFiles.erase(it);
    }
  }

private:
  WDL_Mutex mMutex;
  std::unordered_map<void*, FILE*> mFiles;
  std::unordered_map<std::string, int> mInstanceCounts;
};

static bool IsWhitespace(char c) { return (c == ' ' || c == '\t' || c == '\n' || c == '\r'); }

static const char* CurrentTime()
{
  //    TODO: replace with std::chrono based version
  time_t t = time(0);
  tm* pT = localtime(&t);

  char cStr[32];
  strftime(cStr, 32, "%Y%m%d %H:%M ", pT);

  int tz = 60 * pT->tm_hour + pT->tm_min;
  int yday = pT->tm_yday;
  pT = gmtime(&t);
  tz -= 60 * pT->tm_hour + pT->tm_min;
  yday -= pT->tm_yday;
  if (yday != 0)
  {
    if (yday > 1)
      yday = -1;
    else if (yday < -1)
      yday = 1;
    tz += 24 * 60 * yday;
  }
  int i = (int)strlen(cStr);
  cStr[i++] = tz >= 0 ? '+' : '-';
  if (tz < 0)
    tz = -tz;
  snprintf(&cStr[i], 32, "%02d%02d", tz / 60, tz % 60);

  static char sTimeStr[32];
  strcpy(sTimeStr, cStr);
  return sTimeStr;
}

static const char* AppendTimestamp(const char* Mmm_dd_yyyy, const char* hh_mm_ss, const char* cStr)
{
  static WDL_String str;
  str.Set(cStr);
  str.Append(" ");
  WDL_String tStr;
  tStr.Set("[");
  tStr.Append(Mmm_dd_yyyy);
  tStr.SetLen(7);
  tStr.DeleteSub(4, 1);
  tStr.Append(" ");
  tStr.Append(hh_mm_ss);
  tStr.SetLen(12);
  tStr.Append("]");
  str.Append(tStr.Get());
  return str.Get();
}

  class ScopedTimer
  {
  public:
    ScopedTimer(FILE* logFile, const char* funcName, int line, const char* label)
      : mLogFile(logFile), mFuncName(funcName), mLine(line), mLabel(label)
#ifdef TRACER_BUILD
      , mStart(std::chrono::steady_clock::now())
#endif
    {}

    ~ScopedTimer()
    {
#ifdef TRACER_BUILD
      auto end = std::chrono::steady_clock::now();
      auto us = std::chrono::duration_cast<std::chrono::microseconds>(end - mStart).count();
      Trace(mLogFile, mFuncName, mLine, "%s %lldus", mLabel, (long long) us);
#endif
    }

  private:
    FILE* mLogFile;
    const char* mFuncName;
    int mLine;
    const char* mLabel;
#ifdef TRACER_BUILD
    std::chrono::steady_clock::time_point mStart;
#endif
  };

#ifdef TRACER_BUILD
  #define IPLUG_TRACE_CONCAT_INNER(a, b) a##b
  #define IPLUG_TRACE_CONCAT(a, b) IPLUG_TRACE_CONCAT_INNER(a, b)
  #define TRACE_SCOPE(label) ScopedTimer IPLUG_TRACE_CONCAT(_iplugScopedTimer_, __LINE__)(LogFileManager::GetInstance().GetInstance(nullptr, "IPlugLog"), TRACELOC, label)
  #define TRACE_START(id) Trace(LogFileManager::GetInstance().GetInstance(nullptr, "IPlugLog"), TRACELOC, id, true)
  #define TRACE_END(id)   Trace(LogFileManager::GetInstance().GetInstance(nullptr, "IPlugLog"), TRACELOC, id, false)
  #define TRACE_WINDOW_CREATION_START() TRACE_START("WindowCreation")
  #define TRACE_WINDOW_CREATION_END()   TRACE_END("WindowCreation")
  #define TRACE_CACHE_QUERY_START()     TRACE_START("CacheQuery")
  #define TRACE_CACHE_QUERY_END()       TRACE_END("CacheQuery")
  #define TRACE_RESOURCE_LOAD_START()   TRACE_START("ResourceLoad")
  #define TRACE_RESOURCE_LOAD_END()     TRACE_END("ResourceLoad")

  // Macros that allow specifying a log FILE* explicitly
  #define TRACEF(file) Trace(file, TRACELOC, "")
  #define TRACE_SCOPE_F(file, label) ScopedTimer IPLUG_TRACE_CONCAT(_iplugScopedTimer_, __LINE__)(file, TRACELOC, label)
  #define TRACE_START_F(file, id) Trace(file, TRACELOC, id, true)
  #define TRACE_END_F(file, id)   Trace(file, TRACELOC, id, false)
  #define TRACE_WINDOW_CREATION_START_F(file) TRACE_START_F(file, "WindowCreation")
  #define TRACE_WINDOW_CREATION_END_F(file)   TRACE_END_F(file, "WindowCreation")
  #define TRACE_CACHE_QUERY_START_F(file)     TRACE_START_F(file, "CacheQuery")
  #define TRACE_CACHE_QUERY_END_F(file)       TRACE_END_F(file, "CacheQuery")
  #define TRACE_RESOURCE_LOAD_START_F(file)   TRACE_START_F(file, "ResourceLoad")
  #define TRACE_RESOURCE_LOAD_END_F(file)     TRACE_END_F(file, "ResourceLoad")
#else
  #define TRACE_SCOPE(label)
  #define TRACE_START(id)
  #define TRACE_END(id)
  #define TRACE_WINDOW_CREATION_START()
  #define TRACE_WINDOW_CREATION_END()
  #define TRACE_CACHE_QUERY_START()
  #define TRACE_CACHE_QUERY_END()
  #define TRACE_RESOURCE_LOAD_START()
  #define TRACE_RESOURCE_LOAD_END()
  #define TRACEF(file)
  #define TRACE_SCOPE_F(file, label)
  #define TRACE_START_F(file, id)
  #define TRACE_END_F(file, id)
  #define TRACE_WINDOW_CREATION_START_F(file)
  #define TRACE_WINDOW_CREATION_END_F(file)
  #define TRACE_CACHE_QUERY_START_F(file)
  #define TRACE_CACHE_QUERY_END_F(file)
  #define TRACE_RESOURCE_LOAD_START_F(file)
  #define TRACE_RESOURCE_LOAD_END_F(file)
#endif

  #if defined TRACER_BUILD

  const int TXTLEN = 1024;

  // _vsnsprintf

  #define VARARGS_TO_STR(str) { \
  try { \
  va_list argList;  \
  va_start(argList, format);  \
  int i = vsnprintf(str, TXTLEN-2, format, argList); \
  if (i < 0 || i > TXTLEN-2) {  \
  str[TXTLEN-1] = '\0';  \
  } \
  va_end(argList);  \
  } \
  catch(...) {  \
  strcpy(str, "parse error"); \
  } \
  strcat(str, "\r\n"); \
  }

void Trace(FILE* logFile, const char* funcName, int line, const char* id, bool start)
{
  static std::unordered_map<std::string, std::chrono::steady_clock::time_point> timers;

  if (start)
  {
    timers[id] = std::chrono::steady_clock::now();
  }
  else
  {
    auto it = timers.find(id);
    if (it != timers.end())
    {
      auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - it->second).count();
      Trace(logFile, funcName, line, "%s %lldus", id, (long long) elapsed);
      timers.erase(it);
    }
    else
    {
      Trace(logFile, funcName, line, "%s end", id);
    }
  }
}


static intptr_t GetOrdinalThreadID(intptr_t sysThreadID)
{
  static WDL_TypedBuf<intptr_t> sThreadIDs;
  int i, n = sThreadIDs.GetSize();
  intptr_t* pThreadID = sThreadIDs.Get();
  for (i = 0; i < n; ++i, ++pThreadID)
  {
    if (sysThreadID == *pThreadID)
    {
      return i;
    }
  }
  sThreadIDs.Resize(n + 1);
  *(sThreadIDs.Get() + n) = sysThreadID;
  return n;
}

  #define MAX_LOG_LINES 16384
void Trace(FILE* logFile, const char* funcName, int line, const char* format, ...)
{
  static int sTrace = 0;
  static int32_t sProcessCount = 0;
  static int32_t sIdleCount = 0;

  if (sTrace++ < MAX_LOG_LINES)
  {
  #ifndef TRACETOSTDOUT
    assert(logFile);
  #endif
    static WDL_Mutex sLogMutex;
    char str[TXTLEN];
    VARARGS_TO_STR(str);

  #ifdef TRACETOSTDOUT
    DBGMSG("[%ld:%s:%d]%s", GetOrdinalThreadID(SYS_THREAD_ID), funcName, line, str);
  #else
    WDL_MutexLock lock(&sLogMutex);
    intptr_t threadID = GetOrdinalThreadID(SYS_THREAD_ID);

    if (strstr(funcName, "rocess") || strstr(funcName, "ender")) // These are not typos! by excluding the first character, we can use TRACE in methods called ProcessXXX or process etc.
    {
      if (++sProcessCount > MAX_PROCESS_TRACE_COUNT)
      {
        fflush(logFile);
        return;
      }
      else if (sProcessCount == MAX_PROCESS_TRACE_COUNT)
      {
        fprintf(logFile, "**************** DISABLING PROCESS TRACING AFTER %d HITS ****************\n\n", sProcessCount);
        fflush(logFile);
        return;
      }
    }

    #ifdef VST2_API
    if (strstr(str, "effGetProgram") || strstr(str, "effEditGetRect") || strstr(funcName, "MouseOver"))
    #else
    if (strstr(funcName, "MouseOver") || strstr(funcName, "idle"))
    #endif
    {
      if (++sIdleCount > MAX_IDLE_TRACE_COUNT)
      {
        fflush(logFile);
        return;
      }
      else if (sIdleCount == MAX_IDLE_TRACE_COUNT)
      {
        fprintf(logFile, "**************** DISABLING IDLE/MOUSEOVER TRACING AFTER %d HITS ****************\n", sIdleCount);
        fflush(logFile);
        return;
      }
    }

    if (threadID > 0)
      fprintf(logFile, "*** -");

    fprintf(logFile, "[%ld:%s:%d]%s", threadID, funcName, line, str);
    fflush(logFile);
  #endif
  }
}

void Trace(const char* funcName, int line, const char* format, ...)
{
  FILE* logFile = LogFileManager::GetInstance().GetInstance(nullptr, "IPlugLog");
  static int sTrace = 0;
  static int32_t sProcessCount = 0;
  static int32_t sIdleCount = 0;

  if (sTrace++ < MAX_LOG_LINES)
  {
  #ifndef TRACETOSTDOUT
    assert(logFile);
  #endif
    static WDL_Mutex sLogMutex;
    char str[TXTLEN];
    VARARGS_TO_STR(str);

  #ifdef TRACETOSTDOUT
    DBGMSG("[%ld:%s:%d]%s", GetOrdinalThreadID(SYS_THREAD_ID), funcName, line, str);
  #else
    WDL_MutexLock lock(&sLogMutex);
    intptr_t threadID = GetOrdinalThreadID(SYS_THREAD_ID);

    if (strstr(funcName, "rocess") || strstr(funcName, "ender"))
    {
      if (++sProcessCount > MAX_PROCESS_TRACE_COUNT)
      {
        fflush(logFile);
        return;
      }
      else if (sProcessCount == MAX_PROCESS_TRACE_COUNT)
      {
        fprintf(logFile, "**************** DISABLING PROCESS TRACING AFTER %d HITS ****************\n\n", sProcessCount);
        fflush(logFile);
        return;
      }
    }

    #ifdef VST2_API
    if (strstr(str, "effGetProgram") || strstr(str, "effEditGetRect") || strstr(funcName, "MouseOver"))
    #else
    if (strstr(funcName, "MouseOver") || strstr(funcName, "idle"))
    #endif
    {
      if (++sIdleCount > MAX_IDLE_TRACE_COUNT)
      {
        fflush(logFile);
        return;
      }
      else if (sIdleCount == MAX_IDLE_TRACE_COUNT)
      {
        fprintf(logFile, "**************** DISABLING IDLE/MOUSEOVER TRACING AFTER %d HITS ****************\n", sIdleCount);
        fflush(logFile);
        return;
      }
    }

    if (threadID > 0)
      fprintf(logFile, "*** -");

    fprintf(logFile, "[%ld:%s:%d]%s", threadID, funcName, line, str);
    fflush(logFile);
  #endif
  }
}

  #ifdef VST2_API
    #include "aeffectx.h"
static const char* VSTOpcodeStr(int opCode)
{
  switch (opCode)
  {
  case effOpen:
    return "effOpen";
  case effClose:
    return "effClose";
  case effSetProgram:
    return "effSetProgram";
  case effGetProgram:
    return "effGetProgram";
  case effSetProgramName:
    return "effSetProgramName";
  case effGetProgramName:
    return "effGetProgramName";
  case effGetParamLabel:
    return "effGetParamLabel";
  case effGetParamDisplay:
    return "effGetParamDisplay";
  case effGetParamName:
    return "effGetParamName";
  case __effGetVuDeprecated:
    return "__effGetVuDeprecated";
  case effSetSampleRate:
    return "effSetSampleRate";
  case effSetBlockSize:
    return "effSetBlockSize";
  case effMainsChanged:
    return "effMainsChanged";
  case effEditGetRect:
    return "effEditGetRect";
  case effEditOpen:
    return "effEditOpen";
  case effEditClose:
    return "effEditClose";
  case __effEditDrawDeprecated:
    return "__effEditDrawDeprecated";
  case __effEditMouseDeprecated:
    return "__effEditMouseDeprecated";
  case __effEditKeyDeprecated:
    return "__effEditKeyDeprecated";
  case effEditIdle:
    return "effEditIdle";
  case __effEditTopDeprecated:
    return "__effEditTopDeprecated";
  case __effEditSleepDeprecated:
    return "__effEditSleepDeprecated";
  case __effIdentifyDeprecated:
    return "__effIdentifyDeprecated";
  case effGetChunk:
    return "effGetChunk";
  case effSetChunk:
    return "effSetChunk";
  case effProcessEvents:
    return "effProcessEvents";
  case effCanBeAutomated:
    return "effCanBeAutomated";
  case effString2Parameter:
    return "effString2Parameter";
  case __effGetNumProgramCategoriesDeprecated:
    return "__effGetNumProgramCategoriesDeprecated";
  case effGetProgramNameIndexed:
    return "effGetProgramNameIndexed";
  case __effCopyProgramDeprecated:
    return "__effCopyProgramDeprecated";
  case __effConnectInputDeprecated:
    return "__effConnectInputDeprecated";
  case __effConnectOutputDeprecated:
    return "__effConnectOutputDeprecated";
  case effGetInputProperties:
    return "effGetInputProperties";
  case effGetOutputProperties:
    return "effGetOutputProperties";
  case effGetPlugCategory:
    return "effGetPlugCategory";
  case __effGetCurrentPositionDeprecated:
    return "__effGetCurrentPositionDeprecated";
  case __effGetDestinationBufferDeprecated:
    return "__effGetDestinationBufferDeprecated";
  case effOfflineNotify:
    return "effOfflineNotify";
  case effOfflinePrepare:
    return "effOfflinePrepare";
  case effOfflineRun:
    return "effOfflineRun";
  case effProcessVarIo:
    return "effProcessVarIo";
  case effSetSpeakerArrangement:
    return "effSetSpeakerArrangement";
  case __effSetBlockSizeAndSampleRateDeprecated:
    return "__effSetBlockSizeAndSampleRateDeprecated";
  case effSetBypass:
    return "effSetBypass";
  case effGetEffectName:
    return "effGetEffectName";
  case __effGetErrorTextDeprecated:
    return "__effGetErrorTextDeprecated";
  case effGetVendorString:
    return "effGetVendorString";
  case effGetProductString:
    return "effGetProductString";
  case effGetVendorVersion:
    return "effGetVendorVersion";
  case effVendorSpecific:
    return "effVendorSpecific";
  case effCanDo:
    return "effCanDo";
  case effGetTailSize:
    return "effGetTailSize";
  case __effIdleDeprecated:
    return "__effIdleDeprecated";
  case __effGetIconDeprecated:
    return "__effGetIconDeprecated";
  case __effSetViewPositionDeprecated:
    return "__effSetViewPositionDeprecated";
  case effGetParameterProperties:
    return "effGetParameterProperties";
  case __effKeysRequiredDeprecated:
    return "__effKeysRequiredDeprecated";
  case effGetVstVersion:
    return "effGetVstVersion";
  case effEditKeyDown:
    return "effEditKeyDown";
  case effEditKeyUp:
    return "effEditKeyUp";
  case effSetEditKnobMode:
    return "effSetEditKnobMode";
  case effGetMidiProgramName:
    return "effGetMidiProgramName";
  case effGetCurrentMidiProgram:
    return "effGetCurrentMidiProgram";
  case effGetMidiProgramCategory:
    return "effGetMidiProgramCategory";
  case effHasMidiProgramsChanged:
    return "effHasMidiProgramsChanged";
  case effGetMidiKeyName:
    return "effGetMidiKeyName";
  case effBeginSetProgram:
    return "effBeginSetProgram";
  case effEndSetProgram:
    return "effEndSetProgram";
  case effGetSpeakerArrangement:
    return "effGetSpeakerArrangement";
  case effShellGetNextPlugin:
    return "effShellGetNextPlugin";
  case effStartProcess:
    return "effStartProcess";
  case effStopProcess:
    return "effStopProcess";
  case effSetTotalSampleToProcess:
    return "effSetTotalSampleToProcess";
  case effSetPanLaw:
    return "effSetPanLaw";
  case effBeginLoadBank:
    return "effBeginLoadBank";
  case effBeginLoadProgram:
    return "effBeginLoadProgram";
  case effSetProcessPrecision:
    return "effSetProcessPrecision";
  case effGetNumMidiInputChannels:
    return "effGetNumMidiInputChannels";
  case effGetNumMidiOutputChannels:
    return "effGetNumMidiOutputChannels";
  default:
    return "unknown";
  }
}
  #endif

  #if defined AU_API
    #include <AudioUnit/AudioUnitProperties.h>
    #include <CoreServices/CoreServices.h>
static const char* AUSelectStr(int select)
{
  switch (select)
  {
  case kComponentOpenSelect:
    return "kComponentOpenSelect";
  case kComponentCloseSelect:
    return "kComponentCloseSelect";
  case kComponentVersionSelect:
    return "kComponentVersionSelect";
  case kAudioUnitInitializeSelect:
    return "kAudioUnitInitializeSelect";
  case kAudioUnitUninitializeSelect:
    return "kAudioUnitUninitializeSelect";
  case kAudioUnitGetPropertyInfoSelect:
    return "kAudioUnitGetPropertyInfoSelect";
  case kAudioUnitGetPropertySelect:
    return "kAudioUnitGetPropertySelect";
  case kAudioUnitSetPropertySelect:
    return "kAudioUnitSetPropertySelect";
  case kAudioUnitAddPropertyListenerSelect:
    return "kAudioUnitAddPropertyListenerSelect";
  case kAudioUnitRemovePropertyListenerSelect:
    return "kAudioUnitRemovePropertyListenerSelect";
  case kAudioUnitAddRenderNotifySelect:
    return "kAudioUnitAddRenderNotifySelect";
  case kAudioUnitRemoveRenderNotifySelect:
    return "kAudioUnitRemoveRenderNotifySelect";
  case kAudioUnitGetParameterSelect:
    return "kAudioUnitGetParameterSelect";
  case kAudioUnitSetParameterSelect:
    return "kAudioUnitSetParameterSelect";
  case kAudioUnitScheduleParametersSelect:
    return "kAudioUnitScheduleParametersSelect";
  case kAudioUnitRenderSelect:
    return "kAudioUnitRenderSelect";
  case kAudioUnitResetSelect:
    return "kAudioUnitResetSelect";
  case kComponentCanDoSelect:
    return "kComponentCanDoSelect";
  case kAudioUnitComplexRenderSelect:
    return "kAudioUnitComplexRenderSelect";
  case kAudioUnitProcessSelect:
    return "kAudioUnitProcessSelect";
  case kAudioUnitProcessMultipleSelect:
    return "kAudioUnitProcessMultipleSelect";
  case kAudioUnitRange:
    return "kAudioUnitRange";
  case kAudioUnitRemovePropertyListenerWithUserDataSelect:
    return "kAudioUnitRemovePropertyListenerWithUserDataSelect";
  default:
    return "unknown";
  }
}

static const char* AUPropertyStr(int propID)
{
  switch (propID)
  {
  case kAudioUnitProperty_ClassInfo:
    return "kAudioUnitProperty_ClassInfo";
  case kAudioUnitProperty_MakeConnection:
    return "kAudioUnitProperty_MakeConnection";
  case kAudioUnitProperty_SampleRate:
    return "kAudioUnitProperty_SampleRate";
  case kAudioUnitProperty_ParameterList:
    return "kAudioUnitProperty_ParameterList";
  case kAudioUnitProperty_ParameterInfo:
    return "kAudioUnitProperty_ParameterInfo";
  case kAudioUnitProperty_FastDispatch:
    return "kAudioUnitProperty_FastDispatch";
  case kAudioUnitProperty_CPULoad:
    return "kAudioUnitProperty_CPULoad";
  case kAudioUnitProperty_StreamFormat:
    return "kAudioUnitProperty_StreamFormat";
  case kAudioUnitProperty_ElementCount:
    return "kAudioUnitProperty_ElementCount";
  case kAudioUnitProperty_Latency:
    return "kAudioUnitProperty_Latency";
  case kAudioUnitProperty_SupportedNumChannels:
    return "kAudioUnitProperty_SupportedNumChannels";
  case kAudioUnitProperty_MaximumFramesPerSlice:
    return "kAudioUnitProperty_MaximumFramesPerSlice";
  case kAudioUnitProperty_SetExternalBuffer:
    return "kAudioUnitProperty_SetExternalBuffer";
  case kAudioUnitProperty_ParameterValueStrings:
    return "kAudioUnitProperty_ParameterValueStrings";
  case kAudioUnitProperty_GetUIComponentList:
    return "kAudioUnitProperty_GetUIComponentList";
  case kAudioUnitProperty_AudioChannelLayout:
    return "kAudioUnitProperty_AudioChannelLayout";
  case kAudioUnitProperty_TailTime:
    return "kAudioUnitProperty_TailTime";
  case kAudioUnitProperty_BypassEffect:
    return "kAudioUnitProperty_BypassEffect";
  case kAudioUnitProperty_LastRenderError:
    return "kAudioUnitProperty_LastRenderError";
  case kAudioUnitProperty_SetRenderCallback:
    return "kAudioUnitProperty_SetRenderCallback";
  case kAudioUnitProperty_FactoryPresets:
    return "kAudioUnitProperty_FactoryPresets";
  case kAudioUnitProperty_ContextName:
    return "kAudioUnitProperty_ContextName";
  case kAudioUnitProperty_RenderQuality:
    return "kAudioUnitProperty_RenderQuality";
  case kAudioUnitProperty_HostCallbacks:
    return "kAudioUnitProperty_HostCallbacks";
  case kAudioUnitProperty_CurrentPreset:
    return "kAudioUnitProperty_CurrentPreset";
  case kAudioUnitProperty_InPlaceProcessing:
    return "kAudioUnitProperty_InPlaceProcessing";
  case kAudioUnitProperty_ElementName:
    return "kAudioUnitProperty_ElementName";
  case kAudioUnitProperty_CocoaUI:
    return "kAudioUnitProperty_CocoaUI";
  case kAudioUnitProperty_SupportedChannelLayoutTags:
    return "kAudioUnitProperty_SupportedChannelLayoutTags";
  case kAudioUnitProperty_ParameterIDName:
    return "kAudioUnitProperty_ParameterIDName";
  case kAudioUnitProperty_ParameterClumpName:
    return "kAudioUnitProperty_ParameterClumpName";
  case kAudioUnitProperty_PresentPreset:
    return "kAudioUnitProperty_PresentPreset";
  case kAudioUnitProperty_OfflineRender:
    return "kAudioUnitProperty_OfflineRender";
  case kAudioUnitProperty_ParameterStringFromValue:
    return "kAudioUnitProperty_ParameterStringFromValue";
  case kAudioUnitProperty_ParameterValueFromString:
    return "kAudioUnitProperty_ParameterValueFromString";
  case kAudioUnitProperty_IconLocation:
    return "kAudioUnitProperty_IconLocation";
  case kAudioUnitProperty_PresentationLatency:
    return "kAudioUnitProperty_PresentationLatency";
  case kAudioUnitProperty_DependentParameters:
    return "kAudioUnitProperty_DependentParameters";
  case kAudioUnitProperty_AUHostIdentifier:
    return "kAudioUnitProperty_AUHostIdentifier";
  case kAudioUnitProperty_MIDIOutputCallbackInfo:
    return "kAudioUnitProperty_MIDIOutputCallbackInfo";
  case kAudioUnitProperty_MIDIOutputCallback:
    return "kAudioUnitProperty_MIDIOutputCallback";
  case kAudioUnitProperty_InputSamplesInOutput:
    return "kAudioUnitProperty_InputSamplesInOutput";
  case kAudioUnitProperty_ClassInfoFromDocument:
    return "kAudioUnitProperty_ClassInfoFromDocument";
    #if MAC_OS_X_VERSION_MAX_ALLOWED >= 1010
  case kAudioUnitProperty_ShouldAllocateBuffer:
    return "kAudioUnitProperty_ShouldAllocateBuffer";
  case kAudioUnitProperty_FrequencyResponse:
    return "kAudioUnitProperty_FrequencyResponse";
  case kAudioUnitProperty_ParameterHistoryInfo:
    return "kAudioUnitProperty_FrequencyResponse";
  case kAudioUnitProperty_NickName:
    return "kAudioUnitProperty_NickName";
    #endif
    #if MAC_OS_X_VERSION_MAX_ALLOWED >= 1011
  case kAudioUnitProperty_RequestViewController:
    return "kAudioUnitProperty_RequestViewController";
  case kAudioUnitProperty_ParametersForOverview:
    return "kAudioUnitProperty_ParametersForOverview";
  case kAudioUnitProperty_SupportsMPE:
    return "kAudioUnitProperty_SupportsMPE";
    #endif
  default:
    return "unknown";
  }
}

static const char* AUScopeStr(int scope)
{
  switch (scope)
  {
  case kAudioUnitScope_Global:
    return "kAudioUnitScope_Global";
  case kAudioUnitScope_Input:
    return "kAudioUnitScope_Input";
  case kAudioUnitScope_Output:
    return "kAudioUnitScope_Output";
  case kAudioUnitScope_Group:
    return "kAudioUnitScope_Group";
  case kAudioUnitScope_Part:
    return "kAudioUnitScope_Part";
  case kAudioUnitScope_Note:
    return "kAudioUnitScope_Note";
  default:
    return "unknown";
  }
}
  #endif // AU_API

#else  // TRACER_BUILD
static void Trace(FILE* logFile, const char* funcName, int line, const char* format, ...) {}
static void Trace(const char* funcName, int line, const char* format, ...) {}
static void Trace(FILE* logFile, const char* funcName, int line, const char* id, bool start) {}
static const char* VSTOpcodeStr(int opCode) { return ""; }
static const char* AUSelectStr(int select) { return ""; }
static const char* AUPropertyStr(int propID) { return ""; }
static const char* AUScopeStr(int scope) { return ""; }
#endif // !TRACER_BUILD

END_IPLUG_NAMESPACE
