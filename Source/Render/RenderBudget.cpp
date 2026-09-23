#include "Render/RenderBudget.hpp"

#include "Platform/Diagnostics.hpp"

#include <cstdio>
#include <limits>

namespace apollo::render::budget
{
  namespace
  {
    Snapshot g_State{};

    [[nodiscard]] const char * Name( Resource resource ) noexcept
    {
      switch ( resource )
      {
        case Resource::Presentation:
          return "presentation";
        case Resource::Geometry:
          return "geometry";
        case Resource::Texture:
          return "texture";
        case Resource::Upload:
          return "upload";
        default:
          return "unknown";
      }
    }

    [[nodiscard]] bool Reserve( Resource resource, size_t bytes ) noexcept
    {
      const size_t index = static_cast<size_t>( resource );
      if ( index >= static_cast<size_t>( Resource::Count ) || bytes == 0 ||
           bytes > std::numeric_limits<size_t>::max() - g_State.currentBytes )
      {
        return false;
      }

      if ( g_State.profile.hardBytes != 0 && ( g_State.currentBytes > g_State.profile.hardBytes ||
                                               bytes > g_State.profile.hardBytes - g_State.currentBytes ) )
      {
        ++g_State.deniedRequests;
        char message[ 160 ]{};
        std::snprintf( message,
                       sizeof( message ),
                       "PC renderer budget denied %s request of %llu bytes (in use %llu, hard cap %llu).",
                       Name( resource ),
                       static_cast<unsigned long long>( bytes ),
                       static_cast<unsigned long long>( g_State.currentBytes ),
                       static_cast<unsigned long long>( g_State.profile.hardBytes ) );
        diagnostics::Write( diagnostics::Level::Warning, message );
        return false;
      }

      g_State.currentBytes               += bytes;
      g_State.currentByResource[ index ] += bytes;

      if ( g_State.currentBytes > g_State.peakBytes )
      {
        g_State.peakBytes = g_State.currentBytes;
      }

      if ( !g_State.softWarningIssued && g_State.profile.softBytes != 0 &&
           g_State.currentBytes >= g_State.profile.softBytes )
      {
        g_State.softWarningIssued = true;
        diagnostics::Write( diagnostics::Level::Warning, "PC renderer soft memory budget reached." );
      }
      return true;
    }

    void Unreserve( Resource resource, size_t bytes ) noexcept
    {
      const size_t index = static_cast<size_t>( resource );
      if ( index >= static_cast<size_t>( Resource::Count ) || bytes > g_State.currentByResource[ index ] ||
           bytes > g_State.currentBytes )
      {
        diagnostics::Write( diagnostics::Level::Error, "Renderer budget reservation bookkeeping failed." );
        return;
      }
      g_State.currentBytes               -= bytes;
      g_State.currentByResource[ index ] -= bytes;
    }
  } // namespace

  bool Configure( Profile profile ) noexcept
  {
    if ( g_State.currentBytes != 0 || ( profile.hardBytes == 0 && profile.softBytes != 0 ) ||
         ( profile.hardBytes != 0 && profile.softBytes > profile.hardBytes ) )
    {
      return false;
    }
    g_State         = {};
    g_State.profile = profile;
    return true;
  }

  Snapshot GetSnapshot() noexcept
  {
    return g_State;
  }

  void SetApplicationAvailableBytes( u64 bytes ) noexcept
  {
    g_State.applicationAvailableBytes = bytes;
  }

  bool Reservation::Acquire( Resource resource, size_t bytes ) noexcept
  {
    if ( m_Bytes != 0 || !Reserve( resource, bytes ) )
    {
      return false;
    }
    m_Resource = resource;
    m_Bytes    = bytes;
    return true;
  }

  void Reservation::Release() noexcept
  {
    if ( m_Bytes == 0 )
    {
      return;
    }
    Unreserve( m_Resource, m_Bytes );
    m_Resource = Resource::Count;
    m_Bytes    = 0;
  }
} // namespace apollo::render::budget
