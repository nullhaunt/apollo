#include "Render/Vulkan/VulkanPresenter.hpp"

#include "IndexedQuadShader.hpp"
#include "Platform/Diagnostics.hpp"
#include "Render/IndexedQuad.hpp"
#include "Render/IndexedQuadTexture.hpp"
#include "Render/Vulkan/VulkanBootstrap.hpp"
#include "Render/Vulkan/VulkanContext.hpp"
#include "Render/Vulkan/VulkanSwapchain.hpp"

#if !defined( APOLLO_BUILD_RELEASE )
  #include <backends/imgui_impl_vulkan.h>
  #include <imgui.h>
#endif

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <iterator>
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
    if ( !CreateCommands() || !CreateSynchronization() || !CreateGeometry() ||
         !CreateTexture( IndexedQuadImage, IndexedQuadSampler ) || !CreatePipeline() )
    {
      Shutdown();
      return false;
    }
#if !defined( APOLLO_BUILD_RELEASE )
    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion                   = VK_API_VERSION_1_0;
    info.Instance                     = static_cast<VkInstance>( m_Context->GetInstance() );
    info.PhysicalDevice               = static_cast<VkPhysicalDevice>( m_Context->GetPhysicalDevice() );
    info.Device                       = static_cast<VkDevice>( m_Context->GetDevice() );
    info.QueueFamily                  = m_Context->GetGraphicsFamily();
    info.Queue                        = static_cast<VkQueue>( m_Context->GetGraphicsQueue() );
    info.DescriptorPoolSize           = 64;
    info.MinImageCount                = 2;
    info.ImageCount                   = m_Swapchain->GetImageCount();
    info.PipelineInfoMain.RenderPass  = static_cast<VkRenderPass>( m_Swapchain->GetRenderPass() );
    info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    if ( !ImGui_ImplVulkan_Init( &info ) )
    {
      diagnostics::Write( diagnostics::Level::Error, "ImGui Vulkan backend initialization failed." );
      Shutdown();
      return false;
    }
    m_ImGuiReady = true;
#endif
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
#if !defined( APOLLO_BUILD_RELEASE )
    const auto queues = m_Context->GetPhysicalDevice().getQueueFamilyProperties();
    if ( m_Context->GetGraphicsFamily() < queues.size() &&
         queues[ m_Context->GetGraphicsFamily() ].timestampValidBits != 0 )
    {
      vk::PhysicalDeviceProperties properties{};
      m_Context->GetPhysicalDevice().getProperties( &properties );
      m_TimestampPeriodNs = properties.limits.timestampPeriod;
      const u32 validBits = queues[ m_Context->GetGraphicsFamily() ].timestampValidBits;
      m_TimestampMask     = validBits == 64 ? std::numeric_limits<u64>::max() : ( u64{ 1 } << validBits ) - 1;
      vk::QueryPoolCreateInfo queryInfo{};
      queryInfo.queryType  = vk::QueryType::eTimestamp;
      queryInfo.queryCount = 4;
      result               = device.createQueryPool( &queryInfo, nullptr, &m_TimestampQueries );
      if ( result != vk::Result::eSuccess )
      {
        bootstrap::LogFailure( "vkCreateQueryPool", result );
        m_TimestampPeriodNs = 0.0;
        m_TimestampMask     = 0;
      }
    }
    telemetry::SetGpuTimingAvailable( static_cast<bool>( m_TimestampQueries ) );
#endif
    return true;
  }

