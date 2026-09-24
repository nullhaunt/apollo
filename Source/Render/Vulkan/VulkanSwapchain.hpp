#pragma once

#if !defined( APOLLO_PLATFORM_WINDOWS )
  #error "VulkanSwapchain is available only on Windows."
#endif

#include "Render/RenderBudget.hpp"
#include "Render/RenderTelemetry.hpp"
#include "Render/RenderTypes.hpp"

#include <memory>
#include <vulkan/vulkan.hpp>

namespace apollo::render::vulkan
{
  class VulkanContext;

  class VulkanSwapchain final
  {
  public:
    VulkanSwapchain() noexcept = default;
    ~VulkanSwapchain() noexcept;

    VulkanSwapchain( const VulkanSwapchain & )             = delete;
    VulkanSwapchain & operator=( const VulkanSwapchain & ) = delete;
    VulkanSwapchain( VulkanSwapchain && )                  = delete;
    VulkanSwapchain & operator=( VulkanSwapchain && )      = delete;

    [[nodiscard]] bool   Initialize( const VulkanContext & context, Extent2D requestedExtent ) noexcept;
    [[nodiscard]] Result Resize( Extent2D requestedExtent ) noexcept;
    void                 Shutdown() noexcept;

    [[nodiscard]] vk::SwapchainKHR GetHandle() const noexcept;
    [[nodiscard]] vk::RenderPass   GetRenderPass() const noexcept;
    [[nodiscard]] vk::Framebuffer  GetFramebuffer( u32 imageIndex ) const noexcept;
    [[nodiscard]] vk::Extent2D     GetExtent() const noexcept;
    [[nodiscard]] u32              GetImageCount() const noexcept;

  private:
    [[nodiscard]] bool Create( Extent2D requestedExtent ) noexcept;
    [[nodiscard]] bool CreateSwapchain( Extent2D requestedExtent ) noexcept;
    [[nodiscard]] bool CreateImageViews() noexcept;
    [[nodiscard]] bool CreateRenderPass() noexcept;
    [[nodiscard]] bool CreateDepthImage() noexcept;
    [[nodiscard]] bool CreateFramebuffers() noexcept;
    void               DestroyResources() noexcept;

    const VulkanContext *              m_Context{};
    vk::SwapchainKHR                   m_Swapchain{};
    vk::RenderPass                     m_RenderPass{};
    vk::Image                          m_DepthImage{};
    vk::DeviceMemory                   m_DepthMemory{};
    vk::ImageView                      m_DepthView{};
    telemetry::TrackedAllocation       m_DepthAllocation{};
    vk::Format                         m_Format{ vk::Format::eUndefined };
    std::unique_ptr<vk::ImageView[]>   m_Views{};
    std::unique_ptr<vk::Framebuffer[]> m_Framebuffers{};
    vk::Extent2D                       m_Extent{};
    u32                                m_ImageCount{};
    budget::Reservation                m_PresentationBudget{};
  };
} // namespace apollo::render::vulkan
