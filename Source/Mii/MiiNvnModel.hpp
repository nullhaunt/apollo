#pragma once

#if !defined( APOLLO_PLATFORM_NX )
  #error "MiiNvnModel is available only on NX64."
#endif

#include "Mii/MiiCatalog.hpp"
#include "Mii/MiiResources.hpp"
#include "Render/Nvn/NvnContext.hpp"
#include "Render/RenderTelemetry.hpp"

#include <nn/gfx.h>
#include <nn/mii.h>

#include <cstddef>

namespace apollo::mii
{
  // Owns one generated model and the gfx allocations it needs. The gfx device
  // only borrows the NVN device; NvnContext retains ownership of that device.
  class NvnModel final
  {
  public:
    NvnModel() noexcept = default;
    ~NvnModel() noexcept;
    NvnModel( const NvnModel & )             = delete;
    NvnModel & operator=( const NvnModel & ) = delete;

    [[nodiscard]] bool Initialize( render::nvn::NvnContext & context,
                                   const ResourceFiles &     files,
                                   const Entry &             entry ) noexcept;
    void               Shutdown() noexcept;
    [[nodiscard]] bool IsReady() const noexcept
    {
      return m_Model.IsInitialized();
    }
    [[nodiscard]] bool AreFaceSourcesReady() const noexcept
    {
      return m_Faceline.IsInitialized() && m_Mask.IsInitialized();
    }

  private:
    void               InitializeGfx( render::nvn::NvnContext & context ) noexcept;
    [[nodiscard]] bool InitializeResource( const ResourceFiles & files ) noexcept;
    [[nodiscard]] bool InitializeModel( const Entry & entry ) noexcept;
    [[nodiscard]] bool InitializeFaceSources( const Entry & entry ) noexcept;

    nn::gfx::Device                      m_GfxDevice{};
    nn::gfx::MemoryPool                  m_ModelPool{};
    nn::mii::Resource                    m_Resource{};
    nn::mii::CharModel                   m_Model{};
    nn::gfx::MemoryPool                  m_FaceSourcePool{};
    nn::mii::Faceline                    m_Faceline{};
    nn::mii::Mask                        m_Mask{};
    void *                               m_ResourceMemory{};
    void *                               m_ModelMemory{};
    void *                               m_PoolMemory{};
    void *                               m_FaceSourcePoolMemory{};
    void *                               m_FacelineMemory{};
    void *                               m_MaskMemory{};
    render::telemetry::TrackedAllocation m_PoolAllocation{};
    render::telemetry::TrackedAllocation m_FaceSourcePoolAllocation{};
    bool                                 m_GfxReady{};
    bool                                 m_PoolReady{};
    bool                                 m_FaceSourcePoolReady{};
  };
} // namespace apollo::mii
