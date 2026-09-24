#include "Mii/MiiNvnHeadRenderer.hpp"
#include "Mii/PreviewScene.hpp"

#include "Platform/Diagnostics.hpp"

#include <nn/fs.h>
#include <nn/util/util_BinaryFormat.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <new>
#include <nvn/nvn_CppMethods.h>

namespace apollo::mii
{
  namespace
  {
    constexpr char   ShaderPath[]       = "Contents:/Platform/shader/MiiHead.bnsh";
    constexpr size_t MaximumShaderBytes = 16 * 1024 * 1024;
    constexpr size_t MaximumPoolBytes   = 32 * 1024 * 1024;

    struct CameraBlock
    {
      float clipFromModel[ 16 ]{};
    };

    struct MaterialBlock
    {
      int   mode[ 4 ]{};
      float colors[ 3 ][ 4 ]{};
    };

    [[nodiscard]] size_t RoundUp( size_t value, size_t alignment ) noexcept
    {
      if ( alignment == 0 || value > std::numeric_limits<size_t>::max() - ( alignment - 1 ) )
      {
        return 0;
      }

      return ( value + alignment - 1 ) / alignment * alignment;
    }

    [[nodiscard]] void * AllocateAligned( size_t size, size_t alignment ) noexcept
    {
      alignment            = std::max( alignment, alignof( std::max_align_t ) );
      const size_t rounded = RoundUp( size, alignment );
      return rounded == 0 ? nullptr : aligned_alloc( alignment, rounded );
    }

  } // namespace

  NvnHeadRenderer::~NvnHeadRenderer() noexcept
  {
    Shutdown();
  }

  bool NvnHeadRenderer::Initialize( nn::gfx::Device &    device,
                                    ::nvn::Queue &       queue,
                                    nn::mii::CharModel & model,
                                    NvnFaceRenderer &    faces ) noexcept
  {
    if ( m_Device != nullptr || !model.IsInitialized() || !faces.IsReady() )
    {
      return false;
    }

    m_Device   = &device;
    m_NvnQueue = &queue;
    m_Model    = &model;
    m_Faces    = &faces;

    if ( !LoadShader() || !CreateStates() || !CreateUniforms() )
    {
      Shutdown();
      return false;
    }

    m_Ready = true;
    diagnostics::Write( diagnostics::Level::Information, "Mii head renderer ready." );
    return true;
  }