#if !defined( APOLLO_BUILD_RELEASE )
  void VulkanPresenter::ReadGpuTimings() noexcept
  {
    if ( !m_TimestampPending || !m_TimestampQueries )
    {
      return;
    }
    u64              ticks[ 4 ]{};
    const vk::Result result = m_Context->GetDevice().getQueryPoolResults(
      m_TimestampQueries, 0, 4, sizeof( ticks ), ticks, sizeof( u64 ), vk::QueryResultFlagBits::e64 );
    m_TimestampPending = false;
    if ( result != vk::Result::eSuccess )
    {
      telemetry::InvalidateGpuTimings();
      return;
    }
    constexpr double NanosecondsToMilliseconds = 1.0 / 1000000.0;
    const double     scale                     = m_TimestampPeriodNs * NanosecondsToMilliseconds;
    const double     clear                     = ( ( ticks[ 1 ] - ticks[ 0 ] ) & m_TimestampMask ) * scale;
    const double     quad                      = ( ( ticks[ 2 ] - ticks[ 1 ] ) & m_TimestampMask ) * scale;
    const double     ui                        = ( ( ticks[ 3 ] - ticks[ 2 ] ) & m_TimestampMask ) * scale;
    telemetry::SetGpuTimings( { true, clear, quad, ui, clear + quad + ui } );
    if ( !m_FirstTimingReported )
    {
      char message[ 128 ]{};
      std::snprintf(
        message, sizeof( message ), "Vulkan GPU timestamps active: %.3f ms first frame.", clear + quad + ui );
      diagnostics::Write( diagnostics::Level::Information, message );
      m_FirstTimingReported = true;
    }
  }
