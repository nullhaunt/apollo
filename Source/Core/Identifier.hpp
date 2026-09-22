#pragma once

#include "Types.hpp"

namespace apollo
{
  template <typename Tag, typename Storage> class StrongId
  {
  public:
    using StorageType = Storage;

    constexpr StrongId() noexcept = default;
    explicit constexpr StrongId( Storage value ) noexcept
      : m_Value( value )
    {
    }

    [[nodiscard]] constexpr Storage GetValue() const noexcept
    {
      return m_Value;
    }

    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
      return m_Value != Storage{};
    }

    friend constexpr bool operator==( StrongId lhs, StrongId rhs ) noexcept
    {
      return lhs.m_Value == rhs.m_Value;
    }

    friend constexpr bool operator!=( StrongId lhs, StrongId rhs ) noexcept
    {
      return !( lhs == rhs );
    }

  private:
    Storage m_Value{};
  };

  struct AssetIdTag;
  using AssetId = StrongId<AssetIdTag, u64>;

  class EntityGuid
  {
  public:
    constexpr EntityGuid() noexcept = default;
    constexpr EntityGuid( u64 high, u64 low ) noexcept
      : m_High( high )
      , m_Low( low )
    {
    }

    [[nodiscard]] constexpr u64 GetHigh() const noexcept
    {
      return m_High;
    }

    [[nodiscard]] constexpr u64 GetLow() const noexcept
    {
      return m_Low;
    }

    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
      return m_High != 0 || m_Low != 0;
    }

    friend constexpr bool operator==( EntityGuid lhs, EntityGuid rhs ) noexcept
    {
      return lhs.m_High == rhs.m_High && lhs.m_Low == rhs.m_Low;
    }

    friend constexpr bool operator!=( EntityGuid lhs, EntityGuid rhs ) noexcept
    {
      return !( lhs == rhs );
    }

  private:
    u64 m_High{};
    u64 m_Low{};
  };
} // namespace apollo