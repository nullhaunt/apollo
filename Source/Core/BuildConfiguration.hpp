#pragma once

#if ( defined( APOLLO_BUILD_DEBUG ) + defined( APOLLO_BUILD_DEVELOP ) + defined( APOLLO_BUILD_RELEASE ) ) != 1
  #error "Exactly one Apollo build configuration must be defined."
#endif

namespace apollo::build
{
  enum class Configuration
  {
    Debug,
    Develop,
    Release
  };

#if defined( APOLLO_BUILD_DEBUG )
  inline constexpr Configuration CurrentConfiguration     = Configuration::Debug;
  inline constexpr const char *  CurrentConfigurationName = "Debug";
#elif defined( APOLLO_BUILD_DEVELOP )
  inline constexpr Configuration CurrentConfiguration     = Configuration::Develop;
  inline constexpr const char *  CurrentConfigurationName = "Develop";
#else
  inline constexpr Configuration CurrentConfiguration     = Configuration::Release;
  inline constexpr const char *  CurrentConfigurationName = "Release";
#endif
} // namespace apollo::build