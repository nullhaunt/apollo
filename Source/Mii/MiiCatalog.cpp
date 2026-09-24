#include "Mii/MiiCatalog.hpp"

#include "Platform/Diagnostics.hpp"
#include "Platform/Platform.hpp"

#include <nn/mii/mii_CharInfoAccessor.h>
#include <nn/mii/mii_Database.h>

#include <cstdio>
#include <cstring>

namespace apollo::mii
{
  namespace
  {
    static_assert( sizeof( nn::mii::CharInfo ) == CharInfoBytes );
    static_assert( CatalogCapacity == nn::mii::DatabaseMiiCount + nn::mii::DefaultMiiCount );

    void CopyNickname( std::array<char, 48> & destination, const nn::mii::Nickname & nickname ) noexcept
    {
      size_t output = 0;
      for ( size_t i = 0; i < nn::mii::Nickname::Length && nickname.name[ i ] != 0; ++i )
      {
        std::uint32_t codepoint = nickname.name[ i ];
        if ( codepoint >= 0xD800 && codepoint <= 0xDBFF )
        {
          const std::uint32_t low = i + 1 < nn::mii::Nickname::Length ? nickname.name[ i + 1 ] : 0;
          if ( low < 0xDC00 || low > 0xDFFF )
          {
            break;
          }
          codepoint = 0x10000 + ( ( codepoint - 0xD800 ) << 10 ) + ( low - 0xDC00 );
          ++i;
        }
        else if ( codepoint >= 0xDC00 && codepoint <= 0xDFFF )
        {
          break;
        }

        const size_t width = codepoint < 0x80 ? 1 : codepoint < 0x800 ? 2 : codepoint < 0x10000 ? 3 : 4;
        if ( output + width >= destination.size() )
        {
          break;
        }
        if ( width == 1 )
        {
          destination[ output++ ] = static_cast<char>( codepoint );
        }
        else
        {
          for ( size_t part = width - 1; part > 0; --part )
          {
            destination[ output + part ]   = static_cast<char>( 0x80 | ( codepoint & 0x3F ) );
            codepoint                    >>= 6;
          }
          const std::uint32_t prefix  = width == 2 ? 0xC0 : width == 3 ? 0xE0 : 0xF0;
          destination[ output ]       = static_cast<char>( prefix | codepoint );
          output                     += width;
        }
      }
      destination[ output ] = '\0';
    }
  } // namespace

  bool Catalog::Load() noexcept
  {
    m_Count     = 0;
    m_Available = false;

    nn::mii::Database database;
    if ( !database.Initialize().IsSuccess() )
    {
      diagnostics::Write( diagnostics::Level::Error, "Mii database initialization failed." );
      return false;
    }

    const auto append = [ & ]( const nn::mii::CharInfo & info, Source source, std::uint8_t defaultIndex ) noexcept {
      if ( m_Count == CatalogCapacity )
      {
        return;
      }

      nn::mii::CharInfo restored{};
      std::memcpy( &restored, &info, CharInfoBytes );
      const nn::mii::CharInfoAccessor accessor( restored );
      if ( !accessor.IsValid() )
      {
        return;
      }

      Entry & entry = m_Entries[ m_Count++ ];
      std::memcpy( entry.snapshot.data(), &info, CharInfoBytes );
      entry.source       = source;
      entry.defaultIndex = defaultIndex;
      entry.height       = static_cast<std::uint8_t>( accessor.GetHeight() );
      entry.build        = static_cast<std::uint8_t>( accessor.GetBuild() );
      nn::mii::Nickname nickname{};
      accessor.GetNickname( &nickname, nn::mii::FontRegionFlag_All );
      CopyNickname( entry.name, nickname );
    };

    // BuildDefault gives the same explicit default index on both platforms.
    for ( int index = 0; index < nn::mii::DefaultMiiCount; ++index )
    {
      nn::mii::CharInfo info{};
      database.BuildDefault( &info, index );
      append( info, Source::Default, static_cast<std::uint8_t>( index ) );
    }

#if defined( APOLLO_PLATFORM_NX )
    nn::mii::CharInfoElement elements[ nn::mii::DatabaseMiiCount ]{};
    int                      count = 0;
    const bool               fetched =
      database.Get( &count, elements, nn::mii::DatabaseMiiCount, nn::mii::SourceFlag_Database ).IsSuccess();
    if ( !fetched || count < 0 || count > nn::mii::DatabaseMiiCount )
    {
      database.Finalize();
      diagnostics::Write( diagnostics::Level::Error, "Mii catalog enumeration failed." );
      m_Count = 0;
      return false;
    }

    for ( int index = 0; index < count; ++index )
    {
      append( elements[ index ].info, Source::Database, 0xff );
    }
#endif
    database.Finalize();

    m_Available = true;
    char message[ 96 ]{};
    std::snprintf(
      message, sizeof( message ), "Mii catalog loaded: %u entries.", static_cast<unsigned int>( m_Count ) );
    diagnostics::Write( diagnostics::Level::Information, message );
    return true;
  }

  const Entry * Catalog::Get( size_t index ) const noexcept
  {
    return index < m_Count ? &m_Entries[ index ] : nullptr;
  }
} // namespace apollo::mii