#endif

  bool VulkanPresenter::CreateHostBuffer( vk::DeviceSize                 size,
                                          vk::BufferUsageFlags           usage,
                                          const void *                   data,
                                          vk::Buffer &                   buffer,
                                          vk::DeviceMemory &             memory,
                                          telemetry::TrackedAllocation & tracked ) noexcept
  {
    const vk::Device     device = m_Context->GetDevice();
    vk::BufferCreateInfo bufferInfo{};
    bufferInfo.size   = size;
    bufferInfo.usage  = usage;
    vk::Result result = device.createBuffer( &bufferInfo, nullptr, &buffer );
    if ( result != vk::Result::eSuccess )
    {
      bootstrap::LogFailure( "vkCreateBuffer", result );
      return false;
    }

    vk::MemoryRequirements requirements{};
    device.getBufferMemoryRequirements( buffer, &requirements );
    vk::PhysicalDeviceMemoryProperties properties{};
    m_Context->GetPhysicalDevice().getMemoryProperties( &properties );
    u32 memoryType = properties.memoryTypeCount;
    for ( u32 index = 0; index < properties.memoryTypeCount; ++index )
    {
      const auto flags = properties.memoryTypes[ index ].propertyFlags;
      if ( ( requirements.memoryTypeBits & ( 1u << index ) ) != 0 &&
           ( flags & ( vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent ) ) ==
             ( vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent ) )
      {
        memoryType = index;
        break;
      }
    }
    if ( memoryType == properties.memoryTypeCount )
    {
      diagnostics::Write( diagnostics::Level::Error, "No host-visible coherent Vulkan buffer memory type." );
      return false;
    }

    vk::MemoryAllocateInfo allocation{};
    allocation.allocationSize  = requirements.size;
    allocation.memoryTypeIndex = memoryType;
    result                     = device.allocateMemory( &allocation, nullptr, &memory );
    if ( result != vk::Result::eSuccess )
    {
      bootstrap::LogFailure( "vkAllocateMemory", result );
      return false;
    }
    tracked.Acquire( static_cast<size_t>( allocation.allocationSize ) );
    result = device.bindBufferMemory( buffer, memory, 0 );
    if ( result != vk::Result::eSuccess )
    {
      bootstrap::LogFailure( "vkBindBufferMemory", result );
      return false;
    }

    void * mapped{};
    result = device.mapMemory( memory, 0, size, {}, &mapped );
    if ( result != vk::Result::eSuccess )
    {
      bootstrap::LogFailure( "vkMapMemory", result );
      return false;
    }
    std::memcpy( mapped, data, static_cast<size_t>( size ) );
    device.unmapMemory( memory );
    return true;
  }

  bool VulkanPresenter::CreateGeometry() noexcept
  {
    if ( !m_GeometryBudget.Acquire( budget::Resource::Geometry,
                                    sizeof( IndexedQuadVertices ) + sizeof( IndexedQuadIndices ) ) )
    {
      return false;
    }
    return CreateHostBuffer( sizeof( IndexedQuadVertices ),
                             vk::BufferUsageFlagBits::eVertexBuffer,
                             IndexedQuadVertices,
                             m_VertexBuffer,
                             m_VertexMemory,
                             m_VertexAllocation ) &&
           CreateHostBuffer( sizeof( IndexedQuadIndices ),
                             vk::BufferUsageFlagBits::eIndexBuffer,
                             IndexedQuadIndices,
                             m_IndexBuffer,
                             m_IndexMemory,
                             m_IndexAllocation );
  }

  bool VulkanPresenter::CreateTexture( Rgba8ImageView image, TextureSamplerDesc sampling ) noexcept
  {
    if ( !image.IsValid() )
    {
      diagnostics::Write( diagnostics::Level::Error, "Invalid RGBA8 texture data." );
      return false;
    }
    size_t textureBytes{};
    if ( !budget::Rgba8Footprint( image.width, image.height, 1, textureBytes ) ||
         !m_TextureBudget.Acquire( budget::Resource::Texture, textureBytes ) ||
         !m_UploadBudget.Acquire( budget::Resource::Upload, image.byteCount ) )
    {
      return false;
    }
    const vk::Device device = m_Context->GetDevice();
    if ( !CreateHostBuffer( image.byteCount,
                            vk::BufferUsageFlagBits::eTransferSrc,
                            image.pixels,
                            m_TextureStagingBuffer,
                            m_TextureStagingMemory,
                            m_StagingAllocation ) )
    {
      return false;
    }

    vk::ImageCreateInfo imageInfo{};
    imageInfo.imageType     = vk::ImageType::e2D;
    imageInfo.format        = vk::Format::eR8G8B8A8Unorm;
    imageInfo.extent        = vk::Extent3D{ image.width, image.height, 1 };
    imageInfo.mipLevels     = 1;
    imageInfo.arrayLayers   = 1;
    imageInfo.samples       = vk::SampleCountFlagBits::e1;
    imageInfo.tiling        = vk::ImageTiling::eOptimal;
    imageInfo.usage         = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
    imageInfo.initialLayout = vk::ImageLayout::eUndefined;
    vk::Result result       = device.createImage( &imageInfo, nullptr, &m_TextureImage );
    if ( result != vk::Result::eSuccess )
    {
      bootstrap::LogFailure( "vkCreateImage(texture)", result );
      return false;
    }

    vk::MemoryRequirements requirements{};
    device.getImageMemoryRequirements( m_TextureImage, &requirements );
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
      diagnostics::Write( diagnostics::Level::Error, "No device-local Vulkan image memory type." );
      return false;
    }

    vk::MemoryAllocateInfo allocation{};
    allocation.allocationSize  = requirements.size;
    allocation.memoryTypeIndex = memoryType;
    result                     = device.allocateMemory( &allocation, nullptr, &m_TextureMemory );
    if ( result != vk::Result::eSuccess )
    {
      bootstrap::LogFailure( "vkAllocateMemory(texture)", result );
      return false;
    }

    m_TextureAllocation.Acquire( static_cast<size_t>( allocation.allocationSize ) );
    result = device.bindImageMemory( m_TextureImage, m_TextureMemory, 0 );
    if ( result != vk::Result::eSuccess )
    {
      bootstrap::LogFailure( "vkBindImageMemory(texture)", result );
      return false;
    }

    vk::CommandBufferBeginInfo begin{};
    begin.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
    result      = m_CommandBuffer.begin( &begin );
    if ( result != vk::Result::eSuccess )
    {
      bootstrap::LogFailure( "vkBeginCommandBuffer(texture)", result );
      return false;
    }

    vk::ImageMemoryBarrier barrier{};
    barrier.oldLayout                   = vk::ImageLayout::eUndefined;
    barrier.newLayout                   = vk::ImageLayout::eTransferDstOptimal;
    barrier.srcQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
    barrier.image                       = m_TextureImage;
    barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    barrier.dstAccessMask               = vk::AccessFlagBits::eTransferWrite;
    m_CommandBuffer.pipelineBarrier( vk::PipelineStageFlagBits::eTopOfPipe,
                                     vk::PipelineStageFlagBits::eTransfer,
                                     {},
                                     0,
                                     nullptr,
                                     0,
                                     nullptr,
                                     1,
                                     &barrier );

    vk::BufferImageCopy copy{};
    copy.bufferRowLength             = image.rowPitchBytes / 4;
    copy.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent                 = imageInfo.extent;
    m_CommandBuffer.copyBufferToImage(
      m_TextureStagingBuffer, m_TextureImage, vk::ImageLayout::eTransferDstOptimal, 1, &copy );

    barrier.oldLayout     = vk::ImageLayout::eTransferDstOptimal;
    barrier.newLayout     = vk::ImageLayout::eShaderReadOnlyOptimal;
    barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
    barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
    m_CommandBuffer.pipelineBarrier( vk::PipelineStageFlagBits::eTransfer,
                                     vk::PipelineStageFlagBits::eFragmentShader,
                                     {},
                                     0,
                                     nullptr,
                                     0,
                                     nullptr,
                                     1,
                                     &barrier );

    result = m_CommandBuffer.end();
    if ( result != vk::Result::eSuccess )
    {
      bootstrap::LogFailure( "vkEndCommandBuffer(texture)", result );
      return false;
    }

    vk::SubmitInfo submit{};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers    = &m_CommandBuffer;
    result                    = m_Context->GetGraphicsQueue().submit( 1, &submit, {} );
    if ( result == vk::Result::eSuccess )
    {
      result = m_Context->GetGraphicsQueue().waitIdle();
    }
    if ( result != vk::Result::eSuccess )
    {
      bootstrap::LogFailure( "Vulkan texture upload", result );
      return false;
    }

    device.destroyBuffer( m_TextureStagingBuffer );
    device.freeMemory( m_TextureStagingMemory );
    m_StagingAllocation.Release();
    m_UploadBudget.Release();
    m_TextureStagingBuffer = nullptr;
    m_TextureStagingMemory = nullptr;

    vk::ImageViewCreateInfo viewInfo{};
    viewInfo.image            = m_TextureImage;
    viewInfo.viewType         = vk::ImageViewType::e2D;
    viewInfo.format           = imageInfo.format;
    viewInfo.subresourceRange = barrier.subresourceRange;
    result                    = device.createImageView( &viewInfo, nullptr, &m_TextureView );
    if ( result != vk::Result::eSuccess )
    {
      bootstrap::LogFailure( "vkCreateImageView(texture)", result );
      return false;
    }

    vk::SamplerCreateInfo samplerInfo{};
    const vk::Filter filter = sampling.filter == TextureFilter::Linear ? vk::Filter::eLinear : vk::Filter::eNearest;
    const vk::SamplerAddressMode addressMode = sampling.addressMode == TextureAddressMode::Repeat
                                                 ? vk::SamplerAddressMode::eRepeat
                                                 : vk::SamplerAddressMode::eClampToEdge;
    samplerInfo.magFilter                    = filter;
    samplerInfo.minFilter                    = filter;
    samplerInfo.mipmapMode                   = vk::SamplerMipmapMode::eNearest;
    samplerInfo.addressModeU                 = addressMode;
    samplerInfo.addressModeV                 = addressMode;
    samplerInfo.addressModeW                 = addressMode;
    samplerInfo.maxLod                       = 0.0f;
    result                                   = device.createSampler( &samplerInfo, nullptr, &m_TextureSampler );
    if ( result != vk::Result::eSuccess )
    {
      bootstrap::LogFailure( "vkCreateSampler(texture)", result );
      return false;
    }

    vk::DescriptorSetLayoutBinding bindings[ 2 ]{};
    bindings[ 0 ].binding         = 0;
    bindings[ 0 ].descriptorType  = vk::DescriptorType::eSampledImage;
    bindings[ 0 ].descriptorCount = 1;
    bindings[ 0 ].stageFlags      = vk::ShaderStageFlagBits::eFragment;
    bindings[ 1 ].binding         = 1;
    bindings[ 1 ].descriptorType  = vk::DescriptorType::eSampler;
    bindings[ 1 ].descriptorCount = 1;
    bindings[ 1 ].stageFlags      = vk::ShaderStageFlagBits::eFragment;

    vk::DescriptorSetLayoutCreateInfo setInfo{};
    setInfo.bindingCount = 2;
    setInfo.pBindings    = bindings;
    result               = device.createDescriptorSetLayout( &setInfo, nullptr, &m_TextureSetLayout );
    if ( result != vk::Result::eSuccess )
    {
      bootstrap::LogFailure( "vkCreateDescriptorSetLayout", result );
      return false;
    }

    vk::DescriptorPoolSize poolSizes[]{ { vk::DescriptorType::eSampledImage, 1 }, { vk::DescriptorType::eSampler, 1 } };
    vk::DescriptorPoolCreateInfo poolInfo{};
    poolInfo.maxSets       = 1;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes    = poolSizes;
    result                 = device.createDescriptorPool( &poolInfo, nullptr, &m_DescriptorPool );
    if ( result != vk::Result::eSuccess )
    {
      bootstrap::LogFailure( "vkCreateDescriptorPool", result );
      return false;
    }

    vk::DescriptorSetAllocateInfo setAllocation{};
    setAllocation.descriptorPool     = m_DescriptorPool;
    setAllocation.descriptorSetCount = 1;
    setAllocation.pSetLayouts        = &m_TextureSetLayout;
    result                           = device.allocateDescriptorSets( &setAllocation, &m_TextureSet );
    if ( result != vk::Result::eSuccess )
    {
      bootstrap::LogFailure( "vkAllocateDescriptorSets", result );
      return false;
    }

    vk::DescriptorImageInfo descriptorImage{ m_TextureSampler, m_TextureView, vk::ImageLayout::eShaderReadOnlyOptimal };
    vk::WriteDescriptorSet  writes[ 2 ]{};
    writes[ 0 ].dstSet          = m_TextureSet;
    writes[ 0 ].dstBinding      = 0;
    writes[ 0 ].descriptorCount = 1;
    writes[ 0 ].descriptorType  = vk::DescriptorType::eSampledImage;
    writes[ 0 ].pImageInfo      = &descriptorImage;
    writes[ 1 ].dstSet          = m_TextureSet;
    writes[ 1 ].dstBinding      = 1;
    writes[ 1 ].descriptorCount = 1;
    writes[ 1 ].descriptorType  = vk::DescriptorType::eSampler;
    writes[ 1 ].pImageInfo      = &descriptorImage;

    device.updateDescriptorSets( 2, writes, 0, nullptr );
    return true;
  }

  bool VulkanPresenter::CreatePipeline() noexcept
  {
    const vk::Device           device = m_Context->GetDevice();
    vk::ShaderModuleCreateInfo shaderInfo{};
    shaderInfo.codeSize = sizeof( generated::IndexedQuadVsSpirv );
    shaderInfo.pCode    = reinterpret_cast<const u32 *>( generated::IndexedQuadVsSpirv );
    vk::Result result   = device.createShaderModule( &shaderInfo, nullptr, &m_VertexShader );
    if ( result != vk::Result::eSuccess )
    {
      bootstrap::LogFailure( "vkCreateShaderModule(vertex)", result );
      return false;
    }
    shaderInfo.codeSize = sizeof( generated::IndexedQuadPsSpirv );
    shaderInfo.pCode    = reinterpret_cast<const u32 *>( generated::IndexedQuadPsSpirv );
    result              = device.createShaderModule( &shaderInfo, nullptr, &m_FragmentShader );
    if ( result != vk::Result::eSuccess )
    {
      bootstrap::LogFailure( "vkCreateShaderModule(fragment)", result );
      return false;
    }

    const vk::PipelineShaderStageCreateInfo stages[]{
      { {}, vk::ShaderStageFlagBits::eVertex, m_VertexShader, "VSMain" },
      { {}, vk::ShaderStageFlagBits::eFragment, m_FragmentShader, "PSMain" },
    };
    const vk::VertexInputBindingDescription   binding{ 0, sizeof( IndexedQuadVertex ), vk::VertexInputRate::eVertex };
    const vk::VertexInputAttributeDescription attributes[]{
      { 0, 0, vk::Format::eR32G32Sfloat, static_cast<u32>( offsetof( IndexedQuadVertex, position ) ) },
      { 1, 0, vk::Format::eR32G32B32Sfloat, static_cast<u32>( offsetof( IndexedQuadVertex, color ) ) },
      { 2, 0, vk::Format::eR32G32Sfloat, static_cast<u32>( offsetof( IndexedQuadVertex, uv ) ) },
    };
    vk::PipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.vertexBindingDescriptionCount   = 1;
    vertexInput.pVertexBindingDescriptions      = &binding;
    vertexInput.vertexAttributeDescriptionCount = 3;
    vertexInput.pVertexAttributeDescriptions    = attributes;

    vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;
    vk::PipelineViewportStateCreateInfo viewport{};
    viewport.viewportCount = 1;
    viewport.scissorCount  = 1;
    vk::PipelineRasterizationStateCreateInfo raster{};
    raster.polygonMode = vk::PolygonMode::eFill;
    raster.cullMode    = vk::CullModeFlagBits::eNone;
    raster.frontFace   = vk::FrontFace::eCounterClockwise;
    raster.lineWidth   = 1.0f;
    vk::PipelineMultisampleStateCreateInfo multisample{};
    multisample.rasterizationSamples = vk::SampleCountFlagBits::e1;
    vk::PipelineColorBlendAttachmentState attachment{};
    attachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                                vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
    vk::PipelineColorBlendStateCreateInfo blend{};
    blend.attachmentCount = 1;
    blend.pAttachments    = &attachment;
    const vk::DynamicState             dynamicStates[]{ vk::DynamicState::eViewport, vk::DynamicState::eScissor };
    vk::PipelineDynamicStateCreateInfo dynamic{};
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates    = dynamicStates;

    vk::PipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts    = &m_TextureSetLayout;
    result                    = device.createPipelineLayout( &layoutInfo, nullptr, &m_PipelineLayout );
    if ( result != vk::Result::eSuccess )
    {
      bootstrap::LogFailure( "vkCreatePipelineLayout", result );
      return false;
    }
    vk::GraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.stageCount          = 2;
    pipelineInfo.pStages             = stages;
    pipelineInfo.pVertexInputState   = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState      = &viewport;
    pipelineInfo.pRasterizationState = &raster;
    pipelineInfo.pMultisampleState   = &multisample;
    pipelineInfo.pColorBlendState    = &blend;
    pipelineInfo.pDynamicState       = &dynamic;
    pipelineInfo.layout              = m_PipelineLayout;
    pipelineInfo.renderPass          = m_Swapchain->GetRenderPass();
    result                           = device.createGraphicsPipelines( {}, 1, &pipelineInfo, nullptr, &m_Pipeline );
    if ( result != vk::Result::eSuccess )
    {
      bootstrap::LogFailure( "vkCreateGraphicsPipelines", result );
      return false;
    }
    return true;
  }

  bool VulkanPresenter::RecordFrame( u32 imageIndex, ClearColor color ) noexcept
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
#if !defined( APOLLO_BUILD_RELEASE )
    if ( m_TimestampQueries )
    {
      m_CommandBuffer.resetQueryPool( m_TimestampQueries, 0, 4 );
      m_CommandBuffer.writeTimestamp( vk::PipelineStageFlagBits::eTopOfPipe, m_TimestampQueries, 0 );
    }
