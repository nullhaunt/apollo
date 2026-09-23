#include "Render/Nvn/NvnContext.hpp"

#include "Platform/Diagnostics.hpp"

#include <nv/nv_MemoryManagement.h>
#include <nvn/nvn_CppFuncPtrImpl.h>

#include <cstdio>
#include <cstdlib>
#include <limits>

extern "C" ::nvn::GenericFuncPtrFunc NVNAPIENTRY nvnBootstrapLoader( const char * name );

namespace apollo::render::nvn
{
  namespace
  {
    constexpr size_t GraphicsMemorySize = 8 * 1024 * 1024;
    constexpr size_t GraphicsAlignment  = nv::GraphicsMemoryAlignment;
    constexpr size_t QueueControlSize   = 64 * 1024;

    bool g_GraphicsReady{};

    void * AllocateGraphics( size_t size, size_t alignment, void * ) noexcept
    {
      if ( alignment == 0 || size > std::numeric_limits<size_t>::max() - ( alignment - 1 ) )
      {
        return nullptr;
      }
      const size_t roundedSize = ( size + alignment - 1 ) / alignment * alignment;
      return aligned_alloc( alignment, roundedSize );
    }

    void FreeGraphics( void * address, void * ) noexcept
    {
      std::free( address );
    }

    void * ReallocateGraphics( void * address, size_t size, void * ) noexcept
    {
      return std::realloc( address, size );
    }

    [[nodiscard]] bool InitializeGraphics() noexcept
    {
      if ( g_GraphicsReady )
      {
        return true;
      }

      void * memory = aligned_alloc( GraphicsAlignment, GraphicsMemorySize );
      if ( memory == nullptr )
      {
        diagnostics::Write( diagnostics::Level::Error, "Could not allocate NVN graphics memory." );
        return false;
      }

      nv::SetGraphicsAllocator( AllocateGraphics, FreeGraphics, ReallocateGraphics, nullptr );
      nv::InitializeGraphics( memory, GraphicsMemorySize );
      // The graphics runtime and this block stay alive for the process lifetime.
      g_GraphicsReady = true;
      return true;
    }
  } // namespace

  NvnContext::~NvnContext() noexcept
  {
    Shutdown();
  }

  bool NvnContext::Initialize() noexcept
  {
    if ( m_DeviceReady || m_QueueReady )
    {
      return false;
    }
    if ( !InitializeGraphics() )
    {
      return false;
    }

    const auto getProcAddress =
      reinterpret_cast<::nvn::DeviceGetProcAddressFunc>( nvnBootstrapLoader( "nvnDeviceGetProcAddress" ) );
    if ( getProcAddress == nullptr )
    {
      diagnostics::Write( diagnostics::Level::Error, "NVN bootstrap entry point is unavailable." );
      return false;
    }

    nvnLoadCPPProcs( nullptr, getProcAddress );

    ::nvn::DeviceBuilder deviceBuilder{};
    deviceBuilder.SetDefaults();
    deviceBuilder.SetFlags( ::nvn::DeviceFlagBits::ENABLE_SEPARATE_SAMPLER_TEXTURE_SUPPORT );
    if ( !m_Device.Initialize( &deviceBuilder ) )
    {
      diagnostics::Write( diagnostics::Level::Error, "NVN device initialization failed." );
      return false;
    }
    m_DeviceReady = true;
    nvnLoadCPPProcs( &m_Device, getProcAddress );
    m_Device.SetWindowOriginMode( ::nvn::WindowOriginMode::UPPER_LEFT );

    int majorVersion{};
    int minorVersion{};
    m_Device.GetInteger( ::nvn::DeviceInfo::API_MAJOR_VERSION, &majorVersion );
    m_Device.GetInteger( ::nvn::DeviceInfo::API_MINOR_VERSION, &minorVersion );
    if ( majorVersion != NVN_API_MAJOR_VERSION || minorVersion < NVN_API_MINOR_VERSION )
    {
      diagnostics::Write( diagnostics::Level::Error, "NVN driver version is incompatible with this SDK." );
      Shutdown();
      return false;
    }

    int minimumCommandSize{};
    m_Device.GetInteger( ::nvn::DeviceInfo::QUEUE_COMMAND_MEMORY_MIN_SIZE, &minimumCommandSize );
    if ( minimumCommandSize <= 0 )
    {
      diagnostics::Write( diagnostics::Level::Error, "NVN reported an invalid minimum queue size." );
      Shutdown();
      return false;
    }

    ::nvn::QueueBuilder queueBuilder{};
    queueBuilder.SetDefaults()
      .SetDevice( &m_Device )
      .SetComputeMemorySize( 0 )
      .SetCommandMemorySize( static_cast<size_t>( minimumCommandSize ) )
      .SetCommandFlushThreshold( static_cast<size_t>( minimumCommandSize ) )
      .SetControlMemorySize( QueueControlSize );

    const size_t queueMemorySize = queueBuilder.GetQueueMemorySize();
    if ( queueMemorySize == 0 || queueMemorySize % NVN_MEMORY_POOL_STORAGE_GRANULARITY != 0 )
    {
      diagnostics::Write( diagnostics::Level::Error, "NVN reported an invalid queue memory size." );
      Shutdown();
      return false;
    }
    m_QueueMemory = aligned_alloc( NVN_MEMORY_POOL_STORAGE_ALIGNMENT, queueMemorySize );
    if ( m_QueueMemory == nullptr )
    {
      diagnostics::Write( diagnostics::Level::Error, "Could not allocate NVN queue memory." );
      Shutdown();
      return false;
    }
    queueBuilder.SetQueueMemory( m_QueueMemory, queueMemorySize );
    if ( !m_Queue.Initialize( &queueBuilder ) )
    {
      diagnostics::Write( diagnostics::Level::Error, "NVN queue initialization failed." );
      Shutdown();
      return false;
    }
    m_QueueReady = true;

    char message[ 96 ]{};
    std::snprintf( message, sizeof( message ), "NVN device and queue ready (API %d.%d).", majorVersion, minorVersion );
    diagnostics::Write( diagnostics::Level::Information, message );
    return true;
  }

  void NvnContext::Shutdown() noexcept
  {
    if ( m_QueueReady )
    {
      m_Queue.Finish();
      m_Queue.Finalize();
      m_QueueReady = false;
    }
    std::free( m_QueueMemory );
    m_QueueMemory = nullptr;

    if ( m_DeviceReady )
    {
      m_Device.Finalize();
      m_DeviceReady = false;
    }
  }

  ::nvn::Device * NvnContext::GetDevice() noexcept
  {
    return m_DeviceReady ? &m_Device : nullptr;
  }

  ::nvn::Queue * NvnContext::GetQueue() noexcept
  {
    return m_QueueReady ? &m_Queue : nullptr;
  }
} // namespace apollo::render::nvn
