#include "Debug/DebugUi.hpp"

#include "Core/BuildConfiguration.hpp"
#include "Platform/Diagnostics.hpp"
#include "Platform/Platform.hpp"
#include "Render/RenderTelemetry.hpp"

#include <imgui.h>

#if defined( APOLLO_PLATFORM_WINDOWS )
  #include <backends/imgui_impl_vulkan.h>
  #include <backends/imgui_impl_win32.h>
#else
  #include <nn/hid.h>
#endif

#include <algorithm>

namespace apollo::debug
{
#if defined( APOLLO_PLATFORM_NX )
  namespace
  {
    constexpr nn::hid::NpadIdType SupportedNpadIds[]{
      nn::hid::NpadId::No1, nn::hid::NpadId::No2, nn::hid::NpadId::No3, nn::hid::NpadId::No4,
      nn::hid::NpadId::No5, nn::hid::NpadId::No6, nn::hid::NpadId::No7, nn::hid::NpadId::No8,
      nn::hid::NpadId::Handheld
    };
  } // namespace
#endif

  DebugUi::~DebugUi() noexcept
  {
    Shutdown();
  }

#if defined( APOLLO_PLATFORM_WINDOWS )
  bool DebugUi::Initialize( HWND window ) noexcept
#else
  bool DebugUi::Initialize() noexcept
#endif
  {
    if ( m_Ready ) return false;
    IMGUI_CHECKVERSION();
    if ( ImGui::CreateContext() == nullptr ) return false;
    ImGuiIO & io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();
#if defined( APOLLO_PLATFORM_WINDOWS )
    if ( !ImGui_ImplWin32_Init( window ) )
    {
      ImGui::DestroyContext();
      return false;
    }
    // Apollo renders only into its own window. The Win32 backend installs
    // optional platform-window callbacks even when ViewportsEnable is off;
    // its DPI callback expects viewport-owned data that single-window frames
    // do not need. Use ImGui's monitor DPI instead.
    io.BackendFlags &= ~ImGuiBackendFlags_PlatformHasViewports;
    ImGui::GetPlatformIO().Platform_GetWindowDpiScale = nullptr;
#else
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.BackendPlatformName = "apollo_nx_hid";
    ImFontConfig fontConfig{};
    fontConfig.SizePixels = 22.0f;
    io.Fonts->AddFontDefault( &fontConfig );
    ImGui::GetStyle().ScaleAllSizes( 1.4f );
    nn::hid::InitializeNpad();
    nn::hid::InitializeTouchScreen( 1280, 720 );
    nn::hid::SetSupportedNpadStyleSet( nn::hid::NpadStyleFullKey::Mask |
                                       nn::hid::NpadStyleJoyDual::Mask |
                                       nn::hid::NpadStyleJoyLeft::Mask |
                                       nn::hid::NpadStyleJoyRight::Mask |
                                       nn::hid::NpadStyleHandheld::Mask );
    nn::hid::SetSupportedNpadIdType( SupportedNpadIds,
                                    sizeof( SupportedNpadIds ) / sizeof( SupportedNpadIds[ 0 ] ) );
    nn::hid::SetNpadJoyHoldType( nn::hid::NpadJoyHoldType_Horizontal );
#endif
    m_PreviousFrame = std::chrono::steady_clock::now();
    m_Ready = true;
    diagnostics::Write( diagnostics::Level::Information, "Dear ImGui debug UI initialized." );
    return true;
  }

