#pragma once

namespace apollo
{
  enum class ApplicationExitStatus
  {
    Success,
    InitializationFailed,
    InvalidLifecycleState
  };

  class Application final
  {
  public:
    Application() noexcept = default;
    ~Application() noexcept;

    Application( const Application & )             = delete;
    Application & operator=( const Application & ) = delete;
    Application( Application && )                  = delete;
    Application & operator=( Application && )      = delete;

    [[nodiscard]] ApplicationExitStatus Run() noexcept;

  private:
    enum class State
    {
      Created,
      Initializing,
      Initialized,
      Running,
      ShuttingDown,
      Stopped
    };

    [[nodiscard]] bool Initialize() noexcept;
    void               Shutdown() noexcept;

    State m_State{ State::Created };
  };
} // namespace apollo