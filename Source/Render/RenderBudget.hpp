#pragma once

#include "Core/Types.hpp"

#include <cstddef>
#include <limits>

namespace apollo::render::budget
{
  // Logical resource sizes, independent of Vulkan allocation requirements or
  // NVN pool padding. This is a pressure model, not physical memory usage.
  enum class Resource : u8
  {
    Presentation,
    Geometry,
    Texture,
    Upload,
    Count
  };

  struct Profile
  {
    size_t softBytes{};
    size_t hardBytes{};
  };

  struct Snapshot
  {
    Profile profile{};
    size_t currentBytes{};
    size_t peakBytes{};
    size_t currentByResource[ static_cast<size_t>( Resource::Count ) ]{};
    u32    deniedRequests{};
    bool   softWarningIssued{};
    u64    applicationAvailableBytes{};
  };

  [[nodiscard]] bool Configure( Profile profile ) noexcept;
  [[nodiscard]] Snapshot GetSnapshot() noexcept;
  void SetApplicationAvailableBytes( u64 bytes ) noexcept;

  [[nodiscard]] constexpr bool Rgba8Footprint( u32 width, u32 height, u32 surfaces,
                                                size_t & bytes ) noexcept
  {
    if ( width == 0 || height == 0 || surfaces == 0 ||
         static_cast<size_t>( width ) > std::numeric_limits<size_t>::max() / 4 / height / surfaces )
      return false;
    bytes = static_cast<size_t>( width ) * height * surfaces * 4;
    return true;
  }

  class Reservation final
  {
  public:
    Reservation() noexcept = default;
    ~Reservation() noexcept { Release(); }
    Reservation( const Reservation & ) = delete;
    Reservation & operator=( const Reservation & ) = delete;

    [[nodiscard]] bool Acquire( Resource resource, size_t bytes ) noexcept;
    void Release() noexcept;

  private:
    Resource m_Resource{ Resource::Count };
    size_t   m_Bytes{};
  };
} // namespace apollo::render::budget