  bool NvnHeadRenderer::LoadShader() noexcept
  {
    nn::fs::FileHandle handle{};
    if ( !nn::fs::OpenFile( &handle, ShaderPath, nn::fs::OpenMode_Read ).IsSuccess() )
    {
      diagnostics::Write( diagnostics::Level::Error, "MiiHead.bnsh is missing from Contents." );
      return false;
    }

    int64_t                    fileSize{};
    nn::util::BinaryFileHeader header{};
    const bool                 fileReady = nn::fs::GetFileSize( &fileSize, handle ).IsSuccess() &&
                           fileSize >= static_cast<int64_t>( sizeof( header ) ) &&
                           fileSize <= static_cast<int64_t>( MaximumShaderBytes ) &&
                           nn::fs::ReadFile( handle, 0, &header, sizeof( header ) ).IsSuccess();
    if ( !fileReady )
    {
      nn::fs::CloseFile( handle );
      diagnostics::Write( diagnostics::Level::Error, "Mii head shader file is invalid." );
      return false;
    }

    m_ShaderMemory    = AllocateAligned( static_cast<size_t>( fileSize ), header.GetAlignment() );
    const bool loaded = m_ShaderMemory != nullptr &&
                        nn::fs::ReadFile( handle, 0, m_ShaderMemory, static_cast<size_t>( fileSize ) ).IsSuccess();
    nn::fs::CloseFile( handle );

    if ( !loaded || !nn::gfx::ResShaderFile::IsValid( m_ShaderMemory ) )
    {
      diagnostics::Write( diagnostics::Level::Error, "Mii head shader could not be read or validated." );
      return false;
    }

    m_ShaderFile                            = nn::gfx::ResShaderFile::ResCast( m_ShaderMemory );
    nn::gfx::ResShaderContainer * container = m_ShaderFile->GetShaderContainer();
    if ( container->GetShaderVariationCount() != 1 )
    {
      diagnostics::Write( diagnostics::Level::Error, "Mii head shader variation count is unsupported." );
      return false;
    }

    nn::gfx::ResShaderVariation * variation = container->GetResShaderVariation( 0 );
    nn::gfx::ResShaderProgram *   program{};
    for ( int index = 0; index < nn::gfx::ShaderCodeType_End; ++index )
    {
      const auto type = static_cast<nn::gfx::ShaderCodeType>( index );
      program         = variation->GetResShaderProgram( type );
      if ( program != nullptr )
      {
        m_ShaderCodeType = type;
        break;
      }
    }

    if ( program == nullptr )
    {
      diagnostics::Write( diagnostics::Level::Error, "Mii head shader has no supported program." );
      return false;
    }

    container->Initialize( m_Device );
    m_ShaderContainerReady = true;
    if ( program->Initialize( m_Device ) != nn::gfx::ShaderInitializeResult_Success )
    {
      diagnostics::Write( diagnostics::Level::Error, "Mii head shader program initialization failed." );
      return false;
    }
    m_ShaderProgramReady = true;
    m_Shader             = program->GetShader();
    m_CameraSlot =
      m_Shader->GetInterfaceSlot( nn::gfx::ShaderStage_Vertex, nn::gfx::ShaderInterfaceType_ConstantBuffer, "Camera" );
    m_MaterialSlot =
      m_Shader->GetInterfaceSlot( nn::gfx::ShaderStage_Pixel, nn::gfx::ShaderInterfaceType_ConstantBuffer, "Material" );
    m_TextureSlot =
      m_Shader->GetInterfaceSlot( nn::gfx::ShaderStage_Pixel, nn::gfx::ShaderInterfaceType_Sampler, "surfaceTexture" );

    if ( m_CameraSlot < 0 || m_MaterialSlot < 0 || m_TextureSlot < 0 )
    {
      diagnostics::Write( diagnostics::Level::Error, "Mii head shader interface is incomplete." );
      return false;
    }

    return true;
  }

