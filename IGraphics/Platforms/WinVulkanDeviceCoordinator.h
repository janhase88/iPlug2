#pragma once

#if defined(OS_WIN) && defined(IGRAPHICS_VULKAN)

#include <windows.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_win32.h>

#if !defined(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES)
  #define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES \
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR
#endif
#if !defined(VkPhysicalDeviceSynchronization2Features)
  using VkPhysicalDeviceSynchronization2Features = VkPhysicalDeviceSynchronization2FeaturesKHR;
#endif

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
  uint32_t instanceApiVersion = VK_API_VERSION_1_0;
  uint32_t deviceApiVersion = VK_API_VERSION_1_0;
  VkPhysicalDeviceFeatures enabledFeatures{};
  std::array<const char*, 4> enabledDeviceExtensions{};
  uint32_t enabledDeviceExtensionCount = 0;
  VkPhysicalDeviceSynchronization2Features synchronization2Features{
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES
  };
#if defined(VK_VERSION_1_3)
  VkPhysicalDeviceVulkan13Features vulkan13Features{
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES
  };
#endif
  bool synchronization2Enabled = false;
  bool validationLayerEnabled = false;
  uint64_t generation = 0;
};

class WinVulkanDeviceCoordinator
{
public:
  WinVulkanDeviceCoordinator();
  ~WinVulkanDeviceCoordinator();

  WinVulkanDeviceCoordinator(const WinVulkanDeviceCoordinator&) = delete;
  WinVulkanDeviceCoordinator& operator=(const WinVulkanDeviceCoordinator&) = delete;

  VkResult Initialize(const WinVulkanDeviceRequest& request, WinVulkanDeviceSnapshot& outSnapshot, uint64_t& outGeneration);
  void Teardown(uint64_t generation = 0);
  bool IsInitialized() const { return mState->initialized; }
  const WinVulkanDeviceSnapshot& Snapshot() const { return mState->snapshot; }

private:
  struct SharedState
  {
    bool initialized = false;
    WinVulkanDeviceSnapshot snapshot{};
    uint64_t generationCounter = 0;
    uint64_t snapshotGeneration = 0;
    uint32_t activeClients = 0;
    uint32_t instanceApiVersion = VK_API_VERSION_1_0;
    uint32_t deviceApiVersion = VK_API_VERSION_1_0;
    bool supportsSynchronization2 = false;
    bool hasSynchronization2Extension = false;
    VkPhysicalDeviceSynchronization2Features queriedSynchronization2Features{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES
    };
#if defined(VK_VERSION_1_3)
    VkPhysicalDeviceVulkan13Features queriedVulkan13Features{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES
    };
#endif
  };

  static SharedState& Shared();

  VkResult CreateInstance(const WinVulkanDeviceRequest& request);
  VkResult CreateSurface(const WinVulkanDeviceRequest& request, VkSurfaceKHR& outSurface);
  VkResult SelectPhysicalDevice(const WinVulkanDeviceRequest& request, VkSurfaceKHR surface);
  VkResult CreateLogicalDevice();
  void ResetSnapshot();

  SharedState* mState = nullptr;
  bool mClientRegistered = false;
  uint64_t mClientGeneration = 0;
  VkSurfaceKHR mClientSurface = VK_NULL_HANDLE;
};

namespace winvk
{
static const char* const kValidationLayerName = "VK_LAYER_KHRONOS_validation";
static const std::array<const char*, 2> kRequiredInstanceExtensions{{"VK_KHR_surface", "VK_KHR_win32_surface"}};
static const std::array<const char*, 1> kRequiredDeviceExtensions{{VK_KHR_SWAPCHAIN_EXTENSION_NAME}};
}

inline WinVulkanDeviceCoordinator::SharedState& WinVulkanDeviceCoordinator::Shared()
{
  static SharedState state{};
  return state;
}

inline WinVulkanDeviceCoordinator::WinVulkanDeviceCoordinator()
  : mState(&Shared())
{
}