#endif

    vk::ClearValue clear{};
    clear.color.float32[ 0 ] = color.red;
    clear.color.float32[ 1 ] = color.green;
    clear.color.float32[ 2 ] = color.blue;
    clear.color.float32[ 3 ] = color.alpha;

    vk::RenderPassBeginInfo passInfo{};
    passInfo.renderPass        = m_Swapchain->GetRenderPass();
    passInfo.framebuffer       = m_Swapchain->GetFramebuffer( imageIndex );
    passInfo.renderArea.extent = m_Swapchain->GetExtent();
    m_CommandBuffer.beginRenderPass( &passInfo, vk::SubpassContents::eInline );
    vk::ClearAttachment clearAttachment{};
    clearAttachment.aspectMask      = vk::ImageAspectFlagBits::eColor;
    clearAttachment.colorAttachment = 0;
    clearAttachment.clearValue      = clear;
    vk::ClearRect clearRect{};
    clearRect.rect.extent = m_Swapchain->GetExtent();
    clearRect.layerCount  = 1;
    m_CommandBuffer.clearAttachments( 1, &clearAttachment, 1, &clearRect );

#if !defined( APOLLO_BUILD_RELEASE )
    if ( m_TimestampQueries )
    {
      m_CommandBuffer.writeTimestamp( vk::PipelineStageFlagBits::eBottomOfPipe, m_TimestampQueries, 1 );
    }
