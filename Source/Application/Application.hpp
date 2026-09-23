#pragma once

#include "Platform/Platform.hpp"
#include "Mii/MiiCatalog.hpp"
#include "Mii/MiiResources.hpp"
#if !defined( APOLLO_BUILD_RELEASE )
  #include "Debug/DebugUi.hpp"
#endif

#if defined( APOLLO_PLATFORM_WINDOWS )
  #include "Platform/Windows/WindowsWindow.hpp"
  #include "Render/Vulkan/VulkanContext.hpp"
  #include "Render/Vulkan/VulkanPresenter.hpp"
  #include "Render/Vulkan/VulkanSwapchain.hpp"
#elif defined( APOLLO_PLATFORM_NX )
  #include "Mii/MiiNvnModel.hpp"
  #include "Render/Nvn/NvnContext.hpp"
  #include "Render/Nvn/NvnPresenter.hpp"
#endif

namespace apollo
{
  enum class ApplicationExitStatus
  {
    Success,
    InitializationFailed,
    PlatformFailure,
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
#if defined( APOLLO_PLATFORM_WINDOWS )
    [[nodiscard]] render::Result RecreatePresentation( platform::ClientExtent extent ) noexcept;
#endif

    State m_State{ State::Created };
    mii::Catalog m_MiiCatalog{};
    mii::ResourceFiles m_MiiResources{};

#if !defined( APOLLO_BUILD_RELEASE )
    debug::DebugUi m_DebugUi{};
#endif

#if defined( APOLLO_PLATFORM_WINDOWS )
    platform::WindowsWindow         m_Window{};
    render::vulkan::VulkanContext   m_Vulkan{};
    render::vulkan::VulkanSwapchain m_Swapchain{};
    render::vulkan::VulkanPresenter m_Presenter{};
    bool                            m_PresentationReady{};
#elif defined( APOLLO_PLATFORM_NX )
    render::nvn::NvnContext   m_Nvn{};
    render::nvn::NvnPresenter m_NvnPresenter{};
    mii::NvnModel m_MiiModel{};
#endif
  };
} // namespace apollo