inline WinVulkanDeviceCoordinator::~WinVulkanDeviceCoordinator()
{
  if (mClientRegistered || mClientSurface != VK_NULL_HANDLE)
  {
    Teardown(mClientRegistered ? mClientGeneration : 0);
  }
}

inline VkResult WinVulkanDeviceCoordinator::Initialize(const WinVulkanDeviceRequest& request,
                                                       WinVulkanDeviceSnapshot& outSnapshot,
                                                       uint64_t& outGeneration)
{
  SharedState& state = *mState;

  VkSurfaceKHR surface = VK_NULL_HANDLE;

  if (state.initialized)
  {
    VkResult res = CreateSurface(request, surface);
    if (res != VK_SUCCESS)
    {
      IGRAPHICS_VK_LOG("WinVulkanDeviceCoordinator.Initialize",
                       "vkCreateWin32SurfaceKHR",
                       vulkanlog::Severity::kError,
                       vulkanlog::MakeField("vkResult", static_cast<int>(res)));
      return res;
    }

    VkBool32 presentSupport = VK_FALSE;
    res = vkGetPhysicalDeviceSurfaceSupportKHR(state.snapshot.physicalDevice,
                                               state.snapshot.queueFamily,
                                               surface,
                                               &presentSupport);
    if (res != VK_SUCCESS || presentSupport != VK_TRUE)
    {
      if (res == VK_SUCCESS)
        res = VK_ERROR_INITIALIZATION_FAILED;
      vkDestroySurfaceKHR(state.snapshot.instance, surface, nullptr);
      IGRAPHICS_VK_LOG("WinVulkanDeviceCoordinator.Initialize",
                       "vkGetPhysicalDeviceSurfaceSupportKHR",
                       vulkanlog::Severity::kError,
                       vulkanlog::MakeField("vkResult", static_cast<int>(res)));
      return res;
    }

    ++state.activeClients;
    outSnapshot = state.snapshot;
    outSnapshot.surface = surface;
    outGeneration = state.snapshotGeneration;
    mClientRegistered = true;
    mClientGeneration = outGeneration;
    mClientSurface = surface;
    return VK_SUCCESS;
  }

  ResetSnapshot();
  state.snapshotGeneration = 0;
  outGeneration = 0;
  state.activeClients = 0;
  state.instanceApiVersion = VK_API_VERSION_1_0;
  state.deviceApiVersion = VK_API_VERSION_1_0;
  state.supportsSynchronization2 = false;
  state.hasSynchronization2Extension = false;
  state.queriedSynchronization2Features = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES};
#if defined(VK_VERSION_1_3)
  state.queriedVulkan13Features = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
#endif
  mClientSurface = VK_NULL_HANDLE;

  VkResult res = CreateInstance(request);
  if (res != VK_SUCCESS)
  {
    IGRAPHICS_VK_LOG("WinVulkanDeviceCoordinator.Initialize",
                        "vkCreateInstance",
                        vulkanlog::Severity::kError,
                        vulkanlog::MakeField("vkResult", static_cast<int>(res)));
    Teardown();
    return res;
  }

  res = CreateSurface(request, surface);
  if (res != VK_SUCCESS)
  {
    IGRAPHICS_VK_LOG("WinVulkanDeviceCoordinator.Initialize",
                        "vkCreateWin32SurfaceKHR",
                        vulkanlog::Severity::kError,
                        vulkanlog::MakeField("vkResult", static_cast<int>(res)));
    Teardown();
    return res;
  }

  res = SelectPhysicalDevice(request, surface);
  if (res != VK_SUCCESS)
  {
    IGRAPHICS_VK_LOG("WinVulkanDeviceCoordinator.Initialize",
                        "selectPhysicalDevice",
                        vulkanlog::Severity::kError,
                        vulkanlog::MakeField("vkResult", static_cast<int>(res)));
    vkDestroySurfaceKHR(state.snapshot.instance, surface, nullptr);
    Teardown();
    return res;
  }

  res = CreateLogicalDevice();
  if (res != VK_SUCCESS)
  {
    IGRAPHICS_VK_LOG("WinVulkanDeviceCoordinator.Initialize",
                        "vkCreateDevice",
                        vulkanlog::Severity::kError,
                        vulkanlog::MakeField("vkResult", static_cast<int>(res)));
    vkDestroySurfaceKHR(state.snapshot.instance, surface, nullptr);
    Teardown();
    return res;
  }

  state.initialized = true;
  state.snapshotGeneration = ++state.generationCounter;
  state.snapshot.generation = state.snapshotGeneration;
  outSnapshot = state.snapshot;
  outSnapshot.surface = surface;
  outGeneration = state.snapshotGeneration;
  state.activeClients = 1;
  mClientRegistered = true;
  mClientGeneration = outGeneration;
  mClientSurface = surface;
  return VK_SUCCESS;
}

