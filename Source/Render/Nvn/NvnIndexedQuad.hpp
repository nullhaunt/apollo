#pragma once

#if !defined( APOLLO_PLATFORM_NX )
  #error "NvnIndexedQuad is available only on NX64."
#endif

#include "Render/Nvn/NvnContext.hpp"
#include "Render/RenderBudget.hpp"
#include "Render/RenderTelemetry.hpp"
#include "Render/RenderTypes.hpp"
#include "Render/TextureData.hpp"

namespace apollo::render::nvn
{
  class NvnIndexedQuad final
  {
  public:
    NvnIndexedQuad() noexcept = default;
    ~NvnIndexedQuad() noexcept;

    NvnIndexedQuad( const NvnIndexedQuad & )             = delete;
    NvnIndexedQuad & operator=( const NvnIndexedQuad & ) = delete;

    [[nodiscard]] bool Initialize( NvnContext & context ) noexcept;
    void               RecordDraw( ::nvn::CommandBuffer & commands, Extent2D extent ) const noexcept;
    void               Shutdown() noexcept;

  private:
    [[nodiscard]] bool CreateProgram() noexcept;
    [[nodiscard]] bool CreateGeometry() noexcept;
    [[nodiscard]] bool CreateTexture( Rgba8ImageView image, TextureSamplerDesc sampling ) noexcept;

    ::nvn::Device * m_Device{};

    ::nvn::MemoryPool            m_ShaderPool{};
    void *                       m_ShaderMemory{};
    telemetry::TrackedAllocation m_ShaderAllocation{};
    bool                         m_ShaderPoolReady{};
    ::nvn::Program               m_Program{};
    bool                         m_ProgramReady{};

    ::nvn::MemoryPool            m_GeometryPool{};
    void *                       m_GeometryMemory{};
    telemetry::TrackedAllocation m_GeometryAllocation{};
    bool                         m_GeometryPoolReady{};
    budget::Reservation          m_GeometryBudget{};
    ::nvn::Buffer                m_VertexBuffer{};
    ::nvn::Buffer                m_IndexBuffer{};
    bool                         m_VertexReady{};
    bool                         m_IndexReady{};

    ::nvn::MemoryPool            m_TextureMemoryPool{};
    void *                       m_TextureMemory{};
    telemetry::TrackedAllocation m_TextureAllocation{};
    bool                         m_TextureMemoryPoolReady{};
    budget::Reservation          m_TextureBudget{};
    ::nvn::Texture               m_Texture{};
    bool                         m_TextureReady{};
    ::nvn::TexturePool           m_TexturePool{};
    bool                         m_TexturePoolReady{};
    ::nvn::Sampler               m_Sampler{};
    bool                         m_SamplerReady{};
    ::nvn::SamplerPool           m_SamplerPool{};
    bool                         m_SamplerPoolReady{};
    ::nvn::SeparateTextureHandle m_TextureHandle{};
    ::nvn::SeparateSamplerHandle m_SamplerHandle{};
    int                          m_TextureBinding{ -1 };
    int                          m_SamplerBinding{ -1 };
    bool                         m_Ready{};
  };
} // namespace apollo::render::nvn
