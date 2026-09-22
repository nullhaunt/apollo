#define VK_USE_PLATFORM_WIN32_KHR
#include "VulkanContext.hpp"

#include "Platform/Diagnostics.hpp"

#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <new>

namespace
{
  using apollo::diagnostics::Level;

  constexpr uint32_t InvalidFamily = std::numeric_limits<uint32_t>::max();

  struct Candidate
  {
    VkPhysicalDevice           device{};
    VkPhysicalDeviceProperties properties{};
    uint32_t                   graphicsFamily{ InvalidFamily };
    uint32_t                   presentFamily{ InvalidFamily };
    int                        priority{ -1 };
  };

  void LogFailure( const char * operation, VkResult result ) noexcept
  {
    char message[ 128 ]{};
    std::snprintf( message, sizeof( message ), "%s failed (VkResult %d).", operation, static_cast<int>( result ) );
    apollo::diagnostics::Write( Level::Error, message );
  }

  template <typename T> [[nodiscard]] std::unique_ptr<T[]> Allocate( uint32_t count ) noexcept
  {
    return count == 0 ? nullptr : std::unique_ptr<T[]>( new ( std::nothrow ) T[ count ]{} );
  }

  [[nodiscard]] bool HasExtension( const VkExtensionProperties * properties,
                                   uint32_t                      count,
                                   const char *                  requiredName ) noexcept
  {
    for ( uint32_t index = 0; index < count; ++index )
    {
      if ( std::strcmp( properties[ index ].extensionName, requiredName ) == 0 )
      {
        return true;
      }
    }
    return false;
  }

#if defined( APOLLO_BUILD_DEBUG )
  VkBool32 VKAPI_CALL DebugCallback( VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                     VkDebugUtilsMessageTypeFlagsEXT,
                                     const VkDebugUtilsMessengerCallbackDataEXT * data,
                                     void * ) noexcept
  {
    if ( data != nullptr && data->pMessage != nullptr )
    {
      const Level level = severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT ? Level::Error : Level::Warning;
      apollo::diagnostics::Write( level, "Vulkan validation: ", data->pMessage );
    }
    return VK_FALSE;
  }
#endif

  [[nodiscard]] bool SupportsSwapchain( VkPhysicalDevice device, VkSurfaceKHR surface ) noexcept
  {
    uint32_t extensionCount{};
    VkResult result = vkEnumerateDeviceExtensionProperties( device, nullptr, &extensionCount, nullptr );
    if ( result != VK_SUCCESS )
    {
      LogFailure( "vkEnumerateDeviceExtensionProperties", result );
      return false;
    }

    auto extensions = Allocate<VkExtensionProperties>( extensionCount );
    if ( extensionCount != 0 && !extensions )
    {
      apollo::diagnostics::Write( Level::Error, "Could not allocate Vulkan device extension list." );
      return false;
    }
    result = vkEnumerateDeviceExtensionProperties( device, nullptr, &extensionCount, extensions.get() );
    if ( result != VK_SUCCESS )
    {
      LogFailure( "vkEnumerateDeviceExtensionProperties", result );
      return false;
    }
    if ( !HasExtension( extensions.get(), extensionCount, VK_KHR_SWAPCHAIN_EXTENSION_NAME ) )
    {
      return false;
    }

    uint32_t formatCount{};
    uint32_t presentModeCount{};
    result = vkGetPhysicalDeviceSurfaceFormatsKHR( device, surface, &formatCount, nullptr );
    if ( result != VK_SUCCESS )
    {
      return false;
    }
    result = vkGetPhysicalDeviceSurfacePresentModesKHR( device, surface, &presentModeCount, nullptr );
    return result == VK_SUCCESS && formatCount != 0 && presentModeCount != 0;
  }

