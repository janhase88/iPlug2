#pragma once

#include <initializer_list>
#include <string>
#include <type_traits>
#include <utility>
#include <cstdio>
#include <vector>
#include <cstring>

#include "IPlugLogger.h"

BEGIN_IPLUG_NAMESPACE
BEGIN_IGRAPHICS_NAMESPACE

namespace schedulerlog
{
enum class Verbosity
{
  kNone = 0,
  kError = 1,
  kInfo = 2,
  kDebug = 3
};

#ifndef IGRAPHICS_SCHED_LOG_VERBOSITY
  #if defined(NDEBUG)
    #define IGRAPHICS_SCHED_LOG_VERBOSITY 2
  #else
    #define IGRAPHICS_SCHED_LOG_VERBOSITY 3
  #endif
#endif

constexpr Verbosity kConfiguredVerbosity =
#if IGRAPHICS_SCHED_LOG_VERBOSITY >= 3
  Verbosity::kDebug;
#elif IGRAPHICS_SCHED_LOG_VERBOSITY == 2
  Verbosity::kInfo;
#elif IGRAPHICS_SCHED_LOG_VERBOSITY == 1
  Verbosity::kError;
#else
  Verbosity::kNone;
#endif

enum class Severity
{
  kError,
  kWarn,
  kInfo,
  kDebug
};

constexpr const char kCategoryRoot[] = "/IGRAPHICS/SCHED";
constexpr const char kCategoryPaintBudget[] = "/IGRAPHICS/SCHED/PaintBudget";
constexpr const char kCategoryIdleState[] = "/IGRAPHICS/SCHED/IdleState";
constexpr const char kCategoryIdleForgiveness[] = "/IGRAPHICS/SCHED/IdleForgiveness";
constexpr const char kCategoryVBlankDispatch[] = "/IGRAPHICS/SCHED/VBlankDispatch";
constexpr const char kCategoryParamQueueDepth[] = "/IGRAPHICS/SCHED/ParamQueueDepth";
constexpr const char kCategoryRollout[] = "/IGRAPHICS/SCHED/Rollout";
constexpr const char kCategoryAlerts[] = "/IGRAPHICS/SCHED/Alerts";

#ifndef IGRAPHICS_SCHED_LOG_MIN_ROOT
  #define IGRAPHICS_SCHED_LOG_MIN_ROOT ::iplug::igraphics::schedulerlog::Severity::kInfo
#endif

#ifndef IGRAPHICS_SCHED_LOG_MIN_PAINT
  #define IGRAPHICS_SCHED_LOG_MIN_PAINT ::iplug::igraphics::schedulerlog::Severity::kDebug
#endif

#ifndef IGRAPHICS_SCHED_LOG_MIN_IDLE_STATE
  #define IGRAPHICS_SCHED_LOG_MIN_IDLE_STATE ::iplug::igraphics::schedulerlog::Severity::kInfo
#endif

#ifndef IGRAPHICS_SCHED_LOG_MIN_IDLE_FORGIVENESS
  #define IGRAPHICS_SCHED_LOG_MIN_IDLE_FORGIVENESS ::iplug::igraphics::schedulerlog::Severity::kInfo
#endif

#ifndef IGRAPHICS_SCHED_LOG_MIN_VBLANK
  #define IGRAPHICS_SCHED_LOG_MIN_VBLANK ::iplug::igraphics::schedulerlog::Severity::kDebug
#endif

#ifndef IGRAPHICS_SCHED_LOG_MIN_PARAM_QUEUE
  #define IGRAPHICS_SCHED_LOG_MIN_PARAM_QUEUE ::iplug::igraphics::schedulerlog::Severity::kDebug
#endif

#ifndef IGRAPHICS_SCHED_LOG_MIN_ROLLOUT
  #define IGRAPHICS_SCHED_LOG_MIN_ROLLOUT ::iplug::igraphics::schedulerlog::Severity::kInfo
#endif

#ifndef IGRAPHICS_SCHED_LOG_MIN_ALERTS
  #define IGRAPHICS_SCHED_LOG_MIN_ALERTS ::iplug::igraphics::schedulerlog::Severity::kWarn
#endif

struct Field
{
  const char* key = "";
  std::string value;
  bool quoted = true;

  Field() = default;

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
      case '\\': escaped.append("\\\\"); break;
      case '\"': escaped.append("\\\""); break;
      case '\n': escaped.append("\\n"); break;
      case '\r': escaped.append("\\r"); break;
      case '\t': escaped.append("\\t"); break;
      default: escaped.push_back(c); break;
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
    case Severity::kError: return "error";
    case Severity::kWarn:  return "warn";
    case Severity::kInfo:  return "info";
    case Severity::kDebug: return "debug";
  }
  return "unknown";
}

