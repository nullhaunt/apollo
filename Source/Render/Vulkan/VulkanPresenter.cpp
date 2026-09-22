#include "Render/Vulkan/VulkanPresenter.hpp"

#include "Platform/Diagnostics.hpp"
#include "Render/Vulkan/VulkanBootstrap.hpp"
#include "Render/Vulkan/VulkanContext.hpp"
#include "Render/Vulkan/VulkanSwapchain.hpp"

#include <cstdio>
#include <limits>
#include <new>

namespace apollo::render::vulkan
{
  namespace
  {
    [[nodiscard]] Result MapFailure( const char * operation, vk::Result result ) noexcept
    {
      if ( result == vk::Result::eErrorOutOfDateKHR || result == vk::Result::eSuboptimalKHR )
      {
        return Result::SurfaceOutOfDate;
      }
      if ( result == vk::Result::eErrorSurfaceLostKHR )
      {
        return Result::SurfaceUnavailable;
      }
      if ( result == vk::Result::eErrorDeviceLost )
      {
        return Result::DeviceLost;
      }
      bootstrap::LogFailure( operation, result );
      return Result::Failure;
    }
  } // namespace

  VulkanPresenter::~VulkanPresenter() noexcept
  {
    Shutdown();
  }

  bool VulkanPresenter::Initialize( const VulkanContext & context, const VulkanSwapchain & swapchain ) noexcept
  {
    if ( m_Context != nullptr || !context.GetDevice() || !swapchain.GetHandle() || swapchain.GetImageCount() == 0 )
    {
      return false;
    }
    m_Context   = &context;
    m_Swapchain = &swapchain;
    if ( !CreateCommands() || !CreateSynchronization() )
    {
      Shutdown();
      return false;
    }
    return true;
  }

  bool VulkanPresenter::CreateCommands() noexcept
  {
    vk::CommandPoolCreateInfo poolInfo{};
    poolInfo.flags            = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
    poolInfo.queueFamilyIndex = m_Context->GetGraphicsFamily();

    const vk::Device device = m_Context->GetDevice();
    vk::Result       result = device.createCommandPool( &poolInfo, nullptr, &m_CommandPool );
    if ( result != vk::Result::eSuccess )
    {
      bootstrap::LogFailure( "vkCreateCommandPool", result );
      return false;
    }

    vk::CommandBufferAllocateInfo allocateInfo{};
    allocateInfo.commandPool        = m_CommandPool;
    allocateInfo.level              = vk::CommandBufferLevel::ePrimary;
    allocateInfo.commandBufferCount = 1;
    result                          = device.allocateCommandBuffers( &allocateInfo, &m_CommandBuffer );
    if ( result != vk::Result::eSuccess )
    {
      bootstrap::LogFailure( "vkAllocateCommandBuffers", result );
      return false;
    }
    return true;
  }

  bool VulkanPresenter::CreateSynchronization() noexcept
  {
    const vk::Device        device = m_Context->GetDevice();
    vk::SemaphoreCreateInfo semaphoreInfo{};
    vk::Result              result = device.createSemaphore( &semaphoreInfo, nullptr, &m_ImageAvailable );
    if ( result != vk::Result::eSuccess )
    {
      bootstrap::LogFailure( "vkCreateSemaphore", result );
      return false;
    }

    m_RenderFinishedCount = m_Swapchain->GetImageCount();
    m_RenderFinished.reset( new ( std::nothrow ) vk::Semaphore[ m_RenderFinishedCount ]{} );
    if ( !m_RenderFinished )
    {
      diagnostics::Write( diagnostics::Level::Error, "Could not allocate Vulkan present semaphore list." );
      return false;
    }
    for ( u32 index = 0; index < m_RenderFinishedCount; ++index )
    {
      result = device.createSemaphore( &semaphoreInfo, nullptr, &m_RenderFinished[ index ] );
      if ( result != vk::Result::eSuccess )
      {
        bootstrap::LogFailure( "vkCreateSemaphore", result );
        return false;
      }
    }

    vk::FenceCreateInfo fenceInfo{};
    fenceInfo.flags = vk::FenceCreateFlagBits::eSignaled;
    result          = device.createFence( &fenceInfo, nullptr, &m_FrameFence );
    if ( result != vk::Result::eSuccess )
    {
      bootstrap::LogFailure( "vkCreateFence", result );
      return false;
    }
    return true;
  }

  bool VulkanPresenter::RecordClear( u32 imageIndex, ClearColor color ) noexcept
  {
    vk::Result result = m_CommandBuffer.reset();
    if ( result != vk::Result::eSuccess )
    {
      bootstrap::LogFailure( "vkResetCommandBuffer", result );
      return false;
    }

    vk::CommandBufferBeginInfo beginInfo{};
    beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
    result          = m_CommandBuffer.begin( &beginInfo );
    if ( result != vk::Result::eSuccess )
    {
      bootstrap::LogFailure( "vkBeginCommandBuffer", result );
      return false;
    }

    vk::ClearValue clear{};
    clear.color.float32[ 0 ] = color.red;
    clear.color.float32[ 1 ] = color.green;
    clear.color.float32[ 2 ] = color.blue;
    clear.color.float32[ 3 ] = color.alpha;

    vk::RenderPassBeginInfo passInfo{};
    passInfo.renderPass        = m_Swapchain->GetRenderPass();
    passInfo.framebuffer       = m_Swapchain->GetFramebuffer( imageIndex );
    passInfo.renderArea.extent = m_Swapchain->GetExtent();
    passInfo.clearValueCount   = 1;
    passInfo.pClearValues      = &clear;
    m_CommandBuffer.beginRenderPass( &passInfo, vk::SubpassContents::eInline );
    m_CommandBuffer.endRenderPass();

    result = m_CommandBuffer.end();
    if ( result != vk::Result::eSuccess )
    {
      bootstrap::LogFailure( "vkEndCommandBuffer", result );
      return false;
    }
    return true;
  }

