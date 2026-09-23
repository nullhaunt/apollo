#pragma once

#if !defined( APOLLO_PLATFORM_WINDOWS )
  #error "VulkanPresenter is available only on Windows."
#endif

#include "Render/RenderTypes.hpp"
#include "Render/RenderBudget.hpp"
#include "Render/RenderTelemetry.hpp"
#include "Render/TextureData.hpp"

#include <memory>
#include <vulkan/vulkan.hpp>

namespace apollo::render::vulkan
{
  class VulkanContext;
  class VulkanSwapchain;

  class VulkanPresenter final
  {
  public:
    VulkanPresenter() noexcept = default;
    ~VulkanPresenter() noexcept;

    VulkanPresenter( const VulkanPresenter & )             = delete;
    VulkanPresenter & operator=( const VulkanPresenter & ) = delete;
    VulkanPresenter( VulkanPresenter && )                  = delete;
    VulkanPresenter & operator=( VulkanPresenter && )      = delete;

    [[nodiscard]] bool   Initialize( const VulkanContext & context, const VulkanSwapchain & swapchain ) noexcept;
    [[nodiscard]] Result PresentFrame( ClearColor color ) noexcept;
    void                 Shutdown() noexcept;

  private:
    [[nodiscard]] bool   CreateCommands() noexcept;
    [[nodiscard]] bool   CreateSynchronization() noexcept;
#if !defined( APOLLO_BUILD_RELEASE )
    void                 ReadGpuTimings() noexcept;
#endif
    [[nodiscard]] bool   CreateGeometry() noexcept;
    [[nodiscard]] bool   CreateTexture( Rgba8ImageView image, TextureSamplerDesc sampling ) noexcept;
    [[nodiscard]] bool   CreateHostBuffer( vk::DeviceSize size, vk::BufferUsageFlags usage, const void * data,
                                           vk::Buffer & buffer, vk::DeviceMemory & memory,
                                           telemetry::TrackedAllocation & tracked ) noexcept;
    [[nodiscard]] bool   CreatePipeline() noexcept;
    [[nodiscard]] bool   RecordFrame( u32 imageIndex, ClearColor color ) noexcept;
    [[nodiscard]] Result AcquireImage( u32 & imageIndex, bool & suboptimal ) noexcept;
    [[nodiscard]] Result SubmitFrame( u32 imageIndex ) noexcept;
    [[nodiscard]] Result PresentImage( u32 imageIndex ) noexcept;

    const VulkanContext *            m_Context{};
    const VulkanSwapchain *          m_Swapchain{};
    vk::CommandPool                  m_CommandPool{};
    vk::CommandBuffer                m_CommandBuffer{};
    vk::Semaphore                    m_ImageAvailable{};
    // A present wait may outlive the frame fence; reuse each semaphore with its image.
    std::unique_ptr<vk::Semaphore[]> m_RenderFinished{};
    u32                              m_RenderFinishedCount{};
    vk::Fence                        m_FrameFence{};
#if !defined( APOLLO_BUILD_RELEASE )
    vk::QueryPool                    m_TimestampQueries{};
    double                           m_TimestampPeriodNs{};
    u64                              m_TimestampMask{};
    bool                             m_TimestampPending{};
    bool                             m_FirstTimingReported{};
#endif
    vk::Buffer                       m_VertexBuffer{};
    vk::DeviceMemory                 m_VertexMemory{};
    telemetry::TrackedAllocation     m_VertexAllocation{};
    vk::Buffer                       m_IndexBuffer{};
    vk::DeviceMemory                 m_IndexMemory{};
    telemetry::TrackedAllocation     m_IndexAllocation{};
    budget::Reservation              m_GeometryBudget{};
    vk::Buffer                       m_TextureStagingBuffer{};
    vk::DeviceMemory                 m_TextureStagingMemory{};
    telemetry::TrackedAllocation     m_StagingAllocation{};
    budget::Reservation              m_UploadBudget{};
    vk::Image                        m_TextureImage{};
    vk::DeviceMemory                 m_TextureMemory{};
    telemetry::TrackedAllocation     m_TextureAllocation{};
    budget::Reservation              m_TextureBudget{};
    vk::ImageView                    m_TextureView{};
    vk::Sampler                      m_TextureSampler{};
    vk::DescriptorSetLayout          m_TextureSetLayout{};
    vk::DescriptorPool               m_DescriptorPool{};
    vk::DescriptorSet                m_TextureSet{};
    vk::ShaderModule                 m_VertexShader{};
    vk::ShaderModule                 m_FragmentShader{};
    vk::PipelineLayout               m_PipelineLayout{};
    vk::Pipeline                     m_Pipeline{};
    bool                             m_ImGuiReady{};
    u64                              m_PresentedFrames{};
  };
} // namespace apollo::render::vulkan
