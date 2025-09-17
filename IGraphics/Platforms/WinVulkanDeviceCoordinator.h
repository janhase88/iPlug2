#pragma once

#if defined(OS_WIN) && defined(IGRAPHICS_VULKAN)

#include <windows.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_win32.h>

#include <cstdint>
#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

#include "IPlugPlatform.h"

#include "IPlugLogger.h"

#include "VulkanLogging.h"

BEGIN_IPLUG_NAMESPACE
BEGIN_IGRAPHICS_NAMESPACE

enum class WinVulkanPreferredAdapter
{
  kAny,
  kIntegrated,
  kDiscrete
};

struct WinVulkanDeviceRequest
{
  HINSTANCE instanceHandle = nullptr;
  HWND windowHandle = nullptr;
  WinVulkanPreferredAdapter preferredAdapter = WinVulkanPreferredAdapter::kAny;
  bool enableValidationLayer = false;
};

struct WinVulkanDeviceSnapshot
{
  VkInstance instance = VK_NULL_HANDLE;
  VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  VkQueue presentQueue = VK_NULL_HANDLE;
  uint32_t queueFamily = 0;
  bool validationLayerEnabled = false;
};

class WinVulkanDeviceCoordinator
{
public:
  WinVulkanDeviceCoordinator() = default;
  ~WinVulkanDeviceCoordinator();

  WinVulkanDeviceCoordinator(const WinVulkanDeviceCoordinator&) = delete;
  WinVulkanDeviceCoordinator& operator=(const WinVulkanDeviceCoordinator&) = delete;

  VkResult Initialize(const WinVulkanDeviceRequest& request, WinVulkanDeviceSnapshot& outSnapshot);
  void Teardown();
  bool IsInitialized() const { return mInitialized; }
  const WinVulkanDeviceSnapshot& Snapshot() const { return mSnapshot; }

private:
  VkResult CreateInstance(const WinVulkanDeviceRequest& request);
  VkResult CreateSurface(const WinVulkanDeviceRequest& request);
  VkResult SelectPhysicalDevice(const WinVulkanDeviceRequest& request);
  VkResult CreateLogicalDevice();
  void ResetSnapshot();

  bool mInitialized = false;
  WinVulkanDeviceSnapshot mSnapshot{};
};

namespace winvk
{
static const char* const kValidationLayerName = "VK_LAYER_KHRONOS_validation";
static const std::array<const char*, 2> kRequiredInstanceExtensions{{"VK_KHR_surface", "VK_KHR_win32_surface"}};
static const std::array<const char*, 1> kRequiredDeviceExtensions{{VK_KHR_SWAPCHAIN_EXTENSION_NAME}};
}

inline WinVulkanDeviceCoordinator::~WinVulkanDeviceCoordinator()
{
  Teardown();
}

inline VkResult WinVulkanDeviceCoordinator::Initialize(const WinVulkanDeviceRequest& request, WinVulkanDeviceSnapshot& outSnapshot)
{
  if (mInitialized)
  {
    outSnapshot = mSnapshot;
    return VK_SUCCESS;
  }

  ResetSnapshot();

  VkResult res = CreateInstance(request);
  if (res != VK_SUCCESS)
  {
    IGRAPHICS_VK_LOG("WinVulkanDeviceCoordinator.Initialize",
                        "vkCreateInstance",
                        vulkanlog::Severity::kError,
                        {vulkanlog::MakeField("vkResult", static_cast<int>(res))});
    Teardown();
    return res;
  }

  res = CreateSurface(request);
  if (res != VK_SUCCESS)
  {
    IGRAPHICS_VK_LOG("WinVulkanDeviceCoordinator.Initialize",
                        "vkCreateWin32SurfaceKHR",
                        vulkanlog::Severity::kError,
                        {vulkanlog::MakeField("vkResult", static_cast<int>(res))});
    Teardown();
    return res;
  }

  res = SelectPhysicalDevice(request);
  if (res != VK_SUCCESS)
  {
    IGRAPHICS_VK_LOG("WinVulkanDeviceCoordinator.Initialize",
                        "selectPhysicalDevice",
                        vulkanlog::Severity::kError,
                        {vulkanlog::MakeField("vkResult", static_cast<int>(res))});
    Teardown();
    return res;
  }

  res = CreateLogicalDevice();
  if (res != VK_SUCCESS)
  {
    IGRAPHICS_VK_LOG("WinVulkanDeviceCoordinator.Initialize",
                        "vkCreateDevice",
                        vulkanlog::Severity::kError,
                        {vulkanlog::MakeField("vkResult", static_cast<int>(res))});
    Teardown();
    return res;
  }

  mInitialized = true;
  outSnapshot = mSnapshot;
  return VK_SUCCESS;
}

