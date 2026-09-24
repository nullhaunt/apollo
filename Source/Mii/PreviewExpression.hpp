#pragma once

#include <cstdint>

namespace apollo::mii
{
  // The preview's expression order matches the two SDK mask slots.
  enum class PreviewExpression : std::uint8_t
  {
    Normal = 0,
    Smile  = 1
  };

  inline constexpr int PreviewExpressionCount = 2;
} // namespace apollo::mii
