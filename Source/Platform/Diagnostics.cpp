#include "Platform/Diagnostics.hpp"

#include <nn/nn_Log.h>

namespace
{
  [[nodiscard, maybe_unused]] constexpr const char * GetLevelName( apollo::diagnostics::Level level ) noexcept
  {
    using apollo::diagnostics::Level;

    switch ( level )
    {
      case Level::Information:
        return "Info";
      case Level::Warning:
        return "Warning";
      case Level::Error:
        return "Error";
    }

    return "Unknown";
  }
} // namespace

namespace apollo::diagnostics
{
  void Write( Level level, const char * message ) noexcept
  {
    NN_LOG( "[Apollo][%s] %s\n", GetLevelName( level ), message );

    static_cast<void>( level );
    static_cast<void>( message );
  }

  void Write( Level level, const char * message, const char * value ) noexcept
  {
    NN_LOG( "[Apollo][%s] %s%s\n", GetLevelName( level ), message, value );

    static_cast<void>( level );
    static_cast<void>( message );
    static_cast<void>( value );
  }
} // namespace apollo::diagnostics