  bool NvnHeadRenderer::CreateStates() noexcept
  {
    nn::gfx::BlendTargetStateInfo blendTarget[ 2 ];
    for ( auto & target : blendTarget )
    {
      target.SetDefault();
    }
    blendTarget[ 1 ].SetBlendEnabled( true );
    blendTarget[ 1 ].SetColorBlendFunction( nn::gfx::BlendFunction_Add );
    blendTarget[ 1 ].SetSourceColorBlendFactor( nn::gfx::BlendFactor_SourceAlpha );
    blendTarget[ 1 ].SetDestinationColorBlendFactor( nn::gfx::BlendFactor_OneMinusSourceAlpha );

    nn::gfx::BlendState::InfoType blendInfo[ 2 ];
    for ( int index = 0; index < 2; ++index )
    {
      blendInfo[ index ].SetDefault();
      blendInfo[ index ].SetBlendTargetStateInfoArray( &blendTarget[ index ], 1 );
      const size_t size = nn::gfx::BlendState::GetRequiredMemorySize( blendInfo[ index ] );
      m_BlendMemory[ index ] =
        AllocateAligned( std::max<size_t>( size, 1 ), nn::gfx::BlendState::RequiredMemoryInfo_Alignment );
      if ( m_BlendMemory[ index ] == nullptr )
      {
        diagnostics::Write( diagnostics::Level::Error, "Mii head blend state allocation failed." );
        return false;
      }
    }

    nn::gfx::VertexAttributeStateInfo attributes[ 2 ];
    attributes[ 0 ].SetDefault();
    attributes[ 0 ].SetBufferIndex( 0 );
    attributes[ 0 ].SetFormat( nn::mii::DrawParam::PositionFormat );
    attributes[ 0 ].SetOffset( nn::mii::DrawParam::PositionOffset );
    attributes[ 0 ].SetShaderSlot( 0 );
    attributes[ 1 ].SetDefault();
    attributes[ 1 ].SetBufferIndex( 1 );
    attributes[ 1 ].SetFormat( nn::mii::DrawParam::UvFormat );
    attributes[ 1 ].SetOffset( nn::mii::DrawParam::UvOffset );
    attributes[ 1 ].SetShaderSlot( 1 );

    nn::gfx::VertexBufferStateInfo buffers[ 2 ];
    buffers[ 0 ].SetDefault();
    buffers[ 0 ].SetStride( nn::mii::DrawParam::PositionStride );
    buffers[ 1 ].SetDefault();
    buffers[ 1 ].SetStride( nn::mii::DrawParam::UvStride );

    nn::gfx::VertexState::InfoType vertexInfo;
    vertexInfo.SetDefault();
    vertexInfo.SetVertexAttributeStateInfoArray( attributes, 2 );
    vertexInfo.SetVertexBufferStateInfoArray( buffers, 2 );
    const size_t vertexSize = nn::gfx::VertexState::GetRequiredMemorySize( vertexInfo );
    m_VertexStateMemory =
      AllocateAligned( std::max<size_t>( vertexSize, 1 ), nn::gfx::VertexState::RequiredMemoryInfo_Alignment );
    if ( m_VertexStateMemory == nullptr )
    {
      diagnostics::Write( diagnostics::Level::Error, "Mii head vertex state allocation failed." );
      return false;
    }

    nn::gfx::RasterizerState::InfoType rasterInfo;
    rasterInfo.SetDefault();
    rasterInfo.SetPrimitiveTopologyType( nn::gfx::PrimitiveTopologyType_Triangle );
    rasterInfo.SetScissorEnabled( true );
    rasterInfo.SetDepthClipEnabled( false );
    for ( int index = 0; index < nn::gfx::CullMode_End; ++index )
    {
      rasterInfo.SetCullMode( static_cast<nn::gfx::CullMode>( index ) );
      m_Rasterizer[ index ].Initialize( m_Device, rasterInfo );
    }

    for ( int index = 0; index < 2; ++index )
    {
      m_Blend[ index ].SetMemory( m_BlendMemory[ index ],
                                  nn::gfx::BlendState::GetRequiredMemorySize( blendInfo[ index ] ) );
      m_Blend[ index ].Initialize( m_Device, blendInfo[ index ] );

      nn::gfx::DepthStencilState::InfoType depthInfo;
      depthInfo.SetDefault();
      depthInfo.SetDepthTestEnabled( true );
      depthInfo.SetDepthWriteEnabled( index == 0 );
      m_DepthState[ index ].Initialize( m_Device, depthInfo );
    }

    m_VertexState.SetMemory( m_VertexStateMemory, vertexSize );
    m_VertexState.Initialize( m_Device, vertexInfo, nullptr );
    m_StatesReady = true;
    return true;
  }

