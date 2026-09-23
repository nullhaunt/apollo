#include "Render/Nvn/NvnIndexedQuad.hpp"

#include "Platform/Diagnostics.hpp"
#include "Render/IndexedQuad.hpp"
#include "Render/IndexedQuadTexture.hpp"
#include "IndexedQuadShader.hpp"

#include <nvnTool/nvnTool_GlslcInterface.h>

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <iterator>

namespace apollo::render::nvn
{
  namespace
  {
    [[nodiscard]] constexpr size_t AlignUp( size_t value, size_t alignment ) noexcept
    {
      return ( value + alignment - 1 ) / alignment * alignment;
    }

    [[nodiscard]] constexpr bool Fits( size_t offset, size_t length, size_t total ) noexcept
    {
      return offset <= total && length <= total - offset;
    }

    [[nodiscard]] void * AllocateAligned( size_t size ) noexcept
    {
      return aligned_alloc( NVN_MEMORY_POOL_STORAGE_ALIGNMENT,
                            AlignUp( size, NVN_MEMORY_POOL_STORAGE_GRANULARITY ) );
    }

    struct ShaderSection
    {
      const unsigned char * control{};
      const unsigned char * code{};
      size_t                codeSize{};
      unsigned int          scratchSize{};
    };
  } // namespace

  NvnIndexedQuad::~NvnIndexedQuad() noexcept
  {
    Shutdown();
  }

  bool NvnIndexedQuad::Initialize( NvnContext & context ) noexcept
  {
    if ( m_Device != nullptr )
    {
      return false;
    }
    m_Device = context.GetDevice();
    if ( m_Device == nullptr || !CreateProgram() || !CreateGeometry() || !CreateTexture() )
    {
      Shutdown();
      return false;
    }
    m_Ready = true;
    return true;
  }