#endif

    const vk::Extent2D extent = m_Swapchain->GetExtent();
    const vk::Viewport viewport{
      0.0f, 0.0f, static_cast<float>( extent.width ), static_cast<float>( extent.height ), 0.0f, 1.0f };
    const vk::Rect2D scissor{ { 0, 0 }, extent };
    m_CommandBuffer.setViewport( 0, 1, &viewport );
    m_CommandBuffer.setScissor( 0, 1, &scissor );

    m_CommandBuffer.bindPipeline( vk::PipelineBindPoint::eGraphics, m_Pipeline );
    m_CommandBuffer.bindDescriptorSets(
      vk::PipelineBindPoint::eGraphics, m_PipelineLayout, 0, 1, &m_TextureSet, 0, nullptr );
    const vk::DeviceSize offset{};
    m_CommandBuffer.bindVertexBuffers( 0, 1, &m_VertexBuffer, &offset );
    m_CommandBuffer.bindIndexBuffer( m_IndexBuffer, 0, vk::IndexType::eUint16 );
    m_CommandBuffer.drawIndexed( static_cast<u32>( std::size( IndexedQuadIndices ) ), 1, 0, 0, 0 );

#if !defined( APOLLO_BUILD_RELEASE )
    if ( m_TimestampQueries )
    {
      m_CommandBuffer.writeTimestamp( vk::PipelineStageFlagBits::eBottomOfPipe, m_TimestampQueries, 2 );
    }