inline void WinVulkanDeviceCoordinator::Teardown(uint64_t generation)
{
  SharedState& state = *mState;

  if (generation != 0 && mClientRegistered && generation != mClientGeneration)
  {
    return;
  }

  VkSurfaceKHR clientSurface = mClientSurface;
  if (clientSurface != VK_NULL_HANDLE && state.snapshot.instance != VK_NULL_HANDLE)
  {
    vkDestroySurfaceKHR(state.snapshot.instance, clientSurface, nullptr);
  }
  mClientSurface = VK_NULL_HANDLE;

  if (generation == 0)
  {
    if (mClientRegistered)
    {
      if (state.activeClients > 0)
      {
        --state.activeClients;
      }
      mClientRegistered = false;
      mClientGeneration = 0;
    }

    if (state.activeClients > 0)
    {
      return;
    }

    const VkInstance instance = state.snapshot.instance;
    const VkDevice device = state.snapshot.device;

    state.initialized = false;
    state.snapshotGeneration = 0;
    state.activeClients = 0;
    ResetSnapshot();
    state.supportsSynchronization2 = false;

    if (device != VK_NULL_HANDLE)
    {
      vkDestroyDevice(device, nullptr);
    }

    if (instance != VK_NULL_HANDLE)
    {
      vkDestroyInstance(instance, nullptr);
    }

    return;
  }

  if (!mClientRegistered || generation != mClientGeneration)
  {
    return;
  }

  if (state.activeClients > 0)
  {
    --state.activeClients;
  }

  mClientRegistered = false;
  mClientGeneration = 0;

  if (state.activeClients > 0)
  {
    return;
  }

  const VkInstance instance = state.snapshot.instance;
  const VkDevice device = state.snapshot.device;

  state.initialized = false;
  state.snapshotGeneration = 0;
  state.activeClients = 0;
  ResetSnapshot();
  state.supportsSynchronization2 = false;

  if (device != VK_NULL_HANDLE)
  {
    vkDestroyDevice(device, nullptr);
  }

  if (instance != VK_NULL_HANDLE)
  {
    vkDestroyInstance(instance, nullptr);
  }
}

