#pragma once

#include "Core/Types.hpp"

#if !defined( APOLLO_PLATFORM_WINDOWS )
  #error "WindowsWindow is available only on Windows."
#endif

#ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
  #define NOMINMAX
#endif
#include <Windows.h>

namespace apollo::platform
{
  struct ClientExtent
  {
    u32 width{};
    u32 height{};
  };

  class WindowsWindow final
  {
  public:
    WindowsWindow() noexcept = default;
    ~WindowsWindow() noexcept;

    WindowsWindow( const WindowsWindow & )             = delete;
    WindowsWindow & operator=( const WindowsWindow & ) = delete;
    WindowsWindow( WindowsWindow && )                  = delete;
    WindowsWindow & operator=( WindowsWindow && )      = delete;

    [[nodiscard]] bool Create( const wchar_t * title, ClientExtent clientExtent ) noexcept;
    void               Destroy() noexcept;

    // Waits for and dispatches one message. False indicates GetMessage failed.
    [[nodiscard]] bool         WaitForEvent() noexcept;
    void                       PumpEvents() noexcept;
    [[nodiscard]] bool         IsCloseRequested() const noexcept;
    [[nodiscard]] bool         ConsumeResize( ClientExtent & extent ) noexcept;
    [[nodiscard]] ClientExtent GetClientExtent() const noexcept;

    // The Windows Vulkan backend may use this to create its presentation surface.
    [[nodiscard]] HWND GetNativeHandle() const noexcept;

  private:
    static LRESULT CALLBACK WindowProcedure( HWND handle, UINT message, WPARAM wparam, LPARAM lparam ) noexcept;
    LRESULT                 HandleMessage( HWND handle, UINT message, WPARAM wparam, LPARAM lparam ) noexcept;

    HINSTANCE    m_Instance{};
    HWND         m_Handle{};
    ClientExtent m_ClientExtent{};
    bool         m_ClassRegistered{};
    bool         m_CloseRequested{};
    bool         m_ResizePending{};
  };
} // namespace apollo::platform