  bool NvnHeadRenderer::CreateUniforms() noexcept
  {
    nn::gfx::Buffer::InfoType cameraInfo;
    cameraInfo.SetDefault();
    cameraInfo.SetGpuAccessFlags( nn::gfx::GpuAccess_ConstantBuffer );
    cameraInfo.SetSize( sizeof( CameraBlock ) );
    nn::gfx::Buffer::InfoType materialInfo;
    materialInfo.SetDefault();
    materialInfo.SetGpuAccessFlags( nn::gfx::GpuAccess_ConstantBuffer );
    materialInfo.SetSize( sizeof( MaterialBlock ) );

    const size_t cameraAlignment   = nn::gfx::Buffer::GetBufferAlignment( m_Device, cameraInfo );
    const size_t materialAlignment = nn::gfx::Buffer::GetBufferAlignment( m_Device, materialInfo );
    const size_t cameraStride      = RoundUp( sizeof( CameraBlock ), cameraAlignment );
    const size_t materialStart     = RoundUp( cameraStride * BackbufferCount, materialAlignment );
    const size_t materialStride    = RoundUp( sizeof( MaterialBlock ), materialAlignment );

    nn::gfx::MemoryPool::InfoType poolInfo;
    poolInfo.SetDefault();
    poolInfo.SetMemoryPoolProperty( nn::gfx::MemoryPoolProperty_CpuUncached | nn::gfx::MemoryPoolProperty_GpuCached );
    const size_t poolAlignment = nn::gfx::MemoryPool::GetPoolMemoryAlignment( m_Device, poolInfo );
    const size_t granularity   = nn::gfx::MemoryPool::GetPoolMemorySizeGranularity( m_Device, poolInfo );
    const size_t poolSize      = RoundUp( materialStart + materialStride * DrawTypeCount, granularity );

    if ( cameraStride == 0 || materialStart == 0 || materialStride == 0 || poolSize == 0 ||
         poolSize > MaximumPoolBytes || poolAlignment == 0 )
    {
      diagnostics::Write( diagnostics::Level::Error, "Mii head uniform pool requirements are unsupported." );
      return false;
    }

    m_UniformMemory = AllocateAligned( poolSize, poolAlignment );
    if ( m_UniformMemory == nullptr )
    {
      diagnostics::Write( diagnostics::Level::Error, "Mii head uniform pool allocation failed." );
      return false;
    }

    m_UniformAllocation.Acquire( poolSize );
    poolInfo.SetPoolMemory( m_UniformMemory, poolSize );
    m_UniformPool.Initialize( m_Device, poolInfo );
    m_UniformPoolReady = true;

    for ( int index = 0; index < BackbufferCount; ++index )
    {
      m_Cameras[ index ].Initialize(
        m_Device, cameraInfo, &m_UniformPool, static_cast<ptrdiff_t>( cameraStride * index ), sizeof( CameraBlock ) );
      ++m_CamerasReady;
    }

    for ( int index = 0; index < DrawTypeCount; ++index )
    {
      m_Materials[ index ].Initialize( m_Device,
                                       materialInfo,
                                       &m_UniformPool,
                                       static_cast<ptrdiff_t>( materialStart + materialStride * index ),
                                       sizeof( MaterialBlock ) );
      ++m_MaterialsReady;

      const auto                 type = static_cast<nn::mii::CharModel::DrawType>( index );
      const nn::mii::DrawParam * part =
        m_Model->GetDrawParam( type, nn::mii::CreateModelType_Normal, nn::mii::CreateNoseType_Normal, 0 );
      if ( part == nullptr )
      {
        continue;
      }

      MaterialBlock * material = m_Materials[ index ].Map<MaterialBlock>();
      *material                = {};
      material->mode[ 0 ]      = static_cast<int>( part->GetModulateType() );
      for ( int color = 0; color < static_cast<int>( nn::mii::DrawParam::ConstantColorNum ); ++color )
      {
        const nn::mii::Color3 * source = part->GetConstantColor( color );
        if ( source != nullptr )
        {
          material->colors[ color ][ 0 ] = source->r;
          material->colors[ color ][ 1 ] = source->g;
          material->colors[ color ][ 2 ] = source->b;
          material->colors[ color ][ 3 ] = 1.0f;
        }
      }
      m_Materials[ index ].Unmap();
    }

    return true;
  }

