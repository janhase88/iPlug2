#pragma once

#include "IPlugPlatform.h"

#if defined(IGRAPHICS_VULKAN)
  #if defined(OS_WIN) && !defined(VK_USE_PLATFORM_WIN32_KHR)
    #define VK_USE_PLATFORM_WIN32_KHR
  #endif
  #include <vulkan/vulkan.h>
  #if defined(OS_WIN)
    #include <vulkan/vulkan_win32.h>
  #endif
  #include <vector>

namespace iplug {
namespace igraphics {

#if IGRAPHICS_SANDBOX_LOGGING
namespace vulkanlog
{
struct LoggerContext;
}
#endif

struct VulkanContext
{
  VkInstance instance = VK_NULL_HANDLE;
  VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  VkSwapchainKHR swapchain = VK_NULL_HANDLE;
  VkQueue queue = VK_NULL_HANDLE;
  uint32_t queueFamily = 0;
  VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE;
  VkSemaphore renderFinishedSemaphore = VK_NULL_HANDLE;
  VkFence inFlightFence = VK_NULL_HANDLE;
  std::vector<VkImage>* swapchainImages = nullptr;
  VkFormat format = VK_FORMAT_B8G8R8A8_UNORM;
  VkImageUsageFlags usageFlags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
#if IGRAPHICS_SANDBOX_LOGGING
  const vulkanlog::LoggerContext* loggerContext = nullptr;
#endif
};

} // namespace igraphics
} // namespace iplug

#endif // defined(IGRAPHICS_VULKAN)
