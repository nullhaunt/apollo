#pragma once

#include "Core/Types.hpp"

namespace apollo::render
{
  enum class Backend : u8
  {
    Unknown,
    Vulkan,
    Nvn
  };

  struct Extent2D
  {
    u32 width{};
    u32 height{};

    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
      return width != 0 && height != 0;
    }
  };

  struct ClearColor
  {
    f32 red{};
    f32 green{};
    f32 blue{};
    f32 alpha{ 1.0f };
  };

  struct PresentationConfig
  {
    Extent2D outputExtent{};

    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
      return outputExtent.IsValid();
    }
  };

  struct DeviceConfig
  {
    Backend            backend{ Backend::Unknown };
    PresentationConfig presentation{};

    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
      return ( backend == Backend::Vulkan || backend == Backend::Nvn ) && presentation.IsValid();
    }
  };

  enum class DeviceState : u8
  {
    Uninitialized,
    Ready,
    FrameOpen,
    Lost,
    Stopped
  };

  enum class Result : u8
  {
    Success,
    InvalidConfiguration,
    InvalidState,
    SurfaceUnavailable,
    SurfaceOutOfDate,
    DeviceLost,
    Failure
  };
} // namespace apollo::render
