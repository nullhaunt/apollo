#pragma once

#include "Core/Types.hpp"

namespace apollo::render
{
  struct IndexedQuadVertex
  {
    f32 position[ 2 ];
    f32 color[ 3 ];
    f32 uv[ 2 ];
  };

  inline constexpr IndexedQuadVertex IndexedQuadVertices[]{
    { { -0.55f, -0.55f }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 1.0f } },
    { { 0.55f, -0.55f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f } },
    { { 0.55f, 0.55f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 0.0f } },
    { { -0.55f, 0.55f }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f } },
  };

  inline constexpr u16 IndexedQuadIndices[]{ 0, 1, 2, 2, 3, 0 };
} // namespace apollo::render
