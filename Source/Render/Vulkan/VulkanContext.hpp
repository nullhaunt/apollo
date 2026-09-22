#pragma once

#if !defined( APOLLO_PLATFORM_WINDOWS )
  #error "VulkanContext is available only on Windows."
#endif

#include "Platform/Windows/WindowsWindow.hpp"

#include <vulkan/vulkan.h>

namespace apollo::render::vulkan
{
  class VulkanContext final
  {
  public:
    VulkanContext() noexcept = default;
    ~VulkanContext() noexcept;

    VulkanContext( const VulkanContext & )             = delete;
    VulkanContext & operator=( const VulkanContext & ) = delete;
    VulkanContext( VulkanContext && )                  = delete;
    VulkanContext & operator=( VulkanContext && )      = delete;

    [[nodiscard]] bool Initialize( HWND window ) noexcept;
    void               Shutdown() noexcept;

    [[nodiscard]] const char * GetAdapterName() const noexcept;
    [[nodiscard]] bool         IsValidationEnabled() const noexcept;

  private:
    VkInstance               m_Instance{};
    VkDebugUtilsMessengerEXT m_DebugMessenger{};
    VkSurfaceKHR             m_Surface{};
    VkPhysicalDevice         m_PhysicalDevice{};
    VkDevice                 m_Device{};
    VkQueue                  m_GraphicsQueue{};
    VkQueue                  m_PresentQueue{};
    uint32_t                 m_GraphicsFamily{};
    uint32_t                 m_PresentFamily{};
    char                     m_AdapterName[ VK_MAX_PHYSICAL_DEVICE_NAME_SIZE ]{};
    bool                     m_ValidationEnabled{};
  };
} // namespace apollo::render::vulkan