inline Verbosity VerbosityForSeverity(Severity severity)
{
  switch (severity)
  {
    case Severity::kError: return Verbosity::kError;
    case Severity::kWarn:  return Verbosity::kInfo;
    case Severity::kInfo:  return Verbosity::kInfo;
    case Severity::kDebug: return Verbosity::kDebug;
  }
  return Verbosity::kNone;
}

inline int SeverityRank(Severity severity)
{
  switch (severity)
  {
    case Severity::kError: return 3;
    case Severity::kWarn:  return 2;
    case Severity::kInfo:  return 1;
    case Severity::kDebug: return 0;
  }
  return 0;
}

inline bool ShouldEmit(Severity severity)
{
  return kConfiguredVerbosity >= VerbosityForSeverity(severity);
}

inline Severity MinimumSeverityForCategory(const char* category)
{
  if (!category)
  {
    return IGRAPHICS_SCHED_LOG_MIN_ROOT;
  }

  if (std::strcmp(category, kCategoryPaintBudget) == 0)
  {
    return IGRAPHICS_SCHED_LOG_MIN_PAINT;
  }

  if (std::strcmp(category, kCategoryIdleState) == 0)
  {
    return IGRAPHICS_SCHED_LOG_MIN_IDLE_STATE;
  }

  if (std::strcmp(category, kCategoryIdleForgiveness) == 0)
  {
    return IGRAPHICS_SCHED_LOG_MIN_IDLE_FORGIVENESS;
  }

  if (std::strcmp(category, kCategoryVBlankDispatch) == 0)
  {
    return IGRAPHICS_SCHED_LOG_MIN_VBLANK;
  }

  if (std::strcmp(category, kCategoryParamQueueDepth) == 0)
  {
    return IGRAPHICS_SCHED_LOG_MIN_PARAM_QUEUE;
  }

  if (std::strcmp(category, kCategoryRollout) == 0)
  {
    return IGRAPHICS_SCHED_LOG_MIN_ROLLOUT;
  }

  if (std::strcmp(category, kCategoryAlerts) == 0)
  {
    return IGRAPHICS_SCHED_LOG_MIN_ALERTS;
  }

  return IGRAPHICS_SCHED_LOG_MIN_ROOT;
}

inline bool ShouldEmit(const char* category, Severity severity)
{
  if (!ShouldEmit(severity))
  {
    return false;
  }

  const Severity floor = MinimumSeverityForCategory(category);
  return SeverityRank(severity) >= SeverityRank(floor);
}

inline Field MakeField(const char* key, const std::string& value, bool quoted = true)
{
  return Field(key, value, quoted);
}

template <typename T, typename std::enable_if<std::is_integral<T>::value, int>::type = 0>
inline Field MakeField(const char* key, T value)
{
  using CastType = typename std::conditional<std::is_signed<T>::value, long long, unsigned long long>::type;
  return Field(key, std::to_string(static_cast<CastType>(value)), false);
}

inline Field MakeField(const char* key, double value)
{
  char buffer[64];
  snprintf(buffer, sizeof(buffer), "%0.3f", value);
  return Field(key, buffer, false);
}

inline Field MakeBoolField(const char* key, bool value)
{
  return Field(key, value ? "true" : "false", false);
}

inline Field MakeStringField(const char* key, const char* value)
{
  return Field(key, value ? value : "", true);
}

inline void LogEvent(const char* category, const char* stage, Severity severity, std::initializer_list<Field> fields = {})
{
  if (!ShouldEmit(category, severity))
  {
    return;
  }

  std::string payload{"{"};
  payload.append("\"category\":\"");
  payload.append(category ? category : "unknown");
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

  payload.push_back('}');

  if (LogSink sink = LogSinkSlot())
  {
    sink(payload.c_str());
  }
}

inline void LogEvent(const char* category, const char* stage, Severity severity, const std::vector<Field>& fields)
{
  if (!ShouldEmit(category, severity))
  {
    return;
  }

  std::string payload{"{"};
  payload.append("\"category\":\"");
  payload.append(category ? category : "unknown");
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

  payload.push_back('}');

  if (LogSink sink = LogSinkSlot())
  {
    sink(payload.c_str());
  }
}

#ifndef IGRAPHICS_SCHED_LOG
  #define IGRAPHICS_SCHED_LOG(category, stage, severity, ...)                                           \
    do                                                                                                  \
    {                                                                                                   \
      ::iplug::igraphics::schedulerlog::LogEvent(category, stage, severity, {__VA_ARGS__});            \
    } while (false)
#endif

} // namespace schedulerlog

END_IGRAPHICS_NAMESPACE
END_IPLUG_NAMESPACE

