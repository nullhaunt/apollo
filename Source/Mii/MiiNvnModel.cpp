#include "Mii/MiiNvnModel.hpp"

#include "Platform/Diagnostics.hpp"

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <limits>

namespace apollo::mii
{
  namespace
  {
    constexpr size_t MaximumModelPoolBytes = 32 * 1024 * 1024;

    [[nodiscard]] size_t RoundUp( size_t value, size_t alignment ) noexcept
    {
      if ( alignment == 0 || value > std::numeric_limits<size_t>::max() - ( alignment - 1 ) ) return 0;
      return ( value + alignment - 1 ) / alignment * alignment;
    }

    [[nodiscard]] void * AllocateAligned( size_t size, size_t alignment ) noexcept
    {
      if ( alignment < alignof( std::max_align_t ) ) alignment = alignof( std::max_align_t );
      const size_t rounded = RoundUp( size, alignment );
      return rounded == 0 ? nullptr : aligned_alloc( alignment, rounded );
    }
  } // namespace

  NvnModel::~NvnModel() noexcept
  {
    Shutdown();
  }

  bool NvnModel::Initialize( render::nvn::NvnContext & context,
                             const ResourceFiles & files, const Entry & entry ) noexcept
  {
    if ( m_GfxReady || !files.IsReady() || context.GetDevice() == nullptr ) return false;
    static_assert( sizeof( ::nvn::Device ) == sizeof( NVNdevice ) );
    static_assert( alignof( ::nvn::Device ) == alignof( NVNdevice ) );
    static_assert( sizeof( nn::mii::CharInfo ) == CharInfoBytes );

    nn::gfx::Initialize();
    m_GfxReady = true;
    nn::gfx::TInteroperation<nn::gfx::ApiVariationNvn8>::ConvertToGfxDevice(
      &m_GfxDevice, reinterpret_cast<NVNdevice *>( context.GetDevice() ) );

    nn::mii::ResourceInfo resourceInfo;
    resourceInfo.SetDefault()
      .SetShapeQuality( nn::mii::ShapeQuality_Middle )
      .SetTextureQuality( nn::mii::TextureQuality_Low )
      .SetGammaType( nn::mii::GammaType_Srgb );
    m_ResourceMemory = AllocateAligned( files.ResourceObjectSize(), files.ResourceObjectAlignment() );
    if ( m_ResourceMemory == nullptr ||
         !m_Resource.Initialize( m_ResourceMemory, files.ResourceObjectSize(),
           files.TextureData(), files.TextureSize(), files.ShapeData(), files.ShapeSize(),
           resourceInfo, &m_GfxDevice ).IsSuccess() )
    {
      diagnostics::Write( diagnostics::Level::Error, "Mii Resource initialization failed." );
      Shutdown();
      return false;
    }

    nn::mii::CharModelInfo modelInfo;
    modelInfo.SetDefault()
      .SetCreateFlag( nn::mii::CreateFlag_Normal | nn::mii::CreateFlag_NoseNormal );
    const size_t modelSize = nn::mii::CharModel::CalculateMemorySize( modelInfo );
    const size_t modelAlignment = nn::mii::CharModel::CalculateMemoryAlignment( modelInfo );
    const size_t poolSize = nn::mii::CharModel::CalculateMemoryPoolSize( &m_GfxDevice, m_Resource, modelInfo );
    const size_t poolAlignment = nn::mii::CharModel::CalculateMemoryPoolAlignment( &m_GfxDevice, m_Resource, modelInfo );
    if ( modelSize == 0 || modelAlignment == 0 || poolSize == 0 ||
         poolAlignment == 0 || poolSize > MaximumModelPoolBytes )
    {
      diagnostics::Write( diagnostics::Level::Error, "Mii model layout is invalid or exceeds the checkpoint cap." );
      Shutdown();
      return false;
    }

    nn::gfx::MemoryPool::InfoType poolInfo;
    poolInfo.SetDefault();
    poolInfo.SetMemoryPoolProperty( nn::gfx::MemoryPoolProperty_CpuCached |
                                    nn::gfx::MemoryPoolProperty_GpuCached |
                                    nn::gfx::MemoryPoolProperty_Compressible );
    const size_t memoryAlignment = nn::gfx::MemoryPool::GetPoolMemoryAlignment( &m_GfxDevice, poolInfo );
    const size_t granularity = nn::gfx::MemoryPool::GetPoolMemorySizeGranularity( &m_GfxDevice, poolInfo );
    const size_t allocatedPoolSize = RoundUp( poolSize, granularity );
    if ( memoryAlignment == 0 || allocatedPoolSize == 0 ||
         allocatedPoolSize > MaximumModelPoolBytes )
    {
      diagnostics::Write( diagnostics::Level::Error, "Mii model pool requirements are unsupported." );
      Shutdown();
      return false;
    }
    m_PoolMemory = AllocateAligned( allocatedPoolSize,
                                   memoryAlignment > poolAlignment ? memoryAlignment : poolAlignment );
    m_ModelMemory = AllocateAligned( modelSize, modelAlignment );
    void * temporary = AllocateAligned( nn::mii::TemporaryBufferSize,
                                        alignof( nn::mii::TemporaryBuffer ) );
    if ( m_PoolMemory == nullptr || m_ModelMemory == nullptr || temporary == nullptr )
    {
      std::free( temporary );
      diagnostics::Write( diagnostics::Level::Error, "Mii model allocation failed." );
      Shutdown();
      return false;
    }
    m_PoolAllocation.Acquire( allocatedPoolSize );
    poolInfo.SetPoolMemory( m_PoolMemory, allocatedPoolSize );
    m_ModelPool.Initialize( &m_GfxDevice, poolInfo );
    m_PoolReady = true;

    nn::mii::CharInfo charInfo{};
    std::memcpy( &charInfo, entry.snapshot.data(), CharInfoBytes );
    const nn::Result result = m_Model.Initialize( m_ModelMemory, modelSize,
      &m_GfxDevice, &m_ModelPool, 0, poolSize, m_Resource,
      static_cast<nn::mii::TemporaryBuffer *>( temporary ), modelInfo, charInfo );
    std::free( temporary );
    if ( !result.IsSuccess() )
    {
      diagnostics::Write( diagnostics::Level::Error, "Mii CharModel initialization failed." );
      Shutdown();
      return false;
    }
    char message[ 160 ]{};
    std::snprintf( message, sizeof( message ),
      "Mii CharModel initialized from catalog snapshot; model pool %llu bytes (head draw pending).",
      static_cast<unsigned long long>( allocatedPoolSize ) );
    diagnostics::Write( diagnostics::Level::Information, message );
    return true;
  }

  void NvnModel::Shutdown() noexcept
  {
    if ( m_Model.IsInitialized() ) m_Model.Finalize( &m_GfxDevice );
    if ( m_PoolReady ) m_ModelPool.Finalize( &m_GfxDevice );
    m_PoolReady = false;
    if ( m_Resource.IsInitialized() ) m_Resource.Finalize( &m_GfxDevice );
    std::free( m_PoolMemory );
    m_PoolAllocation.Release();
    std::free( m_ModelMemory );
    std::free( m_ResourceMemory );
    m_PoolMemory = nullptr;
    m_ModelMemory = nullptr;
    m_ResourceMemory = nullptr;
    // Converted gfx device is borrowed from NvnContext; do not Finalize it.
    if ( m_GfxReady ) nn::gfx::Finalize();
    m_GfxReady = false;
  }
} // namespace apollo::mii