#endif
#if !defined( APOLLO_BUILD_RELEASE )
    if ( m_ImGuiReady )
    {
      ImGui_ImplVulkan_RenderDrawData( ImGui::GetDrawData(), static_cast<VkCommandBuffer>( m_CommandBuffer ) );
    }
#endif
    m_CommandBuffer.endRenderPass();
#if !defined( APOLLO_BUILD_RELEASE )
    if ( m_TimestampQueries )
    {
      m_CommandBuffer.writeTimestamp( vk::PipelineStageFlagBits::eBottomOfPipe, m_TimestampQueries, 3 );
    }
#endif

    result = m_CommandBuffer.end();
    if ( result != vk::Result::eSuccess )
    {
      bootstrap::LogFailure( "vkEndCommandBuffer", result );
      return false;
    }
    return true;
  }

  Result VulkanPresenter::PresentFrame( ClearColor color ) noexcept
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
    if ( imageIndex >= m_RenderFinishedCount || !RecordFrame( imageIndex, color ) )
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
#if !defined( APOLLO_BUILD_RELEASE )
    ReadGpuTimings();
#endif

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
#if !defined( APOLLO_BUILD_RELEASE )
    m_TimestampPending = static_cast<bool>( m_TimestampQueries );
#endif
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
#if !defined( APOLLO_BUILD_RELEASE )
      if ( m_ImGuiReady )
      {
        ImGui_ImplVulkan_Shutdown();
        m_ImGuiReady = false;
      }
