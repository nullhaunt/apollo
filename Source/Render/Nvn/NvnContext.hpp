#pragma once

#if !defined( APOLLO_PLATFORM_NX )
  #error "NvnContext is available only on NX64."
#endif

#include <nvn/nvn_Cpp.h>
#include <nvn/nvn_CppMethods.h>

namespace apollo::render::nvn
{
  // Owns the NVN device and its graphics queue. Presentation is a separate step.
  class NvnContext final
  {
  public:
    NvnContext() noexcept = default;
    ~NvnContext() noexcept;

    NvnContext( const NvnContext & )             = delete;
    NvnContext & operator=( const NvnContext & ) = delete;
    NvnContext( NvnContext && )                  = delete;
    NvnContext & operator=( NvnContext && )      = delete;

    [[nodiscard]] bool Initialize() noexcept;
    void               Shutdown() noexcept;

    [[nodiscard]] ::nvn::Device * GetDevice() noexcept;
    [[nodiscard]] ::nvn::Queue *  GetQueue() noexcept;

  private:
    ::nvn::Device m_Device{};
    ::nvn::Queue  m_Queue{};
    void *        m_QueueMemory{};
    bool          m_DeviceReady{};
    bool          m_QueueReady{};
  };
} // namespace apollo::render::nvn
