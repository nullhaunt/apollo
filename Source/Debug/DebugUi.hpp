#pragma once

#include "Mii/MiiCatalog.hpp"
#include "Mii/MiiResources.hpp"
#include "Mii/PreviewCamera.hpp"
#include "Mii/PreviewExpression.hpp"
#include "Render/RenderTypes.hpp"

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

    DebugUi( const DebugUi & )             = delete;
    DebugUi & operator=( const DebugUi & ) = delete;

#if defined( APOLLO_PLATFORM_WINDOWS )
    [[nodiscard]] bool Initialize( HWND window ) noexcept;
#else
    [[nodiscard]] bool Initialize() noexcept;
#endif
    void              BeginFrame( render::Extent2D           extent,
                                  const mii::Catalog &       miiCatalog,
                                  const mii::ResourceFiles & miiResources,
                                  bool                       nxMiiModelReady,
                                  bool                       nxFaceSourcesReady,
                                  bool                       nxFaceTexturesReady,
                                  bool                       nxHeadRendererReady,
                                  mii::PreviewCamera &       previewCamera,
                                  mii::PreviewExpression &   previewExpression ) noexcept;
    void              Shutdown() noexcept;
    [[nodiscard]] int ConsumeMiiSelection() noexcept;
    void              SetSelectedMii( int index ) noexcept;

  private:
    void DrawDiagnosticsWindow( render::Extent2D extent ) noexcept;
    void DrawMiiWindow( render::Extent2D           extent,
                        const mii::Catalog &       miiCatalog,
                        const mii::ResourceFiles & miiResources,
                        bool                       nxMiiModelReady,
                        bool                       nxFaceSourcesReady,
                        bool                       nxFaceTexturesReady,
                        bool                       nxHeadRendererReady,
                        mii::PreviewCamera &       previewCamera,
                        mii::PreviewExpression &   previewExpression ) noexcept;
    void DrawMiiCatalog( const mii::Catalog & miiCatalog ) noexcept;
    void DrawMiiPreviewCamera( mii::PreviewCamera & previewCamera, bool headRendererReady ) noexcept;
    void DrawMiiPreviewExpression( mii::PreviewExpression & previewExpression, bool headRendererReady ) noexcept;
#if defined( APOLLO_PLATFORM_NX )
    void         UpdateNxInput( render::Extent2D extent ) noexcept;
    unsigned int m_ConnectedNpadCount{};
#endif
    std::chrono::steady_clock::time_point m_PreviousFrame{};
    render::Extent2D                      m_PreviousExtent{};
    bool                                  m_Ready{};
    bool                                  m_FirstFrameReported{};
    bool                                  m_ResetLayout{};
    int                                   m_SelectedMii{};
    int                                   m_PendingMiiSelection{ -1 };
  };
} // namespace apollo::debug