  bool NvnIndexedQuad::CreateProgram() noexcept
  {
    constexpr auto & bytes = generated::IndexedQuadNvnGlslc;
    if ( sizeof( bytes ) < sizeof( GLSLCoutput ) )
    {
      return false;
    }
    const auto * output = reinterpret_cast<const GLSLCoutput *>( bytes );
    if ( output->magic != GLSLC_MAGIC_NUMBER || output->size != sizeof( bytes ) || output->numSections < 2 ||
         !Fits( offsetof( GLSLCoutput, headers ),
                static_cast<size_t>( output->numSections ) * sizeof( GLSLCsectionHeaderUnion ), sizeof( bytes ) ) )
    {
      diagnostics::Write( diagnostics::Level::Error, "Invalid NVN shader package." );
      return false;
    }

    ShaderSection sections[ 2 ]{};
    int sectionCount{};
    for ( unsigned int index = 0; index < output->numSections; ++index )
    {
      const auto & header = output->headers[ index ];
      if ( header.genericHeader.common.type == GLSLC_SECTION_TYPE_REFLECTION )
      {
        const auto & reflection = header.programReflectionHeader;
        const size_t base = reflection.common.dataOffset;
        const size_t uniformOffset = base + reflection.uniformOffset;
        const size_t stringsOffset = base + reflection.stringPoolOffset;
        if ( !Fits( base, reflection.uniformOffset, sizeof( bytes ) ) ||
             !Fits( uniformOffset, static_cast<size_t>( reflection.numUniforms ) * sizeof( GLSLCuniformInfo ), sizeof( bytes ) ) ||
             !Fits( base, reflection.stringPoolOffset, sizeof( bytes ) ) ||
             !Fits( stringsOffset, reflection.stringPoolSize, sizeof( bytes ) ) )
        {
          return false;
        }
        const auto * uniforms = reinterpret_cast<const GLSLCuniformInfo *>( bytes + uniformOffset );
        const auto * strings = reinterpret_cast<const char *>( bytes + stringsOffset );
        for ( unsigned int uniformIndex = 0; uniformIndex < reflection.numUniforms; ++uniformIndex )
        {
          const auto & uniform = uniforms[ uniformIndex ];
          if ( !Fits( uniform.nameInfo.nameOffset, uniform.nameInfo.nameLength,
                      reflection.stringPoolSize ) )
          {
            return false;
          }
          const char * name = strings + uniform.nameInfo.nameOffset;
          if ( uniform.kind == GLSLC_PIQ_UNIFORM_KIND_TEXTURE &&
               uniform.nameInfo.nameLength >= sizeof( "QuadTexture" ) - 1 &&
               std::memcmp( name, "QuadTexture", sizeof( "QuadTexture" ) - 1 ) == 0 )
          {
            m_TextureBinding = uniform.bindings[ NVN_SHADER_STAGE_FRAGMENT ];
          }
          if ( uniform.kind == GLSLC_PIQ_UNIFORM_KIND_SAMPLER &&
               uniform.nameInfo.nameLength >= sizeof( "QuadSampler" ) - 1 &&
               std::memcmp( name, "QuadSampler", sizeof( "QuadSampler" ) - 1 ) == 0 )
          {
            m_SamplerBinding = uniform.bindings[ NVN_SHADER_STAGE_FRAGMENT ];
          }
        }
        continue;
      }
      if ( header.genericHeader.common.type != GLSLC_SECTION_TYPE_GPU_CODE )
      {
        continue;
      }
      if ( sectionCount == 2 )
      {
        return false;
      }
      const auto & gpu = header.gpuCodeHeader;
      const size_t base = gpu.common.dataOffset;
      if ( !Fits( base, gpu.controlOffset, sizeof( bytes ) ) ||
           !Fits( base + gpu.controlOffset, gpu.controlSize, sizeof( bytes ) ) ||
           !Fits( base, gpu.dataOffset, sizeof( bytes ) ) ||
           !Fits( base + gpu.dataOffset, gpu.dataSize, sizeof( bytes ) ) || gpu.dataSize == 0 )
      {
        return false;
      }
      if ( gpu.stage != NVN_SHADER_STAGE_VERTEX && gpu.stage != NVN_SHADER_STAGE_FRAGMENT )
      {
        return false;
      }
      const int slot = gpu.stage == NVN_SHADER_STAGE_VERTEX ? 0 : 1;
      if ( sections[ slot ].code != nullptr )
      {
        return false;
      }
      sections[ slot ] = { bytes + base + gpu.controlOffset, bytes + base + gpu.dataOffset,
                           gpu.dataSize, gpu.scratchMemBytesRecommended };
      ++sectionCount;
    }
    if ( sectionCount != 2 || sections[ 0 ].code == nullptr || sections[ 1 ].code == nullptr ||
         sections[ 0 ].scratchSize != 0 ||
         sections[ 1 ].scratchSize != 0 || m_TextureBinding < 0 || m_SamplerBinding < 0 )
    {
      diagnostics::Write( diagnostics::Level::Error, "NVN indexed shader stages or scratch requirements are unsupported." );
      return false;
    }

    int shaderAlignment{};
    int shaderPadding{};
    m_Device->GetInteger( ::nvn::DeviceInfo::SHADER_CODE_ALIGNMENT, &shaderAlignment );
    m_Device->GetInteger( ::nvn::DeviceInfo::SHADER_CODE_MEMORY_POOL_PADDING_SIZE, &shaderPadding );
    if ( shaderAlignment <= 0 || shaderPadding < 0 )
    {
      return false;
    }
    const size_t secondOffset = AlignUp( sections[ 0 ].codeSize, static_cast<size_t>( shaderAlignment ) );
    const size_t poolSize = AlignUp( secondOffset + sections[ 1 ].codeSize + static_cast<size_t>( shaderPadding ),
                                     NVN_MEMORY_POOL_STORAGE_GRANULARITY );
    m_ShaderMemory = AllocateAligned( poolSize );
    if ( m_ShaderMemory == nullptr )
    {
      return false;
    }
    ::nvn::MemoryPoolBuilder poolBuilder{};
    poolBuilder.SetDefaults()
      .SetDevice( m_Device )
      .SetFlags( ::nvn::MemoryPoolFlags::CPU_UNCACHED | ::nvn::MemoryPoolFlags::GPU_CACHED |
                 ::nvn::MemoryPoolFlags::SHADER_CODE )
      .SetStorage( m_ShaderMemory, poolSize );
    if ( !m_ShaderPool.Initialize( &poolBuilder ) )
    {
      return false;
    }
    m_ShaderPoolReady = true;
    auto * mapped = static_cast<unsigned char *>( m_ShaderPool.Map() );
    if ( mapped == nullptr )
    {
      return false;
    }
    std::memcpy( mapped, sections[ 0 ].code, sections[ 0 ].codeSize );
    std::memcpy( mapped + secondOffset, sections[ 1 ].code, sections[ 1 ].codeSize );

    ::nvn::ShaderData shaderData[ 2 ]{};
    shaderData[ 0 ].data    = m_ShaderPool.GetBufferAddress();
    shaderData[ 0 ].control = sections[ 0 ].control;
    shaderData[ 1 ].data    = m_ShaderPool.GetBufferAddress() + secondOffset;
    shaderData[ 1 ].control = sections[ 1 ].control;
    if ( !m_Program.Initialize( m_Device ) )
    {
      return false;
    }
    m_ProgramReady = true;
    if ( !m_Program.SetShaders( 2, shaderData ) )
    {
      diagnostics::Write( diagnostics::Level::Error, "NVN indexed shader program initialization failed." );
      return false;
    }
    return true;
  }

