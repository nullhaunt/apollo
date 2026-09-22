#include "Application.hpp"

#include "Core/BuildConfiguration.hpp"
#include "Platform/Diagnostics.hpp"
#include "Platform/Platform.hpp"

#if defined( APOLLO_PLATFORM_WINDOWS )
  #include <cstdio>
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
      if ( !m_Window.WaitForEvent() )
      {
        diagnostics::Write( diagnostics::Level::Error, "Windows message pump failed." );
        Shutdown();
        return ApplicationExitStatus::PlatformFailure;
      }

      platform::ClientExtent extent{};
      if ( m_Window.ConsumeResize( extent ) )
      {
        char size[ 48 ]{};
        std::snprintf( size, sizeof( size ), "%u x %u", extent.width, extent.height );
        diagnostics::Write( diagnostics::Level::Information, "Client size: ", size );
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
    m_Vulkan.Shutdown();
    m_Window.Destroy();
#endif

    diagnostics::Write( diagnostics::Level::Information, "Shutdown complete." );
    m_State = State::Stopped;
  }
} // namespace apollo