#endif
      if ( m_Pipeline )
      {
        device.destroyPipeline( m_Pipeline );
      }
      if ( m_PipelineLayout )
      {
        device.destroyPipelineLayout( m_PipelineLayout );
      }

      if ( m_DescriptorPool )
      {
        device.destroyDescriptorPool( m_DescriptorPool );
      }
      if ( m_TextureSetLayout )
      {
        device.destroyDescriptorSetLayout( m_TextureSetLayout );
      }

      if ( m_TextureSampler )
      {
        device.destroySampler( m_TextureSampler );
      }
      if ( m_TextureView )
      {
        device.destroyImageView( m_TextureView );
      }
      if ( m_TextureImage )
      {
        device.destroyImage( m_TextureImage );
      }
      if ( m_TextureMemory )
      {
        device.freeMemory( m_TextureMemory );
      }
      m_TextureAllocation.Release();

      if ( m_TextureStagingBuffer )
      {
        device.destroyBuffer( m_TextureStagingBuffer );
      }
      if ( m_TextureStagingMemory )
      {
        device.freeMemory( m_TextureStagingMemory );
      }
      m_StagingAllocation.Release();

      if ( m_FragmentShader )
      {
        device.destroyShaderModule( m_FragmentShader );
      }
      if ( m_VertexShader )
      {
        device.destroyShaderModule( m_VertexShader );
      }

      if ( m_IndexBuffer )
      {
        device.destroyBuffer( m_IndexBuffer );
      }
      if ( m_IndexMemory )
      {
        device.freeMemory( m_IndexMemory );
      }
      m_IndexAllocation.Release();

      if ( m_VertexBuffer )
      {
        device.destroyBuffer( m_VertexBuffer );
      }
      if ( m_VertexMemory )
      {
        device.freeMemory( m_VertexMemory );
      }
      m_VertexAllocation.Release();

      if ( m_PresentedFrames != 0 )
      {
        char message[ 80 ]{};
        std::snprintf( message,
                       sizeof( message ),
                       "Vulkan frames presented: %llu.",
                       static_cast<unsigned long long>( m_PresentedFrames ) );
        diagnostics::Write( diagnostics::Level::Information, message );
      }

      if ( m_FrameFence )
      {
        device.destroyFence( m_FrameFence );
      }
