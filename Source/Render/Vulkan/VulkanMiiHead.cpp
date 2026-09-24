#include "Render/Vulkan/VulkanMiiHead.hpp"

#include "MiiHeadVulkanShader.hpp"
#include "Platform/Diagnostics.hpp"
#include "Render/Vulkan/VulkanBootstrap.hpp"
#include "Render/Vulkan/VulkanContext.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <new>

namespace apollo::render::vulkan
{
  namespace
  {
    struct Vec3
    {
      float x{};
      float y{};
      float z{};
    };

    struct DrawConstants
    {
      float clipFromModel[ 16 ]{};
      int   mode[ 4 ]{};
      float colors[ 3 ][ 4 ]{};
    };
    static_assert( sizeof( DrawConstants ) == 128 );

    [[nodiscard]] Vec3 Cross( Vec3 left, Vec3 right ) noexcept
    {
      return {
        left.y * right.z - left.z * right.y, left.z * right.x - left.x * right.z, left.x * right.y - left.y * right.x };
    }

    [[nodiscard]] float Dot( Vec3 left, Vec3 right ) noexcept
    {
      return left.x * right.x + left.y * right.y + left.z * right.z;
    }

    [[nodiscard]] Vec3 Normalize( Vec3 value ) noexcept
    {
      const float inverse = 1.0f / std::sqrt( Dot( value, value ) );
      return { value.x * inverse, value.y * inverse, value.z * inverse };
    }

    void MakeCameraMatrix( float * output, vk::Extent2D extent, const mii::PreviewCamera & input ) noexcept
    {
      mii::PreviewCamera camera = input;
      camera.Clamp();

      constexpr float Pi         = 3.14159265358979323846f;
      const float     yaw        = camera.yawDegrees * Pi / 180.0f;
      const float     pitch      = camera.pitchDegrees * Pi / 180.0f;
      const float     horizontal = camera.distance * std::cos( pitch );
      const Vec3      eye{ horizontal * std::sin( yaw ),
                      mii::PreviewCamera::TargetHeight + camera.distance * std::sin( pitch ),
                      horizontal * std::cos( yaw ) };
      const Vec3      target{ 0.0f, mii::PreviewCamera::TargetHeight, 0.0f };
      const Vec3      forward = Normalize( { target.x - eye.x, target.y - eye.y, target.z - eye.z } );
      const Vec3      right   = Normalize( Cross( forward, { 0.0f, 1.0f, 0.0f } ) );
      const Vec3      up      = Cross( right, forward );

      const float view[ 16 ]{ right.x,
                              up.x,
                              -forward.x,
                              0.0f,
                              right.y,
                              up.y,
                              -forward.y,
                              0.0f,
                              right.z,
                              up.z,
                              -forward.z,
                              0.0f,
                              -Dot( right, eye ),
                              -Dot( up, eye ),
                              Dot( forward, eye ),
                              1.0f };

      const float     previewWidth = static_cast<float>( extent.width >= 1100 ? extent.width - 460 : extent.width );
      const float     aspect       = previewWidth / static_cast<float>( extent.height );
      const float     scale        = 1.0f / std::tan( Pi / 8.0f );
      constexpr float nearZ        = 1.0f;
      constexpr float farZ         = 500.0f;
      float           projection[ 16 ]{};
      projection[ 0 ]  = scale / aspect;
      projection[ 5 ]  = -scale;
      projection[ 10 ] = farZ / ( nearZ - farZ );
      projection[ 11 ] = -1.0f;
      projection[ 14 ] = farZ * nearZ / ( nearZ - farZ );

      for ( int column = 0; column < 4; ++column )
      {
        for ( int row = 0; row < 4; ++row )
        {
          float value = 0.0f;
          for ( int term = 0; term < 4; ++term )
          {
            value += projection[ term * 4 + row ] * view[ column * 4 + term ];
          }
          output[ column * 4 + row ] = value;
        }
      }
    }

