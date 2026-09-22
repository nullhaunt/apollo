#pragma once

#if !defined( APOLLO_PLATFORM_WINDOWS )
  #error "VulkanBootstrap is available only on Windows."
#endif

#ifndef NOMINMAX
  #define NOMINMAX
#endif

#include "Core/Types.hpp"

#include <limits>
#include <vulkan/vulkan.hpp>

namespace apollo::render::vulkan::bootstrap
{
  inline constexpr char ValidationLayer[] = "VK_LAYER_KHRONOS_validation";
  inline constexpr u32  InvalidFamily     = std::numeric_limits<u32>::max();

  struct InstanceSettings
  {
    const char * extensions[ 3 ]{ VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME };
    u32          extensionCount{ 2 };
    bool         validation{};
  };

  struct DeviceCandidate
  {
    vk::PhysicalDevice           device{};
    vk::PhysicalDeviceProperties properties{};
    u32                          graphicsFamily{ InvalidFamily };
    u32                          presentFamily{ InvalidFamily };
    s32                          priority{ -1 };
  };

  void                          LogFailure( const char * operation, vk::Result result ) noexcept;
  [[nodiscard]] bool            QueryInstanceSettings( InstanceSettings & settings ) noexcept;
  [[nodiscard]] DeviceCandidate SelectDevice( vk::Instance instance, vk::SurfaceKHR surface ) noexcept;
} // namespace apollo::render::vulkan::bootstrap