  bool NvnHeadRenderer::CreateDepth( render::Extent2D extent ) noexcept
  {
    nn::gfx::Texture::InfoType textureInfo;
    textureInfo.SetDefault();
    textureInfo.SetImageStorageDimension( nn::gfx::ImageStorageDimension_2d );
    textureInfo.SetImageFormat( nn::gfx::ImageFormat_D24_Unorm_S8_Uint );
    textureInfo.SetGpuAccessFlags( nn::gfx::GpuAccess_DepthStencil );
    textureInfo.SetTileMode( nn::gfx::TileMode_Optimal );
    textureInfo.SetWidth( static_cast<int>( extent.width ) );
    textureInfo.SetHeight( static_cast<int>( extent.height ) );

    const size_t textureAlignment = nn::gfx::Texture::CalculateMipDataAlignment( m_Device, textureInfo );
    const size_t textureSize      = nn::gfx::Texture::CalculateMipDataSize( m_Device, textureInfo );
    nn::gfx::MemoryPool::InfoType poolInfo;
    poolInfo.SetDefault();
    poolInfo.SetMemoryPoolProperty( nn::gfx::MemoryPoolProperty_CpuCached | nn::gfx::MemoryPoolProperty_GpuCached |
                                    nn::gfx::MemoryPoolProperty_Compressible );
    const size_t poolAlignment = nn::gfx::MemoryPool::GetPoolMemoryAlignment( m_Device, poolInfo );
    const size_t granularity   = nn::gfx::MemoryPool::GetPoolMemorySizeGranularity( m_Device, poolInfo );
    const size_t poolSize      = RoundUp( textureSize, granularity );

    if ( textureAlignment == 0 || textureSize == 0 || poolAlignment == 0 || poolSize == 0 ||
         poolSize > MaximumPoolBytes )
    {
      diagnostics::Write( diagnostics::Level::Error, "Mii head depth target requirements are unsupported." );
      return false;
    }

    m_DepthMemory = AllocateAligned( poolSize, std::max( textureAlignment, poolAlignment ) );
    if ( m_DepthMemory == nullptr )
    {
      diagnostics::Write( diagnostics::Level::Error, "Mii head depth target allocation failed." );
      return false;
    }

    m_DepthAllocation.Acquire( poolSize );
    poolInfo.SetPoolMemory( m_DepthMemory, poolSize );
    m_DepthPool.Initialize( m_Device, poolInfo );
    m_DepthPoolReady = true;
    m_DepthTexture.Initialize( m_Device, textureInfo, &m_DepthPool, 0, textureSize );
    m_DepthTextureReady = true;

    nn::gfx::DepthStencilView::InfoType viewInfo;
    viewInfo.SetDefault();
    viewInfo.SetImageDimension( nn::gfx::ImageDimension_2d );
    viewInfo.SetMipLevel( 0 );
    viewInfo.SetTexturePtr( &m_DepthTexture );
    m_DepthView.Initialize( m_Device, viewInfo );
    m_DepthViewReady = true;
    return true;
  }

  bool NvnHeadRenderer::CreateViewport( render::Extent2D extent ) noexcept
  {
    const render::Extent2D viewport = PreviewScene::HeadViewport( extent );
    if ( !viewport.IsValid() )
    {
      return false;
    }

    nn::gfx::ViewportStateInfo viewportInfo;
    viewportInfo.SetDefault();
    viewportInfo.SetOriginX( 0.0f );
    viewportInfo.SetOriginY( 0.0f );
    viewportInfo.SetWidth( static_cast<float>( viewport.width ) );
    viewportInfo.SetHeight( static_cast<float>( viewport.height ) );

    nn::gfx::ScissorStateInfo scissorInfo;
    scissorInfo.SetDefault();
    scissorInfo.SetOriginX( 0 );
    scissorInfo.SetOriginY( 0 );
    scissorInfo.SetWidth( static_cast<int>( viewport.width ) );
    scissorInfo.SetHeight( static_cast<int>( viewport.height ) );

    nn::gfx::ViewportScissorState::InfoType stateInfo;
    stateInfo.SetDefault();
    stateInfo.SetScissorEnabled( true );
    stateInfo.SetViewportStateInfoArray( &viewportInfo, 1 );
    stateInfo.SetScissorStateInfoArray( &scissorInfo, 1 );
    m_Viewport.Initialize( m_Device, stateInfo );
    m_ViewportReady = true;
    return true;
  }

