#include "WindowsWindow.hpp"

#include <limits>

namespace
{
  constexpr wchar_t WindowClassName[] = L"ApolloMainWindow";
  constexpr DWORD   WindowStyle       = WS_OVERLAPPEDWINDOW;
} // namespace

namespace apollo::platform
{
  WindowsWindow::~WindowsWindow() noexcept
  {
    Destroy();
  }

  bool WindowsWindow::Create( const wchar_t * title, ClientExtent clientExtent ) noexcept
  {
    if ( m_Handle != nullptr || title == nullptr || clientExtent.width == 0 || clientExtent.height == 0 ||
         clientExtent.width > static_cast<u32>( std::numeric_limits<LONG>::max() ) ||
         clientExtent.height > static_cast<u32>( std::numeric_limits<LONG>::max() ) )
    {
      return false;
    }

    m_CloseRequested = false;
    m_ResizePending  = false;

    m_Instance = GetModuleHandleW( nullptr );
    if ( m_Instance == nullptr )
    {
      return false;
    }

    WNDCLASSEXW windowClass{};
    windowClass.cbSize        = sizeof( windowClass );
    windowClass.lpfnWndProc   = WindowProcedure;
    windowClass.hInstance     = m_Instance;
    windowClass.hCursor       = LoadCursorW( nullptr, IDC_ARROW );
    windowClass.lpszClassName = WindowClassName;

    if ( RegisterClassExW( &windowClass ) == 0 )
    {
      m_Instance = nullptr;
      return false;
    }
    m_ClassRegistered = true;

    RECT windowRect{ 0, 0, static_cast<LONG>( clientExtent.width ), static_cast<LONG>( clientExtent.height ) };
    if ( !AdjustWindowRectEx( &windowRect, WindowStyle, FALSE, 0 ) )
    {
      Destroy();
      return false;
    }

    const int windowWidth  = windowRect.right - windowRect.left;
    const int windowHeight = windowRect.bottom - windowRect.top;

    m_Handle = CreateWindowExW( 0,
                                WindowClassName,
                                title,
                                WindowStyle,
                                CW_USEDEFAULT,
                                CW_USEDEFAULT,
                                windowWidth,
                                windowHeight,
                                nullptr,
                                nullptr,
                                m_Instance,
                                this );
    if ( m_Handle == nullptr )
    {
      Destroy();
      return false;
    }

    RECT actualClientRect{};
    if ( !GetClientRect( m_Handle, &actualClientRect ) )
    {
      Destroy();
      return false;
    }

    m_ClientExtent  = { static_cast<u32>( actualClientRect.right ), static_cast<u32>( actualClientRect.bottom ) };
    m_ResizePending = false;
    ShowWindow( m_Handle, SW_SHOW );
    UpdateWindow( m_Handle );
    return true;
  }

  void WindowsWindow::Destroy() noexcept
  {
    if ( m_Handle != nullptr )
    {
      DestroyWindow( m_Handle );
      m_Handle = nullptr;
    }

    if ( m_ClassRegistered )
    {
      UnregisterClassW( WindowClassName, m_Instance );
      m_ClassRegistered = false;
    }

    m_Instance       = nullptr;
    m_ClientExtent   = {};
    m_CloseRequested = true;
    m_ResizePending  = false;
  }

  bool WindowsWindow::WaitForEvent() noexcept
  {
    if ( m_CloseRequested )
    {
      return true;
    }

    MSG        message{};
    const BOOL result = GetMessageW( &message, nullptr, 0, 0 );
    if ( result < 0 )
    {
      return false;
    }
    if ( result == 0 )
    {
      m_CloseRequested = true;
      return true;
    }

    TranslateMessage( &message );
    DispatchMessageW( &message );
    return true;
  }

  void WindowsWindow::PumpEvents() noexcept
  {
    MSG message{};
    while ( PeekMessageW( &message, nullptr, 0, 0, PM_REMOVE ) )
    {
      if ( message.message == WM_QUIT )
      {
        m_CloseRequested = true;
        return;
      }
      TranslateMessage( &message );
      DispatchMessageW( &message );
    }
  }

  bool WindowsWindow::IsCloseRequested() const noexcept
  {
    return m_CloseRequested;
  }

  bool WindowsWindow::ConsumeResize( ClientExtent & extent ) noexcept
  {
    if ( !m_ResizePending )
    {
      return false;
    }

    extent          = m_ClientExtent;
    m_ResizePending = false;
    return true;
  }

  ClientExtent WindowsWindow::GetClientExtent() const noexcept
  {
    return m_ClientExtent;
  }

  HWND WindowsWindow::GetNativeHandle() const noexcept
  {
    return m_Handle;
  }

  LRESULT CALLBACK WindowsWindow::WindowProcedure( HWND handle, UINT message, WPARAM wparam, LPARAM lparam ) noexcept
  {
    if ( message == WM_NCCREATE )
    {
      const auto * create = reinterpret_cast<const CREATESTRUCTW *>( lparam );
      auto *       window = static_cast<WindowsWindow *>( create->lpCreateParams );
      SetWindowLongPtrW( handle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>( window ) );
      window->m_Handle = handle;
    }

    auto * window = reinterpret_cast<WindowsWindow *>( GetWindowLongPtrW( handle, GWLP_USERDATA ) );
    if ( window != nullptr )
    {
      return window->HandleMessage( handle, message, wparam, lparam );
    }
    return DefWindowProcW( handle, message, wparam, lparam );
  }

  LRESULT WindowsWindow::HandleMessage( HWND handle, UINT message, WPARAM wparam, LPARAM lparam ) noexcept
  {
    switch ( message )
    {
      case WM_SIZE:
        m_ClientExtent  = { static_cast<u32>( LOWORD( lparam ) ), static_cast<u32>( HIWORD( lparam ) ) };
        m_ResizePending = true;
        return 0;

      case WM_CLOSE:
        m_CloseRequested = true;
        return 0;

      case WM_PAINT:
      {
        PAINTSTRUCT paint{};
        const HDC   context = BeginPaint( handle, &paint );
        FillRect( context, &paint.rcPaint, GetSysColorBrush( COLOR_WINDOW ) );
        EndPaint( handle, &paint );
        return 0;
      }

      case WM_DESTROY:
        m_CloseRequested = true;
        return 0;

      case WM_NCDESTROY:
        SetWindowLongPtrW( handle, GWLP_USERDATA, 0 );
        m_Handle = nullptr;
        return DefWindowProcW( handle, message, wparam, lparam );

      default:
        return DefWindowProcW( handle, message, wparam, lparam );
    }
  }
} // namespace apollo::platform