  [[nodiscard]] Candidate SelectDevice( VkInstance instance, VkSurfaceKHR surface ) noexcept
  {
    Candidate best{};
    uint32_t  deviceCount{};
    VkResult  result = vkEnumeratePhysicalDevices( instance, &deviceCount, nullptr );
    if ( result != VK_SUCCESS )
    {
      LogFailure( "vkEnumeratePhysicalDevices", result );
      return best;
    }
    if ( deviceCount == 0 )
    {
      apollo::diagnostics::Write( Level::Error, "Vulkan reported no physical devices." );
      return best;
    }

    auto devices = Allocate<VkPhysicalDevice>( deviceCount );
    if ( !devices )
    {
      apollo::diagnostics::Write( Level::Error, "Could not allocate Vulkan device list." );
      return best;
    }
    result = vkEnumeratePhysicalDevices( instance, &deviceCount, devices.get() );
    if ( result != VK_SUCCESS )
    {
      LogFailure( "vkEnumeratePhysicalDevices", result );
      return best;
    }

    for ( uint32_t deviceIndex = 0; deviceIndex < deviceCount; ++deviceIndex )
    {
      const VkPhysicalDevice device = devices[ deviceIndex ];
      uint32_t               familyCount{};
      vkGetPhysicalDeviceQueueFamilyProperties( device, &familyCount, nullptr );
      if ( familyCount == 0 )
      {
        continue;
      }
      auto families = Allocate<VkQueueFamilyProperties>( familyCount );
      if ( !families )
      {
        apollo::diagnostics::Write( Level::Error, "Could not allocate Vulkan queue family list." );
        return {};
      }
      vkGetPhysicalDeviceQueueFamilyProperties( device, &familyCount, families.get() );

      Candidate candidate{};
      candidate.device = device;
      for ( uint32_t family = 0; family < familyCount; ++family )
      {
        if ( families[ family ].queueCount == 0 )
        {
          continue;
        }
        VkBool32 supportsPresent    = VK_FALSE;
        result                      = vkGetPhysicalDeviceSurfaceSupportKHR( device, family, surface, &supportsPresent );
        const bool supportsGraphics = ( families[ family ].queueFlags & VK_QUEUE_GRAPHICS_BIT ) != 0;
        const bool canPresent       = result == VK_SUCCESS && supportsPresent == VK_TRUE;
        if ( supportsGraphics && canPresent )
        {
          candidate.graphicsFamily = family;
          candidate.presentFamily  = family;
          break;
        }
        if ( supportsGraphics && candidate.graphicsFamily == InvalidFamily )
        {
          candidate.graphicsFamily = family;
        }
        if ( canPresent && candidate.presentFamily == InvalidFamily )
        {
          candidate.presentFamily = family;
        }
      }

      if ( candidate.graphicsFamily == InvalidFamily || candidate.presentFamily == InvalidFamily ||
           !SupportsSwapchain( device, surface ) )
      {
        continue;
      }

      vkGetPhysicalDeviceProperties( device, &candidate.properties );
      candidate.priority = candidate.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU     ? 2
                           : candidate.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 1
                                                                                                       : 0;
      if ( candidate.priority > best.priority )
      {
        best = candidate;
      }
    }

    return best;
  }
} // namespace

namespace apollo::render::vulkan
{
  VulkanContext::~VulkanContext() noexcept
  {
    Shutdown();
  }