    [[nodiscard]] std::uint32_t FindMemoryType( const VulkanContext &   context,
                                                std::uint32_t           candidates,
                                                vk::MemoryPropertyFlags required ) noexcept
    {
      vk::PhysicalDeviceMemoryProperties properties{};
      context.GetPhysicalDevice().getMemoryProperties( &properties );
      for ( std::uint32_t index = 0; index < properties.memoryTypeCount; ++index )
      {
        if ( ( candidates & ( 1u << index ) ) != 0 &&
             ( properties.memoryTypes[ index ].propertyFlags & required ) == required )
        {
          return index;
        }
      }
      return properties.memoryTypeCount;
    }
  } // namespace

  VulkanMiiHead::~VulkanMiiHead() noexcept
  {
    Shutdown();
  }

  bool VulkanMiiHead::Initialize( const VulkanContext &       context,
                                  vk::RenderPass              renderPass,
                                  vk::CommandBuffer           uploadCommands,
                                  const mii::PreviewPackage & package ) noexcept
  {
    if ( m_Context != nullptr || !context.GetDevice() || !renderPass || !uploadCommands || !package.IsReady() )
    {
      return false;
    }

    m_Context = &context;
    if ( !CreateGeometry( package ) || !CreateTextures( package, uploadCommands ) || !CreateDescriptors( package ) ||
         !CreatePipelines( renderPass ) )
    {
      Shutdown();
      return false;
    }

    m_Ready = true;
    diagnostics::Write( diagnostics::Level::Information, "Vulkan Mii head renderer ready." );
    return true;
  }

  bool VulkanMiiHead::CreateHostBuffer( size_t                         size,
                                        vk::BufferUsageFlags           usage,
                                        const void *                   data,
                                        vk::Buffer &                   buffer,
                                        vk::DeviceMemory &             memory,
                                        telemetry::TrackedAllocation & tracked ) noexcept
  {
    const vk::Device     device = m_Context->GetDevice();
    vk::BufferCreateInfo info{};
    info.size         = size;
    info.usage        = usage;
    vk::Result result = device.createBuffer( &info, nullptr, &buffer );
    if ( result != vk::Result::eSuccess )
    {
      bootstrap::LogFailure( "vkCreateBuffer(Mii)", result );
      return false;
    }

    vk::MemoryRequirements requirements{};
    device.getBufferMemoryRequirements( buffer, &requirements );
    const std::uint32_t memoryType =
      FindMemoryType( *m_Context,
                      requirements.memoryTypeBits,
                      vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent );
    vk::PhysicalDeviceMemoryProperties properties{};
    m_Context->GetPhysicalDevice().getMemoryProperties( &properties );
    if ( memoryType == properties.memoryTypeCount )
    {
      diagnostics::Write( diagnostics::Level::Error, "Mii host buffer memory is unavailable." );
      return false;
    }

    vk::MemoryAllocateInfo allocation{};
    allocation.allocationSize  = requirements.size;
    allocation.memoryTypeIndex = memoryType;
    result                     = device.allocateMemory( &allocation, nullptr, &memory );
    if ( result != vk::Result::eSuccess )
    {
      bootstrap::LogFailure( "vkAllocateMemory(Mii buffer)", result );
      return false;
    }
    tracked.Acquire( static_cast<size_t>( allocation.allocationSize ) );

    result = device.bindBufferMemory( buffer, memory, 0 );
    if ( result != vk::Result::eSuccess )
    {
      bootstrap::LogFailure( "vkBindBufferMemory(Mii)", result );
      return false;
    }

    void * mapped{};
    result = device.mapMemory( memory, 0, size, {}, &mapped );
    if ( result != vk::Result::eSuccess )
    {
      bootstrap::LogFailure( "vkMapMemory(Mii)", result );
      return false;
    }
    std::memcpy( mapped, data, size );
    device.unmapMemory( memory );
    return true;
  }

