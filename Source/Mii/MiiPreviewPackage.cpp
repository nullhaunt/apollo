#include "Mii/MiiPreviewPackage.hpp"

#include "Platform/Diagnostics.hpp"

#include <nn/mii/mii_CharInfoAccessor.h>

#include <Windows.h>
#include <bcrypt.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <limits>
#include <new>

#pragma comment( lib, "bcrypt.lib" )

namespace apollo::mii
{
  namespace
  {
    constexpr size_t MaximumPackageBytes = 32 * 1024 * 1024;

    struct PackageHeader
    {
      char          magic[ 4 ];
      std::uint32_t version;
      std::uint32_t totalBytes;
      std::uint32_t partCount;
      std::uint32_t textureCount;
      std::uint8_t  charInfo[ 88 ];
      std::uint8_t  resourceHash[ 32 ];
      std::uint8_t  bodyHash[ 32 ];
    };

    struct TextureHeader
    {
      std::uint32_t type;
      std::uint32_t width;
      std::uint32_t height;
      std::uint32_t pixelBytes;
    };

    struct PartHeader
    {
      std::uint32_t drawType;
      std::uint32_t modulateType;
      std::uint32_t cullMode;
      std::uint32_t hasTexture;
      std::uint32_t positionBytes;
      std::uint32_t uvBytes;
      std::uint32_t indexCount;
      float         colors[ 3 ][ 3 ];
    };

    static_assert( sizeof( PackageHeader ) == 172 );
    static_assert( sizeof( TextureHeader ) == 16 );
    static_assert( sizeof( PartHeader ) == 64 );

    [[nodiscard]] bool HashSha256( const std::uint8_t * first,
                                   size_t               firstBytes,
                                   const std::uint8_t * second,
                                   size_t               secondBytes,
                                   std::uint8_t *       output ) noexcept
    {
      if ( firstBytes > std::numeric_limits<ULONG>::max() || secondBytes > std::numeric_limits<ULONG>::max() )
      {
        return false;
      }

      BCRYPT_ALG_HANDLE algorithm{};
      if ( BCryptOpenAlgorithmProvider( &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0 ) < 0 )
      {
        return false;
      }

      ULONG      objectBytes{};
      ULONG      returned{};
      const bool propertiesReady = BCryptGetProperty( algorithm,
                                                      BCRYPT_OBJECT_LENGTH,
                                                      reinterpret_cast<PUCHAR>( &objectBytes ),
                                                      sizeof( objectBytes ),
                                                      &returned,
                                                      0 ) >= 0 &&
                                   returned == sizeof( objectBytes ) && objectBytes != 0;
      std::unique_ptr<std::uint8_t[]> object;
      if ( propertiesReady )
      {
        object.reset( new ( std::nothrow ) std::uint8_t[ objectBytes ] );
      }

      BCRYPT_HASH_HANDLE hash{};
      bool valid = object && BCryptCreateHash( algorithm, &hash, object.get(), objectBytes, nullptr, 0, 0 ) >= 0;
      if ( valid )
      {
        valid = BCryptHashData( hash, const_cast<PUCHAR>( first ), static_cast<ULONG>( firstBytes ), 0 ) >= 0;
      }
      if ( valid && secondBytes != 0 )
      {
        valid = BCryptHashData( hash, const_cast<PUCHAR>( second ), static_cast<ULONG>( secondBytes ), 0 ) >= 0;
      }
      if ( valid )
      {
        valid = BCryptFinishHash( hash, output, 32, 0 ) >= 0;
      }

      if ( hash != nullptr )
      {
        BCryptDestroyHash( hash );
      }
      BCryptCloseAlgorithmProvider( algorithm, 0 );
      return valid;
    }

    [[nodiscard]] bool HalfFinite( const std::uint8_t * data, size_t bytes ) noexcept
    {
      for ( size_t offset = 0; offset < bytes; offset += 2 )
      {
        const std::uint16_t value = static_cast<std::uint16_t>( data[ offset ] | data[ offset + 1 ] << 8 );
        if ( ( value & 0x7c00u ) == 0x7c00u )
        {
          return false;
        }
      }
      return true;
    }

    [[nodiscard]] int TextureTypeForDraw( std::uint32_t drawType ) noexcept
    {
      switch ( drawType )
      {
        case 1:
          return 0;
        case 4:
          return 1;
        case 6:
          return 2;
        case 7:
          return 3;
        case 8:
          return 4;
        default:
          return -1;
      }
    }

