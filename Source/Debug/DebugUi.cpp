#include "Debug/DebugUi.hpp"

#include "Core/BuildConfiguration.hpp"
#include "Platform/Diagnostics.hpp"
#include "Platform/Platform.hpp"
#include "Render/RenderBudget.hpp"
#include "Render/RenderTelemetry.hpp"

#include <imgui.h>

#if defined( APOLLO_PLATFORM_WINDOWS )
  #include <backends/imgui_impl_vulkan.h>
  #include <backends/imgui_impl_win32.h>
#else
  #include <nn/hid.h>
#endif

#include <algorithm>
#include <cstdio>

namespace apollo::debug
{
#if defined( APOLLO_PLATFORM_NX )
  namespace
  {
    constexpr nn::hid::NpadIdType SupportedNpadIds[]{ nn::hid::NpadId::No1,
                                                      nn::hid::NpadId::No2,
                                                      nn::hid::NpadId::No3,
                                                      nn::hid::NpadId::No4,
                                                      nn::hid::NpadId::No5,
                                                      nn::hid::NpadId::No6,
                                                      nn::hid::NpadId::No7,
                                                      nn::hid::NpadId::No8,
                                                      nn::hid::NpadId::Handheld };
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
    if ( m_Ready )
    {
      return false;
    }
    IMGUI_CHECKVERSION();
    if ( ImGui::CreateContext() == nullptr )
    {
      return false;
    }

    ImGuiIO & io    = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename  = nullptr;
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
    io.BackendFlags                                   &= ~ImGuiBackendFlags_PlatformHasViewports;
    ImGui::GetPlatformIO().Platform_GetWindowDpiScale  = nullptr;
#else
    io.ConfigFlags         |= ImGuiConfigFlags_NavEnableGamepad;
    io.BackendPlatformName  = "apollo_nx_hid";

    ImFontConfig fontConfig{};
    fontConfig.SizePixels = 22.0f;
    io.Fonts->AddFontDefault( &fontConfig );
    ImGui::GetStyle().ScaleAllSizes( 1.4f );

    nn::hid::InitializeNpad();
    nn::hid::InitializeTouchScreen( 1280, 720 );
    nn::hid::SetSupportedNpadStyleSet( nn::hid::NpadStyleFullKey::Mask | nn::hid::NpadStyleJoyDual::Mask |
                                       nn::hid::NpadStyleJoyLeft::Mask | nn::hid::NpadStyleJoyRight::Mask |
                                       nn::hid::NpadStyleHandheld::Mask );
    nn::hid::SetSupportedNpadIdType( SupportedNpadIds, sizeof( SupportedNpadIds ) / sizeof( SupportedNpadIds[ 0 ] ) );
    nn::hid::SetNpadJoyHoldType( nn::hid::NpadJoyHoldType_Horizontal );
#endif

    m_PreviousFrame = std::chrono::steady_clock::now();
    m_Ready         = true;
    diagnostics::Write( diagnostics::Level::Information, "Dear ImGui debug UI initialized." );
    return true;
  }

  void DebugUi::BeginFrame( render::Extent2D           extent,
                            const mii::Catalog &       miiCatalog,
                            const mii::ResourceFiles & miiResources,
                            bool                       nxMiiModelReady,
                            mii::PreviewCamera &       previewCamera ) noexcept
  {
    if ( !m_Ready )
    {
      return;
    }
#if defined( APOLLO_PLATFORM_WINDOWS )
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplWin32_NewFrame();
#else
    const auto now = std::chrono::steady_clock::now();
    ImGuiIO &  io  = ImGui::GetIO();
    io.DeltaTime   = std::max( 1.0f / 1000.0f, std::chrono::duration<float>( now - m_PreviousFrame ).count() );
    io.DisplaySize = ImVec2( static_cast<float>( extent.width ), static_cast<float>( extent.height ) );
    UpdateNxInput( extent );
    m_PreviousFrame = now;
#endif

    ImGui::NewFrame();
    ImGui::DockSpaceOverViewport( 0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode );
    ImGui::SetNextWindowPos( ImVec2( 20, 20 ), ImGuiCond_FirstUseEver );
#if defined( APOLLO_PLATFORM_NX )
    ImGui::SetNextWindowSize( ImVec2( 600, 420 ), ImGuiCond_FirstUseEver );
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
      ImGui::TextUnformatted( render::telemetry::IsGpuTimingAvailable() ? "GPU: waiting for a completed frame"
                                                                        : "GPU timestamps unavailable" );
    }