  bool VulkanMiiHead::CreateGeometry( const mii::PreviewPackage & package ) noexcept
  {
    size_t verticesBytes{};
    size_t indicesBytes{};
    for ( std::uint32_t index = 0; index < package.PartCount(); ++index )
    {
      const auto * part  = package.GetPart( index );
      verticesBytes     += static_cast<size_t>( part->vertexCount ) * 12;
      indicesBytes      += static_cast<size_t>( part->indexCount ) * 2;
    }
    if ( !m_GeometryBudget.Acquire( budget::Resource::Geometry, verticesBytes + indicesBytes ) )
    {
      return false;
    }

    auto vertices = std::unique_ptr<std::uint8_t[]>( new ( std::nothrow ) std::uint8_t[ verticesBytes ]{} );
    auto indices  = std::unique_ptr<std::uint8_t[]>( new ( std::nothrow ) std::uint8_t[ indicesBytes ]{} );
    if ( !vertices || !indices )
    {
      return false;
    }

    std::uint32_t vertexBase{};
    std::uint32_t indexBase{};
    for ( std::uint32_t index = 0; index < package.PartCount(); ++index )
    {
      const auto * source = package.GetPart( index );
      Part &       part   = m_Parts[ index ];
      part.indexCount     = source->indexCount;
      part.firstIndex     = indexBase;
      part.vertexOffset   = static_cast<std::int32_t>( vertexBase );
      part.drawType       = source->drawType;
      part.cullMode       = source->cullMode;
      part.textureType    = source->textureType;
      part.modulateType   = static_cast<int>( source->modulateType );
      for ( int color = 0; color < 3; ++color )
      {
        for ( int component = 0; component < 3; ++component )
        {
          part.colors[ color ][ component ] = source->colors[ color ][ component ];
        }
        part.colors[ color ][ 3 ] = 1.0f;
      }

      for ( std::uint32_t vertex = 0; vertex < source->vertexCount; ++vertex )
      {
        std::uint8_t * destination = vertices.get() + static_cast<size_t>( vertexBase + vertex ) * 12;
        std::memcpy( destination, source->positions + static_cast<size_t>( vertex ) * 8, 8 );
        if ( source->uvs != nullptr && source->textureType != 0xffffffffu )
        {
          std::memcpy( destination + 8, source->uvs + static_cast<size_t>( vertex ) * 4, 4 );
        }
      }
      std::memcpy( indices.get() + static_cast<size_t>( indexBase ) * 2,
                   source->indices,
                   static_cast<size_t>( source->indexCount ) * 2 );
      vertexBase += source->vertexCount;
      indexBase  += source->indexCount;
    }
    m_PartCount = package.PartCount();

    return CreateHostBuffer( verticesBytes,
                             vk::BufferUsageFlagBits::eVertexBuffer,
                             vertices.get(),
                             m_VertexBuffer,
                             m_VertexMemory,
                             m_VertexAllocation ) &&
           CreateHostBuffer( indicesBytes,
                             vk::BufferUsageFlagBits::eIndexBuffer,
                             indices.get(),
                             m_IndexBuffer,
                             m_IndexMemory,
                             m_IndexAllocation );
  }

