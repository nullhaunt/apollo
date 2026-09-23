#include "Application.hpp"

#include "Core/BuildConfiguration.hpp"
#include "Platform/Diagnostics.hpp"
#include "Platform/Platform.hpp"

#if defined( APOLLO_PLATFORM_WINDOWS )
  #include "Render/RenderTypes.hpp"
#elif defined( APOLLO_PLATFORM_NX )
  #include <nn/oe.h>
#endif

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
