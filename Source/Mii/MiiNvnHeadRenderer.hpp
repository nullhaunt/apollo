#pragma once

#if !defined( APOLLO_PLATFORM_NX )
  #error "MiiNvnHeadRenderer is available only on NX64."
#endif

#include "Mii/MiiNvnFaceRenderer.hpp"
#include "Mii/PreviewCamera.hpp"
#include "Mii/PreviewExpression.hpp"
#include "Render/RenderTelemetry.hpp"
#include "Render/RenderTypes.hpp"

#include <nn/gfx.h>
#include <nn/mii.h>
#include <nvn/nvn_Cpp.h>

#include <cstddef>

namespace apollo::mii
{
  class NvnHeadRenderer final
  {
  public:
    NvnHeadRenderer() noexcept = default;
    ~NvnHeadRenderer() noexcept;
    NvnHeadRenderer( const NvnHeadRenderer & )             = delete;
    NvnHeadRenderer & operator=( const NvnHeadRenderer & ) = delete;

    [[nodiscard]] bool Initialize( nn::gfx::Device &    device,
                                   ::nvn::Queue &       queue,
                                   nn::mii::CharModel & model,
                                   NvnFaceRenderer &    faces ) noexcept;
    [[nodiscard]] bool RecordDraw( ::nvn::CommandBuffer & commands,
                                   ::nvn::Texture &       target,
                                   render::Extent2D       extent,
                                   int                    backbuffer,
                                   const PreviewCamera &  camera,
                                   PreviewExpression      expression ) noexcept;
    void               ResetTargets() noexcept;
    void               Shutdown() noexcept;
    [[nodiscard]] bool IsReady() const noexcept
    {
      return m_Ready;
    }

  private:
    static constexpr int BackbufferCount = 2;
    static constexpr int DrawTypeCount   = nn::mii::CharModel::DrawType_End;

    [[nodiscard]] bool LoadShader() noexcept;
    [[nodiscard]] bool CreateStates() noexcept;
    [[nodiscard]] bool CreateUniforms() noexcept;
    [[nodiscard]] bool CreateTarget( ::nvn::Texture & target, render::Extent2D extent, int backbuffer ) noexcept;
    [[nodiscard]] bool CreateDepth( render::Extent2D extent ) noexcept;
    [[nodiscard]] bool CreateViewport( render::Extent2D extent ) noexcept;
    void DrawParts( nn::gfx::CommandBuffer & commands, int backbuffer, PreviewExpression expression ) noexcept;
    void DrawPart( nn::gfx::CommandBuffer &   commands,
                   const nn::mii::DrawParam & part,
                   int                        drawType,
                   int                        backbuffer ) noexcept;
    void UpdateCamera( int backbuffer, render::Extent2D extent, const PreviewCamera & camera ) noexcept;

    nn::gfx::Device *        m_Device{};
    ::nvn::Queue *           m_NvnQueue{};
    nn::mii::CharModel *     m_Model{};
    NvnFaceRenderer *        m_Faces{};
    nn::gfx::ResShaderFile * m_ShaderFile{};
    nn::gfx::ShaderCodeType  m_ShaderCodeType{};
    nn::gfx::Shader *        m_Shader{};
    void *                   m_ShaderMemory{};
    bool                     m_ShaderContainerReady{};
    bool                     m_ShaderProgramReady{};
    int                      m_CameraSlot{ -1 };
    int                      m_MaterialSlot{ -1 };
    int                      m_TextureSlot{ -1 };

    nn::gfx::RasterizerState   m_Rasterizer[ nn::gfx::CullMode_End ]{};
    nn::gfx::BlendState        m_Blend[ 2 ]{};
    nn::gfx::DepthStencilState m_DepthState[ 2 ]{};
    nn::gfx::VertexState       m_VertexState{};
    void *                     m_BlendMemory[ 2 ]{};
    void *                     m_VertexStateMemory{};
    bool                       m_StatesReady{};

    nn::gfx::MemoryPool                  m_UniformPool{};
    nn::gfx::Buffer                      m_Cameras[ BackbufferCount ]{};
    nn::gfx::Buffer                      m_Materials[ DrawTypeCount ]{};
    void *                               m_UniformMemory{};
    render::telemetry::TrackedAllocation m_UniformAllocation{};
    bool                                 m_UniformPoolReady{};
    int                                  m_CamerasReady{};
    int                                  m_MaterialsReady{};

    nn::gfx::MemoryPool                  m_DepthPool{};
    nn::gfx::Texture                     m_DepthTexture{};
    nn::gfx::DepthStencilView            m_DepthView{};
    void *                               m_DepthMemory{};
    render::telemetry::TrackedAllocation m_DepthAllocation{};
    bool                                 m_DepthPoolReady{};
    bool                                 m_DepthTextureReady{};
    bool                                 m_DepthViewReady{};
    nn::gfx::Texture *                   m_Targets[ BackbufferCount ]{};
    nn::gfx::ColorTargetView             m_ColorViews[ BackbufferCount ]{};
    bool                                 m_ColorViewsReady[ BackbufferCount ]{};
    nn::gfx::ViewportScissorState        m_Viewport{};
    bool                                 m_ViewportReady{};
    render::Extent2D                     m_Extent{};
    bool                                 m_Ready{};
    bool                                 m_FirstDrawReported{};
  };
} // namespace apollo::mii