  bool NvnIndexedQuad::CreateGeometry() noexcept
  {
    constexpr size_t indexOffset = NVN_MEMORY_POOL_STORAGE_GRANULARITY;
    constexpr size_t poolSize = indexOffset + NVN_MEMORY_POOL_STORAGE_GRANULARITY;
    m_GeometryMemory = AllocateAligned( poolSize );
    if ( m_GeometryMemory == nullptr )
    {
      return false;
    }
    ::nvn::MemoryPoolBuilder poolBuilder{};
    poolBuilder.SetDefaults()
      .SetDevice( m_Device )
      .SetFlags( ::nvn::MemoryPoolFlags::CPU_UNCACHED | ::nvn::MemoryPoolFlags::GPU_CACHED )
      .SetStorage( m_GeometryMemory, poolSize );
    if ( !m_GeometryPool.Initialize( &poolBuilder ) )
    {
      return false;
    }
    m_GeometryPoolReady = true;
    auto * mapped = static_cast<unsigned char *>( m_GeometryPool.Map() );
    if ( mapped == nullptr )
    {
      return false;
    }
    std::memcpy( mapped, IndexedQuadVertices, sizeof( IndexedQuadVertices ) );
    std::memcpy( mapped + indexOffset, IndexedQuadIndices, sizeof( IndexedQuadIndices ) );

    ::nvn::BufferBuilder bufferBuilder{};
    bufferBuilder.SetDefaults().SetDevice( m_Device ).SetStorage( &m_GeometryPool, 0,
                                                                  sizeof( IndexedQuadVertices ) );
    if ( !m_VertexBuffer.Initialize( &bufferBuilder ) )
    {
      return false;
    }
    m_VertexReady = true;
    bufferBuilder.SetStorage( &m_GeometryPool, indexOffset, sizeof( IndexedQuadIndices ) );
    if ( !m_IndexBuffer.Initialize( &bufferBuilder ) )
    {
      return false;
    }
    m_IndexReady = true;
    return true;
  }

