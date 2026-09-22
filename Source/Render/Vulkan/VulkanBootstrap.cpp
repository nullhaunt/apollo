#include "Render/Vulkan/VulkanBootstrap.hpp"

#include "Platform/Diagnostics.hpp"

#include <cstdio>
#include <cstring>
#include <memory>
#include <new>

namespace apollo::render::vulkan::bootstrap
{
  namespace
  {
    using diagnostics::Level;

    template <typename T> [[nodiscard]] std::unique_ptr<T[]> Allocate( u32 count ) noexcept
    {
      return count == 0 ? nullptr : std::unique_ptr<T[]>( new ( std::nothrow ) T[ count ]{} );
    }

    [[nodiscard]] bool HasExtension( const vk::ExtensionProperties * properties,
                                     u32                             count,
                                     const char *                    requiredName ) noexcept
    {
      for ( u32 index = 0; index < count; ++index )
      {
        if ( std::strcmp( properties[ index ].extensionName.data(), requiredName ) == 0 )
        {
          return true;
        }
      }
      return false;
    }

#if defined( APOLLO_BUILD_DEBUG )
    [[nodiscard]] bool QueryValidationLayer( bool & available ) noexcept
    {
      u32        layerCount{};
      vk::Result result = vk::enumerateInstanceLayerProperties( &layerCount, nullptr );
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

      for ( u32 index = 0; index < layerCount; ++index )
      {
        available |= std::strcmp( layers[ index ].layerName.data(), ValidationLayer ) == 0;
      }
      return true;
    }
#endif

    [[nodiscard]] bool SupportsSwapchain( vk::PhysicalDevice device, vk::SurfaceKHR surface ) noexcept
    {
      u32        extensionCount{};
      vk::Result result = device.enumerateDeviceExtensionProperties( nullptr, &extensionCount, nullptr );
      if ( result != vk::Result::eSuccess )
      {
        LogFailure( "vkEnumerateDeviceExtensionProperties", result );
        return false;
      }

      auto extensions = Allocate<vk::ExtensionProperties>( extensionCount );
      if ( extensionCount != 0 && !extensions )
      {
        diagnostics::Write( Level::Error, "Could not allocate Vulkan device extension list." );
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

      u32 formatCount{};
      u32 presentModeCount{};
      result = device.getSurfaceFormatsKHR( surface, &formatCount, nullptr );
      if ( result != vk::Result::eSuccess )
      {
        return false;
      }
      result = device.getSurfacePresentModesKHR( surface, &presentModeCount, nullptr );
      return result == vk::Result::eSuccess && formatCount != 0 && presentModeCount != 0;
    }

    [[nodiscard]] bool FindQueueFamilies( DeviceCandidate & candidate, vk::SurfaceKHR surface ) noexcept
    {
      u32 familyCount{};
      candidate.device.getQueueFamilyProperties( &familyCount, nullptr );
      if ( familyCount == 0 )
      {
        return true;
      }

      auto families = Allocate<vk::QueueFamilyProperties>( familyCount );
      if ( !families )
      {
        diagnostics::Write( Level::Error, "Could not allocate Vulkan queue family list." );
        return false;
      }
      candidate.device.getQueueFamilyProperties( &familyCount, families.get() );

      for ( u32 family = 0; family < familyCount; ++family )
      {
        if ( families[ family ].queueCount == 0 )
        {
          continue;
        }

        vk::Bool32       supportsPresent{};
        const vk::Result result     = candidate.device.getSurfaceSupportKHR( family, surface, &supportsPresent );
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
      return true;
    }

    [[nodiscard]] s32 DevicePriority( vk::PhysicalDeviceType type ) noexcept
    {
      switch ( type )
      {
        case vk::PhysicalDeviceType::eDiscreteGpu:
          return 2;
        case vk::PhysicalDeviceType::eIntegratedGpu:
          return 1;
        default:
          return 0;
      }
    }
  } // namespace

  void LogFailure( const char * operation, vk::Result result ) noexcept
  {
    char message[ 128 ]{};
    std::snprintf( message, sizeof( message ), "%s failed (VkResult %d).", operation, static_cast<int>( result ) );
    diagnostics::Write( Level::Error, message );
  }

  bool QueryInstanceSettings( InstanceSettings & settings ) noexcept
  {
    u32        extensionCount{};
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

#if defined( APOLLO_BUILD_DEBUG )
    bool hasValidationLayer{};
    if ( !QueryValidationLayer( hasValidationLayer ) )
    {
      return false;
    }
    settings.validation =
      hasValidationLayer && HasExtension( extensions.get(), extensionCount, VK_EXT_DEBUG_UTILS_EXTENSION_NAME );
    if ( settings.validation )
    {
      settings.extensions[ settings.extensionCount++ ] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    }
    else
    {
      diagnostics::Write( Level::Warning, "Vulkan validation layer or debug utils extension is unavailable." );
    }
#endif
    return true;
  }

  DeviceCandidate SelectDevice( vk::Instance instance, vk::SurfaceKHR surface ) noexcept
  {
    DeviceCandidate best{};
    u32             deviceCount{};
    vk::Result      result = instance.enumeratePhysicalDevices( &deviceCount, nullptr );
    if ( result != vk::Result::eSuccess )
    {
      LogFailure( "vkEnumeratePhysicalDevices", result );
      return best;
    }
    if ( deviceCount == 0 )
    {
      diagnostics::Write( Level::Error, "Vulkan reported no physical devices." );
      return best;
    }

    auto devices = Allocate<vk::PhysicalDevice>( deviceCount );
    if ( !devices )
    {
      diagnostics::Write( Level::Error, "Could not allocate Vulkan device list." );
      return best;
    }
    result = instance.enumeratePhysicalDevices( &deviceCount, devices.get() );
    if ( result != vk::Result::eSuccess )
    {
      LogFailure( "vkEnumeratePhysicalDevices", result );
      return best;
    }

    for ( u32 deviceIndex = 0; deviceIndex < deviceCount; ++deviceIndex )
    {
      DeviceCandidate candidate{};
      candidate.device = devices[ deviceIndex ];
      if ( !FindQueueFamilies( candidate, surface ) )
      {
        return {};
      }
      if ( candidate.graphicsFamily == InvalidFamily || candidate.presentFamily == InvalidFamily ||
           !SupportsSwapchain( candidate.device, surface ) )
      {
        continue;
      }

      candidate.device.getProperties( &candidate.properties );
      candidate.priority = DevicePriority( candidate.properties.deviceType );
      if ( candidate.priority > best.priority )
      {
        best = candidate;
      }
    }
    return best;
  }
} // namespace apollo::render::vulkan::bootstrap