  bool VulkanMiiHead::CreateTextures( const mii::PreviewPackage & package, vk::CommandBuffer commands ) noexcept
  {
    size_t totalBytes{};
    size_t offsets[ 5 ]{};
    for ( std::uint32_t type = 0; type < 5; ++type )
    {
      const auto * texture = package.GetTexture( type );
      if ( texture != nullptr )
      {
        offsets[ type ]  = totalBytes;
        totalBytes      += static_cast<size_t>( texture->width ) * texture->height * 4;
      }
    }
    if ( !m_TextureBudget.Acquire( budget::Resource::Texture, totalBytes ) ||
         !m_UploadBudget.Acquire( budget::Resource::Upload, totalBytes ) )
    {
      return false;
    }

    auto pixels = std::unique_ptr<std::uint8_t[]>( new ( std::nothrow ) std::uint8_t[ totalBytes ] );
    if ( !pixels )
    {
      return false;
    }
    for ( std::uint32_t type = 0; type < 5; ++type )
    {
      if ( const auto * source = package.GetTexture( type ) )
      {
        std::memcpy(
          pixels.get() + offsets[ type ], source->pixels, static_cast<size_t>( source->width ) * source->height * 4 );
      }
    }
    if ( !CreateHostBuffer( totalBytes,
                            vk::BufferUsageFlagBits::eTransferSrc,
                            pixels.get(),
                            m_StagingBuffer,
                            m_StagingMemory,
                            m_StagingAllocation ) )
    {
      return false;
    }

    for ( std::uint32_t type = 0; type < 5; ++type )
    {
      if ( const auto * source = package.GetTexture( type );
           source != nullptr && !CreateTextureImage( *source, m_Textures[ type ] ) )
      {
        return false;
      }
    }
    if ( !UploadTextures( package, offsets, commands ) )
    {
      return false;
    }

    const vk::Device device = m_Context->GetDevice();
    device.destroyBuffer( m_StagingBuffer );
    device.freeMemory( m_StagingMemory );
    m_StagingAllocation.Release();
    m_UploadBudget.Release();
    m_StagingBuffer = nullptr;
    m_StagingMemory = nullptr;
    return true;
  }

  bool VulkanMiiHead::CreateTextureImage( const mii::PreviewPackage::Texture & source, Texture & image ) noexcept
  {
    const vk::Device    device = m_Context->GetDevice();
    vk::ImageCreateInfo info{};
    info.imageType     = vk::ImageType::e2D;
    info.format        = vk::Format::eR8G8B8A8Unorm;
    info.extent        = vk::Extent3D{ source.width, source.height, 1 };
    info.mipLevels     = 1;
    info.arrayLayers   = 1;
    info.samples       = vk::SampleCountFlagBits::e1;
    info.tiling        = vk::ImageTiling::eOptimal;
    info.usage         = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
    info.initialLayout = vk::ImageLayout::eUndefined;
    vk::Result result  = device.createImage( &info, nullptr, &image.image );
    if ( result != vk::Result::eSuccess )
    {
      bootstrap::LogFailure( "vkCreateImage(Mii)", result );
      return false;
    }

    vk::MemoryRequirements requirements{};
    device.getImageMemoryRequirements( image.image, &requirements );
    const std::uint32_t memoryType =
      FindMemoryType( *m_Context, requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal );
    vk::PhysicalDeviceMemoryProperties properties{};
    m_Context->GetPhysicalDevice().getMemoryProperties( &properties );
    if ( memoryType == properties.memoryTypeCount )
    {
      diagnostics::Write( diagnostics::Level::Error, "Mii image memory is unavailable." );
      return false;
    }

    vk::MemoryAllocateInfo allocation{};
    allocation.allocationSize  = requirements.size;
    allocation.memoryTypeIndex = memoryType;
    result                     = device.allocateMemory( &allocation, nullptr, &image.memory );
    if ( result != vk::Result::eSuccess )
    {
      bootstrap::LogFailure( "vkAllocateMemory(Mii image)", result );
      return false;
    }
    image.allocation.Acquire( static_cast<size_t>( allocation.allocationSize ) );
    result = device.bindImageMemory( image.image, image.memory, 0 );
    if ( result != vk::Result::eSuccess )
    {
      bootstrap::LogFailure( "vkBindImageMemory(Mii)", result );
      return false;
    }

    vk::ImageViewCreateInfo view{};
    view.image                       = image.image;
    view.viewType                    = vk::ImageViewType::e2D;
    view.format                      = info.format;
    view.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    view.subresourceRange.levelCount = 1;
    view.subresourceRange.layerCount = 1;
    result                           = device.createImageView( &view, nullptr, &image.view );
    if ( result != vk::Result::eSuccess )
    {
      bootstrap::LogFailure( "vkCreateImageView(Mii)", result );
      return false;
    }
    return true;
  }

