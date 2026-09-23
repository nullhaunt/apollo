#include "Render/Nvn/NvnPresenter.hpp"

#include "Platform/Diagnostics.hpp"

#if !defined( APOLLO_BUILD_RELEASE )
  #include <imgui.h>
#endif

#include <cstdlib>

namespace apollo::render::nvn
{
  namespace
  {
    constexpr size_t CommandMemoryPerBuffer = 256 * 1024;
    constexpr size_t ControlMemoryPerBuffer = 64 * 1024;
    constexpr float  ClearColor[ 4 ]         = { 0.08f, 0.12f, 0.20f, 1.0f };

    [[nodiscard]] constexpr size_t AlignUp( size_t value, size_t alignment ) noexcept
    {
      return ( value + alignment - 1 ) / alignment * alignment;
    }

    [[nodiscard]] void * AllocateAligned( size_t alignment, size_t size ) noexcept
    {
      return aligned_alloc( alignment, AlignUp( size, alignment ) );
    }

    void LogFailure( const char * message ) noexcept
    {
      diagnostics::Write( diagnostics::Level::Error, message );
    }
  } // namespace

  NvnPresenter::~NvnPresenter() noexcept
  {
    Shutdown();
  }

  bool NvnPresenter::Initialize( NvnContext & context, Extent2D extent ) noexcept
  {
    if ( m_ViReady || !extent.IsValid() )
    {
      return false;
    }
    m_Device = context.GetDevice();
    m_Queue  = context.GetQueue();
    if ( m_Device == nullptr || m_Queue == nullptr )
    {
      return false;
    }

    nn::vi::Initialize();
    m_ViReady = true;
    if ( !nn::vi::OpenDefaultDisplay( &m_Display ).IsSuccess() ||
         !nn::vi::CreateLayer( &m_Layer, m_Display ).IsSuccess() ||
         !nn::vi::GetNativeWindow( &m_NativeWindow, m_Layer ).IsSuccess() )
    {
      LogFailure( "NVN display or layer initialization failed." );
      Shutdown();
      return false;
    }
    if ( !m_DisplayReleaseSync.Initialize( m_Device ) )
    {
      LogFailure( "NVN display release sync initialization failed." );
      Shutdown();
      return false;
    }
    m_SyncReady = true;

    if ( !m_IndexedQuad.Initialize( context ) )
    {
      LogFailure( "NVN indexed geometry initialization failed." );
      Shutdown();
      return false;
    }
#if !defined( APOLLO_BUILD_RELEASE )
    if ( !m_DebugUiRenderer.Initialize( context ) )
    {
      LogFailure( "NVN debug UI renderer initialization failed." );
      Shutdown();
      return false;
    }
#endif

    if ( !CreateImages( extent ) )
    {
      LogFailure( "NVN presentation image initialization failed." );
      Shutdown();
      return false;
    }
    diagnostics::Write( diagnostics::Level::Information, "NVN presentation ready." );
    return true;
  }

  bool NvnPresenter::Resize( Extent2D extent ) noexcept
  {
    if ( !m_ViReady || !extent.IsValid() )
    {
      return false;
    }
    if ( m_Ready && extent.width == m_Extent.width && extent.height == m_Extent.height )
    {
      return true;
    }

    m_Queue->Finish();
    DestroyImages();
    if ( !CreateImages( extent ) )
    {
      LogFailure( "NVN presentation resize failed." );
      return false;
    }
    diagnostics::Write( diagnostics::Level::Information, "NVN presentation resized." );
    return true;
  }