inline void WinVulkanDeviceCoordinator::Teardown()
{
  if (!mInitialized && mSnapshot.instance == VK_NULL_HANDLE && mSnapshot.device == VK_NULL_HANDLE)
  {
    return;
  }

  if (mSnapshot.device != VK_NULL_HANDLE)
  {
    vkDestroyDevice(mSnapshot.device, nullptr);
  }

  if (mSnapshot.surface != VK_NULL_HANDLE && mSnapshot.instance != VK_NULL_HANDLE)
  {
    vkDestroySurfaceKHR(mSnapshot.instance, mSnapshot.surface, nullptr);
  }

  if (mSnapshot.instance != VK_NULL_HANDLE)
  {
    vkDestroyInstance(mSnapshot.instance, nullptr);
  }

  ResetSnapshot();
  mInitialized = false;
}

inline VkResult WinVulkanDeviceCoordinator::CreateInstance(const WinVulkanDeviceRequest& request)
{
  VkApplicationInfo appInfo{};
  appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  appInfo.pApplicationName = "iPlug2";
  appInfo.apiVersion = VK_API_VERSION_1_1;

  VkInstanceCreateInfo instanceInfo{};
  instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceInfo.pApplicationInfo = &appInfo;
  instanceInfo.enabledExtensionCount = static_cast<uint32_t>(winvk::kRequiredInstanceExtensions.size());
  instanceInfo.ppEnabledExtensionNames = winvk::kRequiredInstanceExtensions.data();

  bool enableValidation = false;
#if !defined(NDEBUG)
  if (request.enableValidationLayer)
  {
    uint32_t layerCount = 0;
    if (vkEnumerateInstanceLayerProperties(&layerCount, nullptr) == VK_SUCCESS && layerCount > 0)
    {
      std::vector<VkLayerProperties> layers(layerCount);
      vkEnumerateInstanceLayerProperties(&layerCount, layers.data());
      for (const auto& layer : layers)
      {
        if (std::strcmp(layer.layerName, winvk::kValidationLayerName) == 0)
        {
          enableValidation = true;
          break;
        }
      }
    }
  }
#endif

  if (enableValidation)
  {
    instanceInfo.enabledLayerCount = 1;
    instanceInfo.ppEnabledLayerNames = &winvk::kValidationLayerName;
    mSnapshot.validationLayerEnabled = true;
  }
  else
  {
    instanceInfo.enabledLayerCount = 0;
    instanceInfo.ppEnabledLayerNames = nullptr;
    mSnapshot.validationLayerEnabled = false;
  }

  return vkCreateInstance(&instanceInfo, nullptr, &mSnapshot.instance);
}

inline VkResult WinVulkanDeviceCoordinator::CreateSurface(const WinVulkanDeviceRequest& request)
{
  VkWin32SurfaceCreateInfoKHR surfaceInfo{};
  surfaceInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
  surfaceInfo.hinstance = request.instanceHandle;
  surfaceInfo.hwnd = request.windowHandle;
  return vkCreateWin32SurfaceKHR(mSnapshot.instance, &surfaceInfo, nullptr, &mSnapshot.surface);
}

