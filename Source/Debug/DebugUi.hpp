#pragma once

#include "Render/RenderTypes.hpp"
#include "Mii/MiiCatalog.hpp"
#include "Mii/MiiResources.hpp"

#if defined( APOLLO_PLATFORM_WINDOWS )
  #include "Platform/Windows/WindowsWindow.hpp"
#endif

#include <chrono>

namespace apollo::debug
{
  class DebugUi final
  {
  public:
    DebugUi() noexcept = default;
    ~DebugUi() noexcept;

    DebugUi( const DebugUi & ) = delete;
    DebugUi & operator=( const DebugUi & ) = delete;

#if defined( APOLLO_PLATFORM_WINDOWS )
    [[nodiscard]] bool Initialize( HWND window ) noexcept;
#else
    [[nodiscard]] bool Initialize() noexcept;
#endif
    void BeginFrame( render::Extent2D extent, const mii::Catalog & miiCatalog,
                     const mii::ResourceFiles & miiResources, bool nxMiiModelReady ) noexcept;
    void Shutdown() noexcept;

  private:
#if defined( APOLLO_PLATFORM_NX )
    void UpdateNxInput( render::Extent2D extent ) noexcept;
    unsigned int m_ConnectedNpadCount{};
#endif
    std::chrono::steady_clock::time_point m_PreviousFrame{};
    bool                                  m_Ready{};
    bool                                  m_ShowDemo{};
    bool                                  m_FirstFrameReported{};
    int                                   m_SelectedMii{};
  };
} // namespace apollo::debug
