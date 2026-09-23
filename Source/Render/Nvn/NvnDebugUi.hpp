#pragma once

#if !defined( APOLLO_PLATFORM_NX )
  #error "NvnDebugUi is available only on NX64."
#endif

#include "Render/Nvn/NvnContext.hpp"
#include "Render/RenderTypes.hpp"

struct ImDrawData;

namespace apollo::render::nvn
{
  class NvnDebugUi final
  {
  public:
    NvnDebugUi() noexcept = default;
    ~NvnDebugUi() noexcept;
    NvnDebugUi( const NvnDebugUi & ) = delete;
    NvnDebugUi & operator=( const NvnDebugUi & ) = delete;

    [[nodiscard]] bool Initialize( NvnContext & context ) noexcept;
    [[nodiscard]] bool Upload( const ImDrawData * data, int backbuffer, Extent2D extent ) noexcept;
    void RecordDraw( ::nvn::CommandBuffer & commands, const ImDrawData * data,
                     int backbuffer, Extent2D extent ) const noexcept;
    void Shutdown() noexcept;

  private:
    [[nodiscard]] bool CreateProgram() noexcept;
    [[nodiscard]] bool CreateFontTexture() noexcept;
    [[nodiscard]] bool CreateGeometry() noexcept;
    void BindState( ::nvn::CommandBuffer & commands, int backbuffer, Extent2D extent ) const noexcept;

    static constexpr int BackbufferCount = 2;
    static constexpr size_t VertexBytesPerBuffer = 1024 * 1024;
    static constexpr size_t IndexBytesPerBuffer = 256 * 1024;

    ::nvn::Device * m_Device{};
    ::nvn::MemoryPool m_ShaderPool{};
    void * m_ShaderMemory{};
    bool m_ShaderPoolReady{};
    ::nvn::Program m_Program{};
    bool m_ProgramReady{};
    int m_TextureBinding{ -1 };
    int m_SamplerBinding{ -1 };

    ::nvn::MemoryPool m_FontPool{};
    void * m_FontMemory{};
    bool m_FontPoolReady{};
    ::nvn::Texture m_FontTexture{};
    bool m_FontTextureReady{};
    ::nvn::TexturePool m_TextureDescriptors{};
    bool m_TextureDescriptorsReady{};
    ::nvn::Sampler m_Sampler{};
    bool m_SamplerReady{};
    ::nvn::SamplerPool m_SamplerDescriptors{};
    bool m_SamplerDescriptorsReady{};
    ::nvn::SeparateTextureHandle m_TextureHandle{};
    ::nvn::SeparateSamplerHandle m_SamplerHandle{};

    ::nvn::MemoryPool m_GeometryPool{};
    void * m_GeometryMemory{};
    bool m_GeometryPoolReady{};
    ::nvn::Buffer m_Vertices[ BackbufferCount ]{};
    ::nvn::Buffer m_Indices[ BackbufferCount ]{};
    bool m_VerticesReady[ BackbufferCount ]{};
    bool m_IndicesReady[ BackbufferCount ]{};
    bool m_Ready{};
  };
} // namespace apollo::render::nvn