  void DebugUi::BeginFrame( render::Extent2D extent ) noexcept
  {
    if ( !m_Ready ) return;
#if defined( APOLLO_PLATFORM_WINDOWS )
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplWin32_NewFrame();
#else
    const auto now = std::chrono::steady_clock::now();
    ImGuiIO & io = ImGui::GetIO();
    io.DeltaTime = std::max( 1.0f / 1000.0f,
      std::chrono::duration<float>( now - m_PreviousFrame ).count() );
    io.DisplaySize = ImVec2( static_cast<float>( extent.width ), static_cast<float>( extent.height ) );
    UpdateNxInput( extent );
    m_PreviousFrame = now;
#endif
    ImGui::NewFrame();
    ImGui::DockSpaceOverViewport( 0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode );
    ImGui::SetNextWindowPos( ImVec2( 20, 20 ), ImGuiCond_FirstUseEver );
#if defined( APOLLO_PLATFORM_NX )
    ImGui::SetNextWindowSize( ImVec2( 600, 390 ), ImGuiCond_FirstUseEver );
#else
    ImGui::SetNextWindowSize( ImVec2( 540, 310 ), ImGuiCond_FirstUseEver );
#endif
    ImGui::Begin( "Apollo Diagnostics" );
    ImGui::Text( "Platform: %s", platform::CurrentTargetName );
    ImGui::Text( "Configuration: %s", build::CurrentConfigurationName );
    ImGui::Text( "Display: %u x %u", extent.width, extent.height );
#if defined( APOLLO_PLATFORM_NX )
    ImGui::Text( "Controllers: %u", m_ConnectedNpadCount );
#endif
    ImGui::Text( "ImGui: %s", ImGui::GetVersion() );
    ImGui::Separator();
    const float fps = ImGui::GetIO().Framerate;
    ImGui::Text( "CPU loop: %.2f ms (%.1f FPS)", fps > 0.0f ? 1000.0f / fps : 0.0f, fps );
    const auto gpu = render::telemetry::GetGpuTimings();
    if ( gpu.valid )
    {
      ImGui::Text( "GPU frame: %.3f ms", gpu.totalMs );
      ImGui::Text( "Clear %.3f  Quad %.3f  UI %.3f ms", gpu.clearMs, gpu.quadMs, gpu.uiMs );
    }
    else
    {
      ImGui::TextUnformatted( render::telemetry::IsGpuTimingAvailable()
        ? "GPU: waiting for a completed frame" : "GPU timestamps unavailable" );
    }
    const auto memory = render::telemetry::GetMemory();
    ImGui::Text( "Tracked: %.2f / %.2f MiB (now / peak)",
                 static_cast<double>( memory.currentBytes ) / ( 1024.0 * 1024.0 ),
                 static_cast<double>( memory.peakBytes ) / ( 1024.0 * 1024.0 ) );
    ImGui::Text( "Allocations: %u / %u (now / peak)",
                 memory.currentAllocations, memory.peakAllocations );
#if defined( APOLLO_PLATFORM_NX )
    ImGui::TextUnformatted( "Touch or use D-pad, A and B." );
#endif
    ImGui::Checkbox( "ImGui demo", &m_ShowDemo );
    ImGui::End();
    if ( m_ShowDemo ) ImGui::ShowDemoWindow( &m_ShowDemo );
    ImGui::Render();
    if ( !m_FirstFrameReported )
    {
      diagnostics::Write( ImGui::GetDrawData()->TotalVtxCount > 0
        ? diagnostics::Level::Information : diagnostics::Level::Error,
        ImGui::GetDrawData()->TotalVtxCount > 0
          ? "Dear ImGui produced debug UI draw data."
          : "Dear ImGui produced no debug UI draw data." );
      m_FirstFrameReported = true;
    }
  }

#if defined( APOLLO_PLATFORM_NX )
  void DebugUi::UpdateNxInput( render::Extent2D extent ) noexcept
  {
    ImGuiIO & io = ImGui::GetIO();
    nn::hid::NpadButtonSet buttons{};
    m_ConnectedNpadCount = 0;
    for ( const nn::hid::NpadIdType id : SupportedNpadIds )
    {
      const nn::hid::NpadStyleSet style = nn::hid::GetNpadStyleSet( id );
      if ( style.IsAllOff() ) continue;
      nn::hid::NpadButtonSet current{};
      if ( style.Test<nn::hid::NpadStyleFullKey>() )
      {
        nn::hid::NpadFullKeyState state{};
        nn::hid::GetNpadState( &state, id );
        current = state.buttons;
      }
      else if ( style.Test<nn::hid::NpadStyleJoyDual>() )
      {
        nn::hid::NpadJoyDualState state{};
        nn::hid::GetNpadState( &state, id );
        current = state.buttons;
      }
      else if ( style.Test<nn::hid::NpadStyleHandheld>() )
      {
        nn::hid::NpadHandheldState state{};
        nn::hid::GetNpadState( &state, id );
        current = state.buttons;
      }
      else if ( style.Test<nn::hid::NpadStyleJoyLeft>() )
      {
        nn::hid::NpadJoyLeftState state{};
        nn::hid::GetNpadState( &state, id );
        current = state.buttons;
        if ( nn::hid::GetNpadJoyHoldType() == nn::hid::NpadJoyHoldType_Horizontal )
        {
          current.Set<nn::hid::NpadButton::A>( state.buttons.Test<nn::hid::NpadButton::Down>() );
          current.Set<nn::hid::NpadButton::B>( state.buttons.Test<nn::hid::NpadButton::Left>() );
          current.Set<nn::hid::NpadButton::Left>( state.buttons.Test<nn::hid::NpadButton::StickLUp>() );
          current.Set<nn::hid::NpadButton::Right>( state.buttons.Test<nn::hid::NpadButton::StickLDown>() );
          current.Set<nn::hid::NpadButton::Up>( state.buttons.Test<nn::hid::NpadButton::StickLRight>() );
          current.Set<nn::hid::NpadButton::Down>( state.buttons.Test<nn::hid::NpadButton::StickLLeft>() );
          current.Set<nn::hid::NpadButton::L>( state.buttons.Test<nn::hid::NpadJoyButton::LeftSL>() );
          current.Set<nn::hid::NpadButton::R>( state.buttons.Test<nn::hid::NpadJoyButton::LeftSR>() );
        }
      }
      else if ( style.Test<nn::hid::NpadStyleJoyRight>() )
      {
        nn::hid::NpadJoyRightState state{};
        nn::hid::GetNpadState( &state, id );
        current = state.buttons;
        if ( nn::hid::GetNpadJoyHoldType() == nn::hid::NpadJoyHoldType_Horizontal )
        {
          current.Set<nn::hid::NpadButton::A>( state.buttons.Test<nn::hid::NpadButton::X>() );
          current.Set<nn::hid::NpadButton::B>( state.buttons.Test<nn::hid::NpadButton::A>() );
          current.Set<nn::hid::NpadButton::Left>( state.buttons.Test<nn::hid::NpadButton::StickRDown>() );
          current.Set<nn::hid::NpadButton::Right>( state.buttons.Test<nn::hid::NpadButton::StickRUp>() );
          current.Set<nn::hid::NpadButton::Up>( state.buttons.Test<nn::hid::NpadButton::StickRLeft>() );
          current.Set<nn::hid::NpadButton::Down>( state.buttons.Test<nn::hid::NpadButton::StickRRight>() );
          current.Set<nn::hid::NpadButton::L>( state.buttons.Test<nn::hid::NpadJoyButton::RightSL>() );
          current.Set<nn::hid::NpadButton::R>( state.buttons.Test<nn::hid::NpadButton::R>() ||
                                               state.buttons.Test<nn::hid::NpadJoyButton::RightSR>() );
        }
      }
      else continue;

      if ( !style.Test<nn::hid::NpadStyleJoyLeft>() && !style.Test<nn::hid::NpadStyleJoyRight>() )
      {
        current.Set<nn::hid::NpadButton::Left>( current.Test<nn::hid::NpadButton::Left>() ||
                                                current.Test<nn::hid::NpadButton::StickLLeft>() );
        current.Set<nn::hid::NpadButton::Right>( current.Test<nn::hid::NpadButton::Right>() ||
                                                 current.Test<nn::hid::NpadButton::StickLRight>() );
        current.Set<nn::hid::NpadButton::Up>( current.Test<nn::hid::NpadButton::Up>() ||
                                              current.Test<nn::hid::NpadButton::StickLUp>() );
        current.Set<nn::hid::NpadButton::Down>( current.Test<nn::hid::NpadButton::Down>() ||
                                                current.Test<nn::hid::NpadButton::StickLDown>() );
      }
      buttons |= current;
      ++m_ConnectedNpadCount;
    }
    if ( m_ConnectedNpadCount > 0 ) io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    else io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
    io.AddKeyEvent( ImGuiKey_GamepadDpadLeft, buttons.Test<nn::hid::NpadButton::Left>() );
    io.AddKeyEvent( ImGuiKey_GamepadDpadRight, buttons.Test<nn::hid::NpadButton::Right>() );
    io.AddKeyEvent( ImGuiKey_GamepadDpadUp, buttons.Test<nn::hid::NpadButton::Up>() );
    io.AddKeyEvent( ImGuiKey_GamepadDpadDown, buttons.Test<nn::hid::NpadButton::Down>() );
    io.AddKeyEvent( ImGuiKey_GamepadFaceDown, buttons.Test<nn::hid::NpadButton::A>() );
    io.AddKeyEvent( ImGuiKey_GamepadFaceRight, buttons.Test<nn::hid::NpadButton::B>() );
    io.AddKeyEvent( ImGuiKey_GamepadL1, buttons.Test<nn::hid::NpadButton::L>() );
    io.AddKeyEvent( ImGuiKey_GamepadR1, buttons.Test<nn::hid::NpadButton::R>() );

    nn::hid::TouchScreenState<1> touch{};
    nn::hid::GetTouchScreenState( &touch );
    if ( touch.count > 0 )
    {
      io.AddMousePosEvent( touch.touches[0].x * static_cast<float>( extent.width ) / 1280.0f,
                           touch.touches[0].y * static_cast<float>( extent.height ) / 720.0f );
    }
    io.AddMouseButtonEvent( 0, touch.count > 0 );
  }
#endif

  void DebugUi::Shutdown() noexcept
  {
    if ( !m_Ready ) return;
#if defined( APOLLO_PLATFORM_WINDOWS )
    ImGui_ImplWin32_Shutdown();
#endif
    ImGui::DestroyContext();
    m_Ready = false;
    m_FirstFrameReported = false;
  }
} // namespace apollo::debug
