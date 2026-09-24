#include "Mii/MiiNvnModel.hpp"

#include "Platform/Diagnostics.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace apollo::mii
{
  namespace
  {
    constexpr size_t MaximumModelPoolBytes = 32 * 1024 * 1024;

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

  NvnModel::~NvnModel() noexcept
  {
    Shutdown();
  }

  bool NvnModel::Initialize( render::nvn::NvnContext & context,
                             const ResourceFiles &     files,
                             const Entry &             entry ) noexcept
  {
    if ( m_GfxReady || !files.IsReady() || context.GetDevice() == nullptr )
    {
      return false;
    }
    static_assert( sizeof( ::nvn::Device ) == sizeof( NVNdevice ) );
    static_assert( alignof( ::nvn::Device ) == alignof( NVNdevice ) );
    static_assert( sizeof( nn::mii::CharInfo ) == CharInfoBytes );

    InitializeGfx( context );

    if ( !InitializeResource( files ) || !InitializeModel( entry ) || !InitializeFaceSources( entry ) )
    {
      Shutdown();
      return false;
    }

    if ( !m_FaceRenderer.Initialize( m_GfxDevice, m_Model, m_Faceline, m_Mask ) ||
         !m_HeadRenderer.Initialize( m_GfxDevice, *context.GetQueue(), m_Model, m_FaceRenderer ) )
    {
      Shutdown();
      return false;
    }

    return true;
  }

  void NvnModel::InitializeGfx( render::nvn::NvnContext & context ) noexcept
  {
    nn::gfx::Initialize();
    m_GfxReady = true;

    nn::gfx::TInteroperation<nn::gfx::ApiVariationNvn8>::ConvertToGfxDevice(
      &m_GfxDevice, reinterpret_cast<NVNdevice *>( context.GetDevice() ) );
  }

  bool NvnModel::InitializeResource( const ResourceFiles & files ) noexcept
  {
    nn::mii::ResourceInfo resourceInfo;
    resourceInfo.SetDefault()
      .SetShapeQuality( nn::mii::ShapeQuality_Middle )
      .SetTextureQuality( nn::mii::TextureQuality_Low )
      .SetGammaType( nn::mii::GammaType_Srgb );

    m_ResourceMemory = AllocateAligned( files.ResourceObjectSize(), files.ResourceObjectAlignment() );
    if ( m_ResourceMemory == nullptr )
    {
      diagnostics::Write( diagnostics::Level::Error, "Mii Resource allocation failed." );
      return false;
    }

    const nn::Result result = m_Resource.Initialize( m_ResourceMemory,
                                                     files.ResourceObjectSize(),
                                                     files.TextureData(),
                                                     files.TextureSize(),
                                                     files.ShapeData(),
                                                     files.ShapeSize(),
                                                     resourceInfo,
                                                     &m_GfxDevice );
    if ( !result.IsSuccess() )
    {
      diagnostics::Write( diagnostics::Level::Error, "Mii Resource initialization failed." );
      return false;
    }

    return true;
  }

  bool NvnModel::InitializeModel( const Entry & entry ) noexcept
  {
    nn::mii::CharModelInfo modelInfo;
    modelInfo.SetDefault();
    modelInfo.SetCreateFlag( nn::mii::CreateFlag_Normal | nn::mii::CreateFlag_NoseNormal );
    modelInfo.SetDynamicTextureResolution( 512, 512 );
    modelInfo.SetDynamicTextureFormat( nn::gfx::ImageFormat_R8_G8_B8_A8_Unorm, nn::gfx::ImageFormat_R8_G8_B8_A8_Unorm );
    modelInfo.SetDynamicTextureMipCount( 1, 1 );
    modelInfo.SetMaskCount( PreviewExpressionCount );

    const size_t modelSize      = nn::mii::CharModel::CalculateMemorySize( modelInfo );
    const size_t modelAlignment = nn::mii::CharModel::CalculateMemoryAlignment( modelInfo );
    const size_t poolSize       = nn::mii::CharModel::CalculateMemoryPoolSize( &m_GfxDevice, m_Resource, modelInfo );
    const size_t poolAlignment =
      nn::mii::CharModel::CalculateMemoryPoolAlignment( &m_GfxDevice, m_Resource, modelInfo );

    if ( modelSize == 0 || modelAlignment == 0 || poolSize == 0 || poolAlignment == 0 ||
         poolSize > MaximumModelPoolBytes )
    {
      diagnostics::Write( diagnostics::Level::Error, "Mii model layout is invalid or exceeds the checkpoint cap." );
      return false;
    }

    nn::gfx::MemoryPool::InfoType poolInfo;
    poolInfo.SetDefault();
    poolInfo.SetMemoryPoolProperty( nn::gfx::MemoryPoolProperty_CpuCached | nn::gfx::MemoryPoolProperty_GpuCached |
                                    nn::gfx::MemoryPoolProperty_Compressible );

    const size_t memoryAlignment   = nn::gfx::MemoryPool::GetPoolMemoryAlignment( &m_GfxDevice, poolInfo );
    const size_t granularity       = nn::gfx::MemoryPool::GetPoolMemorySizeGranularity( &m_GfxDevice, poolInfo );
    const size_t allocatedPoolSize = RoundUp( poolSize, granularity );

    if ( memoryAlignment == 0 || allocatedPoolSize == 0 || allocatedPoolSize > MaximumModelPoolBytes )
    {
      diagnostics::Write( diagnostics::Level::Error, "Mii model pool requirements are unsupported." );
      return false;
    }

    m_PoolMemory =
      AllocateAligned( allocatedPoolSize, memoryAlignment > poolAlignment ? memoryAlignment : poolAlignment );
    m_ModelMemory    = AllocateAligned( modelSize, modelAlignment );
    void * temporary = AllocateAligned( nn::mii::TemporaryBufferSize, alignof( nn::mii::TemporaryBuffer ) );

    if ( m_PoolMemory == nullptr || m_ModelMemory == nullptr || temporary == nullptr )
    {
      std::free( temporary );
      diagnostics::Write( diagnostics::Level::Error, "Mii model allocation failed." );
      return false;
    }

    m_PoolAllocation.Acquire( allocatedPoolSize );
    poolInfo.SetPoolMemory( m_PoolMemory, allocatedPoolSize );
    m_ModelPool.Initialize( &m_GfxDevice, poolInfo );
    m_PoolReady = true;

    nn::mii::CharInfo charInfo{};
    std::memcpy( &charInfo, entry.snapshot.data(), CharInfoBytes );

    const nn::Result result = m_Model.Initialize( m_ModelMemory,
                                                  modelSize,
                                                  &m_GfxDevice,
                                                  &m_ModelPool,
                                                  0,
                                                  poolSize,
                                                  m_Resource,
                                                  static_cast<nn::mii::TemporaryBuffer *>( temporary ),
                                                  modelInfo,
                                                  charInfo );
    std::free( temporary );

    if ( !result.IsSuccess() )
    {
      diagnostics::Write( diagnostics::Level::Error, "Mii CharModel initialization failed." );
      return false;
    }

    char message[ 160 ]{};
    std::snprintf( message,
                   sizeof( message ),
                   "Mii CharModel initialized from catalog snapshot; model pool %llu bytes.",
                   static_cast<unsigned long long>( allocatedPoolSize ) );
    diagnostics::Write( diagnostics::Level::Information, message );

    return true;
  }

  bool NvnModel::InitializeFaceSources( const Entry & entry ) noexcept
  {
    constexpr int expressionFlags = nn::mii::ExpressionFlag_Normal | nn::mii::ExpressionFlag_Smile;
    // Apollo's preview samples the generated textures with the opposite V
    // orientation from the SDK source layout. Apply the SDK's source flip to
    // both layers so the face and expression stay aligned.
    constexpr bool verticalFlip = true;

    const size_t facelineSize          = nn::mii::Faceline::CalculateMemorySize();
    const size_t facelineAlignment     = nn::mii::Faceline::CalculateMemoryAlignment();
    const size_t maskSize              = nn::mii::Mask::CalculateMemorySize();
    const size_t maskAlignment         = nn::mii::Mask::CalculateMemoryAlignment();
    const size_t facelinePoolSize      = nn::mii::Faceline::CalculateMemoryPoolSize( &m_GfxDevice, m_Resource );
    const size_t facelinePoolAlignment = nn::mii::Faceline::CalculateMemoryPoolAlignment( &m_GfxDevice, m_Resource );
    const size_t maskPoolSize = nn::mii::Mask::CalculateMemoryPoolSize( &m_GfxDevice, m_Resource, expressionFlags );
    const size_t maskPoolAlignment =
      nn::mii::Mask::CalculateMemoryPoolAlignment( &m_GfxDevice, m_Resource, expressionFlags );

    if ( facelineSize == 0 || facelineAlignment == 0 || maskSize == 0 || maskAlignment == 0 || facelinePoolSize == 0 ||
         facelinePoolAlignment == 0 || maskPoolSize == 0 || maskPoolAlignment == 0 )
    {
      diagnostics::Write( diagnostics::Level::Error, "Mii face source layout calculation failed." );
      return false;
    }

    const size_t maskPoolOffset = RoundUp( facelinePoolSize, maskPoolAlignment );
    if ( maskPoolOffset == 0 || maskPoolOffset > MaximumModelPoolBytes ||
         maskPoolSize > MaximumModelPoolBytes - maskPoolOffset )
    {
      diagnostics::Write( diagnostics::Level::Error, "Mii face source pool exceeds the checkpoint cap." );
      return false;
    }

    nn::gfx::MemoryPool::InfoType poolInfo;
    poolInfo.SetDefault();
    poolInfo.SetMemoryPoolProperty( nn::gfx::MemoryPoolProperty_CpuCached | nn::gfx::MemoryPoolProperty_GpuCached );

    const size_t poolAlignment = nn::gfx::MemoryPool::GetPoolMemoryAlignment( &m_GfxDevice, poolInfo );
    const size_t granularity   = nn::gfx::MemoryPool::GetPoolMemorySizeGranularity( &m_GfxDevice, poolInfo );
    const size_t allocatedSize = RoundUp( maskPoolOffset + maskPoolSize, granularity );

    if ( poolAlignment == 0 || allocatedSize == 0 || allocatedSize > MaximumModelPoolBytes )
    {
      diagnostics::Write( diagnostics::Level::Error, "Mii face source pool requirements are unsupported." );
      return false;
    }

    const size_t requestedAlignment = poolAlignment > facelinePoolAlignment ? poolAlignment : facelinePoolAlignment;
    const size_t memoryAlignment    = requestedAlignment > maskPoolAlignment ? requestedAlignment : maskPoolAlignment;

    m_FaceSourcePoolMemory = AllocateAligned( allocatedSize, memoryAlignment );
    m_FacelineMemory       = AllocateAligned( facelineSize, facelineAlignment );
    m_MaskMemory           = AllocateAligned( maskSize, maskAlignment );
    void * temporary       = AllocateAligned( nn::mii::TemporaryBufferSize, alignof( nn::mii::TemporaryBuffer ) );

    if ( m_FaceSourcePoolMemory == nullptr || m_FacelineMemory == nullptr || m_MaskMemory == nullptr ||
         temporary == nullptr )
    {
      std::free( temporary );
      diagnostics::Write( diagnostics::Level::Error, "Mii face source allocation failed." );
      return false;
    }

    m_FaceSourcePoolAllocation.Acquire( allocatedSize );
    poolInfo.SetPoolMemory( m_FaceSourcePoolMemory, allocatedSize );
    m_FaceSourcePool.Initialize( &m_GfxDevice, poolInfo );
    m_FaceSourcePoolReady = true;

    nn::mii::CharInfo charInfo{};
    std::memcpy( &charInfo, entry.snapshot.data(), CharInfoBytes );

    const nn::Result facelineResult = m_Faceline.Initialize( m_FacelineMemory,
                                                             facelineSize,
                                                             &m_GfxDevice,
                                                             &m_FaceSourcePool,
                                                             0,
                                                             facelinePoolSize,
                                                             static_cast<nn::mii::TemporaryBuffer *>( temporary ),
                                                             m_Resource,
                                                             charInfo,
                                                             verticalFlip );
    if ( !facelineResult.IsSuccess() )
    {
      std::free( temporary );

      char message[ 128 ]{};
      std::snprintf( message,
                     sizeof( message ),
                     "Mii Faceline initialization failed: module %d, description %d.",
                     facelineResult.GetModule(),
                     facelineResult.GetDescription() );
      diagnostics::Write( diagnostics::Level::Error, message );
      return false;
    }

    const nn::Result maskResult = m_Mask.Initialize( m_MaskMemory,
                                                     maskSize,
                                                     &m_GfxDevice,
                                                     &m_FaceSourcePool,
                                                     static_cast<ptrdiff_t>( maskPoolOffset ),
                                                     maskPoolSize,
                                                     static_cast<nn::mii::TemporaryBuffer *>( temporary ),
                                                     m_Resource,
                                                     charInfo,
                                                     expressionFlags,
                                                     verticalFlip );
    std::free( temporary );

    if ( !maskResult.IsSuccess() )
    {
      char message[ 128 ]{};
      std::snprintf( message,
                     sizeof( message ),
                     "Mii Mask initialization failed: module %d, description %d.",
                     maskResult.GetModule(),
                     maskResult.GetDescription() );
      diagnostics::Write( diagnostics::Level::Error, message );
      return false;
    }

    if ( !m_Mask.IsAvailableExpression( nn::mii::Expression_Normal ) ||
         !m_Mask.IsAvailableExpression( nn::mii::Expression_Smile ) )
    {
      diagnostics::Write( diagnostics::Level::Error, "Mii Normal/Smile Mask source is incomplete." );
      return false;
    }

    diagnostics::Write( diagnostics::Level::Information, "Mii Faceline and Normal/Smile Mask sources ready." );
    return true;
  }

  void NvnModel::ResetHeadTargets() noexcept
  {
    m_HeadRenderer.ResetTargets();
  }

  void NvnModel::RecordHead( ::nvn::CommandBuffer & commands,
                             ::nvn::Texture &       target,
                             render::Extent2D       extent,
                             int                    backbuffer,
                             const PreviewCamera &  camera,
                             PreviewExpression      expression ) noexcept
  {
    if ( m_HeadRenderer.IsReady() )
    {
      ( void )m_HeadRenderer.RecordDraw( commands, target, extent, backbuffer, camera, expression );
    }
  }

  void NvnModel::Shutdown() noexcept
  {
    m_HeadRenderer.Shutdown();
    m_FaceRenderer.Shutdown();

    if ( m_Mask.IsInitialized() )
    {
      m_Mask.Finalize( &m_GfxDevice );
    }

    if ( m_Faceline.IsInitialized() )
    {
      m_Faceline.Finalize( &m_GfxDevice );
    }

    if ( m_FaceSourcePoolReady )
    {
      m_FaceSourcePool.Finalize( &m_GfxDevice );
    }
    m_FaceSourcePoolReady = false;

    std::free( m_MaskMemory );
    std::free( m_FacelineMemory );
    std::free( m_FaceSourcePoolMemory );
    m_FaceSourcePoolAllocation.Release();

    m_MaskMemory           = nullptr;
    m_FacelineMemory       = nullptr;
    m_FaceSourcePoolMemory = nullptr;

    if ( m_Model.IsInitialized() )
    {
      m_Model.Finalize( &m_GfxDevice );
    }

    if ( m_PoolReady )
    {
      m_ModelPool.Finalize( &m_GfxDevice );
    }
    m_PoolReady = false;

    if ( m_Resource.IsInitialized() )
    {
      m_Resource.Finalize( &m_GfxDevice );
    }

    std::free( m_PoolMemory );
    m_PoolAllocation.Release();
    std::free( m_ModelMemory );
    std::free( m_ResourceMemory );

    m_PoolMemory     = nullptr;
    m_ModelMemory    = nullptr;
    m_ResourceMemory = nullptr;

    // Converted gfx device is borrowed from NvnContext; do not Finalize it.
    if ( m_GfxReady )
    {
      nn::gfx::Finalize();
    }

    m_GfxReady = false;
  }
} // namespace apollo::mii
