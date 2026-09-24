#include "Mii/MiiNvnFaceRenderer.hpp"

#include "Platform/Diagnostics.hpp"

#include <nn/fs.h>
#include <nn/util/util_BinaryFormat.h>
#include <nvn/nvn_FuncPtr.h>

#include <cstdlib>
#include <limits>

namespace apollo::mii
{
  namespace
  {
    constexpr char   ShaderPath[]           = "Contents:/Platform/shader/TextureShader.bnsh";
    constexpr size_t MaximumShaderBytes     = 16 * 1024 * 1024;
    constexpr size_t MaximumPoolBytes       = 16 * 1024 * 1024;
    constexpr size_t CommandMemoryBytes     = 1024 * 1024;
    constexpr size_t CommandControlBytes    = 1024;
    constexpr int    TextureResolution      = 512;
    constexpr int    TextureMipCount        = 1;
    constexpr int    TextureDescriptorCount = 100;

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
      if ( alignment < alignof( std::max_align_t ) )
      {
        alignment = alignof( std::max_align_t );
      }

      const size_t rounded = RoundUp( size, alignment );
      if ( rounded == 0 )
      {
        return nullptr;
      }

      return aligned_alloc( alignment, rounded );
    }
  } // namespace

  NvnFaceRenderer::~NvnFaceRenderer() noexcept
  {
    Shutdown();
  }

  bool NvnFaceRenderer::Initialize( nn::gfx::Device &    device,
                                    nn::mii::CharModel & model,
                                    nn::mii::Faceline &  faceline,
                                    nn::mii::Mask &      mask ) noexcept
  {
    if ( m_Device != nullptr || !model.IsInitialized() || !faceline.IsInitialized() || !mask.IsInitialized() )
    {
      return false;
    }

    m_Device = &device;

    if ( !LoadShaderFile() || !InitializeTextureShader() || !InitializeGpuBuffers() ||
         !InitializeDescriptors( model, faceline, mask ) || !InitializeCommands() )
    {
      Shutdown();
      return false;
    }

    DrawTextures( model, faceline, mask );
    m_TexturesReady = true;
    diagnostics::Write( diagnostics::Level::Information,
                        "Mii faceline and Normal/Smile mask textures generated on the GPU." );
    return true;
  }

  bool NvnFaceRenderer::LoadShaderFile() noexcept
  {
    size_t cacheSize = 0;
    if ( !nn::fs::QueryMountRomCacheSize( &cacheSize ).IsSuccess() )
    {
      diagnostics::Write( diagnostics::Level::Error, "Mii shader ROM cache query failed." );
      return false;
    }

    m_RomCache = std::malloc( cacheSize > 0 ? cacheSize : 1 );
    if ( m_RomCache == nullptr || !nn::fs::MountRom( "Contents", m_RomCache, cacheSize ).IsSuccess() )
    {
      diagnostics::Write( diagnostics::Level::Error, "Mii shader ROM mount failed." );
      return false;
    }
    m_RomMounted = true;

    nn::fs::FileHandle handle{};
    if ( !nn::fs::OpenFile( &handle, ShaderPath, nn::fs::OpenMode_Read ).IsSuccess() )
    {
      diagnostics::Write( diagnostics::Level::Error, "Mii TextureShader.bnsh is missing from Contents." );
      return false;
    }

    int64_t                    fileSize = 0;
    nn::util::BinaryFileHeader header{};
    const bool                 fileReady = nn::fs::GetFileSize( &fileSize, handle ).IsSuccess() &&
                           fileSize >= static_cast<int64_t>( sizeof( header ) ) &&
                           fileSize <= static_cast<int64_t>( MaximumShaderBytes ) &&
                           nn::fs::ReadFile( handle, 0, &header, sizeof( header ) ).IsSuccess();
    if ( !fileReady )
    {
      nn::fs::CloseFile( handle );
      diagnostics::Write( diagnostics::Level::Error, "Mii texture shader file is invalid." );
      return false;
    }

    m_ShaderMemory    = AllocateAligned( static_cast<size_t>( fileSize ), header.GetAlignment() );
    const bool loaded = m_ShaderMemory != nullptr &&
                        nn::fs::ReadFile( handle, 0, m_ShaderMemory, static_cast<size_t>( fileSize ) ).IsSuccess();
    nn::fs::CloseFile( handle );

    if ( !loaded || !nn::gfx::ResShaderFile::IsValid( m_ShaderMemory ) )
    {
      diagnostics::Write( diagnostics::Level::Error, "Mii texture shader could not be read or validated." );
      return false;
    }

    m_ShaderFile                         = nn::gfx::ResShaderFile::ResCast( m_ShaderMemory );
    nn::gfx::ResShaderContainer * shader = m_ShaderFile->GetShaderContainer();
    if ( shader->GetShaderVariationCount() <= 0 )
    {
      diagnostics::Write( diagnostics::Level::Error, "Mii texture shader has no variations." );
      return false;
    }

    nn::gfx::ResShaderVariation * first         = shader->GetResShaderVariation( 0 );
    bool                          foundCodeType = false;
    for ( int type = 0; type < nn::gfx::ShaderCodeType_End; ++type )
    {
      const auto codeType = static_cast<nn::gfx::ShaderCodeType>( type );
      if ( first->GetResShaderProgram( codeType ) != nullptr )
      {
        m_ShaderCodeType = codeType;
        foundCodeType    = true;
        break;
      }
    }

    if ( !foundCodeType )
    {
      diagnostics::Write( diagnostics::Level::Error, "Mii texture shader has no supported program." );
      return false;
    }

    shader->Initialize( m_Device );
    m_ShaderContainerReady = true;

    for ( int index = 0; index < shader->GetShaderVariationCount(); ++index )
    {
      nn::gfx::ResShaderProgram * program =
        shader->GetResShaderVariation( index )->GetResShaderProgram( m_ShaderCodeType );
      if ( program == nullptr || program->Initialize( m_Device ) != nn::gfx::ShaderInitializeResult_Success )
      {
        diagnostics::Write( diagnostics::Level::Error, "Mii texture shader program initialization failed." );
        return false;
      }

      ++m_InitializedPrograms;
    }

    return true;
  }

  bool NvnFaceRenderer::InitializeTextureShader() noexcept
  {
    m_ShaderInfo.SetFacelineImageFormat( nn::gfx::ImageFormat_R8_G8_B8_A8_Unorm );
    m_ShaderInfo.SetMaskImageFormat( nn::gfx::ImageFormat_R8_G8_B8_A8_Unorm );

    const size_t alignment =
      nn::mii::TextureShader::CalculateMemoryAlignment( *m_ShaderFile, m_ShaderCodeType, m_ShaderInfo, m_Device );
    const size_t size =
      nn::mii::TextureShader::CalculateMemorySize( *m_ShaderFile, m_ShaderCodeType, m_ShaderInfo, m_Device );
    m_TextureShaderMemory = AllocateAligned( size, alignment );

    if ( m_TextureShaderMemory == nullptr ||
         !m_TextureShader
            .Initialize( m_TextureShaderMemory, size, m_Device, *m_ShaderFile, m_ShaderCodeType, m_ShaderInfo )
            .IsSuccess() )
    {
      diagnostics::Write( diagnostics::Level::Error, "Mii TextureShader initialization failed." );
      return false;
    }

    return true;
  }

  bool NvnFaceRenderer::InitializeGpuBuffers() noexcept
  {
    const size_t facelineSize      = nn::mii::FacelineGpuBuffer::CalculateMemorySize();
    const size_t facelineAlignment = nn::mii::FacelineGpuBuffer::CalculateMemoryAlignment();
    const size_t maskSize          = nn::mii::MaskGpuBuffer::CalculateMemorySize();
    const size_t maskAlignment     = nn::mii::MaskGpuBuffer::CalculateMemoryAlignment();
    m_FacelineBufferMemory         = AllocateAligned( facelineSize, facelineAlignment );
    m_MaskBufferMemory             = AllocateAligned( maskSize, maskAlignment );
    m_CommandControlMemory         = AllocateAligned( CommandControlBytes, 256 );

    if ( m_FacelineBufferMemory == nullptr || m_MaskBufferMemory == nullptr || m_CommandControlMemory == nullptr )
    {
      diagnostics::Write( diagnostics::Level::Error, "Mii GPU buffer allocation failed." );
      return false;
    }

    nn::gfx::MemoryPool::InfoType poolInfo;
    poolInfo.SetDefault();
    poolInfo.SetMemoryPoolProperty( nn::gfx::MemoryPoolProperty_CpuUncached | nn::gfx::MemoryPoolProperty_GpuCached );

    const size_t commandAlignment     = nn::gfx::CommandBuffer::GetCommandMemoryAlignment( m_Device );
    const size_t facelineAlignmentGpu = nn::mii::FacelineGpuBuffer::CalculateMemoryPoolAlignment( m_Device );
    const size_t maskAlignmentGpu     = nn::mii::MaskGpuBuffer::CalculateMemoryPoolAlignment( m_Device );
    const size_t facelineBytes        = nn::mii::FacelineGpuBuffer::CalculateMemoryPoolSize( m_Device );
    const size_t maskBytes            = nn::mii::MaskGpuBuffer::CalculateMemoryPoolSize( m_Device );
    const size_t poolAlignment        = nn::gfx::MemoryPool::GetPoolMemoryAlignment( m_Device, poolInfo );
    const size_t granularity          = nn::gfx::MemoryPool::GetPoolMemorySizeGranularity( m_Device, poolInfo );

    if ( facelineBytes == 0 || maskBytes == 0 || facelineBytes > MaximumPoolBytes || maskBytes > MaximumPoolBytes ||
         CommandMemoryBytes > MaximumPoolBytes - facelineBytes ||
         CommandMemoryBytes + facelineBytes > MaximumPoolBytes - maskBytes )
    {
      diagnostics::Write( diagnostics::Level::Error, "Mii GPU buffer sizes exceed the checkpoint cap." );
      return false;
    }

    m_CommandPoolOffset    = 0;
    m_FacelinePoolOffset   = RoundUp( CommandMemoryBytes, facelineAlignmentGpu );
    m_MaskPoolOffset       = RoundUp( m_FacelinePoolOffset + facelineBytes, maskAlignmentGpu );
    const size_t poolBytes = RoundUp( m_MaskPoolOffset + maskBytes, granularity );

    if ( commandAlignment == 0 || facelineAlignmentGpu == 0 || maskAlignmentGpu == 0 || m_FacelinePoolOffset == 0 ||
         m_MaskPoolOffset == 0 || poolBytes == 0 || poolBytes > MaximumPoolBytes )
    {
      diagnostics::Write( diagnostics::Level::Error, "Mii GPU pool requirements are unsupported." );
      return false;
    }

    m_GpuPoolMemory = AllocateAligned( poolBytes, poolAlignment );
    if ( m_GpuPoolMemory == nullptr )
    {
      diagnostics::Write( diagnostics::Level::Error, "Mii GPU pool allocation failed." );
      return false;
    }

    m_GpuAllocation.Acquire( poolBytes );
    poolInfo.SetPoolMemory( m_GpuPoolMemory, poolBytes );
    m_GpuPool.Initialize( m_Device, poolInfo );
    m_GpuPoolReady = true;

    m_FacelineBuffer.Initialize( m_FacelineBufferMemory,
                                 facelineSize,
                                 m_Device,
                                 &m_GpuPool,
                                 static_cast<ptrdiff_t>( m_FacelinePoolOffset ),
                                 facelineBytes );
    m_MaskBuffer.Initialize(
      m_MaskBufferMemory, maskSize, m_Device, &m_GpuPool, static_cast<ptrdiff_t>( m_MaskPoolOffset ), maskBytes );

    return true;
  }

  bool NvnFaceRenderer::InitializeDescriptors( nn::mii::CharModel & model,
                                               nn::mii::Faceline &  faceline,
                                               nn::mii::Mask &      mask ) noexcept
  {
    int                         textureBase = 0;
    int                         samplerBase = 0;
    nn::gfx::Device::DataType & deviceData  = nn::gfx::AccessorToData( *m_Device );
    pfnc_nvnDeviceGetInteger( deviceData.pNvnDevice, NVN_DEVICE_INFO_RESERVED_TEXTURE_DESCRIPTORS, &textureBase );
    pfnc_nvnDeviceGetInteger( deviceData.pNvnDevice, NVN_DEVICE_INFO_RESERVED_SAMPLER_DESCRIPTORS, &samplerBase );

    nn::gfx::DescriptorPool::InfoType textureInfo;
    textureInfo.SetDefault();
    textureInfo.SetDescriptorPoolType( nn::gfx::DescriptorPoolType_TextureView );
    textureInfo.SetSlotCount( textureBase + TextureDescriptorCount );

    nn::gfx::DescriptorPool::InfoType samplerInfo;
    samplerInfo.SetDefault();
    samplerInfo.SetDescriptorPoolType( nn::gfx::DescriptorPoolType_Sampler );
    samplerInfo.SetSlotCount( samplerBase + 1 );

    const size_t textureSize      = nn::gfx::DescriptorPool::CalculateDescriptorPoolSize( m_Device, textureInfo );
    const size_t samplerSize      = nn::gfx::DescriptorPool::CalculateDescriptorPoolSize( m_Device, samplerInfo );
    const size_t textureAlignment = nn::gfx::DescriptorPool::GetDescriptorPoolAlignment( m_Device, textureInfo );
    const size_t samplerAlignment = nn::gfx::DescriptorPool::GetDescriptorPoolAlignment( m_Device, samplerInfo );
    const size_t samplerOffset    = RoundUp( textureSize, samplerAlignment );

    constexpr int MaximumTextureViews = nn::mii::CharModel::TextureType_End + PreviewExpressionCount +
                                        nn::mii::Faceline::TextureType_End + nn::mii::Mask::TextureType_End;
    if ( MaximumTextureViews > TextureDescriptorCount || samplerOffset > MaximumPoolBytes ||
         samplerSize > MaximumPoolBytes - samplerOffset )
    {
      diagnostics::Write( diagnostics::Level::Error, "Mii descriptor requirements exceed the checkpoint cap." );
      return false;
    }

    nn::gfx::MemoryPool::InfoType poolInfo;
    poolInfo.SetDefault();
    poolInfo.SetMemoryPoolProperty( nn::gfx::MemoryPoolProperty_CpuCached | nn::gfx::MemoryPoolProperty_GpuCached );
    const size_t poolAlignment = nn::gfx::MemoryPool::GetPoolMemoryAlignment( m_Device, poolInfo );
    const size_t granularity   = nn::gfx::MemoryPool::GetPoolMemorySizeGranularity( m_Device, poolInfo );
    const size_t poolSize      = RoundUp( samplerOffset + samplerSize, granularity );

    if ( textureBase < 0 || samplerBase < 0 || textureSize == 0 || samplerSize == 0 || textureAlignment == 0 ||
         samplerOffset == 0 || poolSize == 0 || poolSize > MaximumPoolBytes || poolAlignment == 0 )
    {
      diagnostics::Write( diagnostics::Level::Error, "Mii descriptor pool requirements are unsupported." );
      return false;
    }

    const size_t memoryAlignment = poolAlignment > textureAlignment ? poolAlignment : textureAlignment;
    m_DescriptorPoolMemory       = AllocateAligned( poolSize, memoryAlignment );
    if ( m_DescriptorPoolMemory == nullptr )
    {
      diagnostics::Write( diagnostics::Level::Error, "Mii descriptor pool allocation failed." );
      return false;
    }

    m_DescriptorAllocation.Acquire( poolSize );
    poolInfo.SetPoolMemory( m_DescriptorPoolMemory, poolSize );
    m_DescriptorMemoryPool.Initialize( m_Device, poolInfo );
    m_DescriptorPoolReady = true;
    m_TextureDescriptors.Initialize( m_Device, textureInfo, &m_DescriptorMemoryPool, 0, textureSize );
    m_TextureDescriptorsReady = true;
    m_SamplerDescriptors.Initialize(
      m_Device, samplerInfo, &m_DescriptorMemoryPool, static_cast<ptrdiff_t>( samplerOffset ), samplerSize );
    m_SamplerDescriptorsReady = true;

    nn::gfx::Sampler::InfoType miiSamplerInfo;
    nn::mii::GetSamplerInfo( &miiSamplerInfo );
    m_Sampler.Initialize( m_Device, miiSamplerInfo );
    m_SamplerReady = true;
    m_SamplerDescriptors.BeginUpdate();
    m_SamplerDescriptors.SetSampler( samplerBase, &m_Sampler );
    m_SamplerDescriptors.GetDescriptorSlot( &m_SamplerSlot, samplerBase );
    m_SamplerDescriptors.EndUpdate();

    return BindTextureDescriptors( model, faceline, mask, textureBase );
  }

  bool NvnFaceRenderer::BindTextureDescriptors( nn::mii::CharModel & model,
                                                nn::mii::Faceline &  faceline,
                                                nn::mii::Mask &      mask,
                                                int                  firstSlot ) noexcept
  {
    for ( int index = 0; index < PreviewExpressionCount; ++index )
    {
      if ( model.GetTextureView( nn::mii::CharModel::TextureType_Mask, index ) == nullptr )
      {
        diagnostics::Write( diagnostics::Level::Error, "Mii expression mask texture view is missing." );
        return false;
      }
    }

    int nextTexture = firstSlot;
    m_TextureDescriptors.BeginUpdate();

    for ( int index = 0; index < nn::mii::CharModel::TextureType_End; ++index )
    {
      const auto type = static_cast<nn::mii::CharModel::TextureType>( index );
      if ( type == nn::mii::CharModel::TextureType_Mask )
      {
        continue;
      }

      const nn::gfx::TextureView * view = model.GetTextureView( type, 0 );
      if ( view != nullptr )
      {
        nn::gfx::DescriptorSlot slot;
        m_TextureDescriptors.SetTextureView( nextTexture, view );
        m_TextureDescriptors.GetDescriptorSlot( &slot, nextTexture );
        model.SetTextureDescriptorSlot( type, slot, 0 );
        ++nextTexture;
      }
    }

    for ( int index = 0; index < PreviewExpressionCount; ++index )
    {
      const nn::gfx::TextureView * view = model.GetTextureView( nn::mii::CharModel::TextureType_Mask, index );
      if ( view != nullptr )
      {
        nn::gfx::DescriptorSlot slot;
        m_TextureDescriptors.SetTextureView( nextTexture, view );
        m_TextureDescriptors.GetDescriptorSlot( &slot, nextTexture );
        model.SetTextureDescriptorSlot( nn::mii::CharModel::TextureType_Mask, slot, index );
        ++nextTexture;
      }
    }

    for ( int index = 0; index < nn::mii::Faceline::TextureType_End; ++index )
    {
      const auto                   type = static_cast<nn::mii::Faceline::TextureType>( index );
      const nn::gfx::TextureView * view = faceline.GetTextureView( type );
      if ( view != nullptr )
      {
        nn::gfx::DescriptorSlot slot;
        m_TextureDescriptors.SetTextureView( nextTexture, view );
        m_TextureDescriptors.GetDescriptorSlot( &slot, nextTexture );
        faceline.SetTextureDescriptorSlot( type, slot );
        ++nextTexture;
      }
    }

    for ( int index = 0; index < nn::mii::Mask::TextureType_End; ++index )
    {
      const auto                   type = static_cast<nn::mii::Mask::TextureType>( index );
      const nn::gfx::TextureView * view = mask.GetTextureView( type );
      if ( view != nullptr )
      {
        nn::gfx::DescriptorSlot slot;
        m_TextureDescriptors.SetTextureView( nextTexture, view );
        m_TextureDescriptors.GetDescriptorSlot( &slot, nextTexture );
        mask.SetTextureDescriptorSlot( type, slot );
        ++nextTexture;
      }
    }

    m_TextureDescriptors.EndUpdate();
    return true;
  }

  bool NvnFaceRenderer::InitializeCommands() noexcept
  {
    nn::gfx::Queue::InfoType queueInfo;
    queueInfo.SetDefault();
    queueInfo.SetCapability( nn::gfx::QueueCapability_Graphics );
    m_Queue.Initialize( m_Device, queueInfo );
    m_QueueReady = true;

    nn::gfx::CommandBuffer::InfoType commandInfo;
    commandInfo.SetDefault();
    commandInfo.SetQueueCapability( nn::gfx::QueueCapability_Graphics );
    commandInfo.SetCommandBufferType( nn::gfx::CommandBufferType_Direct );
    m_Commands.Initialize( m_Device, commandInfo );
    m_CommandsReady = true;
    return true;
  }

  void NvnFaceRenderer::BeginCommands() noexcept
  {
    m_Commands.Reset();
    m_Commands.AddControlMemory( m_CommandControlMemory, CommandControlBytes );
    m_Commands.AddCommandMemory( &m_GpuPool, static_cast<ptrdiff_t>( m_CommandPoolOffset ), CommandMemoryBytes );
    m_Commands.Begin();
    m_Commands.SetDescriptorPool( &m_TextureDescriptors );
    m_Commands.SetDescriptorPool( &m_SamplerDescriptors );
    m_Commands.InvalidateMemory( nn::gfx::GpuAccess_Texture | nn::gfx::GpuAccess_IndexBuffer |
                                 nn::gfx::GpuAccess_ConstantBuffer | nn::gfx::GpuAccess_VertexBuffer );
  }

  void NvnFaceRenderer::SubmitCommands() noexcept
  {
    m_Commands.End();
    m_Queue.ExecuteCommand( &m_Commands, 0 );
    m_Queue.Sync();
  }

  void NvnFaceRenderer::DrawTextures( nn::mii::CharModel &      model,
                                      const nn::mii::Faceline & faceline,
                                      const nn::mii::Mask &     mask ) noexcept
  {
    m_FacelineBuffer.SetColorTarget(
      m_Device, model.GetFacelineTexture(), m_ShaderInfo, TextureResolution / 2, TextureResolution, TextureMipCount );
    m_FacelineBuffer.SetFaceline( faceline );

    BeginCommands();
    m_TextureShader.DrawFaceline( &m_Commands, &m_FacelineBuffer, m_SamplerSlot );
    SubmitCommands();

    for ( int index = 0; index < PreviewExpressionCount; ++index )
    {
      m_MaskBuffer.SetColorTarget(
        m_Device, model.GetMaskTexture( index ), m_ShaderInfo, TextureResolution, TextureMipCount );
      m_MaskBuffer.SetMask( mask, static_cast<nn::mii::Expression>( index ) );

      BeginCommands();
      m_TextureShader.DrawMask( &m_Commands, &m_MaskBuffer, m_SamplerSlot );
      SubmitCommands();
    }
  }

  void NvnFaceRenderer::Shutdown() noexcept
  {
    if ( m_Device == nullptr )
    {
      return;
    }

    if ( m_QueueReady )
    {
      m_Queue.Sync();
    }

    if ( m_CommandsReady )
    {
      m_Commands.Finalize( m_Device );
    }
    if ( m_QueueReady )
    {
      m_Queue.Finalize( m_Device );
    }
    m_CommandsReady = false;
    m_QueueReady    = false;

    if ( m_MaskBuffer.IsInitialized() )
    {
      m_MaskBuffer.Finalize( m_Device );
    }
    if ( m_FacelineBuffer.IsInitialized() )
    {
      m_FacelineBuffer.Finalize( m_Device );
    }
    if ( m_SamplerReady )
    {
      m_Sampler.Finalize( m_Device );
    }
    m_SamplerReady = false;

    if ( m_SamplerDescriptorsReady )
    {
      m_SamplerDescriptors.Finalize( m_Device );
    }
    if ( m_TextureDescriptorsReady )
    {
      m_TextureDescriptors.Finalize( m_Device );
    }
    if ( m_DescriptorPoolReady )
    {
      m_DescriptorMemoryPool.Finalize( m_Device );
    }
    if ( m_GpuPoolReady )
    {
      m_GpuPool.Finalize( m_Device );
    }
    m_SamplerDescriptorsReady = false;
    m_TextureDescriptorsReady = false;
    m_DescriptorPoolReady     = false;
    m_GpuPoolReady            = false;

    if ( m_TextureShader.IsInitialized() )
    {
      m_TextureShader.Finalize( m_Device );
    }
    if ( m_ShaderContainerReady )
    {
      nn::gfx::ResShaderContainer * shader = m_ShaderFile->GetShaderContainer();
      for ( int index = 0; index < m_InitializedPrograms; ++index )
      {
        shader->GetResShaderVariation( index )->GetResShaderProgram( m_ShaderCodeType )->Finalize( m_Device );
      }
      shader->Finalize( m_Device );
    }
    m_ShaderContainerReady = false;
    m_InitializedPrograms  = 0;
    m_ShaderFile           = nullptr;

    std::free( m_CommandControlMemory );
    std::free( m_MaskBufferMemory );
    std::free( m_FacelineBufferMemory );
    std::free( m_TextureShaderMemory );
    std::free( m_GpuPoolMemory );
    std::free( m_DescriptorPoolMemory );
    std::free( m_ShaderMemory );
    m_GpuAllocation.Release();
    m_DescriptorAllocation.Release();

    if ( m_RomMounted )
    {
      nn::fs::Unmount( "Contents" );
    }
    std::free( m_RomCache );

    m_CommandControlMemory = nullptr;
    m_MaskBufferMemory     = nullptr;
    m_FacelineBufferMemory = nullptr;
    m_TextureShaderMemory  = nullptr;
    m_GpuPoolMemory        = nullptr;
    m_DescriptorPoolMemory = nullptr;
    m_ShaderMemory         = nullptr;
    m_RomCache             = nullptr;
    m_RomMounted           = false;
    m_TexturesReady        = false;
    m_Device               = nullptr;
  }
} // namespace apollo::mii