    const auto memory = render::telemetry::GetMemory();
    ImGui::Text( "Tracked: %.2f / %.2f MiB (now / peak)",
                 static_cast<double>( memory.currentBytes ) / ( 1024.0 * 1024.0 ),
                 static_cast<double>( memory.peakBytes ) / ( 1024.0 * 1024.0 ) );
    ImGui::Text( "Allocations: %u / %u (now / peak)", memory.currentAllocations, memory.peakAllocations );

    const auto       budget = render::budget::GetSnapshot();
    constexpr double MiB    = 1024.0 * 1024.0;
    ImGui::Text( "Logical: %.2f / %.2f MiB (now / peak)",
                 static_cast<double>( budget.currentBytes ) / MiB,
                 static_cast<double>( budget.peakBytes ) / MiB );
    if ( budget.profile.hardBytes != 0 )
    {
      ImGui::Text( "PC cap: %.0f / %.0f MiB (soft / hard), %u denied",
                   static_cast<double>( budget.profile.softBytes ) / MiB,
                   static_cast<double>( budget.profile.hardBytes ) / MiB,
                   budget.deniedRequests );
    }

    if ( budget.applicationAvailableBytes != 0 )
    {
      ImGui::Text( "NX app available: %.0f MiB (85%%: %.0f MiB)",
                   static_cast<double>( budget.applicationAvailableBytes ) / MiB,
                   static_cast<double>( budget.applicationAvailableBytes ) * 0.85 / MiB );
    }

    if ( ImGui::TreeNode( "Logical resource charges" ) )
    {
      const auto chargeKiB = [ & ]( render::budget::Resource resource ) noexcept {
        return static_cast<double>( budget.currentByResource[ static_cast<size_t>( resource ) ] ) / 1024.0;
      };
      ImGui::Text( "Presentation: %.1f KiB", chargeKiB( render::budget::Resource::Presentation ) );
      ImGui::Text( "Geometry: %.1f KiB", chargeKiB( render::budget::Resource::Geometry ) );
      ImGui::Text( "Texture: %.1f KiB", chargeKiB( render::budget::Resource::Texture ) );
      ImGui::Text( "Upload: %.1f KiB", chargeKiB( render::budget::Resource::Upload ) );
      ImGui::TreePop();
    }

    if ( ImGui::TreeNode( "Mii catalog" ) )
    {
#if defined( APOLLO_PLATFORM_WINDOWS )
      ImGui::TextUnformatted( "Nintendo defaults in the SDK Generic environment." );
#else
      ImGui::TextUnformatted( "Console database and Nintendo defaults." );
#endif
      if ( miiResources.IsReady() )
      {
        constexpr double MiB = 1024.0 * 1024.0;
        ImGui::Text( "SDK inputs: shape %.2f MiB, texture %.2f MiB, Resource object %u B",
                     static_cast<double>( miiResources.ShapeSize() ) / MiB,
                     static_cast<double>( miiResources.TextureSize() ) / MiB,
                     static_cast<unsigned int>( miiResources.ResourceObjectSize() ) );
      }
      else
      {
        ImGui::TextUnformatted( "SDK Mii resources unavailable; see diagnostics." );
      }

#if defined( APOLLO_PLATFORM_NX )
      ImGui::Text( "NVN Mii model: %s", nxMiiModelReady ? "initialized (draw pending)" : "unavailable" );
#else
      ( void )nxMiiModelReady;
#endif

      if ( !miiCatalog.IsAvailable() )
      {
        ImGui::TextUnformatted( "Database unavailable; see diagnostics." );
      }
      else
      {
        ImGui::Text( "%u valid Miis (opaque snapshots in memory)", static_cast<unsigned int>( miiCatalog.Count() ) );

        if ( ImGui::BeginChild( "Mii entries", ImVec2( 0, 180 ), ImGuiChildFlags_Borders ) )
        {
          for ( size_t i = 0; i < miiCatalog.Count(); ++i )
          {
            const mii::Entry * entry = miiCatalog.Get( i );
            ImGui::PushID( static_cast<int>( i ) );
            char label[ 72 ]{};
            std::snprintf( label,
                           sizeof( label ),
                           "#%03u  %s",
                           static_cast<unsigned int>( i + 1 ),
                           entry->name[ 0 ] ? entry->name.data() : "(Unnamed)" );
            if ( ImGui::Selectable( label, m_SelectedMii == static_cast<int>( i ) ) )
            {
              m_SelectedMii = static_cast<int>( i );
            }
            ImGui::PopID();
          }
        }
        ImGui::EndChild();

        if ( const mii::Entry * selected = miiCatalog.Get( static_cast<size_t>( m_SelectedMii ) ) )
        {
          ImGui::Text( "Source: %s | Height: %u | Build: %u | Snapshot: %u bytes",
                       selected->source == mii::Source::Database ? "Console" : "Default",
                       static_cast<unsigned int>( selected->height ),
                       static_cast<unsigned int>( selected->build ),
                       static_cast<unsigned int>( selected->snapshot.size() ) );
        }
      }
      ImGui::TreePop();
    }