  bool NvnIndexedQuad::CreateTexture() noexcept
  {
    ::nvn::TextureBuilder textureBuilder{};
    textureBuilder.SetDefaults()
      .SetDevice( m_Device )
      .SetTarget( ::nvn::TextureTarget::TARGET_2D )
      .SetFormat( ::nvn::Format::RGBA8 )
      .SetSize2D( static_cast<int>( IndexedQuadTextureSize ), static_cast<int>( IndexedQuadTextureSize ) );
    const size_t textureSize = textureBuilder.GetStorageSize();
    const size_t textureAlignment = textureBuilder.GetStorageAlignment();
    int textureDescriptorSize{}, samplerDescriptorSize{};
    int reservedTextures{}, reservedSamplers{};
    m_Device->GetInteger( ::nvn::DeviceInfo::TEXTURE_DESCRIPTOR_SIZE, &textureDescriptorSize );
    m_Device->GetInteger( ::nvn::DeviceInfo::SAMPLER_DESCRIPTOR_SIZE, &samplerDescriptorSize );
    m_Device->GetInteger( ::nvn::DeviceInfo::RESERVED_TEXTURE_DESCRIPTORS, &reservedTextures );
    m_Device->GetInteger( ::nvn::DeviceInfo::RESERVED_SAMPLER_DESCRIPTORS, &reservedSamplers );
    if ( textureSize == 0 || textureAlignment == 0 || textureDescriptorSize <= 0 ||
         samplerDescriptorSize <= 0 || reservedTextures <= 0 || reservedSamplers <= 0 )
    {
      return false;
    }
    const size_t textureDescriptorOffset = AlignUp( textureSize, static_cast<size_t>( textureDescriptorSize ) );
    const size_t samplerDescriptorOffset = AlignUp( textureDescriptorOffset +
      static_cast<size_t>( reservedTextures + 1 ) * textureDescriptorSize,
      static_cast<size_t>( samplerDescriptorSize ) );
    const size_t poolSize = AlignUp( samplerDescriptorOffset +
      static_cast<size_t>( reservedSamplers + 1 ) * samplerDescriptorSize,
      NVN_MEMORY_POOL_STORAGE_GRANULARITY );
    m_TextureMemory = AllocateAligned( poolSize );
    if ( m_TextureMemory == nullptr )
    {
      return false;
    }
    ::nvn::MemoryPoolBuilder poolBuilder{};
    poolBuilder.SetDefaults()
      .SetDevice( m_Device )
      .SetFlags( ::nvn::MemoryPoolFlags::CPU_UNCACHED | ::nvn::MemoryPoolFlags::GPU_CACHED )
      .SetStorage( m_TextureMemory, poolSize );
    if ( !m_TextureMemoryPool.Initialize( &poolBuilder ) )
    {
      return false;
    }
    m_TextureMemoryPoolReady = true;
    textureBuilder.SetStorage( &m_TextureMemoryPool, 0 );
    if ( !m_Texture.Initialize( &textureBuilder ) )
    {
      return false;
    }
    m_TextureReady = true;
    ::nvn::CopyRegion region{};
    region.width = static_cast<int>( IndexedQuadTextureSize );
    region.height = static_cast<int>( IndexedQuadTextureSize );
    region.depth = 1;
    m_Texture.WriteTexels( nullptr, &region, IndexedQuadTexels.data() );

    if ( !m_TexturePool.Initialize( &m_TextureMemoryPool, static_cast<ptrdiff_t>( textureDescriptorOffset ),
                                    reservedTextures + 1 ) )
    {
      return false;
    }
    m_TexturePoolReady = true;
    m_TexturePool.RegisterTexture( reservedTextures, &m_Texture, nullptr );
    ::nvn::SamplerBuilder samplerBuilder{};
    samplerBuilder.SetDefaults()
      .SetDevice( m_Device )
      .SetMinMagFilter( ::nvn::MinFilter::NEAREST, ::nvn::MagFilter::NEAREST );
    if ( !m_Sampler.Initialize( &samplerBuilder ) )
    {
      return false;
    }
    m_SamplerReady = true;
    if ( !m_SamplerPool.Initialize( &m_TextureMemoryPool, static_cast<ptrdiff_t>( samplerDescriptorOffset ),
                                    reservedSamplers + 1 ) )
    {
      return false;
    }
    m_SamplerPoolReady = true;
    m_SamplerPool.RegisterSampler( reservedSamplers, &m_Sampler );
    m_TextureHandle = m_Device->GetSeparateTextureHandle( reservedTextures );
    m_SamplerHandle = m_Device->GetSeparateSamplerHandle( reservedSamplers );
    return true;
  }

