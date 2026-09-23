#pragma once

#if !defined( APOLLO_PLATFORM_NX )
  #error "NvnPresenter is available only on NX64."
#endif

#include "Render/Nvn/NvnContext.hpp"
#include "Render/Nvn/NvnIndexedQuad.hpp"
#if !defined( APOLLO_BUILD_RELEASE )
  #include "Render/Nvn/NvnDebugUi.hpp"
#endif
#include "Render/RenderBudget.hpp"
#include "Render/RenderTelemetry.hpp"
#include "Render/RenderTypes.hpp"

#include <nn/vi.h>

namespace apollo::render::nvn
{
  class NvnPresenter final
  {
  public:
    NvnPresenter() noexcept = default;
    ~NvnPresenter() noexcept;

    NvnPresenter( const NvnPresenter & )             = delete;
    NvnPresenter & operator=( const NvnPresenter & ) = delete;
    NvnPresenter( NvnPresenter && )                  = delete;
    NvnPresenter & operator=( NvnPresenter && )      = delete;

    [[nodiscard]] bool   Initialize( NvnContext & context, Extent2D extent ) noexcept;
    [[nodiscard]] bool   Resize( Extent2D extent ) noexcept;
    [[nodiscard]] Result PresentFrame() noexcept;
    void                 Shutdown() noexcept;

    [[nodiscard]] Extent2D GetExtent() const noexcept;

  private:
    static constexpr int BackbufferCount = 2;

    [[nodiscard]] bool CreateImages( Extent2D extent ) noexcept;
#if !defined( APOLLO_BUILD_RELEASE )
    [[nodiscard]] bool CreateGpuCounters() noexcept;
    void               ReadGpuCounters( int backbuffer ) noexcept;
#endif
    void DestroyImages() noexcept;

    ::nvn::Device * m_Device{};
    ::nvn::Queue *  m_Queue{};
    NvnIndexedQuad  m_IndexedQuad{};
#if !defined( APOLLO_BUILD_RELEASE )
    NvnDebugUi m_DebugUiRenderer{};
#endif

    nn::vi::Display *          m_Display{};
    nn::vi::Layer *            m_Layer{};
    nn::vi::NativeWindowHandle m_NativeWindow{};
    bool                       m_ViReady{};

    ::nvn::Window m_Window{};
    bool          m_WindowReady{};
    ::nvn::Sync   m_DisplayReleaseSync{};
    bool          m_SyncReady{};

    ::nvn::MemoryPool            m_TexturePool{};
    budget::Reservation          m_PresentationBudget{};
    void *                       m_TextureMemory{};
    telemetry::TrackedAllocation m_TextureAllocation{};
    bool                         m_TexturePoolReady{};
    ::nvn::Texture               m_Textures[ BackbufferCount ]{};
    bool                         m_TextureReady[ BackbufferCount ]{};

    ::nvn::MemoryPool            m_CommandPool{};
    void *                       m_CommandMemory{};
    telemetry::TrackedAllocation m_CommandAllocation{};
    bool                         m_CommandPoolReady{};
    ::nvn::CommandBuffer         m_FrameBuffers[ BackbufferCount ]{};
    ::nvn::CommandHandle         m_FrameCommands[ BackbufferCount ]{};
    void *                       m_ControlMemory[ BackbufferCount ]{};
    telemetry::TrackedAllocation m_ControlAllocations[ BackbufferCount ]{};
    bool                         m_FrameBufferReady[ BackbufferCount ]{};

#if !defined( APOLLO_BUILD_RELEASE )
    ::nvn::MemoryPool            m_CounterPool{};
    void *                       m_CounterMemory{};
    telemetry::TrackedAllocation m_CounterAllocation{};
    ::nvn::CounterData *         m_CounterReports{};
    size_t                       m_CounterStride{};
    bool                         m_CounterPoolReady{};
    bool                         m_CounterPending[ BackbufferCount ]{};
    bool                         m_FirstTimingReported{};
#endif

    Extent2D m_Extent{};
    bool     m_Ready{};
  };
} // namespace apollo::render::nvn
