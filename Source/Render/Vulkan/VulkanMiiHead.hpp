#pragma once

#if !defined( APOLLO_PLATFORM_WINDOWS )
  #error "VulkanMiiHead is available only on Windows."
#endif

#include "Mii/MiiPreviewPackage.hpp"
#include "Mii/PreviewCamera.hpp"
#include "Render/RenderBudget.hpp"
#include "Render/RenderTelemetry.hpp"

#include <vulkan/vulkan.hpp>

namespace apollo::render::vulkan
{
  class VulkanContext;

  class VulkanMiiHead final
  {
  public:
    VulkanMiiHead() noexcept = default;
    ~VulkanMiiHead() noexcept;

    VulkanMiiHead( const VulkanMiiHead & )             = delete;
    VulkanMiiHead & operator=( const VulkanMiiHead & ) = delete;

    [[nodiscard]] bool Initialize( const VulkanContext &       context,
                                   vk::RenderPass              renderPass,
                                   vk::CommandBuffer           uploadCommands,
                                   const mii::PreviewPackage & package ) noexcept;
    void Draw( vk::CommandBuffer commands, vk::Extent2D extent, const mii::PreviewCamera & camera ) const noexcept;
    void Shutdown() noexcept;
    [[nodiscard]] bool IsReady() const noexcept;

  private:
    struct Texture
    {
      vk::Image                    image{};
      vk::DeviceMemory             memory{};
      vk::ImageView                view{};
      vk::DescriptorSet            set{};
      telemetry::TrackedAllocation allocation{};
    };

    struct Part
    {
      std::uint32_t indexCount{};
      std::uint32_t firstIndex{};
      std::int32_t  vertexOffset{};
      std::uint32_t drawType{};
      std::uint32_t cullMode{};
      std::uint32_t textureType{ 0xffffffffu };
      int           modulateType{};
      float         colors[ 3 ][ 4 ]{};
    };

    [[nodiscard]] bool CreateHostBuffer( size_t                         size,
                                         vk::BufferUsageFlags           usage,
                                         const void *                   data,
                                         vk::Buffer &                   buffer,
                                         vk::DeviceMemory &             memory,
                                         telemetry::TrackedAllocation & tracked ) noexcept;
    [[nodiscard]] bool CreateGeometry( const mii::PreviewPackage & package ) noexcept;
    [[nodiscard]] bool CreateTextures( const mii::PreviewPackage & package, vk::CommandBuffer commands ) noexcept;
    [[nodiscard]] bool CreateTextureImage( const mii::PreviewPackage::Texture & source, Texture & image ) noexcept;
    [[nodiscard]] bool UploadTextures( const mii::PreviewPackage & package,
                                       const size_t *              offsets,
                                       vk::CommandBuffer           commands ) noexcept;
    [[nodiscard]] bool CreateDescriptors( const mii::PreviewPackage & package ) noexcept;
    [[nodiscard]] bool CreatePipelines( vk::RenderPass renderPass ) noexcept;

    const VulkanContext *        m_Context{};
    vk::Buffer                   m_VertexBuffer{};
    vk::DeviceMemory             m_VertexMemory{};
    vk::Buffer                   m_IndexBuffer{};
    vk::DeviceMemory             m_IndexMemory{};
    vk::Buffer                   m_StagingBuffer{};
    vk::DeviceMemory             m_StagingMemory{};
    telemetry::TrackedAllocation m_VertexAllocation{};
    telemetry::TrackedAllocation m_IndexAllocation{};
    telemetry::TrackedAllocation m_StagingAllocation{};
    budget::Reservation          m_GeometryBudget{};
    budget::Reservation          m_TextureBudget{};
    budget::Reservation          m_UploadBudget{};

    Texture                 m_Textures[ 5 ]{};
    vk::Sampler             m_Sampler{};
    vk::DescriptorSetLayout m_SetLayout{};
    vk::DescriptorPool      m_DescriptorPool{};
    vk::ShaderModule        m_VertexShader{};
    vk::ShaderModule        m_FragmentShader{};
    vk::PipelineLayout      m_PipelineLayout{};
    vk::Pipeline            m_Pipelines[ 2 ][ 3 ]{};
    Part                    m_Parts[ 9 ]{};
    std::uint32_t           m_PartCount{};
    bool                    m_Ready{};
  };
} // namespace apollo::render::vulkan