  bool NvnHeadRenderer::CreateTarget( ::nvn::Texture & target, render::Extent2D extent, int backbuffer ) noexcept
  {
    if ( !m_DepthViewReady )
    {
      if ( !CreateDepth( extent ) || !CreateViewport( extent ) )
      {
        return false;
      }
      m_Extent = extent;
    }

    m_Targets[ backbuffer ] = new ( std::nothrow ) nn::gfx::Texture{};
    if ( m_Targets[ backbuffer ] == nullptr )
    {
      return false;
    }
    nn::gfx::TInteroperation<nn::gfx::ApiVariationNvn8>::ConvertToGfxTexture(
      m_Targets[ backbuffer ], reinterpret_cast<NVNtexture *>( &target ) );

    nn::gfx::ColorTargetView::InfoType viewInfo;
    viewInfo.SetDefault();
    viewInfo.SetImageDimension( nn::gfx::ImageDimension_2d );
    viewInfo.SetImageFormat( nn::gfx::ImageFormat_R8_G8_B8_A8_Unorm );
    viewInfo.SetTexturePtr( m_Targets[ backbuffer ] );
    m_ColorViews[ backbuffer ].Initialize( m_Device, viewInfo );
    m_ColorViewsReady[ backbuffer ] = true;
    return true;
  }

  bool NvnHeadRenderer::RecordDraw( ::nvn::CommandBuffer & commands,
                                    ::nvn::Texture &       target,
                                    render::Extent2D       extent,
                                    int                    backbuffer,
                                    const PreviewCamera &  camera ) noexcept
  {
    if ( !m_Ready || !extent.IsValid() || backbuffer < 0 || backbuffer >= BackbufferCount )
    {
      return false;
    }

    if ( m_Extent.IsValid() && ( m_Extent.width != extent.width || m_Extent.height != extent.height ) )
    {
      diagnostics::Write( diagnostics::Level::Error, "Mii head targets were not reset before resize." );
      return false;
    }

    if ( !m_ColorViewsReady[ backbuffer ] && !CreateTarget( target, extent, backbuffer ) )
    {
      diagnostics::Write( diagnostics::Level::Error, "Mii head target initialization failed." );
      return false;
    }

    UpdateCamera( backbuffer, extent, camera );

    nn::gfx::CommandBuffer gfxCommands{};
    nn::gfx::TInteroperation<nn::gfx::ApiVariationNvn8>::ConvertToGfxCommandBuffer(
      &gfxCommands, m_Device, reinterpret_cast<NVNcommandBuffer *>( &commands ) );
    nn::gfx::ColorTargetView * colorView = &m_ColorViews[ backbuffer ];
    gfxCommands.ClearDepthStencil( &m_DepthView, 1.0f, 0, nn::gfx::DepthStencilClearMode_DepthStencil, nullptr );
    gfxCommands.SetRenderTargets( 1, &colorView, &m_DepthView );
    gfxCommands.SetViewportScissorState( &m_Viewport );
    gfxCommands.SetDescriptorPool( m_Faces->TextureDescriptors() );
    gfxCommands.SetDescriptorPool( m_Faces->SamplerDescriptors() );
    gfxCommands.InvalidateMemory( nn::gfx::GpuAccess_Texture | nn::gfx::GpuAccess_IndexBuffer |
                                  nn::gfx::GpuAccess_ConstantBuffer | nn::gfx::GpuAccess_VertexBuffer );
    DrawParts( gfxCommands, backbuffer );

    if ( !m_FirstDrawReported )
    {
      diagnostics::Write( diagnostics::Level::Information, "Mii head draw commands recorded." );
      m_FirstDrawReported = true;
    }
    return true;
  }

