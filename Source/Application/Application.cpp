#include "Application.hpp"

#include "Core/BuildConfiguration.hpp"
#include "Platform/Diagnostics.hpp"
#include "Platform/Platform.hpp"
#include "Render/RenderBudget.hpp"

#if defined( APOLLO_PLATFORM_WINDOWS )
  #include "Render/RenderTypes.hpp"
#elif defined( APOLLO_PLATFORM_NX )
  #include <nn/oe.h>
  #include <nn/os/os_DebugApi.h>
#endif

#include <cstdio>

#if defined( APOLLO_PLATFORM_WINDOWS )
  #include <charconv>
  #include <limits>
#endif

namespace
{
#if defined( APOLLO_PLATFORM_WINDOWS )
  [[nodiscard]] bool ReadBudgetOverride( const char * name, size_t & bytes, bool & provided ) noexcept
  {
    char value[ 32 ]{};
    const DWORD length = GetEnvironmentVariableA( name, value, sizeof( value ) );
    if ( length == 0 )
    {
      provided = GetLastError() != ERROR_ENVVAR_NOT_FOUND;
      return !provided;
    }
    provided = true;
    if ( length >= sizeof( value ) ) return false;
    unsigned long long parsed{};
    const auto result = std::from_chars( value, value + length, parsed );
    if ( result.ec != std::errc{} || result.ptr != value + length || parsed == 0 ||
         parsed > std::numeric_limits<size_t>::max() ) return false;
    bytes = static_cast<size_t>( parsed );
    return true;
  }

  [[nodiscard]] bool ConfigurePcRenderBudget() noexcept
  {
    constexpr size_t MiB = 1024 * 1024;
    // A deliberately small pressure profile for this renderer slice. It is
    // not an estimate of Switch application memory.
    apollo::render::budget::Profile profile{ 32 * MiB, 48 * MiB };
    size_t nxAvailable{};
    bool nxProvided{}, softProvided{}, hardProvided{};
    if ( !ReadBudgetOverride( "APOLLO_NX_APP_AVAILABLE_BYTES", nxAvailable, nxProvided ) )
    {
      apollo::diagnostics::Write( apollo::diagnostics::Level::Error, "Invalid NX application memory reference." );
      return false;
    }
    if ( nxProvided )
    {
      // Divide first to avoid overflow if an invalidly large reference is supplied.
      profile.softBytes = nxAvailable / 100 * 75 + nxAvailable % 100 * 75 / 100;
      profile.hardBytes = nxAvailable / 100 * 85 + nxAvailable % 100 * 85 / 100;
    }
    if ( !ReadBudgetOverride( "APOLLO_RENDER_SOFT_BYTES", profile.softBytes, softProvided ) ||
         !ReadBudgetOverride( "APOLLO_RENDER_HARD_BYTES", profile.hardBytes, hardProvided ) )
    {
      apollo::diagnostics::Write( apollo::diagnostics::Level::Error, "Invalid PC renderer budget override." );
      return false;
    }
    if ( hardProvided && !softProvided )
      profile.softBytes = profile.hardBytes / 4 * 3 + profile.hardBytes % 4 * 3 / 4;
    if ( softProvided && !hardProvided && profile.softBytes > profile.hardBytes )
      profile.hardBytes = profile.softBytes;
    if ( profile.hardBytes == 0 || !apollo::render::budget::Configure( profile ) )
    {
      apollo::diagnostics::Write( apollo::diagnostics::Level::Error, "Invalid PC renderer budget profile." );
      return false;
    }
    if ( nxProvided ) apollo::render::budget::SetApplicationAvailableBytes( nxAvailable );
    char message[ 160 ]{};
    std::snprintf( message, sizeof( message ), "PC renderer logical budget: soft %llu, hard %llu bytes%s.",
                   static_cast<unsigned long long>( profile.softBytes ),
                   static_cast<unsigned long long>( profile.hardBytes ),
                   softProvided || hardProvided ? " (override)" :
                   nxProvided ? " (NX available reference)" : " (stress profile)" );
    apollo::diagnostics::Write( apollo::diagnostics::Level::Information, message );
    return true;
  }
#endif
} // namespace

namespace apollo
{
  Application::~Application() noexcept
  {
    Shutdown();
  }

