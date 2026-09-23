#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace apollo::mii
{
  inline constexpr size_t CharInfoBytes   = 88;
  inline constexpr size_t CatalogCapacity = 106;

  enum class Source : std::uint8_t
  {
    Database,
    Default
  };

  // CharInfo is opaque. Keep its bytes intact for later validation by nn::mii.
  struct Entry
  {
    std::array<std::uint8_t, CharInfoBytes> snapshot{};
    std::array<char, 48>                    name{};
    Source                                  source{};
    std::uint8_t                            height{};
    std::uint8_t                            build{};
  };

  class Catalog final
  {
  public:
    [[nodiscard]] bool          Load() noexcept;
    [[nodiscard]] const Entry * Get( size_t index ) const noexcept;
    [[nodiscard]] size_t        Count() const noexcept
    {
      return m_Count;
    }
    [[nodiscard]] bool IsAvailable() const noexcept
    {
      return m_Available;
    }

  private:
    std::array<Entry, CatalogCapacity> m_Entries{};
    size_t                             m_Count{};
    bool                               m_Available{};
  };
} // namespace apollo::mii
