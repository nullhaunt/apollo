#include "Application.hpp"

#include "Core/BuildConfiguration.hpp"
#include "Platform/Diagnostics.hpp"
#include "Platform/Platform.hpp"

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
    diagnostics::Write( diagnostics::Level::Information, "Shutdown complete." );
    m_State = State::Stopped;
  }
} // namespace apollo