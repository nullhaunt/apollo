#pragma once

#include "Mii/PreviewCamera.hpp"
#include "Render/RenderTypes.hpp"

#include <algorithm>
#include <cmath>

namespace apollo::mii
{
  // The preview's composition is shared. Only clip-space conventions belong
  // to the graphics backends.
  enum class PreviewClipSpace
  {
    Nvn,
    Vulkan
  };

  struct PreviewRect
  {
    float x{};
    float y{};
    float width{};
    float height{};
  };

  struct PreviewPanelLayout
  {
    PreviewRect diagnostics{};
    PreviewRect inspector{};
  };

  struct PreviewScene
  {
    static constexpr render::ClearColor Background{ 0.08f, 0.12f, 0.20f, 1.0f };

    [[nodiscard]] static constexpr bool DrawPlaceholder( bool headReady ) noexcept
    {
      return !headReady;
    }

    [[nodiscard]] static PreviewPanelLayout Panels( render::Extent2D extent ) noexcept
    {
      const float width  = static_cast<float>( extent.width );
      const float height = static_cast<float>( extent.height );
      const float margin = std::min( { 20.0f, width * 0.25f, height * 0.25f } );
      const float gap    = std::min( 16.0f, height * 0.1f );

      if ( width >= 900.0f )
      {
        const float panelWidth = width >= 1100.0f ? 350.0f : 260.0f;
        return { { margin, margin, panelWidth, std::min( 360.0f, height - 2.0f * margin ) },
                 { width - margin - panelWidth, margin, panelWidth, std::min( 440.0f, height - 2.0f * margin ) } };
      }

      const float panelWidth        = width - 2.0f * margin;
      const float diagnosticsHeight = std::min( 220.0f, ( height - 3.0f * margin - gap ) * 0.4f );
      const float inspectorY        = margin + diagnosticsHeight + gap;
      return { { margin, margin, panelWidth, diagnosticsHeight },
               { margin, inspectorY, panelWidth, height - inspectorY - margin } };
    }

    [[nodiscard]] static constexpr render::Extent2D HeadViewport( render::Extent2D extent ) noexcept
    {
      return extent;
    }

    static void MakeCameraMatrix( float *               output,
                                  render::Extent2D      extent,
                                  const PreviewCamera & input,
                                  PreviewClipSpace      clipSpace ) noexcept
    {
      PreviewCamera camera = input;
      camera.Clamp();

      constexpr float pi         = 3.14159265358979323846f;
      const float     yaw        = camera.yawDegrees * pi / 180.0f;
      const float     pitch      = camera.pitchDegrees * pi / 180.0f;
      const float     horizontal = camera.distance * std::cos( pitch );

      struct Vec3
      {
        float x;
        float y;
        float z;
      };
      const auto dot = []( Vec3 left, Vec3 right ) noexcept {
        return left.x * right.x + left.y * right.y + left.z * right.z;
      };
      const auto cross = []( Vec3 left, Vec3 right ) noexcept -> Vec3 {
        return { left.y * right.z - left.z * right.y,
                 left.z * right.x - left.x * right.z,
                 left.x * right.y - left.y * right.x };
      };
      const auto normalize = [ & ]( Vec3 value ) noexcept -> Vec3 {
        const float inverse = 1.0f / std::sqrt( dot( value, value ) );
        return { value.x * inverse, value.y * inverse, value.z * inverse };
      };

      const Vec3 eye{ horizontal * std::sin( yaw ),
                      PreviewCamera::TargetHeight + camera.distance * std::sin( pitch ),
                      horizontal * std::cos( yaw ) };
      const Vec3 target{ 0.0f, PreviewCamera::TargetHeight, 0.0f };
      const Vec3 forward = normalize( { target.x - eye.x, target.y - eye.y, target.z - eye.z } );
      const Vec3 right   = normalize( cross( forward, { 0.0f, 1.0f, 0.0f } ) );
      const Vec3 up      = cross( right, forward );

      const float view[ 16 ]{ right.x,
                              up.x,
                              -forward.x,
                              0.0f,
                              right.y,
                              up.y,
                              -forward.y,
                              0.0f,
                              right.z,
                              up.z,
                              -forward.z,
                              0.0f,
                              -dot( right, eye ),
                              -dot( up, eye ),
                              dot( forward, eye ),
                              1.0f };

      const float     aspect = static_cast<float>( extent.width ) / static_cast<float>( extent.height );
      const float     scale  = 1.0f / std::tan( pi / 8.0f );
      constexpr float nearZ  = 1.0f;
      constexpr float farZ   = 500.0f;
      float           projection[ 16 ]{};
      projection[ 0 ] = scale / aspect;
      projection[ 5 ] = clipSpace == PreviewClipSpace::Vulkan ? -scale : scale;
      projection[ 10 ] =
        clipSpace == PreviewClipSpace::Vulkan ? farZ / ( nearZ - farZ ) : ( farZ + nearZ ) / ( nearZ - farZ );
      projection[ 11 ] = -1.0f;
      projection[ 14 ] = clipSpace == PreviewClipSpace::Vulkan ? farZ * nearZ / ( nearZ - farZ )
                                                               : 2.0f * farZ * nearZ / ( nearZ - farZ );

      for ( int column = 0; column < 4; ++column )
      {
        for ( int row = 0; row < 4; ++row )
        {
          float value = 0.0f;
          for ( int term = 0; term < 4; ++term )
          {
            value += projection[ term * 4 + row ] * view[ column * 4 + term ];
          }
          output[ column * 4 + row ] = value;
        }
      }
    }
  };
} // namespace apollo::mii
