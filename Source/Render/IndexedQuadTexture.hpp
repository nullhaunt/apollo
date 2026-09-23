#pragma once

#include "Core/Types.hpp"

#include <array>

namespace apollo::render
{
  inline constexpr u32 IndexedQuadTextureSize = 16;

  // Linear RGBA8 texels shared by both upload paths. Distinct quadrants make UV errors visible.
  inline constexpr auto IndexedQuadTexels = []
  {
    std::array<u8, IndexedQuadTextureSize * IndexedQuadTextureSize * 4> pixels{};
    constexpr u8 colors[ 4 ][ 4 ] = {
      { 255, 80, 64, 255 }, { 72, 220, 112, 255 },
      { 72, 144, 255, 255 }, { 255, 208, 64, 255 },
    };
    for ( u32 y = 0; y < IndexedQuadTextureSize; ++y )
    {
      for ( u32 x = 0; x < IndexedQuadTextureSize; ++x )
      {
        const u32 quadrant = ( y >= 8 ? 2u : 0u ) + ( x >= 8 ? 1u : 0u );
        for ( u32 channel = 0; channel < 4; ++channel )
        {
          pixels[ ( y * IndexedQuadTextureSize + x ) * 4 + channel ] = colors[ quadrant ][ channel ];
        }
      }
    }
    return pixels;
  }();
} // namespace apollo::render