  Result VulkanPresenter::PresentClear( ClearColor color ) noexcept
  {
    if ( m_Context == nullptr || m_Swapchain == nullptr )
    {
      return Result::InvalidState;
    }

    u32          imageIndex{};
    bool         suboptimal{};
    const Result acquired = AcquireImage( imageIndex, suboptimal );
    if ( acquired != Result::Success )
    {
      return acquired;
    }
    if ( imageIndex >= m_RenderFinishedCount || !RecordClear( imageIndex, color ) )
    {
      return Result::Failure;
    }

    const Result submitted = SubmitFrame( imageIndex );
    if ( submitted != Result::Success )
    {
      return submitted;
    }
    const Result presented = PresentImage( imageIndex );
    return presented == Result::Success && suboptimal ? Result::SurfaceOutOfDate : presented;
  }

  Result VulkanPresenter::AcquireImage( u32 & imageIndex, bool & suboptimal ) noexcept
  {
    const vk::Device device = m_Context->GetDevice();
    vk::Result       result = device.waitForFences( 1, &m_FrameFence, VK_TRUE, std::numeric_limits<u64>::max() );
    if ( result != vk::Result::eSuccess )
    {
      return MapFailure( "vkWaitForFences", result );
    }

    result = device.acquireNextImageKHR(
      m_Swapchain->GetHandle(), std::numeric_limits<u64>::max(), m_ImageAvailable, vk::Fence{}, &imageIndex );
    if ( result == vk::Result::eErrorOutOfDateKHR )
    {
      return Result::SurfaceOutOfDate;
    }
    if ( result != vk::Result::eSuccess && result != vk::Result::eSuboptimalKHR )
    {
      return MapFailure( "vkAcquireNextImageKHR", result );
    }
    suboptimal = result == vk::Result::eSuboptimalKHR;
    return Result::Success;
  }

  Result VulkanPresenter::SubmitFrame( u32 imageIndex ) noexcept
  {
    const vk::Device device = m_Context->GetDevice();
    vk::Result       result = device.resetFences( 1, &m_FrameFence );
    if ( result != vk::Result::eSuccess )
    {
      return MapFailure( "vkResetFences", result );
    }

    const vk::PipelineStageFlags waitStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    vk::SubmitInfo               submitInfo{};
    submitInfo.waitSemaphoreCount   = 1;
    submitInfo.pWaitSemaphores      = &m_ImageAvailable;
    submitInfo.pWaitDstStageMask    = &waitStage;
    submitInfo.commandBufferCount   = 1;
    submitInfo.pCommandBuffers      = &m_CommandBuffer;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores    = &m_RenderFinished[ imageIndex ];
    result                          = m_Context->GetGraphicsQueue().submit( 1, &submitInfo, m_FrameFence );
    if ( result != vk::Result::eSuccess )
    {
      return MapFailure( "vkQueueSubmit", result );
    }
    return Result::Success;
  }

  Result VulkanPresenter::PresentImage( u32 imageIndex ) noexcept
  {
    const vk::SwapchainKHR handle = m_Swapchain->GetHandle();
    vk::PresentInfoKHR     presentInfo{};
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores    = &m_RenderFinished[ imageIndex ];
    presentInfo.swapchainCount     = 1;
    presentInfo.pSwapchains        = &handle;
    presentInfo.pImageIndices      = &imageIndex;
    const vk::Result result        = m_Context->GetPresentQueue().presentKHR( &presentInfo );
    if ( result == vk::Result::eSuccess || result == vk::Result::eSuboptimalKHR )
    {
      ++m_PresentedFrames;
    }
    if ( result != vk::Result::eSuccess )
    {
      return MapFailure( "vkQueuePresentKHR", result );
    }
    return Result::Success;
  }

  void VulkanPresenter::Shutdown() noexcept
  {
    if ( m_Context == nullptr )
    {
      return;
    }
    const vk::Device device = m_Context->GetDevice();
    if ( device )
    {
      ( void )device.waitIdle();
      if ( m_PresentedFrames != 0 )
      {
        char message[ 80 ]{};
        std::snprintf( message,
                       sizeof( message ),
                       "Vulkan clear frames presented: %llu.",
                       static_cast<unsigned long long>( m_PresentedFrames ) );
        diagnostics::Write( diagnostics::Level::Information, message );
      }
      if ( m_FrameFence )
      {
        device.destroyFence( m_FrameFence );
      }
      for ( u32 index = 0; index < m_RenderFinishedCount; ++index )
      {
        if ( m_RenderFinished && m_RenderFinished[ index ] )
        {
          device.destroySemaphore( m_RenderFinished[ index ] );
        }
      }
      if ( m_ImageAvailable )
      {
        device.destroySemaphore( m_ImageAvailable );
      }
      if ( m_CommandPool )
      {
        device.destroyCommandPool( m_CommandPool );
      }
    }
    m_FrameFence      = nullptr;
    m_PresentedFrames = 0;
    m_RenderFinished.reset();
    m_RenderFinishedCount = 0;
    m_ImageAvailable      = nullptr;
    m_CommandBuffer       = nullptr;
    m_CommandPool         = nullptr;
    m_Swapchain           = nullptr;
    m_Context             = nullptr;
  }
} // namespace apollo::render::vulkan