inline VkResult WinVulkanDeviceCoordinator::CreateInstance(const WinVulkanDeviceRequest& request)
{
  SharedState& state = *mState;
  VkApplicationInfo appInfo{};
  appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  appInfo.pApplicationName = "iPlug2";
#if defined(VK_VERSION_1_1)
  uint32_t loaderVersion = VK_API_VERSION_1_0;
  if (vkEnumerateInstanceVersion)
  {
    if (vkEnumerateInstanceVersion(&loaderVersion) != VK_SUCCESS)
      loaderVersion = VK_API_VERSION_1_0;
  }
#else
  uint32_t loaderVersion = VK_API_VERSION_1_0;
#endif

  uint32_t requestedVersion = VK_API_VERSION_1_1;
#if defined(VK_API_VERSION_1_3)
  if (loaderVersion >= VK_API_VERSION_1_3)
    requestedVersion = VK_API_VERSION_1_3;
  else if (loaderVersion >= VK_API_VERSION_1_2)
    requestedVersion = VK_API_VERSION_1_2;
  else if (loaderVersion >= VK_API_VERSION_1_1)
    requestedVersion = VK_API_VERSION_1_1;
  else
    requestedVersion = loaderVersion;
#elif defined(VK_API_VERSION_1_2)
  if (loaderVersion >= VK_API_VERSION_1_2)
    requestedVersion = VK_API_VERSION_1_2;
  else if (loaderVersion >= VK_API_VERSION_1_1)
    requestedVersion = VK_API_VERSION_1_1;
  else
    requestedVersion = loaderVersion;
#else
  if (loaderVersion < requestedVersion)
    requestedVersion = loaderVersion;
#endif

  if (requestedVersion < VK_API_VERSION_1_1)
    requestedVersion = VK_API_VERSION_1_1;

  appInfo.apiVersion = requestedVersion;
  state.instanceApiVersion = requestedVersion;
  state.snapshot.instanceApiVersion = requestedVersion;

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
    state.snapshot.validationLayerEnabled = true;
  }
  else
  {
    instanceInfo.enabledLayerCount = 0;
    instanceInfo.ppEnabledLayerNames = nullptr;
    state.snapshot.validationLayerEnabled = false;
  }

  return vkCreateInstance(&instanceInfo, nullptr, &state.snapshot.instance);
}

inline VkResult WinVulkanDeviceCoordinator::CreateSurface(const WinVulkanDeviceRequest& request, VkSurfaceKHR& outSurface)
{
  VkWin32SurfaceCreateInfoKHR surfaceInfo{};
  surfaceInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
  surfaceInfo.hinstance = request.instanceHandle;
  surfaceInfo.hwnd = request.windowHandle;
  return vkCreateWin32SurfaceKHR(mState->snapshot.instance, &surfaceInfo, nullptr, &outSurface);
}

inline VkResult WinVulkanDeviceCoordinator::SelectPhysicalDevice(const WinVulkanDeviceRequest& request, VkSurfaceKHR surface)
{
  SharedState& state = *mState;
  uint32_t gpuCount = 0;
  VkResult res = vkEnumeratePhysicalDevices(state.snapshot.instance, &gpuCount, nullptr);
  if (res != VK_SUCCESS || gpuCount == 0)
  {
    return (gpuCount == 0) ? VK_ERROR_INITIALIZATION_FAILED : res;
  }

  std::vector<VkPhysicalDevice> devices(gpuCount);
  res = vkEnumeratePhysicalDevices(state.snapshot.instance, &gpuCount, devices.data());
  if (res != VK_SUCCESS)
  {
    return res;
  }

  VkPhysicalDevice selectedDevice = VK_NULL_HANDLE;
  VkPhysicalDeviceProperties selectedProperties{};
  uint32_t selectedQueueFamily = 0;
  uint32_t bestScore = 0;
  bool selectedSupportsSync2 = false;
  bool selectedHasSync2Extension = false;
  VkPhysicalDeviceSynchronization2Features selectedSync2Features{
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES
  };
#if defined(VK_VERSION_1_3)
  VkPhysicalDeviceVulkan13Features selectedVulkan13Features{
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES
  };
#endif

  PFN_vkGetPhysicalDeviceFeatures2 getFeatures2 =
    reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(vkGetInstanceProcAddr(state.snapshot.instance,
                                                                            "vkGetPhysicalDeviceFeatures2"));

  for (const auto device : devices)
  {
    uint32_t extensionCount = 0;
    if (vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr) != VK_SUCCESS)
      continue;

    std::vector<VkExtensionProperties> extensions(extensionCount);
    if (vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, extensions.data()) != VK_SUCCESS)
      continue;

    bool hasSwapchain = false;
    bool hasSynchronization2Extension = false;
    for (const auto& prop : extensions)
    {
      if (std::strcmp(prop.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0)
        hasSwapchain = true;
      else if (std::strcmp(prop.extensionName, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME) == 0)
        hasSynchronization2Extension = true;
    }

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
      if (vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport) == VK_SUCCESS &&
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

    VkPhysicalDeviceSynchronization2Features sync2Features{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES
    };
#if defined(VK_VERSION_1_3)
    VkPhysicalDeviceVulkan13Features vulkan13Features{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES
    };
    sync2Features.pNext = &vulkan13Features;
#endif
    if (getFeatures2)
    {
      VkPhysicalDeviceFeatures2 features2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
      features2.pNext = &sync2Features;
      getFeatures2(device, &features2);
    }

    const uint32_t apiVersion = props.apiVersion;
    const bool promotedSync2 = VK_VERSION_MAJOR(apiVersion) > 1 ||
                               (VK_VERSION_MAJOR(apiVersion) == 1 && VK_VERSION_MINOR(apiVersion) >= 3);
    const bool sync2FeatureSupported = sync2Features.synchronization2 == VK_TRUE;
    const bool supportsSync2 = sync2FeatureSupported && (promotedSync2 || hasSynchronization2Extension);

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
      selectedSupportsSync2 = supportsSync2;
      selectedHasSync2Extension = hasSynchronization2Extension;
      selectedSync2Features = sync2Features;
      selectedProperties = props;
#if defined(VK_VERSION_1_3)
      selectedVulkan13Features = vulkan13Features;
#endif
    }
  }

  if (selectedDevice == VK_NULL_HANDLE)
  {
    return VK_ERROR_INITIALIZATION_FAILED;
  }

  state.snapshot.physicalDevice = selectedDevice;
  state.snapshot.queueFamily = selectedQueueFamily;
  state.snapshot.deviceApiVersion = selectedProperties.apiVersion;
  state.deviceApiVersion = selectedProperties.apiVersion;
  state.supportsSynchronization2 = selectedSupportsSync2;
  state.hasSynchronization2Extension = selectedHasSync2Extension;
  state.queriedSynchronization2Features = selectedSync2Features;
  state.snapshot.synchronization2Features = selectedSync2Features;