  Result NvnPresenter::PresentFrame() noexcept
  {
    if ( !m_Ready )
    {
      return Result::InvalidState;
    }

    int textureIndex{};
    if ( m_Window.AcquireTexture( &m_DisplayReleaseSync, &textureIndex ) != ::nvn::WindowAcquireTextureResult::SUCCESS ||
         textureIndex < 0 || textureIndex >= BackbufferCount )
    {
      LogFailure( "NVN backbuffer acquisition failed." );
      return Result::SurfaceUnavailable;
    }

    const auto waited = m_DisplayReleaseSync.Wait( NVN_WAIT_TIMEOUT_MAXIMUM );
    if ( waited != ::nvn::SyncWaitResult::ALREADY_SIGNALED &&
         waited != ::nvn::SyncWaitResult::CONDITION_SATISFIED )
    {
      LogFailure( "NVN backbuffer release wait failed." );
      return Result::Failure;
    }

    ::nvn::CommandBuffer & commands = m_FrameBuffers[ textureIndex ];
    // AcquireTexture's release sync has completed for this backbuffer. Reattach its
    // command/control chunks before recording so each frame starts at offset zero.
    // NVN keeps advancing within a chunk across BeginRecording calls otherwise.
    commands.AddCommandMemory( &m_CommandPool, CommandMemoryPerBuffer * textureIndex,
                               CommandMemoryPerBuffer );
    commands.AddControlMemory( m_ControlMemory[ textureIndex ], ControlMemoryPerBuffer );
    commands.BeginRecording();
    ::nvn::Texture * target = &m_Textures[ textureIndex ];
    commands.SetRenderTargets( 1, &target, nullptr, nullptr, nullptr );
    // NVN clears are clipped by the current scissor. The previous ImGui pass
    // leaves its last panel clip active on this command buffer.
    commands.SetScissor( 0, 0, static_cast<int>( m_Extent.width ), static_cast<int>( m_Extent.height ) );
    commands.ClearColor( 0, ClearColor, ::nvn::ClearColorMask::RGBA );
    m_IndexedQuad.RecordDraw( commands, m_Extent );
#if !defined( APOLLO_BUILD_RELEASE )
    const ImDrawData * drawData = ImGui::GetDrawData();
    if ( m_DebugUiRenderer.Upload( drawData, textureIndex, m_Extent ) )
    {
      m_DebugUiRenderer.RecordDraw( commands, drawData, textureIndex, m_Extent );
    }
#endif
    m_FrameCommands[ textureIndex ] = commands.EndRecording();

    m_Queue->SubmitCommands( 1, &m_FrameCommands[ textureIndex ] );
    m_Queue->PresentTexture( &m_Window, textureIndex );
    return Result::Success;
  }

  void NvnPresenter::Shutdown() noexcept
  {
    if ( m_Queue != nullptr )
    {
      m_Queue->Finish();
    }
    DestroyImages();
#if !defined( APOLLO_BUILD_RELEASE )
    m_DebugUiRenderer.Shutdown();
#endif
    m_IndexedQuad.Shutdown();

    if ( m_SyncReady )
    {
      m_DisplayReleaseSync.Finalize();
      m_SyncReady = false;
    }
    if ( m_Layer != nullptr )
    {
      nn::vi::DestroyLayer( m_Layer );
      m_Layer = nullptr;
    }
    if ( m_Display != nullptr )
    {
      nn::vi::CloseDisplay( m_Display );
      m_Display = nullptr;
    }
    if ( m_ViReady )
    {
      nn::vi::Finalize();
      m_ViReady = false;
    }
    m_NativeWindow = {};
    m_Device       = nullptr;
    m_Queue        = nullptr;
  }

  Extent2D NvnPresenter::GetExtent() const noexcept
  {
    return m_Extent;
  }