inline VkResult WinVulkanDeviceCoordinator::SelectPhysicalDevice(const WinVulkanDeviceRequest& request)
{
  uint32_t gpuCount = 0;
  VkResult res = vkEnumeratePhysicalDevices(mSnapshot.instance, &gpuCount, nullptr);
  if (res != VK_SUCCESS || gpuCount == 0)
  {
    return (gpuCount == 0) ? VK_ERROR_INITIALIZATION_FAILED : res;
  }

  std::vector<VkPhysicalDevice> devices(gpuCount);
  res = vkEnumeratePhysicalDevices(mSnapshot.instance, &gpuCount, devices.data());
  if (res != VK_SUCCESS)
  {
    return res;
  }

  VkPhysicalDevice selectedDevice = VK_NULL_HANDLE;
  uint32_t selectedQueueFamily = 0;
  uint32_t bestScore = 0;

  for (const auto device : devices)
  {
    uint32_t extensionCount = 0;
    if (vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr) != VK_SUCCESS)
      continue;

    std::vector<VkExtensionProperties> extensions(extensionCount);
    if (vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, extensions.data()) != VK_SUCCESS)
      continue;

    bool hasSwapchain = std::any_of(extensions.begin(), extensions.end(), [](const VkExtensionProperties& prop) {
      return std::strcmp(prop.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0;
    });

    if (!hasSwapchain)
      continue;

    uint32_t queueCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueCount, nullptr);
    std::vector<VkQueueFamilyProperties> queues(queueCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueCount, queues.data());

    uint32_t queueIndex = UINT32_MAX;
    for (uint32_t i = 0; i < queueCount; ++i)
    {
      VkBool32 presentSupport = VK_FALSE;
      if (vkGetPhysicalDeviceSurfaceSupportKHR(device, i, mSnapshot.surface, &presentSupport) == VK_SUCCESS &&
          (queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && presentSupport)
      {
        queueIndex = i;
        break;
      }
    }

    if (queueIndex == UINT32_MAX)
      continue;

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(device, &props);

    VkPhysicalDeviceFeatures features;
    vkGetPhysicalDeviceFeatures(device, &features);
    if (!features.samplerAnisotropy)
      continue;

    uint32_t score = props.limits.maxImageDimension2D;
    if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
      score += 1000;
    else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
      score += 100;

    if (request.preferredAdapter == WinVulkanPreferredAdapter::kDiscrete &&
        props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
    {
      score += 10000;
    }
    else if (request.preferredAdapter == WinVulkanPreferredAdapter::kIntegrated &&
             props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
    {
      score += 10000;
    }

    if (score > bestScore)
    {
      bestScore = score;
      selectedDevice = device;
      selectedQueueFamily = queueIndex;
    }
  }

  if (selectedDevice == VK_NULL_HANDLE)
  {
    return VK_ERROR_INITIALIZATION_FAILED;
  }

  mSnapshot.physicalDevice = selectedDevice;
  mSnapshot.queueFamily = selectedQueueFamily;
  return VK_SUCCESS;
}

inline VkResult WinVulkanDeviceCoordinator::CreateLogicalDevice()
{
  if (mSnapshot.physicalDevice == VK_NULL_HANDLE)
  {
    return VK_ERROR_INITIALIZATION_FAILED;
  }

  VkPhysicalDeviceFeatures supportedFeatures;
  vkGetPhysicalDeviceFeatures(mSnapshot.physicalDevice, &supportedFeatures);

  VkPhysicalDeviceFeatures enabledFeatures{};
  enabledFeatures.samplerAnisotropy = supportedFeatures.samplerAnisotropy;
  if (supportedFeatures.textureCompressionBC)
    enabledFeatures.textureCompressionBC = VK_TRUE;

  float queuePriority = 1.f;
  VkDeviceQueueCreateInfo queueInfo{};
  queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queueInfo.queueFamilyIndex = mSnapshot.queueFamily;
  queueInfo.queueCount = 1;
  queueInfo.pQueuePriorities = &queuePriority;

  VkDeviceCreateInfo deviceInfo{};
  deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceInfo.queueCreateInfoCount = 1;
  deviceInfo.pQueueCreateInfos = &queueInfo;
  deviceInfo.enabledExtensionCount = static_cast<uint32_t>(winvk::kRequiredDeviceExtensions.size());
  deviceInfo.ppEnabledExtensionNames = winvk::kRequiredDeviceExtensions.data();
  deviceInfo.pEnabledFeatures = &enabledFeatures;

#if !defined(NDEBUG)
  if (mSnapshot.validationLayerEnabled)
  {
    deviceInfo.enabledLayerCount = 1;
    deviceInfo.ppEnabledLayerNames = &winvk::kValidationLayerName;
  }
  else
#endif
  {
    deviceInfo.enabledLayerCount = 0;
    deviceInfo.ppEnabledLayerNames = nullptr;
  }

  VkResult res = vkCreateDevice(mSnapshot.physicalDevice, &deviceInfo, nullptr, &mSnapshot.device);
  if (res != VK_SUCCESS)
  {
    return res;
  }

  vkGetDeviceQueue(mSnapshot.device, mSnapshot.queueFamily, 0, &mSnapshot.presentQueue);
  return VK_SUCCESS;
}

inline void WinVulkanDeviceCoordinator::ResetSnapshot()
{
  mSnapshot.instance = VK_NULL_HANDLE;
  mSnapshot.physicalDevice = VK_NULL_HANDLE;
  mSnapshot.device = VK_NULL_HANDLE;
  mSnapshot.surface = VK_NULL_HANDLE;
  mSnapshot.presentQueue = VK_NULL_HANDLE;
  mSnapshot.queueFamily = 0;
  mSnapshot.validationLayerEnabled = false;
}

END_IGRAPHICS_NAMESPACE
END_IPLUG_NAMESPACE

#endif // defined(OS_WIN) && defined(IGRAPHICS_VULKAN)
