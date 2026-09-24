#include "Render/Vulkan/VulkanSwapchain.hpp"

#include "Platform/Diagnostics.hpp"
#include "Render/Vulkan/VulkanBootstrap.hpp"
#include "Render/Vulkan/VulkanContext.hpp"

#include <algorithm>
#include <cstdio>
#include <limits>
#include <memory>
#include <new>

namespace apollo::render::vulkan
{
  namespace
  {
    using diagnostics::Level;

    [[nodiscard]] vk::Extent2D ChooseExtent( const vk::SurfaceCapabilitiesKHR & capabilities,
                                             Extent2D                           requested ) noexcept
    {
      if ( capabilities.currentExtent.width != std::numeric_limits<u32>::max() )
      {
        return capabilities.currentExtent;
      }

      return { std::clamp( requested.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width ),
               std::clamp( requested.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height ) };
    }

    [[nodiscard]] vk::SurfaceFormatKHR ChooseFormat( const vk::SurfaceFormatKHR * formats, u32 count ) noexcept
    {
      const vk::SurfaceFormatKHR preferred{ vk::Format::eB8G8R8A8Srgb, vk::ColorSpaceKHR::eSrgbNonlinear };
      if ( count == 1 && formats[ 0 ].format == vk::Format::eUndefined )
      {
        return preferred;
      }
      for ( u32 index = 0; index < count; ++index )
      {
        if ( formats[ index ].format == preferred.format && formats[ index ].colorSpace == preferred.colorSpace )
        {
          return formats[ index ];
        }
      }
      return formats[ 0 ];
    }

    [[nodiscard]] vk::CompositeAlphaFlagBitsKHR ChooseCompositeAlpha( vk::CompositeAlphaFlagsKHR supported ) noexcept
    {
      constexpr vk::CompositeAlphaFlagBitsKHR modes[]{ vk::CompositeAlphaFlagBitsKHR::eOpaque,
                                                       vk::CompositeAlphaFlagBitsKHR::ePreMultiplied,
                                                       vk::CompositeAlphaFlagBitsKHR::ePostMultiplied,
                                                       vk::CompositeAlphaFlagBitsKHR::eInherit };
      for ( vk::CompositeAlphaFlagBitsKHR mode : modes )
      {
        if ( static_cast<bool>( supported & mode ) )
        {
          return mode;
        }
      }
      return vk::CompositeAlphaFlagBitsKHR::eOpaque;
    }
  } // namespace

  VulkanSwapchain::~VulkanSwapchain() noexcept
  {
    Shutdown();
  }

  bool VulkanSwapchain::Initialize( const VulkanContext & context, Extent2D requestedExtent ) noexcept
  {
    if ( m_Context != nullptr || !requestedExtent.IsValid() || !context.GetDevice() )
    {
      return false;
    }

    m_Context = &context;
    if ( !Create( requestedExtent ) )
    {
      Shutdown();
      return false;
    }
    return true;
  }

  Result VulkanSwapchain::Resize( Extent2D requestedExtent ) noexcept
  {
    if ( m_Context == nullptr )
    {
      return Result::InvalidState;
    }
    if ( !requestedExtent.IsValid() )
    {
      return Result::InvalidConfiguration;
    }

    vk::SurfaceCapabilitiesKHR capabilities{};
    const vk::Result           surface =
      m_Context->GetPhysicalDevice().getSurfaceCapabilitiesKHR( m_Context->GetSurface(), &capabilities );
    if ( surface != vk::Result::eSuccess )
    {
      bootstrap::LogFailure( "vkGetPhysicalDeviceSurfaceCapabilitiesKHR", surface );
      return Result::Failure;
    }
    if ( capabilities.currentExtent.width == 0 || capabilities.currentExtent.height == 0 )
    {
      return Result::SurfaceUnavailable;
    }

    const vk::Result idle = m_Context->GetDevice().waitIdle();
    if ( idle != vk::Result::eSuccess )
    {
      bootstrap::LogFailure( "vkDeviceWaitIdle", idle );
      return Result::Failure;
    }

    DestroyResources();
    if ( !Create( requestedExtent ) )
    {
      DestroyResources();
      return Result::Failure;
    }
    return Result::Success;
  }