  void NvnHeadRenderer::UpdateCamera( int backbuffer, render::Extent2D extent, const PreviewCamera & camera ) noexcept
  {
    CameraBlock * uniform = m_Cameras[ backbuffer ].Map<CameraBlock>();
    PreviewScene::MakeCameraMatrix( uniform->clipFromModel, extent, camera, PreviewClipSpace::Nvn );
    m_Cameras[ backbuffer ].Unmap();
  }

  void NvnHeadRenderer::DrawParts( nn::gfx::CommandBuffer & commands, int backbuffer ) noexcept
  {
    for ( int index = 0; index < DrawTypeCount; ++index )
    {
      const auto                 type = static_cast<nn::mii::CharModel::DrawType>( index );
      const nn::mii::DrawParam * part =
        m_Model->GetDrawParam( type, nn::mii::CreateModelType_Normal, nn::mii::CreateNoseType_Normal, 0 );
      if ( part != nullptr )
      {
        DrawPart( commands, *part, index, backbuffer );
      }
    }
  }

  void NvnHeadRenderer::DrawPart( nn::gfx::CommandBuffer &   commands,
                                  const nn::mii::DrawParam & part,
                                  int                        drawType,
                                  int                        backbuffer ) noexcept
  {
    const int blend = drawType >= nn::mii::CharModel::DrawType_XluMin ? 1 : 0;
    const int cull  = static_cast<int>( part.GetCullMode() );
    if ( cull < 0 || cull >= nn::gfx::CullMode_End )
    {
      return;
    }

    commands.SetRasterizerState( &m_Rasterizer[ cull ] );
    commands.SetBlendState( &m_Blend[ blend ] );
    commands.SetDepthStencilState( &m_DepthState[ blend ] );
    commands.SetVertexState( &m_VertexState );
    commands.SetShader( m_Shader, nn::gfx::ShaderStageBit_All );

    nn::gfx::GpuAddress address;
    m_Cameras[ backbuffer ].GetGpuAddress( &address );
    commands.SetConstantBuffer( m_CameraSlot, nn::gfx::ShaderStage_Vertex, address, sizeof( CameraBlock ) );
    m_Materials[ drawType ].GetGpuAddress( &address );
    commands.SetConstantBuffer( m_MaterialSlot, nn::gfx::ShaderStage_Pixel, address, sizeof( MaterialBlock ) );

    if ( part.IsValidTexture() )
    {
      commands.SetTextureAndSampler(
        m_TextureSlot, nn::gfx::ShaderStage_Pixel, *part.GetTextureDescriptorSlot(), m_Faces->SamplerSlot() );
    }

    const nn::gfx::Buffer * position = part.GetAttribute( nn::mii::DrawParam::AttributeType_Position );
    const nn::gfx::Buffer * indices  = part.GetIndexBuffer();
    if ( position == nullptr || indices == nullptr || part.GetIndexCount() <= 0 )
    {
      return;
    }

    const size_t positionBytes = part.GetBufferSize( nn::mii::DrawParam::AttributeType_Position );
    position->GetGpuAddress( &address );
    commands.SetVertexBuffer( 0, address, nn::mii::DrawParam::PositionStride, positionBytes );

    const nn::gfx::Buffer * uv = part.GetAttribute( nn::mii::DrawParam::AttributeType_Uv );
    const size_t uvBytes = uv != nullptr ? part.GetBufferSize( nn::mii::DrawParam::AttributeType_Uv ) : positionBytes;
    if ( uv == nullptr )
    {
      uv = position;
    }
    uv->GetGpuAddress( &address );
    commands.SetVertexBuffer( 1, address, nn::mii::DrawParam::UvStride, uvBytes );

    indices->GetGpuAddress( &address );
    commands.DrawIndexed(
      nn::mii::DrawParam::PrimitiveTopology, nn::mii::DrawParam::IndexFormat, address, part.GetIndexCount(), 0 );
  }

