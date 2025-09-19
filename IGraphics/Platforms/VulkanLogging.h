#pragma once

#if defined(IGRAPHICS_VULKAN)

#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <type_traits>
#include <utility>

#include "IPlugLogger.h"

BEGIN_IPLUG_NAMESPACE
BEGIN_IGRAPHICS_NAMESPACE

namespace vulkanlog
{
enum class Verbosity
{
  kNone = 0,
  kError = 1,
  kVerbose = 2
};

#ifndef IGRAPHICS_VULKAN_LOG_VERBOSITY
  #define IGRAPHICS_VULKAN_LOG_VERBOSITY 1
#endif

constexpr Verbosity kConfiguredVerbosity =
#if IGRAPHICS_VULKAN_LOG_VERBOSITY >= 2
  Verbosity::kVerbose;
#elif IGRAPHICS_VULKAN_LOG_VERBOSITY == 1
  Verbosity::kError;
#else
  Verbosity::kNone;
#endif

enum class Severity
{
  kError,
  kInfo,
  kDebug
};

struct Field
{
  const char* key;
  std::string value;
  bool quoted;

  Field()
    : key("")
    , value()
    , quoted(true)
  {
  }

  Field(const char* inKey, std::string inValue, bool inQuoted = true)
    : key(inKey ? inKey : "")
    , value(std::move(inValue))
    , quoted(inQuoted)
  {
  }
};

using LogSink = void (*)(const char* message);

inline void DefaultLogSink(const char* message)
{
  DBGMSG("%s", message);
}

inline LogSink& LogSinkSlot()
{
  static LogSink sink = DefaultLogSink;
  return sink;
}

inline void SetLogSinkForTesting(LogSink sink)
{
  // Allow tests to divert structured log payloads without touching DBGMSG.
  LogSinkSlot() = sink ? sink : DefaultLogSink;
}

inline void ResetLogSink()
{
  LogSinkSlot() = DefaultLogSink;
}

inline std::string Escape(const std::string& text)
{
  std::string escaped;
  escaped.reserve(text.size());
  for (char c : text)
  {
    switch (c)
    {
    case '\\':
      escaped.append("\\\\");
      break;
    case '\"':
      escaped.append("\\\"");
      break;
    case '\n':
      escaped.append("\\n");
      break;
    case '\r':
      escaped.append("\\r");
      break;
    case '\t':
      escaped.append("\\t");
      break;
    default:
      escaped.push_back(c);
      break;
    }
  }
  return escaped;
}

inline std::string Escape(const char* text)
{
  return text ? Escape(std::string(text)) : std::string{};
}

inline const char* ToString(Severity severity)
{
  switch (severity)
  {
  case Severity::kError:
    return "error";
  case Severity::kInfo:
    return "info";
  case Severity::kDebug:
    return "debug";
  }
  return "unknown";
}

inline bool ShouldEmit(Severity severity)
{
  switch (severity)
  {
  case Severity::kError:
    return kConfiguredVerbosity >= Verbosity::kError;
  case Severity::kInfo:
  case Severity::kDebug:
    return kConfiguredVerbosity >= Verbosity::kVerbose;
  }
  return false;
}

#ifndef IGRAPHICS_VK_LOG
  #define IGRAPHICS_VK_LOG(event, stage, severity, ...)                                                            \
    do                                                                                                             \
    {                                                                                                              \
      if (::iplug::igraphics::vulkanlog::ShouldEmit(severity))                                                     \
      {                                                                                                            \
        ::iplug::igraphics::vulkanlog::LogEvent(event, stage, severity, {__VA_ARGS__});                            \
      }                                                                                                            \
    } while (false)

  #define IGRAPHICS_VK_LOG_SIMPLE(event, stage, severity)                                                          \
    do                                                                                                             \
    {                                                                                                              \
      if (::iplug::igraphics::vulkanlog::ShouldEmit(severity))                                                     \
      {                                                                                                            \
        ::iplug::igraphics::vulkanlog::LogEvent(event, stage, severity);                                           \
      }                                                                                                            \
    } while (false)
#endif

inline void LogEvent(const char* event, const char* stage, Severity severity, std::initializer_list<Field> fields = {})
{
  if (!ShouldEmit(severity))
  {
    return;
  }

  std::string payload{"{"};
  payload.append("\"event\":\"");
  payload.append(event ? event : "unknown");
  payload.append("\",\"stage\":\"");
  payload.append(stage ? stage : "unspecified");
  payload.append("\",\"severity\":\"");
  payload.append(ToString(severity));
  payload.append("\"");

  for (const auto& field : fields)
  {
    payload.append(",\"");
    payload.append(field.key ? field.key : "field");
    payload.append("\":");
    if (field.quoted)
    {
      payload.push_back('\"');
      payload.append(Escape(field.value));
      payload.push_back('\"');
    }
    else
    {
      payload.append(field.value);
    }
  }

  payload.append("}\n");
  if (LogSink sink = LogSinkSlot())
  {
    sink(payload.c_str());
  }
}

inline Field MakeField(const char* key, const char* value)
{
  return Field(key, value ? value : "", true);
}

inline Field MakeField(const char* key, std::string value)
{
  return Field(key, std::move(value), true);
}

inline Field MakeField(const char* key, bool value)
{
  return Field(key, value ? "true" : "false", false);
}

inline Field MakeField(const char* key, int value)
{
  return Field(key, std::to_string(value), false);
}

inline Field MakeField(const char* key, uint32_t value)
{
  return Field(key, std::to_string(value), false);
}

inline Field MakeField(const char* key, uint64_t value)
{
  return Field(key, std::to_string(value), false);
}

inline Field MakeHexField(const char* key, uint64_t value)
{
  char buffer[32]{};
  std::snprintf(buffer, sizeof(buffer), "0x%llX", static_cast<unsigned long long>(value));
  return Field(key, std::string(buffer), true);
}

template <typename Integral,
          typename std::enable_if<std::is_integral<Integral>::value, int>::type = 0>
inline uint64_t HandleToUint64(Integral value)
{
  return static_cast<uint64_t>(value);
}

template <typename Pointer,
          typename std::enable_if<std::is_pointer<Pointer>::value, int>::type = 0>
inline uint64_t HandleToUint64(Pointer value)
{
  return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(value));
}

inline Field MakeHandleField(const char* key, const void* value)
{
  return MakeHexField(key, HandleToUint64(value));
}

inline Field MakeHandleField(const char* key, uint64_t value)
{
  return MakeHexField(key, value);
}

} // namespace vulkanlog

END_IGRAPHICS_NAMESPACE
END_IPLUG_NAMESPACE

#endif // defined(IGRAPHICS_VULKAN)