#if defined(VK_VERSION_1_3)
  state.queriedVulkan13Features = selectedVulkan13Features;
  state.snapshot.vulkan13Features = selectedVulkan13Features;
#endif
  return VK_SUCCESS;
}

inline VkResult WinVulkanDeviceCoordinator::CreateLogicalDevice()
{
  SharedState& state = *mState;

  if (state.snapshot.physicalDevice == VK_NULL_HANDLE)
  {
    return VK_ERROR_INITIALIZATION_FAILED;
  }

  VkPhysicalDeviceFeatures supportedFeatures;
  vkGetPhysicalDeviceFeatures(state.snapshot.physicalDevice, &supportedFeatures);

  VkPhysicalDeviceFeatures enabledFeatures{};
  enabledFeatures.samplerAnisotropy = supportedFeatures.samplerAnisotropy;
  if (supportedFeatures.textureCompressionBC)
    enabledFeatures.textureCompressionBC = VK_TRUE;

  std::array<const char*, 4> enabledExtensions{};
  uint32_t enabledExtensionCount = 0;
  enabledExtensions[enabledExtensionCount++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;

  VkPhysicalDeviceSynchronization2Features synchronization2Features = state.queriedSynchronization2Features;
  synchronization2Features.pNext = nullptr;
#if defined(VK_VERSION_1_3)
  VkPhysicalDeviceVulkan13Features vulkan13Features = state.queriedVulkan13Features;
  vulkan13Features.pNext = nullptr;
#endif

  void* featureChain = nullptr;
  const bool enableSynchronization2 = false; // Legacy barrier plumbing does not yet support vkCmdPipelineBarrier2.
  if (enableSynchronization2 && state.supportsSynchronization2)
  {
    synchronization2Features.synchronization2 = VK_TRUE;
#if defined(VK_VERSION_1_3)
    if (state.deviceApiVersion >= VK_API_VERSION_1_3)
    {
      vulkan13Features.synchronization2 = VK_TRUE;
      vulkan13Features.pNext = featureChain;
      featureChain = &vulkan13Features;
    }
#endif
    synchronization2Features.pNext = featureChain;
    featureChain = &synchronization2Features;
    if (state.hasSynchronization2Extension && enabledExtensionCount < enabledExtensions.size())
    {
      enabledExtensions[enabledExtensionCount++] = VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME;
    }
  }

  float queuePriority = 1.f;
  VkDeviceQueueCreateInfo queueInfo{};
  queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queueInfo.queueFamilyIndex = state.snapshot.queueFamily;
  queueInfo.queueCount = 1;
  queueInfo.pQueuePriorities = &queuePriority;

  VkDeviceCreateInfo deviceInfo{};
  deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceInfo.queueCreateInfoCount = 1;
  deviceInfo.pQueueCreateInfos = &queueInfo;
  deviceInfo.enabledExtensionCount = enabledExtensionCount;
  deviceInfo.ppEnabledExtensionNames = enabledExtensions.data();

  VkPhysicalDeviceFeatures2 features2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
  features2.features = enabledFeatures;
  features2.pNext = featureChain;
  deviceInfo.pNext = &features2;
  deviceInfo.pEnabledFeatures = nullptr;

#if !defined(NDEBUG)
  if (state.snapshot.validationLayerEnabled)
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

  VkResult res = vkCreateDevice(state.snapshot.physicalDevice, &deviceInfo, nullptr, &state.snapshot.device);
  if (res != VK_SUCCESS)
  {
    return res;
  }

  state.snapshot.enabledFeatures = enabledFeatures;
  state.snapshot.enabledDeviceExtensions = enabledExtensions;
  state.snapshot.enabledDeviceExtensionCount = enabledExtensionCount;
  state.snapshot.synchronization2Enabled = enableSynchronization2 && state.supportsSynchronization2;
  if (state.snapshot.synchronization2Enabled)
  {
    state.snapshot.synchronization2Features = synchronization2Features;
#if defined(VK_VERSION_1_3)
    if (state.deviceApiVersion >= VK_API_VERSION_1_3)
      state.snapshot.vulkan13Features = vulkan13Features;
#endif
  }
  else
  {
    state.snapshot.synchronization2Features.synchronization2 = VK_FALSE;
#if defined(VK_VERSION_1_3)
    state.snapshot.vulkan13Features.synchronization2 = VK_FALSE;
#endif
  }
  vkGetDeviceQueue(state.snapshot.device, state.snapshot.queueFamily, 0, &state.snapshot.presentQueue);
  return VK_SUCCESS;
}

inline void WinVulkanDeviceCoordinator::ResetSnapshot()
{
  WinVulkanDeviceSnapshot& snapshot = mState->snapshot;
  snapshot.instance = VK_NULL_HANDLE;
  snapshot.physicalDevice = VK_NULL_HANDLE;
  snapshot.device = VK_NULL_HANDLE;
  snapshot.surface = VK_NULL_HANDLE;
  snapshot.presentQueue = VK_NULL_HANDLE;
  snapshot.queueFamily = 0;
  snapshot.instanceApiVersion = VK_API_VERSION_1_0;
  snapshot.deviceApiVersion = VK_API_VERSION_1_0;
  snapshot.enabledFeatures = {};
  snapshot.enabledDeviceExtensions = {};
  snapshot.enabledDeviceExtensionCount = 0;
  snapshot.synchronization2Features =
    {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES};
#if defined(VK_VERSION_1_3)
  snapshot.vulkan13Features = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
#endif
  snapshot.synchronization2Enabled = false;
  snapshot.validationLayerEnabled = false;
  snapshot.generation = 0;
  mState->queriedSynchronization2Features = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES};
#if defined(VK_VERSION_1_3)
  mState->queriedVulkan13Features = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
#endif
}

END_IGRAPHICS_NAMESPACE
END_IPLUG_NAMESPACE

#endif // defined(OS_WIN) && defined(IGRAPHICS_VULKAN)