  bool VulkanContext::Initialize( HWND window ) noexcept
  {
    if ( window == nullptr || m_Instance != VK_NULL_HANDLE )
    {
      diagnostics::Write( Level::Error, "Invalid Vulkan context initialization state." );
      return false;
    }

    uint32_t extensionCount{};
    VkResult result = vkEnumerateInstanceExtensionProperties( nullptr, &extensionCount, nullptr );
    if ( result != VK_SUCCESS )
    {
      LogFailure( "vkEnumerateInstanceExtensionProperties", result );
      return false;
    }
    auto extensions = Allocate<VkExtensionProperties>( extensionCount );
    if ( extensionCount != 0 && !extensions )
    {
      diagnostics::Write( Level::Error, "Could not allocate Vulkan instance extension list." );
      return false;
    }
    result = vkEnumerateInstanceExtensionProperties( nullptr, &extensionCount, extensions.get() );
    if ( result != VK_SUCCESS )
    {
      LogFailure( "vkEnumerateInstanceExtensionProperties", result );
      return false;
    }
    if ( !HasExtension( extensions.get(), extensionCount, VK_KHR_SURFACE_EXTENSION_NAME ) ||
         !HasExtension( extensions.get(), extensionCount, VK_KHR_WIN32_SURFACE_EXTENSION_NAME ) )
    {
      diagnostics::Write( Level::Error, "Required Vulkan Windows surface extensions are unavailable." );
      return false;
    }

    const char * instanceExtensions[ 3 ]{ VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME };
    uint32_t     enabledExtensionCount = 2;
    const char * validationLayer       = "VK_LAYER_KHRONOS_validation";
    bool         enableValidation      = false;

#if defined( APOLLO_BUILD_DEBUG )
    uint32_t layerCount{};
    result = vkEnumerateInstanceLayerProperties( &layerCount, nullptr );
    if ( result != VK_SUCCESS )
    {
      LogFailure( "vkEnumerateInstanceLayerProperties", result );
      return false;
    }
    auto layers = Allocate<VkLayerProperties>( layerCount );
    if ( layerCount != 0 && !layers )
    {
      diagnostics::Write( Level::Error, "Could not allocate Vulkan instance layer list." );
      return false;
    }
    result = vkEnumerateInstanceLayerProperties( &layerCount, layers.get() );
    if ( result != VK_SUCCESS )
    {
      LogFailure( "vkEnumerateInstanceLayerProperties", result );
      return false;
    }
    for ( uint32_t index = 0; index < layerCount; ++index )
    {
      enableValidation |= std::strcmp( layers[ index ].layerName, validationLayer ) == 0;
    }
    enableValidation &= HasExtension( extensions.get(), extensionCount, VK_EXT_DEBUG_UTILS_EXTENSION_NAME );
    if ( enableValidation )
    {
      instanceExtensions[ enabledExtensionCount++ ] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    }
    else
    {
      diagnostics::Write( Level::Warning, "Vulkan validation layer or debug utils extension is unavailable." );
    }
#endif

    VkApplicationInfo applicationInfo{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
    applicationInfo.pApplicationName = "Apollo";
    applicationInfo.pEngineName      = "Apollo";
    applicationInfo.apiVersion       = VK_API_VERSION_1_0;

    VkDebugUtilsMessengerCreateInfoEXT debugInfo{ VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
#if defined( APOLLO_BUILD_DEBUG )
    debugInfo.messageSeverity =
      VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debugInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debugInfo.pfnUserCallback = DebugCallback;
#endif

    VkInstanceCreateInfo instanceInfo{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    instanceInfo.pNext                   = enableValidation ? &debugInfo : nullptr;
    instanceInfo.pApplicationInfo        = &applicationInfo;
    instanceInfo.enabledExtensionCount   = enabledExtensionCount;
    instanceInfo.ppEnabledExtensionNames = instanceExtensions;
    instanceInfo.enabledLayerCount       = enableValidation ? 1u : 0u;
    instanceInfo.ppEnabledLayerNames     = enableValidation ? &validationLayer : nullptr;

    result = vkCreateInstance( &instanceInfo, nullptr, &m_Instance );
    if ( result != VK_SUCCESS )
    {
      LogFailure( "vkCreateInstance", result );
      Shutdown();
      return false;
    }
    m_ValidationEnabled = enableValidation;

#if defined( APOLLO_BUILD_DEBUG )
    if ( enableValidation )
    {
      auto createMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr( m_Instance, "vkCreateDebugUtilsMessengerEXT" ) );
      if ( createMessenger == nullptr )
      {
        diagnostics::Write( Level::Error, "Vulkan debug messenger entry point is unavailable." );
        Shutdown();
        return false;
      }
      result = createMessenger( m_Instance, &debugInfo, nullptr, &m_DebugMessenger );
      if ( result != VK_SUCCESS )
      {
        LogFailure( "vkCreateDebugUtilsMessengerEXT", result );
        Shutdown();
        return false;
      }
    }
#endif

    VkWin32SurfaceCreateInfoKHR surfaceInfo{ VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR };
    surfaceInfo.hinstance = GetModuleHandleW( nullptr );
    surfaceInfo.hwnd      = window;
    result                = vkCreateWin32SurfaceKHR( m_Instance, &surfaceInfo, nullptr, &m_Surface );
    if ( result != VK_SUCCESS )
    {
      LogFailure( "vkCreateWin32SurfaceKHR", result );
      Shutdown();
      return false;
    }

    const Candidate selected = SelectDevice( m_Instance, m_Surface );
    if ( selected.device == VK_NULL_HANDLE )
    {
      diagnostics::Write( Level::Error, "No Vulkan device supports graphics, presentation, and swapchains." );
      Shutdown();
      return false;
    }

    const float             queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueInfos[ 2 ]{};
    queueInfos[ 0 ].sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfos[ 0 ].queueFamilyIndex = selected.graphicsFamily;
    queueInfos[ 0 ].queueCount       = 1;
    queueInfos[ 0 ].pQueuePriorities = &queuePriority;
    uint32_t queueInfoCount          = 1;
    if ( selected.presentFamily != selected.graphicsFamily )
    {
      queueInfos[ 1 ]                  = queueInfos[ 0 ];
      queueInfos[ 1 ].queueFamilyIndex = selected.presentFamily;
      queueInfoCount                   = 2;
    }

    const char *       swapchainExtension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    VkDeviceCreateInfo deviceInfo{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    deviceInfo.queueCreateInfoCount    = queueInfoCount;
    deviceInfo.pQueueCreateInfos       = queueInfos;
    deviceInfo.enabledExtensionCount   = 1;
    deviceInfo.ppEnabledExtensionNames = &swapchainExtension;
    result                             = vkCreateDevice( selected.device, &deviceInfo, nullptr, &m_Device );
    if ( result != VK_SUCCESS )
    {
      LogFailure( "vkCreateDevice", result );
      Shutdown();
      return false;
    }

    m_PhysicalDevice = selected.device;
    m_GraphicsFamily = selected.graphicsFamily;
    m_PresentFamily  = selected.presentFamily;
    vkGetDeviceQueue( m_Device, m_GraphicsFamily, 0, &m_GraphicsQueue );
    vkGetDeviceQueue( m_Device, m_PresentFamily, 0, &m_PresentQueue );
    if ( m_GraphicsQueue == VK_NULL_HANDLE || m_PresentQueue == VK_NULL_HANDLE )
    {
      diagnostics::Write( Level::Error, "Vulkan device did not return the requested queues." );
      Shutdown();
      return false;
    }

    std::memcpy( m_AdapterName, selected.properties.deviceName, sizeof( m_AdapterName ) );
    m_AdapterName[ sizeof( m_AdapterName ) - 1 ] = '\0';
    diagnostics::Write( Level::Information, "Vulkan adapter: ", m_AdapterName );
    diagnostics::Write( Level::Information,
                        m_ValidationEnabled ? "Vulkan validation enabled." : "Vulkan validation disabled." );
    return true;
  }

  void VulkanContext::Shutdown() noexcept
  {
    if ( m_Device != VK_NULL_HANDLE )
    {
      vkDeviceWaitIdle( m_Device );
      vkDestroyDevice( m_Device, nullptr );
      m_Device = VK_NULL_HANDLE;
    }
    if ( m_Surface != VK_NULL_HANDLE )
    {
      vkDestroySurfaceKHR( m_Instance, m_Surface, nullptr );
      m_Surface = VK_NULL_HANDLE;
    }
    if ( m_DebugMessenger != VK_NULL_HANDLE )
    {
      auto destroyMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr( m_Instance, "vkDestroyDebugUtilsMessengerEXT" ) );
      if ( destroyMessenger != nullptr )
      {
        destroyMessenger( m_Instance, m_DebugMessenger, nullptr );
      }
      m_DebugMessenger = VK_NULL_HANDLE;
    }
    if ( m_Instance != VK_NULL_HANDLE )
    {
      vkDestroyInstance( m_Instance, nullptr );
      m_Instance = VK_NULL_HANDLE;
    }

    m_PhysicalDevice    = VK_NULL_HANDLE;
    m_GraphicsQueue     = VK_NULL_HANDLE;
    m_PresentQueue      = VK_NULL_HANDLE;
    m_GraphicsFamily    = 0;
    m_PresentFamily     = 0;
    m_AdapterName[ 0 ]  = '\0';
    m_ValidationEnabled = false;
  }

  const char * VulkanContext::GetAdapterName() const noexcept
  {
    return m_AdapterName;
  }

  bool VulkanContext::IsValidationEnabled() const noexcept
  {
    return m_ValidationEnabled;
  }
} // namespace apollo::render::vulkan