  bool VulkanSwapchain::Create( Extent2D requestedExtent ) noexcept
  {
    if ( !CreateSwapchain( requestedExtent ) || !CreateImageViews() || !CreateRenderPass() || !CreateDepthImage() ||
         !CreateFramebuffers() )
    {
      return false;
    }

    char message[ 96 ]{};
    std::snprintf( message,
                   sizeof( message ),
                   "Vulkan swapchain: %u x %u, %u images.",
                   m_Extent.width,
                   m_Extent.height,
                   m_ImageCount );
    diagnostics::Write( Level::Information, message );
    return true;
  }

  bool VulkanSwapchain::CreateSwapchain( Extent2D requestedExtent ) noexcept
  {
    const vk::PhysicalDevice   physicalDevice = m_Context->GetPhysicalDevice();
    const vk::SurfaceKHR       surface        = m_Context->GetSurface();
    vk::SurfaceCapabilitiesKHR capabilities{};
    vk::Result                 result = physicalDevice.getSurfaceCapabilitiesKHR( surface, &capabilities );
    if ( result != vk::Result::eSuccess )
    {
      bootstrap::LogFailure( "vkGetPhysicalDeviceSurfaceCapabilitiesKHR", result );
      return false;
    }
    if ( !static_cast<bool>( capabilities.supportedUsageFlags & vk::ImageUsageFlagBits::eColorAttachment ) )
    {
      diagnostics::Write( Level::Error, "Vulkan surface does not support color attachments." );
      return false;
    }

    u32 formatCount{};
    result = physicalDevice.getSurfaceFormatsKHR( surface, &formatCount, nullptr );
    if ( result != vk::Result::eSuccess )
    {
      bootstrap::LogFailure( "vkGetPhysicalDeviceSurfaceFormatsKHR", result );
      return false;
    }
    if ( formatCount == 0 )
    {
      diagnostics::Write( Level::Error, "Vulkan surface reported no formats." );
      return false;
    }
    std::unique_ptr<vk::SurfaceFormatKHR[]> formats( new ( std::nothrow ) vk::SurfaceFormatKHR[ formatCount ]{} );
    if ( !formats )
    {
      diagnostics::Write( Level::Error, "Could not allocate Vulkan surface format list." );
      return false;
    }
    result = physicalDevice.getSurfaceFormatsKHR( surface, &formatCount, formats.get() );
    if ( result != vk::Result::eSuccess )
    {
      bootstrap::LogFailure( "vkGetPhysicalDeviceSurfaceFormatsKHR", result );
      return false;
    }

    const vk::SurfaceFormatKHR selectedFormat = ChooseFormat( formats.get(), formatCount );
    const vk::Extent2D         selectedExtent = ChooseExtent( capabilities, requestedExtent );
    if ( selectedExtent.width == 0 || selectedExtent.height == 0 )
    {
      diagnostics::Write( Level::Warning, "Vulkan surface has no drawable extent." );
      return false;
    }
    size_t logicalBytes{};
    if ( !budget::Rgba8Footprint( selectedExtent.width, selectedExtent.height, 3, logicalBytes ) ||
         !m_PresentationBudget.Acquire( budget::Resource::Presentation, logicalBytes ) )
    {
      diagnostics::Write( Level::Warning, "Vulkan presentation request exceeded the logical renderer budget." );
      return false;
    }
    u32 imageCount = capabilities.minImageCount;
    if ( imageCount < std::numeric_limits<u32>::max() &&
         ( capabilities.maxImageCount == 0 || imageCount < capabilities.maxImageCount ) )
    {
      ++imageCount;
    }

    const u32                  families[]{ m_Context->GetGraphicsFamily(), m_Context->GetPresentFamily() };
    vk::SwapchainCreateInfoKHR createInfo{};
    createInfo.surface          = surface;
    createInfo.minImageCount    = imageCount;
    createInfo.imageFormat      = selectedFormat.format;
    createInfo.imageColorSpace  = selectedFormat.colorSpace;
    createInfo.imageExtent      = selectedExtent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage       = vk::ImageUsageFlagBits::eColorAttachment;
    createInfo.imageSharingMode =
      families[ 0 ] == families[ 1 ] ? vk::SharingMode::eExclusive : vk::SharingMode::eConcurrent;
    createInfo.queueFamilyIndexCount = families[ 0 ] == families[ 1 ] ? 0u : 2u;
    createInfo.pQueueFamilyIndices   = families[ 0 ] == families[ 1 ] ? nullptr : families;
    createInfo.preTransform          = capabilities.currentTransform;
    createInfo.compositeAlpha        = ChooseCompositeAlpha( capabilities.supportedCompositeAlpha );
    createInfo.presentMode           = vk::PresentModeKHR::eFifo;
    createInfo.clipped               = VK_TRUE;

    result = m_Context->GetDevice().createSwapchainKHR( &createInfo, nullptr, &m_Swapchain );
    if ( result != vk::Result::eSuccess )
    {
      bootstrap::LogFailure( "vkCreateSwapchainKHR", result );
      return false;
    }
    m_Format = selectedFormat.format;
    m_Extent = selectedExtent;
    return true;
  }