  void NvnHeadRenderer::ResetTargets() noexcept
  {
    if ( m_NvnQueue != nullptr && m_Extent.IsValid() )
    {
      m_NvnQueue->Finish();
    }

    for ( int index = 0; index < BackbufferCount; ++index )
    {
      if ( m_ColorViewsReady[ index ] )
      {
        m_ColorViews[ index ].Finalize( m_Device );
        m_ColorViewsReady[ index ] = false;
      }
      // Converted textures borrow the presenter's NVN objects.
      delete m_Targets[ index ];
      m_Targets[ index ] = nullptr;
    }

    if ( m_ViewportReady )
    {
      m_Viewport.Finalize( m_Device );
    }
    if ( m_DepthViewReady )
    {
      m_DepthView.Finalize( m_Device );
    }
    if ( m_DepthTextureReady )
    {
      m_DepthTexture.Finalize( m_Device );
    }
    if ( m_DepthPoolReady )
    {
      m_DepthPool.Finalize( m_Device );
    }
    std::free( m_DepthMemory );
    m_DepthAllocation.Release();

    m_ViewportReady     = false;
    m_DepthViewReady    = false;
    m_DepthTextureReady = false;
    m_DepthPoolReady    = false;
    m_DepthMemory       = nullptr;
    m_Extent            = {};
  }

  void NvnHeadRenderer::Shutdown() noexcept
  {
    if ( m_Device == nullptr )
    {
      return;
    }

    m_Ready = false;
    ResetTargets();

    for ( int index = 0; index < m_MaterialsReady; ++index )
    {
      m_Materials[ index ].Finalize( m_Device );
    }
    for ( int index = 0; index < m_CamerasReady; ++index )
    {
      m_Cameras[ index ].Finalize( m_Device );
    }
    if ( m_UniformPoolReady )
    {
      m_UniformPool.Finalize( m_Device );
    }
    std::free( m_UniformMemory );
    m_UniformAllocation.Release();
    m_MaterialsReady   = 0;
    m_CamerasReady     = 0;
    m_UniformPoolReady = false;
    m_UniformMemory    = nullptr;

    if ( m_StatesReady )
    {
      m_VertexState.Finalize( m_Device );
      for ( int index = 0; index < 2; ++index )
      {
        m_DepthState[ index ].Finalize( m_Device );
        m_Blend[ index ].Finalize( m_Device );
      }
      for ( int index = 0; index < nn::gfx::CullMode_End; ++index )
      {
        m_Rasterizer[ index ].Finalize( m_Device );
      }
    }
    m_StatesReady = false;
    std::free( m_VertexStateMemory );
    m_VertexStateMemory = nullptr;
    for ( void *& memory : m_BlendMemory )
    {
      std::free( memory );
      memory = nullptr;
    }

    if ( m_ShaderProgramReady )
    {
      m_ShaderFile->GetShaderContainer()
        ->GetResShaderVariation( 0 )
        ->GetResShaderProgram( m_ShaderCodeType )
        ->Finalize( m_Device );
    }
    if ( m_ShaderContainerReady )
    {
      m_ShaderFile->GetShaderContainer()->Finalize( m_Device );
    }
    std::free( m_ShaderMemory );

    m_ShaderProgramReady   = false;
    m_ShaderContainerReady = false;
    m_ShaderMemory         = nullptr;
    m_ShaderFile           = nullptr;
    m_Shader               = nullptr;
    m_Faces                = nullptr;
    m_Model                = nullptr;
    m_NvnQueue             = nullptr;
    m_Device               = nullptr;
    m_FirstDrawReported    = false;
  }
} // namespace apollo::mii