  void NvnIndexedQuad::RecordDraw( ::nvn::CommandBuffer & commands, Extent2D extent ) const noexcept
  {
    if ( !m_Ready )
    {
      return;
    }
    commands.SetScissor( 0, 0, static_cast<int>( extent.width ), static_cast<int>( extent.height ) );
    commands.SetViewport( 0, 0, static_cast<int>( extent.width ), static_cast<int>( extent.height ) );

    ::nvn::BlendState blend{};
    ::nvn::ChannelMaskState channelMask{};
    ::nvn::ColorState color{};
    ::nvn::DepthStencilState depth{};
    ::nvn::MultisampleState multisample{};
    ::nvn::PolygonState polygon{};
    blend.SetDefaults();
    channelMask.SetDefaults();
    color.SetDefaults();
    depth.SetDefaults().SetDepthTestEnable( false ).SetDepthWriteEnable( false );
    multisample.SetDefaults();
    polygon.SetDefaults().SetCullFace( ::nvn::Face::NONE );
    commands.BindBlendState( &blend );
    commands.BindChannelMaskState( &channelMask );
    commands.BindColorState( &color );
    commands.BindDepthStencilState( &depth );
    commands.BindMultisampleState( &multisample );
    commands.BindPolygonState( &polygon );
    commands.BindProgram( &m_Program, ::nvn::ShaderStageBits::VERTEX | ::nvn::ShaderStageBits::FRAGMENT );

    ::nvn::VertexAttribState attributes[ 3 ]{};
    attributes[ 0 ].SetDefaults().SetFormat( ::nvn::Format::RG32F, offsetof( IndexedQuadVertex, position ) ).SetStreamIndex( 0 );
    attributes[ 1 ].SetDefaults().SetFormat( ::nvn::Format::RGB32F, offsetof( IndexedQuadVertex, color ) ).SetStreamIndex( 0 );
    attributes[ 2 ].SetDefaults().SetFormat( ::nvn::Format::RG32F, offsetof( IndexedQuadVertex, uv ) ).SetStreamIndex( 0 );
    ::nvn::VertexStreamState stream{};
    stream.SetDefaults().SetStride( sizeof( IndexedQuadVertex ) );
    commands.BindVertexAttribState( 3, attributes );
    commands.BindVertexStreamState( 1, &stream );
    commands.SetTexturePool( &m_TexturePool );
    commands.SetSamplerPool( &m_SamplerPool );
    commands.BindSeparateTexture( ::nvn::ShaderStage::FRAGMENT, m_TextureBinding, m_TextureHandle );
    commands.BindSeparateSampler( ::nvn::ShaderStage::FRAGMENT, m_SamplerBinding, m_SamplerHandle );
    commands.BindVertexBuffer( 0, m_VertexBuffer.GetAddress(), m_VertexBuffer.GetSize() );
    commands.DrawElements( ::nvn::DrawPrimitive::TRIANGLES, ::nvn::IndexType::UNSIGNED_SHORT,
                           static_cast<int>( std::size( IndexedQuadIndices ) ), m_IndexBuffer.GetAddress() );
  }

  void NvnIndexedQuad::Shutdown() noexcept
  {
    m_Ready = false;
    if ( m_SamplerPoolReady ) m_SamplerPool.Finalize();
    if ( m_SamplerReady ) m_Sampler.Finalize();
    if ( m_TexturePoolReady ) m_TexturePool.Finalize();
    if ( m_TextureReady ) m_Texture.Finalize();
    if ( m_TextureMemoryPoolReady ) m_TextureMemoryPool.Finalize();
    std::free( m_TextureMemory );
    m_TextureMemory = nullptr;
    m_SamplerPoolReady = false;
    m_SamplerReady = false;
    m_TexturePoolReady = false;
    m_TextureReady = false;
    m_TextureMemoryPoolReady = false;
    m_TextureBinding = -1;
    m_SamplerBinding = -1;
    if ( m_IndexReady )
    {
      m_IndexBuffer.Finalize();
    }
    if ( m_VertexReady )
    {
      m_VertexBuffer.Finalize();
    }
    m_IndexReady = false;
    m_VertexReady = false;
    if ( m_GeometryPoolReady )
    {
      m_GeometryPool.Finalize();
    }
    m_GeometryPoolReady = false;
    std::free( m_GeometryMemory );
    m_GeometryMemory = nullptr;

    if ( m_ProgramReady )
    {
      m_Program.Finalize();
    }
    m_ProgramReady = false;
    if ( m_ShaderPoolReady )
    {
      m_ShaderPool.Finalize();
    }
    m_ShaderPoolReady = false;
    std::free( m_ShaderMemory );
    m_ShaderMemory = nullptr;
    m_Device = nullptr;
  }
} // namespace apollo::render::nvn
