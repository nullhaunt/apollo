#pragma once

#if !defined( APOLLO_PLATFORM_WINDOWS )
  #error "VulkanPresenter is available only on Windows."
#endif

#include "Render/RenderTypes.hpp"

#include <memory>
#include <vulkan/vulkan.hpp>

namespace apollo::render::vulkan
{
  class VulkanContext;
  class VulkanSwapchain;

  class VulkanPresenter final
  {
  public:
    VulkanPresenter() noexcept = default;
    ~VulkanPresenter() noexcept;

    VulkanPresenter( const VulkanPresenter & )             = delete;
    VulkanPresenter & operator=( const VulkanPresenter & ) = delete;
    VulkanPresenter( VulkanPresenter && )                  = delete;
    VulkanPresenter & operator=( VulkanPresenter && )      = delete;

    [[nodiscard]] bool   Initialize( const VulkanContext & context, const VulkanSwapchain & swapchain ) noexcept;
    [[nodiscard]] Result PresentClear( ClearColor color ) noexcept;
    void                 Shutdown() noexcept;

  private:
    [[nodiscard]] bool   CreateCommands() noexcept;
    [[nodiscard]] bool   CreateSynchronization() noexcept;
    [[nodiscard]] bool   RecordClear( u32 imageIndex, ClearColor color ) noexcept;
    [[nodiscard]] Result AcquireImage( u32 & imageIndex, bool & suboptimal ) noexcept;
    [[nodiscard]] Result SubmitFrame( u32 imageIndex ) noexcept;
    [[nodiscard]] Result PresentImage( u32 imageIndex ) noexcept;

    const VulkanContext *            m_Context{};
    const VulkanSwapchain *          m_Swapchain{};
    vk::CommandPool                  m_CommandPool{};
    vk::CommandBuffer                m_CommandBuffer{};
    vk::Semaphore                    m_ImageAvailable{};
    // A present wait may outlive the frame fence; reuse each semaphore with its image.
    std::unique_ptr<vk::Semaphore[]> m_RenderFinished{};
    u32                              m_RenderFinishedCount{};
    vk::Fence                        m_FrameFence{};
    u64                              m_PresentedFrames{};
  };
} // namespace apollo::render::vulkan
