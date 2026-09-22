#pragma once

#include "Core/Types.hpp"

namespace apollo
{
  struct WorldCell
  {
    s32 x{};
    s32 y{};
    s32 z{};

    friend constexpr bool operator==( WorldCell lhs, WorldCell rhs ) noexcept
    {
      return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
    }
  };

  struct LocalPosition
  {
    f32 x{};
    f32 y{};
    f32 z{};

    friend constexpr bool operator==( LocalPosition lhs, LocalPosition rhs ) noexcept
    {
      return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
    }
  };

  struct WorldPosition
  {
    WorldCell     cell{};
    LocalPosition local{};

    friend constexpr bool operator==( const WorldPosition & lhs, const WorldPosition & rhs ) noexcept
    {
      return lhs.cell == rhs.cell && lhs.local == rhs.local;
    }
  };
} // namespace apollo