    [[nodiscard]] std::FILE * OpenDefaultPackage() noexcept
    {
      wchar_t     module[ MAX_PATH ]{};
      const DWORD length = GetModuleFileNameW( nullptr, module, MAX_PATH );
      if ( length == 0 || length >= MAX_PATH )
      {
        return nullptr;
      }

      wchar_t * separator = std::wcsrchr( module, L'\\' );
      if ( separator == nullptr )
      {
        return nullptr;
      }
      *separator = L'\0';

      wchar_t     path[ MAX_PATH ]{};
      std::FILE * file = nullptr;
      if ( swprintf_s( path, L"%s\\Mii\\Default0.apmp", module ) > 0 )
      {
        ( void )_wfopen_s( &file, path, L"rb" );
      }
      if ( file == nullptr && swprintf_s( path, L"%s\\..\\..\\MiiGeometryProbe\\Default0.apmp", module ) > 0 )
      {
        ( void )_wfopen_s( &file, path, L"rb" );
      }
      return file;
    }
  } // namespace

  bool PreviewPackage::Load( const ResourceFiles & resources ) noexcept
  {
    Clear();
    if ( !resources.IsReady() )
    {
      return false;
    }

    std::FILE * file = OpenDefaultPackage();
    if ( file == nullptr )
    {
      diagnostics::Write( diagnostics::Level::Warning, "Mii preview package absent; run Tools/CookMiiPreview.ps1." );
      return false;
    }

    bool valid = false;
    if ( std::fseek( file, 0, SEEK_END ) == 0 )
    {
      const long length = std::ftell( file );
      if ( length >= static_cast<long>( sizeof( PackageHeader ) ) &&
           static_cast<size_t>( length ) <= MaximumPackageBytes && std::fseek( file, 0, SEEK_SET ) == 0 )
      {
        m_Data.reset( new ( std::nothrow ) std::uint8_t[ length ] );
        if ( m_Data &&
             std::fread( m_Data.get(), 1, static_cast<size_t>( length ), file ) == static_cast<size_t>( length ) )
        {
          valid = Parse( resources, static_cast<size_t>( length ) );
        }
      }
    }
    std::fclose( file );

    if ( !valid )
    {
      Clear();
      diagnostics::Write( diagnostics::Level::Error, "Mii preview package is incomplete or mismatched." );
      return false;
    }

    char message[ 96 ]{};
    std::snprintf( message,
                   sizeof( message ),
                   "Mii preview package validated: %u parts, %u textures.",
                   m_PartCount,
                   m_TextureCount );
    diagnostics::Write( diagnostics::Level::Information, message );
    return true;
  }

