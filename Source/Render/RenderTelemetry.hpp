#pragma once

#include "Core/Types.hpp"

#include <cstddef>

namespace apollo::render::telemetry
{
  // Only allocations made explicitly by Apollo are counted. Driver-owned
  // swapchain storage, ImGui allocations, and NVN runtime internals are not.
  struct MemorySnapshot
  {
    size_t currentBytes{};
    size_t peakBytes{};
    u32    currentAllocations{};
    u32    peakAllocations{};
  };

  struct GpuTimings
  {
    bool   valid{};
    double clearMs{};
    double quadMs{};
    double uiMs{};
    double totalMs{};
  };

  inline MemorySnapshot g_Memory{};
  inline GpuTimings     g_GpuTimings{};
  inline bool           g_GpuTimingAvailable{};

  class TrackedAllocation final
  {
  public:
    TrackedAllocation() noexcept = default;
    ~TrackedAllocation() noexcept { Release(); }
    TrackedAllocation( const TrackedAllocation & ) = delete;
    TrackedAllocation & operator=( const TrackedAllocation & ) = delete;

    void Acquire( size_t bytes ) noexcept
    {
#if !defined( APOLLO_BUILD_RELEASE )
      Release();
      if ( bytes == 0 ) return;
      m_Bytes = bytes;
      g_Memory.currentBytes += bytes;
      ++g_Memory.currentAllocations;
      if ( g_Memory.currentBytes > g_Memory.peakBytes ) g_Memory.peakBytes = g_Memory.currentBytes;
      if ( g_Memory.currentAllocations > g_Memory.peakAllocations )
        g_Memory.peakAllocations = g_Memory.currentAllocations;
#else
      ( void )bytes;
#endif
    }

    void Release() noexcept
    {
#if !defined( APOLLO_BUILD_RELEASE )
      if ( m_Bytes == 0 ) return;
      g_Memory.currentBytes -= m_Bytes;
      --g_Memory.currentAllocations;
      m_Bytes = 0;
#endif
    }

  private:
#if !defined( APOLLO_BUILD_RELEASE )
    size_t m_Bytes{};
#endif
  };

  [[nodiscard]] inline MemorySnapshot GetMemory() noexcept { return g_Memory; }
  [[nodiscard]] inline GpuTimings GetGpuTimings() noexcept { return g_GpuTimings; }
  [[nodiscard]] inline bool IsGpuTimingAvailable() noexcept { return g_GpuTimingAvailable; }
  inline void SetGpuTimings( GpuTimings timings ) noexcept { g_GpuTimings = timings; }
  inline void SetGpuTimingAvailable( bool available ) noexcept { g_GpuTimingAvailable = available; }
  inline void InvalidateGpuTimings() noexcept { g_GpuTimings = {}; }
} // namespace apollo::render::telemetry