  bool VulkanSwapchain::CreateImageViews() noexcept
  {
    const vk::Device device = m_Context->GetDevice();
    vk::Result       result = device.getSwapchainImagesKHR( m_Swapchain, &m_ImageCount, nullptr );
    if ( result != vk::Result::eSuccess )
    {
      bootstrap::LogFailure( "vkGetSwapchainImagesKHR", result );
      return false;
    }
    if ( m_ImageCount == 0 )
    {
      diagnostics::Write( Level::Error, "Vulkan swapchain reported no images." );
      return false;
    }

    std::unique_ptr<vk::Image[]> images( new ( std::nothrow ) vk::Image[ m_ImageCount ]{} );
    m_Views.reset( new ( std::nothrow ) vk::ImageView[ m_ImageCount ]{} );
    if ( !images || !m_Views )
    {
      diagnostics::Write( Level::Error, "Could not allocate Vulkan swapchain image list." );
      return false;
    }
    result = device.getSwapchainImagesKHR( m_Swapchain, &m_ImageCount, images.get() );
    if ( result != vk::Result::eSuccess )
    {
      bootstrap::LogFailure( "vkGetSwapchainImagesKHR", result );
      return false;
    }
    if ( m_ImageCount == 0 )
    {
      diagnostics::Write( Level::Error, "Vulkan swapchain image list became empty." );
      return false;
    }

    for ( u32 index = 0; index < m_ImageCount; ++index )
    {
      vk::ImageViewCreateInfo info{};
      info.image                       = images[ index ];
      info.viewType                    = vk::ImageViewType::e2D;
      info.format                      = m_Format;
      info.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
      info.subresourceRange.levelCount = 1;
      info.subresourceRange.layerCount = 1;
      result                           = device.createImageView( &info, nullptr, &m_Views[ index ] );
      if ( result != vk::Result::eSuccess )
      {
        bootstrap::LogFailure( "vkCreateImageView", result );
        return false;
      }
    }
    return true;
  }

  bool VulkanSwapchain::CreateRenderPass() noexcept
  {
    vk::AttachmentDescription color{};
    color.format  = m_Format;
    color.samples = vk::SampleCountFlagBits::e1;
    // Apollo clears explicitly inside the pass so the GPU timestamp after
    // the clear has a well-defined operation to measure.
    color.loadOp         = vk::AttachmentLoadOp::eDontCare;
    color.storeOp        = vk::AttachmentStoreOp::eStore;
    color.stencilLoadOp  = vk::AttachmentLoadOp::eDontCare;
    color.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    color.initialLayout  = vk::ImageLayout::eUndefined;
    color.finalLayout    = vk::ImageLayout::ePresentSrcKHR;

    vk::AttachmentReference colorReference{};
    colorReference.attachment = 0;
    colorReference.layout     = vk::ImageLayout::eColorAttachmentOptimal;

    vk::AttachmentDescription depth{};
    depth.format         = vk::Format::eD32Sfloat;
    depth.samples        = vk::SampleCountFlagBits::e1;
    depth.loadOp         = vk::AttachmentLoadOp::eClear;
    depth.storeOp        = vk::AttachmentStoreOp::eDontCare;
    depth.stencilLoadOp  = vk::AttachmentLoadOp::eDontCare;
    depth.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    depth.initialLayout  = vk::ImageLayout::eUndefined;
    depth.finalLayout    = vk::ImageLayout::eDepthStencilAttachmentOptimal;

    vk::AttachmentReference depthReference{};
    depthReference.attachment = 1;
    depthReference.layout     = vk::ImageLayout::eDepthStencilAttachmentOptimal;

    vk::SubpassDescription subpass{};
    subpass.pipelineBindPoint       = vk::PipelineBindPoint::eGraphics;
    subpass.colorAttachmentCount    = 1;
    subpass.pColorAttachments       = &colorReference;
    subpass.pDepthStencilAttachment = &depthReference;

    vk::SubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask =
      vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests;
    dependency.dstStageMask = dependency.srcStageMask;
    dependency.dstAccessMask =
      vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite;

    const vk::AttachmentDescription attachments[]{ color, depth };

    vk::RenderPassCreateInfo info{};
    info.attachmentCount = 2;
    info.pAttachments    = attachments;
    info.subpassCount    = 1;
    info.pSubpasses      = &subpass;
    info.dependencyCount = 1;
    info.pDependencies   = &dependency;

    const vk::Result result = m_Context->GetDevice().createRenderPass( &info, nullptr, &m_RenderPass );
    if ( result != vk::Result::eSuccess )
    {
      bootstrap::LogFailure( "vkCreateRenderPass", result );
      return false;
    }
    return true;
  }

