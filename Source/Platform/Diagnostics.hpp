#pragma once

namespace apollo::diagnostics
{
  enum class Level
  {
    Information,
    Warning,
    Error
  };

  void Write( Level level, const char * message ) noexcept;
  void Write( Level level, const char * message, const char * value ) noexcept;
} // namespace apollo::diagnostics
