#pragma once

#if !defined( APOLLO_PLATFORM_NX )
  #error "MiiNvnFaceRenderer is available only on NX64."
#endif

#include "Render/RenderTelemetry.hpp"

#include <nn/gfx.h>
#include <nn/mii.h>

#include <cstddef>

namespace apollo::mii
{
  // Generates the model's faceline and normal mask once before the first frame.
  // The device and Mii objects are borrowed from NvnModel.
  class NvnFaceRenderer final
  {
  public:
    NvnFaceRenderer() noexcept = default;
    ~NvnFaceRenderer() noexcept;
    NvnFaceRenderer( const NvnFaceRenderer & )             = delete;
    NvnFaceRenderer & operator=( const NvnFaceRenderer & ) = delete;

    [[nodiscard]] bool Initialize( nn::gfx::Device &    device,
                                   nn::mii::CharModel & model,
                                   nn::mii::Faceline &  faceline,
                                   nn::mii::Mask &      mask ) noexcept;
    void               Shutdown() noexcept;
    [[nodiscard]] bool IsReady() const noexcept
    {
      return m_TexturesReady;
    }
    [[nodiscard]] nn::gfx::DescriptorPool * TextureDescriptors() noexcept
    {
      return &m_TextureDescriptors;
    }
    [[nodiscard]] nn::gfx::DescriptorPool * SamplerDescriptors() noexcept
    {
      return &m_SamplerDescriptors;
    }
    [[nodiscard]] const nn::gfx::DescriptorSlot & SamplerSlot() const noexcept
    {
      return m_SamplerSlot;
    }

  private:
    [[nodiscard]] bool LoadShaderFile() noexcept;
    [[nodiscard]] bool InitializeTextureShader() noexcept;
    [[nodiscard]] bool InitializeGpuBuffers() noexcept;
    [[nodiscard]] bool InitializeDescriptors( nn::mii::CharModel & model,
                                              nn::mii::Faceline &  faceline,
                                              nn::mii::Mask &      mask ) noexcept;
    void               BindTextureDescriptors( nn::mii::CharModel & model,
                                               nn::mii::Faceline &  faceline,
                                               nn::mii::Mask &      mask,
                                               int                  firstSlot ) noexcept;
    [[nodiscard]] bool InitializeCommands() noexcept;
    void               DrawTextures( nn::mii::CharModel &      model,
                                     const nn::mii::Faceline & faceline,
                                     const nn::mii::Mask &     mask ) noexcept;
    void               BeginCommands() noexcept;
    void               SubmitCommands() noexcept;

    nn::gfx::Device *                    m_Device{};
    nn::gfx::ResShaderFile *             m_ShaderFile{};
    nn::gfx::ShaderCodeType              m_ShaderCodeType{};
    nn::mii::TextureShaderInfo           m_ShaderInfo{};
    nn::mii::TextureShader               m_TextureShader{};
    nn::mii::FacelineGpuBuffer           m_FacelineBuffer{};
    nn::mii::MaskGpuBuffer               m_MaskBuffer{};
    nn::gfx::MemoryPool                  m_GpuPool{};
    nn::gfx::MemoryPool                  m_DescriptorMemoryPool{};
    nn::gfx::DescriptorPool              m_TextureDescriptors{};
    nn::gfx::DescriptorPool              m_SamplerDescriptors{};
    nn::gfx::Sampler                     m_Sampler{};
    nn::gfx::DescriptorSlot              m_SamplerSlot{};
    nn::gfx::CommandBuffer               m_Commands{};
    nn::gfx::Queue                       m_Queue{};
    void *                               m_RomCache{};
    void *                               m_ShaderMemory{};
    void *                               m_TextureShaderMemory{};
    void *                               m_FacelineBufferMemory{};
    void *                               m_MaskBufferMemory{};
    void *                               m_GpuPoolMemory{};
    void *                               m_DescriptorPoolMemory{};
    void *                               m_CommandControlMemory{};
    size_t                               m_CommandPoolOffset{};
    size_t                               m_FacelinePoolOffset{};
    size_t                               m_MaskPoolOffset{};
    render::telemetry::TrackedAllocation m_GpuAllocation{};
    render::telemetry::TrackedAllocation m_DescriptorAllocation{};
    int                                  m_InitializedPrograms{};
    bool                                 m_RomMounted{};
    bool                                 m_ShaderContainerReady{};
    bool                                 m_GpuPoolReady{};
    bool                                 m_DescriptorPoolReady{};
    bool                                 m_TextureDescriptorsReady{};
    bool                                 m_SamplerDescriptorsReady{};
    bool                                 m_SamplerReady{};
    bool                                 m_CommandsReady{};
    bool                                 m_QueueReady{};
    bool                                 m_TexturesReady{};
  };
} // namespace apollo::mii
