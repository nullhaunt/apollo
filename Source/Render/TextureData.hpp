#pragma once

#include "Core/Types.hpp"

#include <cstddef>
#include <limits>

namespace apollo::render
{
  // Linear RGBA8 texels. Each row may include padding; the caller owns pixels
  // until the backend has finished its upload.
  struct Rgba8ImageView
  {
    u32        width{};
    u32        height{};
    u32        rowPitchBytes{};
    const u8 * pixels{};
    size_t     byteCount{};

    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
      if ( pixels == nullptr || width == 0 || height == 0 || width > std::numeric_limits<u32>::max() / 4 ||
           rowPitchBytes < width * 4 || rowPitchBytes % 4 != 0 )
      {
        return false;
      }
      const size_t pitch = rowPitchBytes;
      return height <= std::numeric_limits<size_t>::max() / pitch && byteCount >= pitch * height;
    }
  };

  enum class TextureFilter : u8
  {
    Nearest,
    Linear
  };

  enum class TextureAddressMode : u8
  {
    ClampToEdge,
    Repeat
  };

  struct TextureSamplerDesc
  {
    TextureFilter      filter{ TextureFilter::Nearest };
    TextureAddressMode addressMode{ TextureAddressMode::ClampToEdge };
  };
} // namespace apollo::render