  bool PreviewPackage::Parse( const ResourceFiles & resources, size_t fileBytes ) noexcept
  {
    PackageHeader header{};
    std::memcpy( &header, m_Data.get(), sizeof( header ) );
    if ( std::memcmp( header.magic, "APMP", 4 ) != 0 || header.version != 1 || header.totalBytes != fileBytes ||
         header.partCount == 0 || header.partCount > 9 || header.textureCount == 0 || header.textureCount > 5 )
    {
      return false;
    }

    nn::mii::CharInfo charInfo{};
    std::memcpy( &charInfo, header.charInfo, sizeof( charInfo ) );
    if ( !nn::mii::CharInfoAccessor( charInfo ).IsValid() )
    {
      return false;
    }

    std::uint8_t digest[ 32 ]{};
    if ( !HashSha256( m_Data.get() + sizeof( header ), fileBytes - sizeof( header ), nullptr, 0, digest ) ||
         std::memcmp( digest, header.bodyHash, sizeof( digest ) ) != 0 ||
         !HashSha256( static_cast<const std::uint8_t *>( resources.TextureData() ),
                      resources.TextureSize(),
                      static_cast<const std::uint8_t *>( resources.ShapeData() ),
                      resources.ShapeSize(),
                      digest ) ||
         std::memcmp( digest, header.resourceHash, sizeof( digest ) ) != 0 )
    {
      return false;
    }

    size_t     offset = sizeof( header );
    const auto take   = [ & ]( size_t bytes ) noexcept -> const std::uint8_t * {
      if ( bytes > fileBytes - offset )
      {
        return nullptr;
      }
      const std::uint8_t * pointer  = m_Data.get() + offset;
      offset                       += bytes;
      return pointer;
    };

    std::uint32_t textureMask{};
    for ( std::uint32_t index = 0; index < header.textureCount; ++index )
    {
      const std::uint8_t * record = take( sizeof( TextureHeader ) );
      if ( record == nullptr )
      {
        return false;
      }
      TextureHeader texture{};
      std::memcpy( &texture, record, sizeof( texture ) );
      if ( texture.type > 4 || ( textureMask & ( 1u << texture.type ) ) != 0 || texture.width == 0 ||
           texture.height == 0 || texture.width > 2048 || texture.height > 2048 ||
           texture.pixelBytes != texture.width * texture.height * 4u )
      {
        return false;
      }

      const std::uint8_t * pixels = take( texture.pixelBytes );
      if ( pixels == nullptr )
      {
        return false;
      }
      m_Textures[ index ]  = { texture.type, texture.width, texture.height, pixels };
      textureMask         |= 1u << texture.type;
    }

    std::uint32_t partMask{};
    std::uint32_t requiredTextureMask{};
    for ( std::uint32_t index = 0; index < header.partCount; ++index )
    {
      const std::uint8_t * record = take( sizeof( PartHeader ) );
      if ( record == nullptr )
      {
        return false;
      }
      PartHeader part{};
      std::memcpy( &part, record, sizeof( part ) );
      const std::uint32_t vertexCount = part.positionBytes / 8;
      if ( part.drawType > 8 || ( partMask & ( 1u << part.drawType ) ) != 0 || part.modulateType > 5 ||
           part.cullMode > 2 || part.hasTexture > 1 || part.positionBytes == 0 || part.positionBytes % 8 != 0 ||
           vertexCount > 65536 || part.uvBytes % 4 != 0 || ( part.uvBytes != 0 && part.uvBytes != vertexCount * 4 ) ||
           ( part.hasTexture != 0 && part.uvBytes == 0 ) || part.indexCount == 0 || part.indexCount % 3 != 0 )
      {
        return false;
      }
      for ( const float * color = &part.colors[ 0 ][ 0 ]; color != &part.colors[ 0 ][ 0 ] + 9; ++color )
      {
        if ( !std::isfinite( *color ) )
        {
          return false;
        }
      }

      const std::uint8_t * positions = take( part.positionBytes );
      const std::uint8_t * uvs       = take( part.uvBytes );
      const std::uint8_t * indices   = take( static_cast<size_t>( part.indexCount ) * 2 );
      if ( positions == nullptr || uvs == nullptr || indices == nullptr ||
           !HalfFinite( positions, part.positionBytes ) || !HalfFinite( uvs, part.uvBytes ) )
      {
        return false;
      }
      for ( std::uint32_t element = 0; element < part.indexCount; ++element )
      {
        const std::uint16_t vertex =
          static_cast<std::uint16_t>( indices[ element * 2 ] | indices[ element * 2 + 1 ] << 8 );
        if ( vertex >= vertexCount )
        {
          return false;
        }
      }

      Part & output       = m_Parts[ index ];
      output.drawType     = part.drawType;
      output.modulateType = part.modulateType;
      output.cullMode     = part.cullMode;
      output.vertexCount  = vertexCount;
      output.indexCount   = part.indexCount;
      output.positions    = positions;
      output.uvs          = uvs;
      output.indices      = indices;
      std::memcpy( output.colors, part.colors, sizeof( output.colors ) );

      if ( part.hasTexture != 0 )
      {
        const int textureType = TextureTypeForDraw( part.drawType );
        if ( textureType < 0 )
        {
          return false;
        }
        output.textureType   = static_cast<std::uint32_t>( textureType );
        requiredTextureMask |= 1u << textureType;
      }
      partMask |= 1u << part.drawType;
    }

    if ( offset != fileBytes || textureMask != requiredTextureMask ||
         ( partMask & ( ( 1u << 1 ) | ( 1u << 6 ) ) ) != ( ( 1u << 1 ) | ( 1u << 6 ) ) )
    {
      return false;
    }
    m_TextureCount = header.textureCount;
    m_PartCount    = header.partCount;
    return true;
  }

  void PreviewPackage::Clear() noexcept
  {
    m_Data.reset();
    for ( auto & texture : m_Textures )
    {
      texture = {};
    }
    for ( auto & part : m_Parts )
    {
      part = {};
    }
    m_TextureCount = 0;
    m_PartCount    = 0;
  }

  bool PreviewPackage::IsReady() const noexcept
  {
    return m_Data != nullptr && m_PartCount != 0;
  }

  std::uint32_t PreviewPackage::TextureCount() const noexcept
  {
    return m_TextureCount;
  }

  std::uint32_t PreviewPackage::PartCount() const noexcept
  {
    return m_PartCount;
  }

  const PreviewPackage::Texture * PreviewPackage::GetTexture( std::uint32_t type ) const noexcept
  {
    for ( std::uint32_t index = 0; index < m_TextureCount; ++index )
    {
      if ( m_Textures[ index ].type == type )
      {
        return &m_Textures[ index ];
      }
    }
    return nullptr;
  }

  const PreviewPackage::Part * PreviewPackage::GetPart( std::uint32_t index ) const noexcept
  {
    return index < m_PartCount ? &m_Parts[ index ] : nullptr;
  }
} // namespace apollo::mii