#if !defined( APOLLO_BUILD_RELEASE )
      if ( m_TimestampQueries )
      {
        device.destroyQueryPool( m_TimestampQueries );
      }
#endif

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

    m_TextureBudget.Release();
    m_UploadBudget.Release();
    m_GeometryBudget.Release();
    m_FrameFence = nullptr;

#if !defined( APOLLO_BUILD_RELEASE )
    m_TimestampQueries    = nullptr;
    m_TimestampPeriodNs   = 0.0;
    m_TimestampMask       = 0;
    m_TimestampPending    = false;
    m_FirstTimingReported = false;
    telemetry::InvalidateGpuTimings();
    telemetry::SetGpuTimingAvailable( false );
#endif

    m_Pipeline             = nullptr;
    m_PipelineLayout       = nullptr;
    m_DescriptorPool       = nullptr;
    m_TextureSetLayout     = nullptr;
    m_TextureSet           = nullptr;
    m_TextureSampler       = nullptr;
    m_TextureView          = nullptr;
    m_TextureImage         = nullptr;
    m_TextureMemory        = nullptr;
    m_TextureStagingBuffer = nullptr;
    m_TextureStagingMemory = nullptr;

    m_VertexShader    = nullptr;
    m_FragmentShader  = nullptr;
    m_VertexBuffer    = nullptr;
    m_VertexMemory    = nullptr;
    m_IndexBuffer     = nullptr;
    m_IndexMemory     = nullptr;
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