  bool VulkanSwapchain::CreateDepthImage() noexcept
  {
    vk::FormatProperties formatProperties{};
    m_Context->GetPhysicalDevice().getFormatProperties( vk::Format::eD32Sfloat, &formatProperties );
    if ( !( formatProperties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment ) )
    {
      diagnostics::Write( Level::Error, "Vulkan D32 depth attachments are unavailable." );
      return false;
    }

    const vk::Device    device = m_Context->GetDevice();
    vk::ImageCreateInfo imageInfo{};
    imageInfo.imageType     = vk::ImageType::e2D;
    imageInfo.format        = vk::Format::eD32Sfloat;
    imageInfo.extent        = vk::Extent3D{ m_Extent.width, m_Extent.height, 1 };
    imageInfo.mipLevels     = 1;
    imageInfo.arrayLayers   = 1;
    imageInfo.samples       = vk::SampleCountFlagBits::e1;
    imageInfo.tiling        = vk::ImageTiling::eOptimal;
    imageInfo.usage         = vk::ImageUsageFlagBits::eDepthStencilAttachment;
    imageInfo.initialLayout = vk::ImageLayout::eUndefined;

    vk::Result result = device.createImage( &imageInfo, nullptr, &m_DepthImage );
    if ( result != vk::Result::eSuccess )
    {
      bootstrap::LogFailure( "vkCreateImage(depth)", result );
      return false;
    }

    vk::MemoryRequirements requirements{};
    device.getImageMemoryRequirements( m_DepthImage, &requirements );
    vk::PhysicalDeviceMemoryProperties properties{};
    m_Context->GetPhysicalDevice().getMemoryProperties( &properties );
    u32 memoryType = properties.memoryTypeCount;
    for ( u32 index = 0; index < properties.memoryTypeCount; ++index )
    {
      if ( ( requirements.memoryTypeBits & ( 1u << index ) ) != 0 &&
           ( properties.memoryTypes[ index ].propertyFlags & vk::MemoryPropertyFlagBits::eDeviceLocal ) )
      {
        memoryType = index;
        break;
      }
    }
    if ( memoryType == properties.memoryTypeCount )
    {
      diagnostics::Write( Level::Error, "No device-local Vulkan depth memory type." );
      return false;
    }

    vk::MemoryAllocateInfo allocation{};
    allocation.allocationSize  = requirements.size;
    allocation.memoryTypeIndex = memoryType;
    result                     = device.allocateMemory( &allocation, nullptr, &m_DepthMemory );
    if ( result != vk::Result::eSuccess )
    {
      bootstrap::LogFailure( "vkAllocateMemory(depth)", result );
      return false;
    }
    m_DepthAllocation.Acquire( static_cast<size_t>( allocation.allocationSize ) );

    result = device.bindImageMemory( m_DepthImage, m_DepthMemory, 0 );
    if ( result != vk::Result::eSuccess )
    {
      bootstrap::LogFailure( "vkBindImageMemory(depth)", result );
      return false;
    }

    vk::ImageViewCreateInfo viewInfo{};
    viewInfo.image                       = m_DepthImage;
    viewInfo.viewType                    = vk::ImageViewType::e2D;
    viewInfo.format                      = imageInfo.format;
    viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    result                               = device.createImageView( &viewInfo, nullptr, &m_DepthView );
    if ( result != vk::Result::eSuccess )
    {
      bootstrap::LogFailure( "vkCreateImageView(depth)", result );
      return false;
    }
    return true;
  }