    DrawMiiPreviewCamera( previewCamera );

#if defined( APOLLO_PLATFORM_NX )
    ImGui::TextUnformatted( "Touch or use D-pad, A and B." );
#endif
    ImGui::End();
    ImGui::Render();

    if ( !m_FirstFrameReported )
    {
      diagnostics::Write( ImGui::GetDrawData()->TotalVtxCount > 0 ? diagnostics::Level::Information
                                                                  : diagnostics::Level::Error,
                          ImGui::GetDrawData()->TotalVtxCount > 0 ? "Dear ImGui produced debug UI draw data."
                                                                  : "Dear ImGui produced no debug UI draw data." );
      m_FirstFrameReported = true;
    }
  }

  void DebugUi::DrawMiiPreviewCamera( mii::PreviewCamera & previewCamera ) noexcept
  {
    if ( !ImGui::TreeNode( "Mii preview camera" ) )
    {
      return;
    }

    ImGui::TextUnformatted( "Head draw pending." );

    if ( ImGui::Button( "Front" ) )
    {
      previewCamera.SetFront();
    }
    ImGui::SameLine();

    if ( ImGui::Button( "Three-quarter" ) )
    {
      previewCamera.SetThreeQuarter();
    }
    ImGui::SameLine();

    if ( ImGui::Button( "Profile" ) )
    {
      previewCamera.SetProfile();
    }
    ImGui::SameLine();

    if ( ImGui::Button( "Reset" ) )
    {
      previewCamera.SetFront();
    }

    ImGui::SliderFloat( "Yaw", &previewCamera.yawDegrees, -180.0f, 180.0f, "%.0f deg" );
    ImGui::SliderFloat( "Pitch", &previewCamera.pitchDegrees, -80.0f, 80.0f, "%.0f deg" );
    ImGui::SliderFloat( "Distance",
                        &previewCamera.distance,
                        mii::PreviewCamera::MinimumDistance,
                        mii::PreviewCamera::MaximumDistance,
                        "%.0f units" );
    previewCamera.Clamp();

    ImGui::TreePop();
  }

#if defined( APOLLO_PLATFORM_NX )
  void DebugUi::UpdateNxInput( render::Extent2D extent ) noexcept
  {
    ImGuiIO &              io = ImGui::GetIO();
    nn::hid::NpadButtonSet buttons{};
    m_ConnectedNpadCount = 0;

    for ( const nn::hid::NpadIdType id : SupportedNpadIds )
    {
      const nn::hid::NpadStyleSet style = nn::hid::GetNpadStyleSet( id );
      if ( style.IsAllOff() )
      {
        continue;
      }

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
      else
      {
        continue;
      }

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
    if ( m_ConnectedNpadCount > 0 )
    {
      io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    }
    else
    {
      io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
    }
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
      io.AddMousePosEvent( touch.touches[ 0 ].x * static_cast<float>( extent.width ) / 1280.0f,
                           touch.touches[ 0 ].y * static_cast<float>( extent.height ) / 720.0f );
    }
    io.AddMouseButtonEvent( 0, touch.count > 0 );
  }
#endif

  void DebugUi::Shutdown() noexcept
  {
    if ( !m_Ready )
    {
      return;
    }
#if defined( APOLLO_PLATFORM_WINDOWS )
    ImGui_ImplWin32_Shutdown();
#endif
    ImGui::DestroyContext();
    m_Ready              = false;
    m_FirstFrameReported = false;
  }
} // namespace apollo::debug