  bool VulkanMiiHead::UploadTextures( const mii::PreviewPackage & package,
                                      const size_t *              offsets,
                                      vk::CommandBuffer           commands ) noexcept
  {
    if ( commands.reset() != vk::Result::eSuccess )
    {
      return false;
    }
    vk::CommandBufferBeginInfo begin{};
    begin.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
    if ( commands.begin( &begin ) != vk::Result::eSuccess )
    {
      return false;
    }
    for ( std::uint32_t type = 0; type < 5; ++type )
    {
      const auto * source = package.GetTexture( type );
      if ( source == nullptr )
      {
        continue;
      }

      vk::ImageMemoryBarrier barrier{};
      barrier.oldLayout                   = vk::ImageLayout::eUndefined;
      barrier.newLayout                   = vk::ImageLayout::eTransferDstOptimal;
      barrier.srcQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
      barrier.dstQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
      barrier.image                       = m_Textures[ type ].image;
      barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
      barrier.subresourceRange.levelCount = 1;
      barrier.subresourceRange.layerCount = 1;
      barrier.dstAccessMask               = vk::AccessFlagBits::eTransferWrite;
      commands.pipelineBarrier( vk::PipelineStageFlagBits::eTopOfPipe,
                                vk::PipelineStageFlagBits::eTransfer,
                                {},
                                0,
                                nullptr,
                                0,
                                nullptr,
                                1,
                                &barrier );

      vk::BufferImageCopy copy{};
      copy.bufferOffset                = offsets[ type ];
      copy.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
      copy.imageSubresource.layerCount = 1;
      copy.imageExtent                 = vk::Extent3D{ source->width, source->height, 1 };
      commands.copyBufferToImage(
        m_StagingBuffer, m_Textures[ type ].image, vk::ImageLayout::eTransferDstOptimal, 1, &copy );

      barrier.oldLayout     = vk::ImageLayout::eTransferDstOptimal;
      barrier.newLayout     = vk::ImageLayout::eShaderReadOnlyOptimal;
      barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
      barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
      commands.pipelineBarrier( vk::PipelineStageFlagBits::eTransfer,
                                vk::PipelineStageFlagBits::eFragmentShader,
                                {},
                                0,
                                nullptr,
                                0,
                                nullptr,
                                1,
                                &barrier );
    }
    if ( commands.end() != vk::Result::eSuccess )
    {
      return false;
    }

    vk::SubmitInfo submit{};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers    = &commands;
    vk::Result result         = m_Context->GetGraphicsQueue().submit( 1, &submit, {} );
    if ( result == vk::Result::eSuccess )
    {
      result = m_Context->GetGraphicsQueue().waitIdle();
    }
    if ( result != vk::Result::eSuccess )
    {
      bootstrap::LogFailure( "Vulkan Mii texture upload", result );
      return false;
    }

    return true;
  }

