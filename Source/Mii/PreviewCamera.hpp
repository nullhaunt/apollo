#pragma once

#include <algorithm>
#include <cmath>

namespace apollo::mii
{
  // Camera settings shared by the NX and Vulkan head renderers.
  // Angles are in degrees; renderers convert them when building their view matrix.
  struct PreviewCamera
  {
    // nn::mii's default model uses a larger coordinate scale than Apollo's
    // later world units. Keep this preview camera in model coordinates.
    static constexpr float TargetHeight    = 30.0f;
    static constexpr float DefaultDistance = 140.0f;
    static constexpr float MinimumDistance = 50.0f;
    static constexpr float MaximumDistance = 250.0f;

    float yawDegrees{};
    float pitchDegrees{};
    float distance{ DefaultDistance };

    void SetFront() noexcept
    {
      yawDegrees   = 0.0f;
      pitchDegrees = 0.0f;
      distance     = DefaultDistance;
    }

    void SetThreeQuarter() noexcept
    {
      yawDegrees   = 35.0f;
      pitchDegrees = 0.0f;
      distance     = DefaultDistance;
    }

    void SetProfile() noexcept
    {
      yawDegrees   = 90.0f;
      pitchDegrees = 0.0f;
      distance     = DefaultDistance;
    }

    void Clamp() noexcept
    {
      if ( !std::isfinite( yawDegrees ) || !std::isfinite( pitchDegrees ) || !std::isfinite( distance ) )
      {
        SetFront();
        return;
      }

      yawDegrees   = std::clamp( yawDegrees, -180.0f, 180.0f );
      pitchDegrees = std::clamp( pitchDegrees, -80.0f, 80.0f );
      distance     = std::clamp( distance, MinimumDistance, MaximumDistance );
    }
  };
} // namespace apollo::mii
