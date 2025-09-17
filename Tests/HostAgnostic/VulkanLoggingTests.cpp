#include <cassert>
#include <string>
#include <vector>

#define IGRAPHICS_VULKAN 1
#define IGRAPHICS_VULKAN_LOG_VERBOSITY 2

#include "IGraphics/Platforms/VulkanLogging.h"

using namespace iplug::igraphics::vulkanlog;

namespace
{
std::vector<std::string>* gCollectedLogs = nullptr;

void CollectLog(const char* message)
{
  if (gCollectedLogs && message)
  {
    gCollectedLogs->emplace_back(message);
  }
}
} // namespace

int main()
{
  std::vector<std::string> logs;
  gCollectedLogs = &logs;
  SetLogSinkForTesting(&CollectLog);

  assert(ShouldEmit(Severity::kError));
  assert(ShouldEmit(Severity::kInfo));
  assert(std::string(ToString(Severity::kDebug)) == "debug");

  LogEvent("acquire", "begin", Severity::kInfo, {MakeField("imageIndex", 3), MakeField("surface", "VK")});
  assert(logs.size() == 1);
  assert(logs.front() == "{\"event\":\"acquire\",\"stage\":\"begin\",\"severity\":\"info\",\"imageIndex\":3,\"surface\":\"VK\"}\n");

  const std::string escaped = Escape(std::string("line1\n\"quoted\"\t\\"));
  assert(escaped == "line1\\n\\\"quoted\\\"\\t\\\\");

  logs.clear();
  IGRAPHICS_VK_LOG("swapchain", "acquire", Severity::kError, {MakeField("retry", false)});
  assert(logs.size() == 1);
  assert(logs.front().find("\"retry\":false") != std::string::npos);

  logs.clear();
  IGRAPHICS_VK_LOG_SIMPLE("frame", "present", Severity::kDebug);
  assert(logs.size() == 1);
  assert(logs.front().find("\"severity\":\"debug\"") != std::string::npos);

  ResetLogSink();
  gCollectedLogs = nullptr;
  return 0;
}
