#include "Render/Nvn/NvnDebugUi.hpp"

#include "Platform/Diagnostics.hpp"
#include "DebugUiShader.hpp"

#include <imgui.h>
#include <nvnTool/nvnTool_GlslcInterface.h>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cstring>

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
      size_t codeSize{};
      unsigned int scratchSize{};
    };
  } // namespace

  NvnDebugUi::~NvnDebugUi() noexcept
  {
    Shutdown();
  }

  bool NvnDebugUi::Initialize( NvnContext & context ) noexcept
  {
    if ( m_Device != nullptr || ImGui::GetCurrentContext() == nullptr ) return false;
    m_Device = context.GetDevice();
    if ( m_Device == nullptr || !CreateProgram() || !CreateFontTexture() || !CreateGeometry() )
    {
      Shutdown();
      return false;
    }
    m_Ready = true;
    diagnostics::Write( diagnostics::Level::Information, "NVN debug UI renderer ready." );
    return true;
  }

  bool NvnDebugUi::CreateProgram() noexcept
  {
    constexpr auto & bytes = generated::DebugUiNvnGlslc;
    if ( sizeof( bytes ) < sizeof( GLSLCoutput ) ) return false;
    const auto * output = reinterpret_cast<const GLSLCoutput *>( bytes );
    if ( output->magic != GLSLC_MAGIC_NUMBER || output->size != sizeof( bytes ) ||
         !Fits( offsetof( GLSLCoutput, headers ),
                static_cast<size_t>( output->numSections ) * sizeof( GLSLCsectionHeaderUnion ), sizeof( bytes ) ) )
    {
      return false;
    }
    ShaderSection sections[ 2 ]{};
    int sectionCount{};
    for ( unsigned int i = 0; i < output->numSections; ++i )
    {
      const auto & header = output->headers[ i ];
      if ( header.genericHeader.common.type == GLSLC_SECTION_TYPE_REFLECTION )
      {
        const auto & reflection = header.programReflectionHeader;
        const size_t base = reflection.common.dataOffset;
        if ( !Fits( base, reflection.uniformOffset, sizeof( bytes ) ) ||
             !Fits( base + reflection.uniformOffset,
                    static_cast<size_t>( reflection.numUniforms ) * sizeof( GLSLCuniformInfo ), sizeof( bytes ) ) ||
             !Fits( base, reflection.stringPoolOffset, sizeof( bytes ) ) ||
             !Fits( base + reflection.stringPoolOffset, reflection.stringPoolSize, sizeof( bytes ) ) )
        {
          return false;
        }
        const auto * uniforms = reinterpret_cast<const GLSLCuniformInfo *>( bytes + base + reflection.uniformOffset );
        const auto * strings = reinterpret_cast<const char *>( bytes + base + reflection.stringPoolOffset );
        for ( unsigned int u = 0; u < reflection.numUniforms; ++u )
        {
          const auto & uniform = uniforms[ u ];
          if ( !Fits( uniform.nameInfo.nameOffset, uniform.nameInfo.nameLength,
                      reflection.stringPoolSize ) ) return false;
          const char * name = strings + uniform.nameInfo.nameOffset;
          if ( uniform.kind == GLSLC_PIQ_UNIFORM_KIND_TEXTURE &&
               uniform.nameInfo.nameLength >= sizeof( "UiTexture" ) - 1 &&
               std::memcmp( name, "UiTexture", sizeof( "UiTexture" ) - 1 ) == 0 )
          {
            m_TextureBinding = uniform.bindings[ NVN_SHADER_STAGE_FRAGMENT ];
          }
          if ( uniform.kind == GLSLC_PIQ_UNIFORM_KIND_SAMPLER &&
               uniform.nameInfo.nameLength >= sizeof( "UiSampler" ) - 1 &&
               std::memcmp( name, "UiSampler", sizeof( "UiSampler" ) - 1 ) == 0 )
          {
            m_SamplerBinding = uniform.bindings[ NVN_SHADER_STAGE_FRAGMENT ];
          }
        }
        continue;
      }
      if ( header.genericHeader.common.type != GLSLC_SECTION_TYPE_GPU_CODE ) continue;
      const auto & gpu = header.gpuCodeHeader;
      if ( gpu.stage != NVN_SHADER_STAGE_VERTEX && gpu.stage != NVN_SHADER_STAGE_FRAGMENT ) return false;
      const size_t base = gpu.common.dataOffset;
      if ( !Fits( base, gpu.controlOffset, sizeof( bytes ) ) ||
           !Fits( base + gpu.controlOffset, gpu.controlSize, sizeof( bytes ) ) ||
           !Fits( base, gpu.dataOffset, sizeof( bytes ) ) ||
           !Fits( base + gpu.dataOffset, gpu.dataSize, sizeof( bytes ) ) || gpu.dataSize == 0 ) return false;
      const int slot = gpu.stage == NVN_SHADER_STAGE_VERTEX ? 0 : 1;
      if ( sections[ slot ].code != nullptr ) return false;
      sections[ slot ] = { bytes + base + gpu.controlOffset, bytes + base + gpu.dataOffset,
                           gpu.dataSize, gpu.scratchMemBytesRecommended };
      ++sectionCount;
    }
    if ( sectionCount != 2 || m_TextureBinding < 0 || m_SamplerBinding < 0 ||
         sections[ 0 ].scratchSize != 0 || sections[ 1 ].scratchSize != 0 )
    {
      diagnostics::Write( diagnostics::Level::Error, "NVN debug UI shader reflection or scratch requirement is unsupported." );
      return false;
    }
    int alignment{}, padding{};
    m_Device->GetInteger( ::nvn::DeviceInfo::SHADER_CODE_ALIGNMENT, &alignment );
    m_Device->GetInteger( ::nvn::DeviceInfo::SHADER_CODE_MEMORY_POOL_PADDING_SIZE, &padding );
    if ( alignment <= 0 || padding < 0 ) return false;
    const size_t fragmentOffset = AlignUp( sections[ 0 ].codeSize, static_cast<size_t>( alignment ) );
    const size_t poolSize = AlignUp( fragmentOffset + sections[ 1 ].codeSize + static_cast<size_t>( padding ),
                                     NVN_MEMORY_POOL_STORAGE_GRANULARITY );
    m_ShaderMemory = AllocateAligned( poolSize );
    if ( m_ShaderMemory == nullptr ) return false;
    ::nvn::MemoryPoolBuilder poolBuilder{};
    poolBuilder.SetDefaults().SetDevice( m_Device )
      .SetFlags( ::nvn::MemoryPoolFlags::CPU_UNCACHED | ::nvn::MemoryPoolFlags::GPU_CACHED |
                 ::nvn::MemoryPoolFlags::SHADER_CODE )
      .SetStorage( m_ShaderMemory, poolSize );
    if ( !m_ShaderPool.Initialize( &poolBuilder ) ) return false;
    m_ShaderPoolReady = true;
    auto * mapped = static_cast<unsigned char *>( m_ShaderPool.Map() );
    if ( mapped == nullptr ) return false;
    std::memcpy( mapped, sections[ 0 ].code, sections[ 0 ].codeSize );
    std::memcpy( mapped + fragmentOffset, sections[ 1 ].code, sections[ 1 ].codeSize );
    ::nvn::ShaderData shaderData[ 2 ]{};
    shaderData[ 0 ].data = m_ShaderPool.GetBufferAddress();
    shaderData[ 0 ].control = sections[ 0 ].control;
    shaderData[ 1 ].data = m_ShaderPool.GetBufferAddress() + fragmentOffset;
    shaderData[ 1 ].control = sections[ 1 ].control;
    if ( !m_Program.Initialize( m_Device ) ) return false;
    m_ProgramReady = true;
    return m_Program.SetShaders( 2, shaderData );
  }

  bool NvnDebugUi::CreateFontTexture() noexcept
  {
    unsigned char * pixels{};
    int width{}, height{}, channels{};
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32( &pixels, &width, &height, &channels );
    if ( pixels == nullptr || width <= 0 || height <= 0 || channels != 4 ) return false;
    ::nvn::TextureBuilder textureBuilder{};
    textureBuilder.SetDefaults().SetDevice( m_Device ).SetTarget( ::nvn::TextureTarget::TARGET_2D )
      .SetFormat( ::nvn::Format::RGBA8 ).SetSize2D( width, height );
    const size_t textureSize = textureBuilder.GetStorageSize();
    int textureDescriptorSize{}, samplerDescriptorSize{}, reservedTextures{}, reservedSamplers{};
    m_Device->GetInteger( ::nvn::DeviceInfo::TEXTURE_DESCRIPTOR_SIZE, &textureDescriptorSize );
    m_Device->GetInteger( ::nvn::DeviceInfo::SAMPLER_DESCRIPTOR_SIZE, &samplerDescriptorSize );
    m_Device->GetInteger( ::nvn::DeviceInfo::RESERVED_TEXTURE_DESCRIPTORS, &reservedTextures );
    m_Device->GetInteger( ::nvn::DeviceInfo::RESERVED_SAMPLER_DESCRIPTORS, &reservedSamplers );
    if ( textureSize == 0 || textureDescriptorSize <= 0 || samplerDescriptorSize <= 0 ||
         reservedTextures <= 0 || reservedSamplers <= 0 ) return false;
    const size_t textureDescriptorsOffset = AlignUp( textureSize, static_cast<size_t>( textureDescriptorSize ) );
    const size_t samplerDescriptorsOffset = AlignUp( textureDescriptorsOffset +
      static_cast<size_t>( reservedTextures + 1 ) * textureDescriptorSize,
      static_cast<size_t>( samplerDescriptorSize ) );
    const size_t poolSize = AlignUp( samplerDescriptorsOffset +
      static_cast<size_t>( reservedSamplers + 1 ) * samplerDescriptorSize,
      NVN_MEMORY_POOL_STORAGE_GRANULARITY );
    m_FontMemory = AllocateAligned( poolSize );
    if ( m_FontMemory == nullptr ) return false;
    ::nvn::MemoryPoolBuilder poolBuilder{};
    poolBuilder.SetDefaults().SetDevice( m_Device )
      .SetFlags( ::nvn::MemoryPoolFlags::CPU_UNCACHED | ::nvn::MemoryPoolFlags::GPU_CACHED )
      .SetStorage( m_FontMemory, poolSize );
    if ( !m_FontPool.Initialize( &poolBuilder ) ) return false;
    m_FontPoolReady = true;
    textureBuilder.SetStorage( &m_FontPool, 0 );
    if ( !m_FontTexture.Initialize( &textureBuilder ) ) return false;
    m_FontTextureReady = true;
    ::nvn::CopyRegion region{};
    region.width = width;
    region.height = height;
    region.depth = 1;
    m_FontTexture.WriteTexels( nullptr, &region, pixels );
    if ( !m_TextureDescriptors.Initialize( &m_FontPool,
        static_cast<ptrdiff_t>( textureDescriptorsOffset ), reservedTextures + 1 ) ) return false;
    m_TextureDescriptorsReady = true;
    m_TextureDescriptors.RegisterTexture( reservedTextures, &m_FontTexture, nullptr );
    ::nvn::SamplerBuilder samplerBuilder{};
    samplerBuilder.SetDefaults().SetDevice( m_Device )
      .SetMinMagFilter( ::nvn::MinFilter::LINEAR, ::nvn::MagFilter::LINEAR );
    if ( !m_Sampler.Initialize( &samplerBuilder ) ) return false;
    m_SamplerReady = true;
    if ( !m_SamplerDescriptors.Initialize( &m_FontPool,
        static_cast<ptrdiff_t>( samplerDescriptorsOffset ), reservedSamplers + 1 ) ) return false;
    m_SamplerDescriptorsReady = true;
    m_SamplerDescriptors.RegisterSampler( reservedSamplers, &m_Sampler );
    m_TextureHandle = m_Device->GetSeparateTextureHandle( reservedTextures );
    m_SamplerHandle = m_Device->GetSeparateSamplerHandle( reservedSamplers );
    ImGui::GetIO().Fonts->SetTexID( static_cast<ImTextureID>( 1 ) );
    return true;
  }

  bool NvnDebugUi::CreateGeometry() noexcept
  {
    static_assert( sizeof( ImDrawVert ) == 20 );
    static_assert( sizeof( ImDrawIdx ) == 2 );
    constexpr size_t stride = VertexBytesPerBuffer + IndexBytesPerBuffer;
    constexpr size_t poolSize = stride * BackbufferCount;
    m_GeometryMemory = AllocateAligned( poolSize );
    if ( m_GeometryMemory == nullptr ) return false;
    ::nvn::MemoryPoolBuilder poolBuilder{};
    poolBuilder.SetDefaults().SetDevice( m_Device )
      .SetFlags( ::nvn::MemoryPoolFlags::CPU_UNCACHED | ::nvn::MemoryPoolFlags::GPU_CACHED )
      .SetStorage( m_GeometryMemory, poolSize );
    if ( !m_GeometryPool.Initialize( &poolBuilder ) ) return false;
    m_GeometryPoolReady = true;
    ::nvn::BufferBuilder builder{};
    builder.SetDefaults().SetDevice( m_Device );
    for ( int index = 0; index < BackbufferCount; ++index )
    {
      builder.SetStorage( &m_GeometryPool, stride * index, VertexBytesPerBuffer );
      if ( !m_Vertices[ index ].Initialize( &builder ) ) return false;
      m_VerticesReady[ index ] = true;
      builder.SetStorage( &m_GeometryPool, stride * index + VertexBytesPerBuffer, IndexBytesPerBuffer );
      if ( !m_Indices[ index ].Initialize( &builder ) ) return false;
      m_IndicesReady[ index ] = true;
    }
    return true;
  }

  bool NvnDebugUi::Upload( const ImDrawData * data, int backbuffer, Extent2D extent ) noexcept
  {
    if ( !m_Ready || data == nullptr || backbuffer < 0 || backbuffer >= BackbufferCount ) return false;
    if ( static_cast<size_t>( data->TotalVtxCount ) * sizeof( ImDrawVert ) > VertexBytesPerBuffer ||
         static_cast<size_t>( data->TotalIdxCount ) * sizeof( ImDrawIdx ) > IndexBytesPerBuffer )
    {
      diagnostics::Write( diagnostics::Level::Warning, "NVN debug UI geometry capacity exceeded." );
      return false;
    }
    auto * mapped = static_cast<unsigned char *>( m_GeometryPool.Map() );
    if ( mapped == nullptr ) return false;
    const size_t stride = VertexBytesPerBuffer + IndexBytesPerBuffer;
    auto * vertices = reinterpret_cast<ImDrawVert *>( mapped + stride * backbuffer );
    auto * indices = mapped + stride * backbuffer + VertexBytesPerBuffer;
    size_t vertexOffset{}, indexOffset{};
    for ( int listIndex = 0; listIndex < data->CmdLists.Size; ++listIndex )
    {
      const ImDrawList * list = data->CmdLists[ listIndex ];
      for ( int v = 0; v < list->VtxBuffer.Size; ++v )
      {
        ImDrawVert vertex = list->VtxBuffer[ v ];
        vertex.pos.x = ( ( vertex.pos.x - data->DisplayPos.x ) / data->DisplaySize.x ) * 2.0f - 1.0f;
        vertex.pos.y = 1.0f - ( ( vertex.pos.y - data->DisplayPos.y ) / data->DisplaySize.y ) * 2.0f;
        vertices[ vertexOffset + v ] = vertex;
      }
      std::memcpy( indices + indexOffset * sizeof( ImDrawIdx ), list->IdxBuffer.Data,
                   static_cast<size_t>( list->IdxBuffer.Size ) * sizeof( ImDrawIdx ) );
      vertexOffset += list->VtxBuffer.Size;
      indexOffset += list->IdxBuffer.Size;
    }
    ( void )extent;
    return true;
  }

  void NvnDebugUi::BindState( ::nvn::CommandBuffer & commands, int backbuffer, Extent2D extent ) const noexcept
  {
    commands.SetViewport( 0, 0, static_cast<int>( extent.width ), static_cast<int>( extent.height ) );
    ::nvn::BlendState blend{};
    ::nvn::ChannelMaskState mask{};
    ::nvn::ColorState color{};
    ::nvn::DepthStencilState depth{};
    ::nvn::MultisampleState multisample{};
    ::nvn::PolygonState polygon{};
    blend.SetDefaults().SetBlendTarget( 0 )
      .SetBlendEquation( ::nvn::BlendEquation::ADD, ::nvn::BlendEquation::ADD )
      .SetBlendFunc( ::nvn::BlendFunc::SRC_ALPHA, ::nvn::BlendFunc::ONE_MINUS_SRC_ALPHA,
                     ::nvn::BlendFunc::ONE, ::nvn::BlendFunc::ONE_MINUS_SRC_ALPHA );
    mask.SetDefaults();
    color.SetDefaults().SetBlendEnable( 0, true );
    depth.SetDefaults().SetDepthTestEnable( false ).SetDepthWriteEnable( false );
    multisample.SetDefaults();
    polygon.SetDefaults().SetCullFace( ::nvn::Face::NONE );
    commands.BindBlendState( &blend );
    commands.BindChannelMaskState( &mask );
    commands.BindColorState( &color );
    commands.BindDepthStencilState( &depth );
    commands.BindMultisampleState( &multisample );
    commands.BindPolygonState( &polygon );
    commands.BindProgram( &m_Program, ::nvn::ShaderStageBits::VERTEX | ::nvn::ShaderStageBits::FRAGMENT );
    ::nvn::VertexAttribState attributes[ 3 ]{};
    attributes[ 0 ].SetDefaults().SetFormat( ::nvn::Format::RG32F, offsetof( ImDrawVert, pos ) ).SetStreamIndex( 0 );
    attributes[ 1 ].SetDefaults().SetFormat( ::nvn::Format::RG32F, offsetof( ImDrawVert, uv ) ).SetStreamIndex( 0 );
    attributes[ 2 ].SetDefaults().SetFormat( ::nvn::Format::RGBA8, offsetof( ImDrawVert, col ) ).SetStreamIndex( 0 );
    ::nvn::VertexStreamState stream{};
    stream.SetDefaults().SetStride( sizeof( ImDrawVert ) );
    commands.BindVertexAttribState( 3, attributes );
    commands.BindVertexStreamState( 1, &stream );
    commands.BindVertexBuffer( 0, m_Vertices[ backbuffer ].GetAddress(), VertexBytesPerBuffer );
    commands.SetTexturePool( &m_TextureDescriptors );
    commands.SetSamplerPool( &m_SamplerDescriptors );
    commands.BindSeparateTexture( ::nvn::ShaderStage::FRAGMENT, m_TextureBinding, m_TextureHandle );
    commands.BindSeparateSampler( ::nvn::ShaderStage::FRAGMENT, m_SamplerBinding, m_SamplerHandle );
  }

  void NvnDebugUi::RecordDraw( ::nvn::CommandBuffer & commands, const ImDrawData * data,
                                int backbuffer, Extent2D extent ) const noexcept
  {
    if ( !m_Ready || data == nullptr || data->TotalVtxCount == 0 ) return;
    BindState( commands, backbuffer, extent );
    size_t vertexOffset{}, indexOffset{};
    for ( int listIndex = 0; listIndex < data->CmdLists.Size; ++listIndex )
    {
      const ImDrawList * list = data->CmdLists[ listIndex ];
      for ( const ImDrawCmd & cmd : list->CmdBuffer )
      {
        if ( cmd.UserCallback != nullptr )
        {
          if ( cmd.UserCallback == ImDrawCallback_ResetRenderState ) BindState( commands, backbuffer, extent );
          else cmd.UserCallback( list, &cmd );
          continue;
        }
        if ( cmd.GetTexID() != static_cast<ImTextureID>( 1 ) ) continue;
        const int x0 = std::clamp( static_cast<int>( ( cmd.ClipRect.x - data->DisplayPos.x ) * data->FramebufferScale.x ),
                                   0, static_cast<int>( extent.width ) );
        const int y0 = std::clamp( static_cast<int>( ( cmd.ClipRect.y - data->DisplayPos.y ) * data->FramebufferScale.y ),
                                   0, static_cast<int>( extent.height ) );
        const int x1 = std::clamp( static_cast<int>( ( cmd.ClipRect.z - data->DisplayPos.x ) * data->FramebufferScale.x ),
                                   0, static_cast<int>( extent.width ) );
        const int y1 = std::clamp( static_cast<int>( ( cmd.ClipRect.w - data->DisplayPos.y ) * data->FramebufferScale.y ),
                                   0, static_cast<int>( extent.height ) );
        if ( x1 <= x0 || y1 <= y0 ) continue;
        commands.SetScissor( x0, y0, x1 - x0, y1 - y0 );
        commands.DrawElementsBaseVertex( ::nvn::DrawPrimitive::TRIANGLES,
          ::nvn::IndexType::UNSIGNED_SHORT, static_cast<int>( cmd.ElemCount ),
          m_Indices[ backbuffer ].GetAddress() + ( indexOffset + cmd.IdxOffset ) * sizeof( ImDrawIdx ),
          static_cast<int>( vertexOffset + cmd.VtxOffset ) );
      }
      vertexOffset += list->VtxBuffer.Size;
      indexOffset += list->IdxBuffer.Size;
    }
  }

  void NvnDebugUi::Shutdown() noexcept
  {
    m_Ready = false;
    for ( int i = 0; i < BackbufferCount; ++i )
    {
      if ( m_IndicesReady[ i ] ) m_Indices[ i ].Finalize();
      if ( m_VerticesReady[ i ] ) m_Vertices[ i ].Finalize();
      m_IndicesReady[ i ] = false;
      m_VerticesReady[ i ] = false;
    }
    if ( m_GeometryPoolReady ) m_GeometryPool.Finalize();
    m_GeometryPoolReady = false;
    std::free( m_GeometryMemory );
    m_GeometryMemory = nullptr;
    if ( m_SamplerDescriptorsReady ) m_SamplerDescriptors.Finalize();
    if ( m_SamplerReady ) m_Sampler.Finalize();
    if ( m_TextureDescriptorsReady ) m_TextureDescriptors.Finalize();
    if ( m_FontTextureReady ) m_FontTexture.Finalize();
    if ( m_FontPoolReady ) m_FontPool.Finalize();
    m_SamplerDescriptorsReady = false;
    m_SamplerReady = false;
    m_TextureDescriptorsReady = false;
    m_FontTextureReady = false;
    m_FontPoolReady = false;
    std::free( m_FontMemory );
    m_FontMemory = nullptr;
    if ( m_ProgramReady ) m_Program.Finalize();
    if ( m_ShaderPoolReady ) m_ShaderPool.Finalize();
    m_ProgramReady = false;
    m_ShaderPoolReady = false;
    std::free( m_ShaderMemory );
    m_ShaderMemory = nullptr;
    m_TextureBinding = -1;
    m_SamplerBinding = -1;
    m_Device = nullptr;
  }
} // namespace apollo::render::nvn
