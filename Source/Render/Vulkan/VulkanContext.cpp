#include "Render/Vulkan/VulkanContext.hpp"

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
    vk::PhysicalDevice           device{};
    vk::PhysicalDeviceProperties properties{};
    uint32_t                     graphicsFamily{ InvalidFamily };
    uint32_t                     presentFamily{ InvalidFamily };
    int                          priority{ -1 };
  };

  void LogFailure( const char * operation, vk::Result result ) noexcept
  {
    char message[ 128 ]{};
    std::snprintf( message, sizeof( message ), "%s failed (VkResult %d).", operation, static_cast<int>( result ) );
    apollo::diagnostics::Write( Level::Error, message );
  }

  template <typename T> [[nodiscard]] std::unique_ptr<T[]> Allocate( uint32_t count ) noexcept
  {
    return count == 0 ? nullptr : std::unique_ptr<T[]>( new ( std::nothrow ) T[ count ]{} );
  }

  [[nodiscard]] bool HasExtension( const vk::ExtensionProperties * properties,
                                   uint32_t                        count,
                                   const char *                    requiredName ) noexcept
  {
    for ( uint32_t index = 0; index < count; ++index )
    {
      if ( std::strcmp( properties[ index ].extensionName.data(), requiredName ) == 0 )
      {
        return true;
      }
    }
    return false;
  }

#if defined( APOLLO_BUILD_DEBUG )
  vk::Bool32 VKAPI_CALL DebugCallback( vk::DebugUtilsMessageSeverityFlagBitsEXT severity,
                                       vk::DebugUtilsMessageTypeFlagsEXT,
                                       const vk::DebugUtilsMessengerCallbackDataEXT * data,
                                       void * ) noexcept
  {
    if ( data != nullptr && data->pMessage != nullptr )
    {
      const Level level = severity >= vk::DebugUtilsMessageSeverityFlagBitsEXT::eError ? Level::Error : Level::Warning;
      apollo::diagnostics::Write( level, "Vulkan validation: ", data->pMessage );
    }
    return VK_FALSE;
  }