  ApplicationExitStatus Application::Run() noexcept
  {
    if ( m_State != State::Created )
    {
      diagnostics::Write( diagnostics::Level::Error, "Application::Run called from an invalid lifecycle state." );
      return ApplicationExitStatus::InvalidLifecycleState;
    }

    if ( !Initialize() )
    {
      Shutdown();
      return ApplicationExitStatus::InitializationFailed;
    }

    m_State = State::Running;

#if defined( APOLLO_PLATFORM_WINDOWS )
    while ( !m_Window.IsCloseRequested() )
    {
      m_Window.PumpEvents();
      if ( m_Window.IsCloseRequested() )
      {
        break;
      }

      platform::ClientExtent extent{};
      if ( m_Window.ConsumeResize( extent ) )
      {
        if ( extent.width == 0 || extent.height == 0 )
        {
          m_Presenter.Shutdown();
          m_PresentationReady = false;
        }
        else if ( const render::Result resized = RecreatePresentation( extent );
                  resized != render::Result::Success && resized != render::Result::SurfaceUnavailable )
        {
          diagnostics::Write( diagnostics::Level::Error, "Vulkan presentation resize failed." );
          Shutdown();
          return ApplicationExitStatus::PlatformFailure;
        }
      }

      extent = m_Window.GetClientExtent();
      if ( extent.width == 0 || extent.height == 0 )
      {
        if ( !m_Window.WaitForEvent() )
        {
          diagnostics::Write( diagnostics::Level::Error, "Windows message pump failed." );
          Shutdown();
          return ApplicationExitStatus::PlatformFailure;
        }
        continue;
      }

      if ( !m_PresentationReady )
      {
        const render::Result recovered = RecreatePresentation( extent );
        if ( recovered != render::Result::Success && recovered != render::Result::SurfaceUnavailable )
        {
          diagnostics::Write( diagnostics::Level::Error, "Vulkan presentation recovery failed." );
          Shutdown();
          return ApplicationExitStatus::PlatformFailure;
        }
        if ( !m_PresentationReady )
        {
          Sleep( 16 );
          continue;
        }
      }

#if !defined( APOLLO_BUILD_RELEASE )
      m_DebugUi.BeginFrame( { extent.width, extent.height } );
#endif
      const render::Result frame = m_Presenter.PresentFrame( { 0.08f, 0.12f, 0.20f, 1.0f } );
      if ( frame == render::Result::SurfaceOutOfDate )
      {
        const render::Result recovered = RecreatePresentation( extent );
        if ( recovered != render::Result::Success && recovered != render::Result::SurfaceUnavailable )
        {
          diagnostics::Write( diagnostics::Level::Error, "Vulkan presentation recovery failed." );
          Shutdown();
          return ApplicationExitStatus::PlatformFailure;
        }
        continue;
      }
      if ( frame != render::Result::Success )
      {
        diagnostics::Write( diagnostics::Level::Error, "Vulkan presentation failed." );
        Shutdown();
        return ApplicationExitStatus::PlatformFailure;
      }
    }
#elif defined( APOLLO_PLATFORM_NX )
    while ( true )
    {
      nn::oe::Message message{};
      bool exitRequested{};
      while ( nn::oe::TryPopNotificationMessage( &message ) )
      {
        if ( message == nn::oe::MessageExitRequest )
        {
          exitRequested = true;
        }
      }
      if ( exitRequested )
      {
        break;
      }

      const bool handheld = nn::oe::GetOperationMode() == nn::oe::OperationMode_Handheld;
      const render::Extent2D extent = handheld ? render::Extent2D{ 1280, 720 } : render::Extent2D{ 1920, 1080 };
      if ( !m_NvnPresenter.Resize( extent ) )
      {
        diagnostics::Write( diagnostics::Level::Error, "NVN presentation resize failed." );
        Shutdown();
        return ApplicationExitStatus::PlatformFailure;
      }
#if !defined( APOLLO_BUILD_RELEASE )
      m_DebugUi.BeginFrame( extent );
#endif
      if ( m_NvnPresenter.PresentFrame() != render::Result::Success )
      {
        diagnostics::Write( diagnostics::Level::Error, "NVN presentation failed." );
        Shutdown();
        return ApplicationExitStatus::PlatformFailure;
      }
    }
#endif

    diagnostics::Write( diagnostics::Level::Information, "Run phase complete." );

    Shutdown();
    return ApplicationExitStatus::Success;
  }