  bool NvnPresenter::CreateImages( Extent2D extent ) noexcept
  {
    ::nvn::TextureBuilder textureBuilder{};
    textureBuilder.SetDefaults()
      .SetDevice( m_Device )
      .SetSize2D( static_cast<int>( extent.width ), static_cast<int>( extent.height ) )
      .SetTarget( ::nvn::TextureTarget::TARGET_2D )
      .SetFlags( ::nvn::TextureFlags::DISPLAY | ::nvn::TextureFlags::COMPRESSIBLE )
      .SetFormat( ::nvn::Format::RGBA8 );

    const size_t textureSize      = textureBuilder.GetStorageSize();
    const size_t textureAlignment = textureBuilder.GetStorageAlignment();
    if ( textureSize == 0 || textureAlignment == 0 )
    {
      return false;
    }
    const size_t textureStride = AlignUp( textureSize, textureAlignment );
    const size_t poolSize      = AlignUp( textureStride * BackbufferCount, NVN_MEMORY_POOL_STORAGE_GRANULARITY );
    m_TextureMemory           = AllocateAligned( NVN_MEMORY_POOL_STORAGE_ALIGNMENT, poolSize );
    if ( m_TextureMemory == nullptr )
    {
      return false;
    }

    ::nvn::MemoryPoolBuilder poolBuilder{};
    poolBuilder.SetDefaults()
      .SetDevice( m_Device )
      .SetFlags( ::nvn::MemoryPoolFlags::CPU_NO_ACCESS | ::nvn::MemoryPoolFlags::GPU_CACHED |
                 ::nvn::MemoryPoolFlags::COMPRESSIBLE )
      .SetStorage( m_TextureMemory, poolSize );
    if ( !m_TexturePool.Initialize( &poolBuilder ) )
    {
      DestroyImages();
      return false;
    }
    m_TexturePoolReady = true;

    ::nvn::Texture * texturePointers[ BackbufferCount ]{};
    for ( int index = 0; index < BackbufferCount; ++index )
    {
      textureBuilder.SetStorage( &m_TexturePool, static_cast<ptrdiff_t>( textureStride * index ) );
      if ( !m_Textures[ index ].Initialize( &textureBuilder ) )
      {
        DestroyImages();
        return false;
      }
      m_TextureReady[ index ] = true;
      texturePointers[ index ] = &m_Textures[ index ];
    }

    ::nvn::WindowBuilder windowBuilder{};
    windowBuilder.SetDefaults()
      .SetDevice( m_Device )
      .SetNativeWindow( m_NativeWindow )
      .SetTextures( BackbufferCount, texturePointers )
      .SetPresentInterval( 2 );
    if ( !m_Window.Initialize( &windowBuilder ) )
    {
      DestroyImages();
      return false;
    }
    m_WindowReady = true;
    m_Window.SetCrop( 0, 0, static_cast<int>( extent.width ), static_cast<int>( extent.height ) );

    const size_t commandPoolSize = CommandMemoryPerBuffer * BackbufferCount;
    m_CommandMemory = AllocateAligned( NVN_MEMORY_POOL_STORAGE_ALIGNMENT, commandPoolSize );
    if ( m_CommandMemory == nullptr )
    {
      DestroyImages();
      return false;
    }
    poolBuilder.SetDefaults()
      .SetDevice( m_Device )
      .SetFlags( ::nvn::MemoryPoolFlags::CPU_UNCACHED | ::nvn::MemoryPoolFlags::GPU_UNCACHED )
      .SetStorage( m_CommandMemory, commandPoolSize );
    if ( !m_CommandPool.Initialize( &poolBuilder ) )
    {
      DestroyImages();
      return false;
    }
    m_CommandPoolReady = true;

    int controlAlignment{};
    m_Device->GetInteger( ::nvn::DeviceInfo::COMMAND_BUFFER_CONTROL_ALIGNMENT, &controlAlignment );
    if ( controlAlignment <= 0 )
    {
      DestroyImages();
      return false;
    }
    for ( int index = 0; index < BackbufferCount; ++index )
    {
      m_ControlMemory[ index ] = AllocateAligned( static_cast<size_t>( controlAlignment ), ControlMemoryPerBuffer );
      if ( m_ControlMemory[ index ] == nullptr || !m_FrameBuffers[ index ].Initialize( m_Device ) )
      {
        DestroyImages();
        return false;
      }
      m_FrameBufferReady[ index ] = true;
      m_FrameBuffers[ index ].AddCommandMemory( &m_CommandPool, CommandMemoryPerBuffer * index,
                                               CommandMemoryPerBuffer );
      m_FrameBuffers[ index ].AddControlMemory( m_ControlMemory[ index ], ControlMemoryPerBuffer );
    }

    m_Extent = extent;
    m_Ready  = true;
    return true;
  }

  void NvnPresenter::DestroyImages() noexcept
  {
    m_Ready = false;
    for ( int index = 0; index < BackbufferCount; ++index )
    {
      if ( m_FrameBufferReady[ index ] )
      {
        m_FrameBuffers[ index ].Finalize();
        m_FrameBufferReady[ index ] = false;
      }
      std::free( m_ControlMemory[ index ] );
      m_ControlMemory[ index ] = nullptr;
      m_FrameCommands[ index ] = {};
    }
    if ( m_CommandPoolReady )
    {
      m_CommandPool.Finalize();
      m_CommandPoolReady = false;
    }
    std::free( m_CommandMemory );
    m_CommandMemory = nullptr;

    if ( m_WindowReady )
    {
      m_Window.Finalize();
      m_WindowReady = false;
    }
    for ( int index = 0; index < BackbufferCount; ++index )
    {
      if ( m_TextureReady[ index ] )
      {
        m_Textures[ index ].Finalize();
        m_TextureReady[ index ] = false;
      }
    }
    if ( m_TexturePoolReady )
    {
      m_TexturePool.Finalize();
      m_TexturePoolReady = false;
    }
    std::free( m_TextureMemory );
    m_TextureMemory = nullptr;
    m_Extent        = {};
  }
} // namespace apollo::render::nvn