#endif

  [[nodiscard]] bool SupportsSwapchain( vk::PhysicalDevice device, vk::SurfaceKHR surface ) noexcept
  {
    uint32_t   extensionCount{};
    vk::Result result = device.enumerateDeviceExtensionProperties( nullptr, &extensionCount, nullptr );
    if ( result != vk::Result::eSuccess )
    {
      LogFailure( "vkEnumerateDeviceExtensionProperties", result );
      return false;
    }

    auto extensions = Allocate<vk::ExtensionProperties>( extensionCount );
    if ( extensionCount != 0 && !extensions )
    {
      apollo::diagnostics::Write( Level::Error, "Could not allocate Vulkan device extension list." );
      return false;
    }
    result = device.enumerateDeviceExtensionProperties( nullptr, &extensionCount, extensions.get() );
    if ( result != vk::Result::eSuccess )
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
    result = device.getSurfaceFormatsKHR( surface, &formatCount, nullptr );
    if ( result != vk::Result::eSuccess )
    {
      return false;
    }
    result = device.getSurfacePresentModesKHR( surface, &presentModeCount, nullptr );
    return result == vk::Result::eSuccess && formatCount != 0 && presentModeCount != 0;
  }

  [[nodiscard]] Candidate SelectDevice( vk::Instance instance, vk::SurfaceKHR surface ) noexcept
  {
    Candidate  best{};
    uint32_t   deviceCount{};
    vk::Result result = instance.enumeratePhysicalDevices( &deviceCount, nullptr );
    if ( result != vk::Result::eSuccess )
    {
      LogFailure( "vkEnumeratePhysicalDevices", result );
      return best;
    }
    if ( deviceCount == 0 )
    {
      apollo::diagnostics::Write( Level::Error, "Vulkan reported no physical devices." );
      return best;
    }

    auto devices = Allocate<vk::PhysicalDevice>( deviceCount );
    if ( !devices )
    {
      apollo::diagnostics::Write( Level::Error, "Could not allocate Vulkan device list." );
      return best;
    }
    result = instance.enumeratePhysicalDevices( &deviceCount, devices.get() );
    if ( result != vk::Result::eSuccess )
    {
      LogFailure( "vkEnumeratePhysicalDevices", result );
      return best;
    }

    for ( uint32_t deviceIndex = 0; deviceIndex < deviceCount; ++deviceIndex )
    {
      const vk::PhysicalDevice device = devices[ deviceIndex ];
      uint32_t                 familyCount{};
      device.getQueueFamilyProperties( &familyCount, nullptr );
      if ( familyCount == 0 )
      {
        continue;
      }
      auto families = Allocate<vk::QueueFamilyProperties>( familyCount );
      if ( !families )
      {
        apollo::diagnostics::Write( Level::Error, "Could not allocate Vulkan queue family list." );
        return {};
      }
      device.getQueueFamilyProperties( &familyCount, families.get() );

      Candidate candidate{};
      candidate.device = device;
      for ( uint32_t family = 0; family < familyCount; ++family )
      {
        if ( families[ family ].queueCount == 0 )
        {
          continue;
        }
        vk::Bool32 supportsPresent{};
        result                      = device.getSurfaceSupportKHR( family, surface, &supportsPresent );
        const bool supportsGraphics = static_cast<bool>( families[ family ].queueFlags & vk::QueueFlagBits::eGraphics );
        const bool canPresent       = result == vk::Result::eSuccess && supportsPresent != 0;
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

      device.getProperties( &candidate.properties );
      candidate.priority = candidate.properties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu     ? 2
                           : candidate.properties.deviceType == vk::PhysicalDeviceType::eIntegratedGpu ? 1
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
    if ( window == nullptr || m_Instance )
    {
      diagnostics::Write( Level::Error, "Invalid Vulkan context initialization state." );
      return false;
    }

    uint32_t   extensionCount{};
    vk::Result result = vk::enumerateInstanceExtensionProperties( nullptr, &extensionCount, nullptr );
    if ( result != vk::Result::eSuccess )
    {
      LogFailure( "vkEnumerateInstanceExtensionProperties", result );
      return false;
    }
    auto extensions = Allocate<vk::ExtensionProperties>( extensionCount );
    if ( extensionCount != 0 && !extensions )
    {
      diagnostics::Write( Level::Error, "Could not allocate Vulkan instance extension list." );
      return false;
    }
    result = vk::enumerateInstanceExtensionProperties( nullptr, &extensionCount, extensions.get() );
    if ( result != vk::Result::eSuccess )
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
    result = vk::enumerateInstanceLayerProperties( &layerCount, nullptr );
    if ( result != vk::Result::eSuccess )
    {
      LogFailure( "vkEnumerateInstanceLayerProperties", result );
      return false;
    }
    auto layers = Allocate<vk::LayerProperties>( layerCount );
    if ( layerCount != 0 && !layers )
    {
      diagnostics::Write( Level::Error, "Could not allocate Vulkan instance layer list." );
      return false;
    }
    result = vk::enumerateInstanceLayerProperties( &layerCount, layers.get() );
    if ( result != vk::Result::eSuccess )
    {
      LogFailure( "vkEnumerateInstanceLayerProperties", result );
      return false;
    }
    for ( uint32_t index = 0; index < layerCount; ++index )
    {
      enableValidation |= std::strcmp( layers[ index ].layerName.data(), validationLayer ) == 0;
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

    vk::ApplicationInfo applicationInfo{};
    applicationInfo.pApplicationName = "Apollo";
    applicationInfo.pEngineName      = "Apollo";
    applicationInfo.apiVersion       = VK_API_VERSION_1_0;

    vk::DebugUtilsMessengerCreateInfoEXT debugInfo{};
#if defined( APOLLO_BUILD_DEBUG )
    debugInfo.messageSeverity =
      vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError;
    debugInfo.messageType = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                            vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
                            vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance;
    debugInfo.pfnUserCallback = DebugCallback;
#endif

    vk::InstanceCreateInfo instanceInfo{};
    instanceInfo.pNext                   = enableValidation ? &debugInfo : nullptr;
    instanceInfo.pApplicationInfo        = &applicationInfo;
    instanceInfo.enabledExtensionCount   = enabledExtensionCount;
    instanceInfo.ppEnabledExtensionNames = instanceExtensions;
    instanceInfo.enabledLayerCount       = enableValidation ? 1u : 0u;
    instanceInfo.ppEnabledLayerNames     = enableValidation ? &validationLayer : nullptr;

    result = vk::createInstance( &instanceInfo, nullptr, &m_Instance );
    if ( result != vk::Result::eSuccess )
    {
      LogFailure( "vkCreateInstance", result );
      Shutdown();
      return false;
    }
    m_ValidationEnabled = enableValidation;

#if defined( APOLLO_BUILD_DEBUG )
    if ( enableValidation )
    {
      const vk::detail::DispatchLoaderDynamic dispatch( static_cast<VkInstance>( m_Instance ), vkGetInstanceProcAddr );
      if ( dispatch.vkCreateDebugUtilsMessengerEXT == nullptr )
      {
        diagnostics::Write( Level::Error, "Vulkan debug messenger entry point is unavailable." );
        Shutdown();
        return false;
      }
      result = m_Instance.createDebugUtilsMessengerEXT( &debugInfo, nullptr, &m_DebugMessenger, dispatch );
      if ( result != vk::Result::eSuccess )
      {
        LogFailure( "vkCreateDebugUtilsMessengerEXT", result );
        Shutdown();
        return false;
      }
    }
#endif

    vk::Win32SurfaceCreateInfoKHR surfaceInfo{};
    surfaceInfo.hinstance = GetModuleHandleW( nullptr );
    surfaceInfo.hwnd      = window;
    result                = m_Instance.createWin32SurfaceKHR( &surfaceInfo, nullptr, &m_Surface );
    if ( result != vk::Result::eSuccess )
    {
      LogFailure( "vkCreateWin32SurfaceKHR", result );
      Shutdown();
      return false;
    }

    const Candidate selected = SelectDevice( m_Instance, m_Surface );
    if ( !selected.device )
    {
      diagnostics::Write( Level::Error, "No Vulkan device supports graphics, presentation, and swapchains." );
      Shutdown();
      return false;
    }

    const float               queuePriority = 1.0f;
    vk::DeviceQueueCreateInfo queueInfos[ 2 ]{};
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

    const char *         swapchainExtension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    vk::DeviceCreateInfo deviceInfo{};
    deviceInfo.queueCreateInfoCount    = queueInfoCount;
    deviceInfo.pQueueCreateInfos       = queueInfos;
    deviceInfo.enabledExtensionCount   = 1;
    deviceInfo.ppEnabledExtensionNames = &swapchainExtension;
    result                             = selected.device.createDevice( &deviceInfo, nullptr, &m_Device );
    if ( result != vk::Result::eSuccess )
    {
      LogFailure( "vkCreateDevice", result );
      Shutdown();
      return false;
    }

    m_PhysicalDevice = selected.device;
    m_GraphicsFamily = selected.graphicsFamily;
    m_PresentFamily  = selected.presentFamily;
    m_Device.getQueue( m_GraphicsFamily, 0, &m_GraphicsQueue );
    m_Device.getQueue( m_PresentFamily, 0, &m_PresentQueue );
    if ( !m_GraphicsQueue || !m_PresentQueue )
    {
      diagnostics::Write( Level::Error, "Vulkan device did not return the requested queues." );
      Shutdown();
      return false;
    }

    std::memcpy( m_AdapterName, selected.properties.deviceName.data(), sizeof( m_AdapterName ) );
    m_AdapterName[ sizeof( m_AdapterName ) - 1 ] = '\0';
    diagnostics::Write( Level::Information, "Vulkan adapter: ", m_AdapterName );
    diagnostics::Write( Level::Information,
                        m_ValidationEnabled ? "Vulkan validation enabled." : "Vulkan validation disabled." );
    return true;
  }

  void VulkanContext::Shutdown() noexcept
  {
    if ( m_Device )
    {
      ( void )m_Device.waitIdle();
      m_Device.destroy();
      m_Device = nullptr;
    }
    if ( m_Surface )
    {
      m_Instance.destroySurfaceKHR( m_Surface );
      m_Surface = nullptr;
    }
    if ( m_DebugMessenger )
    {
      const vk::detail::DispatchLoaderDynamic dispatch( static_cast<VkInstance>( m_Instance ), vkGetInstanceProcAddr );
      if ( dispatch.vkDestroyDebugUtilsMessengerEXT != nullptr )
      {
        m_Instance.destroyDebugUtilsMessengerEXT( m_DebugMessenger, nullptr, dispatch );
      }
      m_DebugMessenger = nullptr;
    }
    if ( m_Instance )
    {
      m_Instance.destroy();
      m_Instance = nullptr;
    }

    m_PhysicalDevice    = nullptr;
    m_GraphicsQueue     = nullptr;
    m_PresentQueue      = nullptr;
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