  bool Application::Initialize() noexcept
  {
    m_State = State::Initializing;

    diagnostics::Write( diagnostics::Level::Information, "Starting Apollo." );
    diagnostics::Write( diagnostics::Level::Information, "Platform: ", platform::CurrentTargetName );
    diagnostics::Write( diagnostics::Level::Information, "Configuration: ", build::CurrentConfigurationName );

#if defined( APOLLO_PLATFORM_WINDOWS )
    if ( !ConfigurePcRenderBudget() ) return false;
    if ( !m_Window.Create( L"Apollo", { 1280, 720 } ) )
    {
      diagnostics::Write( diagnostics::Level::Error, "Windows window creation failed." );
      return false;
    }
    diagnostics::Write( diagnostics::Level::Information, "Windows window opened." );
    if ( !m_Vulkan.Initialize( m_Window.GetNativeHandle() ) )
    {
      diagnostics::Write( diagnostics::Level::Error, "Vulkan context initialization failed." );
      return false;
    }
#if !defined( APOLLO_BUILD_RELEASE )
    if ( !m_DebugUi.Initialize( m_Window.GetNativeHandle() ) )
    {
      diagnostics::Write( diagnostics::Level::Error, "Debug UI initialization failed." );
      return false;
    }
#endif
    const platform::ClientExtent extent = m_Window.GetClientExtent();
    if ( !m_Swapchain.Initialize( m_Vulkan, { extent.width, extent.height } ) ||
         !m_Presenter.Initialize( m_Vulkan, m_Swapchain ) )
    {
      diagnostics::Write( diagnostics::Level::Error, "Vulkan presentation initialization failed." );
      return false;
    }
    m_PresentationReady = true;
#elif defined( APOLLO_PLATFORM_NX )
    if ( !render::budget::Configure( {} ) ) return false;
    nn::os::MemoryInfo memoryInfo{};
    nn::os::QueryMemoryInfo( &memoryInfo );
    render::budget::SetApplicationAvailableBytes( memoryInfo.totalAvailableMemorySize );
    char memoryMessage[ 128 ]{};
    std::snprintf( memoryMessage, sizeof( memoryMessage ),
                   "NX application memory available: %llu bytes; 85%% target: %llu bytes.",
                   static_cast<unsigned long long>( memoryInfo.totalAvailableMemorySize ),
                   static_cast<unsigned long long>( memoryInfo.totalAvailableMemorySize * 85 / 100 ) );
    diagnostics::Write( diagnostics::Level::Information, memoryMessage );
    if ( !m_Nvn.Initialize() )
    {
      diagnostics::Write( diagnostics::Level::Error, "NVN context initialization failed." );
      return false;
    }
#if !defined( APOLLO_BUILD_RELEASE )
    if ( !m_DebugUi.Initialize() )
    {
      diagnostics::Write( diagnostics::Level::Error, "Debug UI initialization failed." );
      return false;
    }
#endif
    const bool handheld = nn::oe::GetOperationMode() == nn::oe::OperationMode_Handheld;
    const render::Extent2D extent = handheld ? render::Extent2D{ 1280, 720 } : render::Extent2D{ 1920, 1080 };
    if ( !m_NvnPresenter.Initialize( m_Nvn, extent ) )
    {
      diagnostics::Write( diagnostics::Level::Error, "NVN presentation initialization failed." );
      return false;
    }
#endif

    m_State = State::Initialized;
    diagnostics::Write( diagnostics::Level::Information, "Initialization complete." );
    return true;
  }

  void Application::Shutdown() noexcept
  {
    if ( m_State == State::Created || m_State == State::ShuttingDown || m_State == State::Stopped )
    {
      return;
    }

    m_State = State::ShuttingDown;

#if defined( APOLLO_PLATFORM_WINDOWS )
    m_PresentationReady = false;
    m_Presenter.Shutdown();
#if !defined( APOLLO_BUILD_RELEASE )
    m_DebugUi.Shutdown();
#endif
    m_Swapchain.Shutdown();
    m_Vulkan.Shutdown();
    m_Window.Destroy();
#elif defined( APOLLO_PLATFORM_NX )
    m_NvnPresenter.Shutdown();
#if !defined( APOLLO_BUILD_RELEASE )
    m_DebugUi.Shutdown();
#endif
    m_Nvn.Shutdown();
#endif

    if ( render::budget::GetSnapshot().currentBytes != 0 )
      diagnostics::Write( diagnostics::Level::Error, "Renderer logical budget reservations remain at shutdown." );

    diagnostics::Write( diagnostics::Level::Information, "Shutdown complete." );
    m_State = State::Stopped;
  }

#if defined( APOLLO_PLATFORM_WINDOWS )
  render::Result Application::RecreatePresentation( platform::ClientExtent extent ) noexcept
  {
    m_Presenter.Shutdown();
    m_PresentationReady = false;

    const render::Result resized = m_Swapchain.Resize( { extent.width, extent.height } );
    if ( resized != render::Result::Success )
    {
      return resized;
    }
    if ( !m_Presenter.Initialize( m_Vulkan, m_Swapchain ) )
    {
      return render::Result::Failure;
    }
    m_PresentationReady = true;
    return render::Result::Success;
  }
#endif
} // namespace apollo
