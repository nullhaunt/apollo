#include "Render/Vulkan/VulkanContext.hpp"

#include "Platform/Diagnostics.hpp"
#include "Render/Vulkan/VulkanBootstrap.hpp"

#include <cstring>

namespace
{
  using apollo::diagnostics::Level;

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

  [[nodiscard]] vk::DebugUtilsMessengerCreateInfoEXT MakeDebugMessengerInfo() noexcept
  {
    vk::DebugUtilsMessengerCreateInfoEXT info{};
    info.messageSeverity =
      vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError;
    info.messageType = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                       vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
                       vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance;
    info.pfnUserCallback = DebugCallback;
    return info;
  }
#endif
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

    if ( !CreateInstance() || !CreateDebugMessenger() || !CreateSurface( window ) || !CreateDevice() )
    {
      Shutdown();
      return false;
    }

    diagnostics::Write( Level::Information, "Vulkan adapter: ", m_AdapterName );
    diagnostics::Write( Level::Information,
                        m_ValidationEnabled ? "Vulkan validation enabled." : "Vulkan validation disabled." );
    return true;
  }

  bool VulkanContext::CreateInstance() noexcept
  {
    bootstrap::InstanceSettings settings{};
    if ( !bootstrap::QueryInstanceSettings( settings ) )
    {
      return false;
    }

    vk::ApplicationInfo applicationInfo{};
    applicationInfo.pApplicationName = "Apollo";
    applicationInfo.pEngineName      = "Apollo";
    applicationInfo.apiVersion       = VK_API_VERSION_1_0;

    vk::DebugUtilsMessengerCreateInfoEXT debugInfo{};
#if defined( APOLLO_BUILD_DEBUG )
    debugInfo = MakeDebugMessengerInfo();
#endif

    const char *           validationLayer = bootstrap::ValidationLayer;
    vk::InstanceCreateInfo instanceInfo{};
    instanceInfo.pNext                   = settings.validation ? &debugInfo : nullptr;
    instanceInfo.pApplicationInfo        = &applicationInfo;
    instanceInfo.enabledExtensionCount   = settings.extensionCount;
    instanceInfo.ppEnabledExtensionNames = settings.extensions;
    instanceInfo.enabledLayerCount       = settings.validation ? 1u : 0u;
    instanceInfo.ppEnabledLayerNames     = settings.validation ? &validationLayer : nullptr;

    const vk::Result result = vk::createInstance( &instanceInfo, nullptr, &m_Instance );
    if ( result != vk::Result::eSuccess )
    {
      bootstrap::LogFailure( "vkCreateInstance", result );
      return false;
    }
    m_ValidationEnabled = settings.validation;
    return true;
  }

  bool VulkanContext::CreateDebugMessenger() noexcept
  {
#if defined( APOLLO_BUILD_DEBUG )
    if ( !m_ValidationEnabled )
    {
      return true;
    }

    const vk::detail::DispatchLoaderDynamic dispatch( static_cast<VkInstance>( m_Instance ), vkGetInstanceProcAddr );
    if ( dispatch.vkCreateDebugUtilsMessengerEXT == nullptr )
    {
      diagnostics::Write( Level::Error, "Vulkan debug messenger entry point is unavailable." );
      return false;
    }

    const vk::DebugUtilsMessengerCreateInfoEXT debugInfo = MakeDebugMessengerInfo();
    const vk::Result                           result =
      m_Instance.createDebugUtilsMessengerEXT( &debugInfo, nullptr, &m_DebugMessenger, dispatch );
    if ( result != vk::Result::eSuccess )
    {
      bootstrap::LogFailure( "vkCreateDebugUtilsMessengerEXT", result );
      return false;
    }
#endif
    return true;
  }

  bool VulkanContext::CreateSurface( HWND window ) noexcept
  {
    vk::Win32SurfaceCreateInfoKHR surfaceInfo{};
    surfaceInfo.hinstance = GetModuleHandleW( nullptr );
    surfaceInfo.hwnd      = window;

    const vk::Result result = m_Instance.createWin32SurfaceKHR( &surfaceInfo, nullptr, &m_Surface );
    if ( result != vk::Result::eSuccess )
    {
      bootstrap::LogFailure( "vkCreateWin32SurfaceKHR", result );
      return false;
    }
    return true;
  }

  bool VulkanContext::CreateDevice() noexcept
  {
    const bootstrap::DeviceCandidate selected = bootstrap::SelectDevice( m_Instance, m_Surface );
    if ( !selected.device )
    {
      diagnostics::Write( Level::Error, "No Vulkan device supports graphics, presentation, and swapchains." );
      return false;
    }

    const f32                 queuePriority = 1.0f;
    vk::DeviceQueueCreateInfo queueInfos[ 2 ]{};
    queueInfos[ 0 ].queueFamilyIndex = selected.graphicsFamily;
    queueInfos[ 0 ].queueCount       = 1;
    queueInfos[ 0 ].pQueuePriorities = &queuePriority;

    u32 queueInfoCount = 1;
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

    const vk::Result result = selected.device.createDevice( &deviceInfo, nullptr, &m_Device );
    if ( result != vk::Result::eSuccess )
    {
      bootstrap::LogFailure( "vkCreateDevice", result );
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
      return false;
    }

    std::memcpy( m_AdapterName, selected.properties.deviceName.data(), sizeof( m_AdapterName ) );
    m_AdapterName[ sizeof( m_AdapterName ) - 1 ] = '\0';
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