  bool VulkanSwapchain::CreateFramebuffers() noexcept
  {
    m_Framebuffers.reset( new ( std::nothrow ) vk::Framebuffer[ m_ImageCount ]{} );
    if ( !m_Framebuffers )
    {
      diagnostics::Write( Level::Error, "Could not allocate Vulkan framebuffer list." );
      return false;
    }

    const vk::Device device = m_Context->GetDevice();
    for ( u32 index = 0; index < m_ImageCount; ++index )
    {
      const vk::ImageView       attachments[]{ m_Views[ index ], m_DepthView };
      vk::FramebufferCreateInfo info{};
      info.renderPass      = m_RenderPass;
      info.attachmentCount = 2;
      info.pAttachments    = attachments;
      info.width           = m_Extent.width;
      info.height          = m_Extent.height;
      info.layers          = 1;

      const vk::Result result = device.createFramebuffer( &info, nullptr, &m_Framebuffers[ index ] );
      if ( result != vk::Result::eSuccess )
      {
        bootstrap::LogFailure( "vkCreateFramebuffer", result );
        return false;
      }
    }
    return true;
  }

  void VulkanSwapchain::DestroyResources() noexcept
  {
    const vk::Device device = m_Context->GetDevice();
    for ( u32 index = 0; index < m_ImageCount; ++index )
    {
      if ( m_Framebuffers && m_Framebuffers[ index ] )
      {
        device.destroyFramebuffer( m_Framebuffers[ index ] );
      }
    }
    m_Framebuffers.reset();

    if ( m_DepthView )
    {
      device.destroyImageView( m_DepthView );
      m_DepthView = nullptr;
    }
    if ( m_DepthImage )
    {
      device.destroyImage( m_DepthImage );
      m_DepthImage = nullptr;
    }
    if ( m_DepthMemory )
    {
      device.freeMemory( m_DepthMemory );
      m_DepthMemory = nullptr;
    }
    m_DepthAllocation.Release();

    if ( m_RenderPass )
    {
      device.destroyRenderPass( m_RenderPass );
      m_RenderPass = nullptr;
    }
    for ( u32 index = 0; index < m_ImageCount; ++index )
    {
      if ( m_Views && m_Views[ index ] )
      {
        device.destroyImageView( m_Views[ index ] );
      }
    }
    m_Views.reset();

    if ( m_Swapchain )
    {
      device.destroySwapchainKHR( m_Swapchain );
      m_Swapchain = nullptr;
    }
    m_PresentationBudget.Release();
    m_Format     = vk::Format::eUndefined;
    m_Extent     = vk::Extent2D{};
    m_ImageCount = 0;
  }

  void VulkanSwapchain::Shutdown() noexcept
  {
    if ( m_Context == nullptr )
    {
      return;
    }
    if ( m_Context->GetDevice() )
    {
      ( void )m_Context->GetDevice().waitIdle();
      DestroyResources();
    }
    m_PresentationBudget.Release();
    m_Context = nullptr;
  }

  vk::SwapchainKHR VulkanSwapchain::GetHandle() const noexcept
  {
    return m_Swapchain;
  }

  vk::RenderPass VulkanSwapchain::GetRenderPass() const noexcept
  {
    return m_RenderPass;
  }

  vk::Framebuffer VulkanSwapchain::GetFramebuffer( u32 imageIndex ) const noexcept
  {
    return imageIndex < m_ImageCount && m_Framebuffers ? m_Framebuffers[ imageIndex ] : vk::Framebuffer{};
  }

  vk::Extent2D VulkanSwapchain::GetExtent() const noexcept
  {
    return m_Extent;
  }

  u32 VulkanSwapchain::GetImageCount() const noexcept
  {
    return m_ImageCount;
  }
} // namespace apollo::render::vulkan
