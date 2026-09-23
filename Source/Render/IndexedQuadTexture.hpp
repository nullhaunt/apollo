#pragma once

#include "Render/TextureData.hpp"

#include <array>

namespace apollo::render
{
  inline constexpr u32 IndexedQuadTextureWidth    = 19;
  inline constexpr u32 IndexedQuadTextureHeight   = 13;
  inline constexpr u32 IndexedQuadTextureRowPitch = 80;

  // A non-square image with padded rows exercises both upload paths. Padding
  // is magenta so an incorrect row pitch is easy to spot on the quad.
  inline constexpr auto IndexedQuadTexels = [] {
    std::array<u8, IndexedQuadTextureRowPitch * IndexedQuadTextureHeight> pixels{};
    constexpr u8                                                          colors[ 4 ][ 4 ] = {
      { 255, 80, 64, 255 },
      { 72, 220, 112, 255 },
      { 72, 144, 255, 255 },
      { 255, 208, 64, 255 },
    };
    for ( u32 y = 0; y < IndexedQuadTextureHeight; ++y )
    {
      for ( u32 x = 0; x < IndexedQuadTextureWidth; ++x )
      {
        const u32 quadrant =
          ( y >= IndexedQuadTextureHeight / 2 ? 2u : 0u ) + ( x >= IndexedQuadTextureWidth / 2 ? 1u : 0u );
        for ( u32 channel = 0; channel < 4; ++channel )
        {
          pixels[ y * IndexedQuadTextureRowPitch + x * 4 + channel ] = colors[ quadrant ][ channel ];
        }
      }
      for ( u32 byte = IndexedQuadTextureWidth * 4; byte < IndexedQuadTextureRowPitch; ++byte )
      {
        pixels[ y * IndexedQuadTextureRowPitch + byte ] = byte % 4 == 1 ? 0 : 255;
      }
    }
    return pixels;
  }();

  inline constexpr Rgba8ImageView     IndexedQuadImage{ IndexedQuadTextureWidth,
                                                    IndexedQuadTextureHeight,
                                                    IndexedQuadTextureRowPitch,
                                                    IndexedQuadTexels.data(),
                                                    IndexedQuadTexels.size() };
  inline constexpr TextureSamplerDesc IndexedQuadSampler{};
  static_assert( IndexedQuadImage.IsValid() );
} // namespace apollo::render