  bool VulkanMiiHead::CreateDescriptors( const mii::PreviewPackage & package ) noexcept
  {
    const vk::Device      device = m_Context->GetDevice();
    vk::SamplerCreateInfo sampler{};
    sampler.magFilter    = vk::Filter::eLinear;
    sampler.minFilter    = vk::Filter::eLinear;
    sampler.mipmapMode   = vk::SamplerMipmapMode::eNearest;
    sampler.addressModeU = vk::SamplerAddressMode::eRepeat;
    sampler.addressModeV = vk::SamplerAddressMode::eClampToEdge;
    sampler.addressModeW = vk::SamplerAddressMode::eClampToEdge;
    vk::Result result    = device.createSampler( &sampler, nullptr, &m_Sampler );
    if ( result != vk::Result::eSuccess )
    {
      bootstrap::LogFailure( "vkCreateSampler(Mii)", result );
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
    vk::DescriptorSetLayoutCreateInfo layout{};
    layout.bindingCount = 2;
    layout.pBindings    = bindings;
    result              = device.createDescriptorSetLayout( &layout, nullptr, &m_SetLayout );
    if ( result != vk::Result::eSuccess )
    {
      bootstrap::LogFailure( "vkCreateDescriptorSetLayout(Mii)", result );
      return false;
    }

    vk::DescriptorPoolSize       sizes[]{ { vk::DescriptorType::eSampledImage, package.TextureCount() },
                                          { vk::DescriptorType::eSampler, package.TextureCount() } };
    vk::DescriptorPoolCreateInfo pool{};
    pool.maxSets       = package.TextureCount();
    pool.poolSizeCount = 2;
    pool.pPoolSizes    = sizes;
    result             = device.createDescriptorPool( &pool, nullptr, &m_DescriptorPool );
    if ( result != vk::Result::eSuccess )
    {
      bootstrap::LogFailure( "vkCreateDescriptorPool(Mii)", result );
      return false;
    }

    for ( std::uint32_t type = 0; type < 5; ++type )
    {
      Texture & texture = m_Textures[ type ];
      if ( !texture.view )
      {
        continue;
      }
      vk::DescriptorSetAllocateInfo allocation{};
      allocation.descriptorPool     = m_DescriptorPool;
      allocation.descriptorSetCount = 1;
      allocation.pSetLayouts        = &m_SetLayout;
      result                        = device.allocateDescriptorSets( &allocation, &texture.set );
      if ( result != vk::Result::eSuccess )
      {
        bootstrap::LogFailure( "vkAllocateDescriptorSets(Mii)", result );
        return false;
      }

      vk::DescriptorImageInfo image{ m_Sampler, texture.view, vk::ImageLayout::eShaderReadOnlyOptimal };
      vk::WriteDescriptorSet  writes[ 2 ]{};
      writes[ 0 ].dstSet          = texture.set;
      writes[ 0 ].dstBinding      = 0;
      writes[ 0 ].descriptorCount = 1;
      writes[ 0 ].descriptorType  = vk::DescriptorType::eSampledImage;
      writes[ 0 ].pImageInfo      = &image;
      writes[ 1 ].dstSet          = texture.set;
      writes[ 1 ].dstBinding      = 1;
      writes[ 1 ].descriptorCount = 1;
      writes[ 1 ].descriptorType  = vk::DescriptorType::eSampler;
      writes[ 1 ].pImageInfo      = &image;
      device.updateDescriptorSets( 2, writes, 0, nullptr );
    }
    return true;
  }

  bool VulkanMiiHead::CreatePipelines( vk::RenderPass renderPass ) noexcept
  {
    const vk::Device           device = m_Context->GetDevice();
    vk::ShaderModuleCreateInfo module{};
    module.codeSize   = sizeof( generated::MiiHeadVsSpirv );
    module.pCode      = reinterpret_cast<const std::uint32_t *>( generated::MiiHeadVsSpirv );
    vk::Result result = device.createShaderModule( &module, nullptr, &m_VertexShader );
    if ( result != vk::Result::eSuccess )
    {
      bootstrap::LogFailure( "vkCreateShaderModule(Mii vertex)", result );
      return false;
    }
    module.codeSize = sizeof( generated::MiiHeadPsSpirv );
    module.pCode    = reinterpret_cast<const std::uint32_t *>( generated::MiiHeadPsSpirv );
    result          = device.createShaderModule( &module, nullptr, &m_FragmentShader );
    if ( result != vk::Result::eSuccess )
    {
      bootstrap::LogFailure( "vkCreateShaderModule(Mii fragment)", result );
      return false;
    }

    const vk::PipelineShaderStageCreateInfo stages[]{
      { {}, vk::ShaderStageFlagBits::eVertex, m_VertexShader, "VSMain" },
      { {}, vk::ShaderStageFlagBits::eFragment, m_FragmentShader, "PSMain" },
    };
    const vk::VertexInputBindingDescription   binding{ 0, 12, vk::VertexInputRate::eVertex };
    const vk::VertexInputAttributeDescription attributes[]{
      { 0, 0, vk::Format::eR16G16B16A16Sfloat, 0 },
      { 1, 0, vk::Format::eR16G16Sfloat, 8 },
    };
    vk::PipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.vertexBindingDescriptionCount   = 1;
    vertexInput.pVertexBindingDescriptions      = &binding;
    vertexInput.vertexAttributeDescriptionCount = 2;
    vertexInput.pVertexAttributeDescriptions    = attributes;
    vk::PipelineInputAssemblyStateCreateInfo assembly{};
    assembly.topology = vk::PrimitiveTopology::eTriangleList;
    vk::PipelineViewportStateCreateInfo viewport{};
    viewport.viewportCount = 1;
    viewport.scissorCount  = 1;
    vk::PipelineRasterizationStateCreateInfo raster{};
    raster.polygonMode = vk::PolygonMode::eFill;
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
    vk::PipelineDepthStencilStateCreateInfo depth{};
    depth.depthTestEnable = VK_TRUE;
    depth.depthCompareOp  = vk::CompareOp::eLessOrEqual;
    const vk::DynamicState             dynamicStates[]{ vk::DynamicState::eViewport, vk::DynamicState::eScissor };
    vk::PipelineDynamicStateCreateInfo dynamic{};
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates    = dynamicStates;

    vk::PushConstantRange range{};
    range.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
    range.size       = sizeof( DrawConstants );
    vk::PipelineLayoutCreateInfo layout{};
    layout.setLayoutCount         = 1;
    layout.pSetLayouts            = &m_SetLayout;
    layout.pushConstantRangeCount = 1;
    layout.pPushConstantRanges    = &range;
    result                        = device.createPipelineLayout( &layout, nullptr, &m_PipelineLayout );
    if ( result != vk::Result::eSuccess )
    {
      bootstrap::LogFailure( "vkCreatePipelineLayout(Mii)", result );
      return false;
    }

    vk::GraphicsPipelineCreateInfo info{};
    info.stageCount          = 2;
    info.pStages             = stages;
    info.pVertexInputState   = &vertexInput;
    info.pInputAssemblyState = &assembly;
    info.pViewportState      = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState   = &multisample;
    info.pDepthStencilState  = &depth;
    info.pColorBlendState    = &blend;
    info.pDynamicState       = &dynamic;
    info.layout              = m_PipelineLayout;
    info.renderPass          = renderPass;

    constexpr vk::CullModeFlags culls[]{
      vk::CullModeFlagBits::eNone, vk::CullModeFlagBits::eFront, vk::CullModeFlagBits::eBack };
    for ( int blendIndex = 0; blendIndex < 2; ++blendIndex )
    {
      attachment.blendEnable         = blendIndex != 0;
      attachment.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
      attachment.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
      attachment.colorBlendOp        = vk::BlendOp::eAdd;
      attachment.srcAlphaBlendFactor = vk::BlendFactor::eOne;
      attachment.dstAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
      attachment.alphaBlendOp        = vk::BlendOp::eAdd;
      depth.depthWriteEnable         = blendIndex == 0;
      for ( int cull = 0; cull < 3; ++cull )
      {
        raster.cullMode = culls[ cull ];
        result          = device.createGraphicsPipelines( {}, 1, &info, nullptr, &m_Pipelines[ blendIndex ][ cull ] );
        if ( result != vk::Result::eSuccess )
        {
          bootstrap::LogFailure( "vkCreateGraphicsPipelines(Mii)", result );
          return false;
        }
      }
    }
    return true;
  }

  void VulkanMiiHead::Draw( vk::CommandBuffer          commands,
                            vk::Extent2D               extent,
                            const mii::PreviewCamera & camera ) const noexcept
  {
    if ( !m_Ready )
    {
      return;
    }

    DrawConstants constants{};
    MakeCameraMatrix( constants.clipFromModel, extent, camera );
    const vk::DeviceSize offset{};
    commands.bindVertexBuffers( 0, 1, &m_VertexBuffer, &offset );
    commands.bindIndexBuffer( m_IndexBuffer, 0, vk::IndexType::eUint16 );

    vk::DescriptorSet fallback{};
    for ( const Texture & texture : m_Textures )
    {
      if ( texture.set )
      {
        fallback = texture.set;
        break;
      }
    }
    for ( std::uint32_t index = 0; index < m_PartCount; ++index )
    {
      const Part & part  = m_Parts[ index ];
      const int    blend = part.drawType >= 6 ? 1 : 0;
      commands.bindPipeline( vk::PipelineBindPoint::eGraphics, m_Pipelines[ blend ][ part.cullMode ] );
      const vk::DescriptorSet set = part.textureType < 5 ? m_Textures[ part.textureType ].set : fallback;
      commands.bindDescriptorSets( vk::PipelineBindPoint::eGraphics, m_PipelineLayout, 0, 1, &set, 0, nullptr );

      constants.mode[ 0 ] = part.modulateType;
      std::memcpy( constants.colors, part.colors, sizeof( constants.colors ) );
      commands.pushConstants( m_PipelineLayout,
                              vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                              0,
                              sizeof( constants ),
                              &constants );
      commands.drawIndexed( part.indexCount, 1, part.firstIndex, part.vertexOffset, 0 );
    }
  }

  void VulkanMiiHead::Shutdown() noexcept
  {
    if ( m_Context == nullptr )
    {
      return;
    }
    const vk::Device device = m_Context->GetDevice();
    ( void )device.waitIdle();

    for ( auto & group : m_Pipelines )
    {
      for ( auto & pipeline : group )
      {
        if ( pipeline )
        {
          device.destroyPipeline( pipeline );
          pipeline = nullptr;
        }
      }
    }
    if ( m_PipelineLayout )
    {
      device.destroyPipelineLayout( m_PipelineLayout );
      m_PipelineLayout = nullptr;
    }
    if ( m_VertexShader )
    {
      device.destroyShaderModule( m_VertexShader );
      m_VertexShader = nullptr;
    }
    if ( m_FragmentShader )
    {
      device.destroyShaderModule( m_FragmentShader );
      m_FragmentShader = nullptr;
    }
    if ( m_DescriptorPool )
    {
      device.destroyDescriptorPool( m_DescriptorPool );
      m_DescriptorPool = nullptr;
    }
    if ( m_SetLayout )
    {
      device.destroyDescriptorSetLayout( m_SetLayout );
      m_SetLayout = nullptr;
    }
    if ( m_Sampler )
    {
      device.destroySampler( m_Sampler );
      m_Sampler = nullptr;
    }
    for ( Texture & texture : m_Textures )
    {
      if ( texture.view )
      {
        device.destroyImageView( texture.view );
      }
      if ( texture.image )
      {
        device.destroyImage( texture.image );
      }
      if ( texture.memory )
      {
        device.freeMemory( texture.memory );
      }
      texture.allocation.Release();
      texture.view   = nullptr;
      texture.image  = nullptr;
      texture.memory = nullptr;
      texture.set    = nullptr;
    }
    if ( m_StagingBuffer )
    {
      device.destroyBuffer( m_StagingBuffer );
    }
    if ( m_StagingMemory )
    {
      device.freeMemory( m_StagingMemory );
    }
    m_StagingAllocation.Release();
    if ( m_VertexBuffer )
    {
      device.destroyBuffer( m_VertexBuffer );
    }
    if ( m_VertexMemory )
    {
      device.freeMemory( m_VertexMemory );
    }
    m_VertexAllocation.Release();
    if ( m_IndexBuffer )
    {
      device.destroyBuffer( m_IndexBuffer );
    }
    if ( m_IndexMemory )
    {
      device.freeMemory( m_IndexMemory );
    }
    m_IndexAllocation.Release();
    m_GeometryBudget.Release();
    m_TextureBudget.Release();
    m_UploadBudget.Release();
    m_VertexBuffer  = nullptr;
    m_VertexMemory  = nullptr;
    m_IndexBuffer   = nullptr;
    m_IndexMemory   = nullptr;
    m_StagingBuffer = nullptr;
    m_StagingMemory = nullptr;
    m_PartCount     = 0;
    m_Ready         = false;
    m_Context       = nullptr;
  }

  bool VulkanMiiHead::IsReady() const noexcept
  {
    return m_Ready;
  }
} // namespace apollo::render::vulkan
