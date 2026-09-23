#pragma once

#if ( defined( APOLLO_PLATFORM_WINDOWS ) + defined( APOLLO_PLATFORM_NX ) ) != 1
  #error "Exactly one Apollo target platform must be defined."
#endif

namespace apollo::platform
{
  enum class Target
  {
    Windows,
    Nx
  };

#if defined( APOLLO_PLATFORM_WINDOWS )
  inline constexpr Target       CurrentTarget     = Target::Windows;
  inline constexpr const char * CurrentTargetName = "Windows";
#else
  inline constexpr Target       CurrentTarget     = Target::Nx;
  inline constexpr const char * CurrentTargetName = "NX64";
#endif
} // namespace apollo::platform
