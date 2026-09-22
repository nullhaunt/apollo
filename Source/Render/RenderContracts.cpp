#include "Render/RenderDevice.hpp"

#include <type_traits>

namespace
{
  using namespace apollo::render;

  static_assert( !Extent2D{}.IsValid() );
  static_assert( Extent2D{ 1280, 720 }.IsValid() );
  static_assert( !DeviceConfig{}.IsValid() );
  static_assert( !DeviceConfig{ static_cast<Backend>( 255 ), PresentationConfig{ Extent2D{ 1280, 720 } } }.IsValid() );
  static_assert( DeviceConfig{ Backend::Vulkan, PresentationConfig{ Extent2D{ 1280, 720 } } }.IsValid() );
  static_assert( DeviceConfig{ Backend::Nvn, PresentationConfig{ Extent2D{ 1920, 1080 } } }.IsValid() );

  static_assert( std::is_trivially_copyable_v<Extent2D> );
  static_assert( std::is_trivially_copyable_v<ClearColor> );
  static_assert( std::is_trivially_copyable_v<PresentationConfig> );
  static_assert( std::is_trivially_copyable_v<DeviceConfig> );
  static_assert( std::is_abstract_v<RenderDevice> );
  static_assert( !std::is_copy_constructible_v<RenderDevice> );
} // namespace
