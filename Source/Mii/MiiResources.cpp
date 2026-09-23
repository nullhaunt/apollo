#include "Mii/MiiResources.hpp"

#include "Platform/Diagnostics.hpp"
#include "Platform/Platform.hpp"

#include <nn/mii/mii_Resource.h>

#include <cstdio>
#include <cstdlib>
#include <new>
#include <utility>

namespace apollo::mii
{
  namespace
  {
    constexpr size_t MaximumResourceBytes = 64 * 1024 * 1024;

#if defined( APOLLO_PLATFORM_WINDOWS )
    bool ReadSdkResource( const char * fileName, std::unique_ptr<std::uint8_t[]> & data, size_t & size ) noexcept
    {
      const char * sdkRoot = std::getenv( "NINTENDO_SDK_ROOT" );
      if ( sdkRoot == nullptr || sdkRoot[ 0 ] == '\0' )
      {
        return false;
      }
      char      path[ 512 ]{};
      const int length =
        std::snprintf( path, sizeof( path ), "%s/Resources/Mii/Common/resource/%s", sdkRoot, fileName );
      if ( length <= 0 || static_cast<size_t>( length ) >= sizeof( path ) )
      {
        return false;
      }

      std::FILE * file = nullptr;
      if ( fopen_s( &file, path, "rb" ) != 0 || file == nullptr )
      {
        return false;
      }
      bool success = false;
      if ( std::fseek( file, 0, SEEK_END ) == 0 )
      {
        const long fileSize = std::ftell( file );
        if ( fileSize > 0 && static_cast<size_t>( fileSize ) <= MaximumResourceBytes &&
             std::fseek( file, 0, SEEK_SET ) == 0 )
        {
          auto bytes = std::unique_ptr<std::uint8_t[]>( new ( std::nothrow ) std::uint8_t[ fileSize ] );
          if ( bytes &&
               std::fread( bytes.get(), 1, static_cast<size_t>( fileSize ), file ) == static_cast<size_t>( fileSize ) )
          {
            size    = static_cast<size_t>( fileSize );
            data    = std::move( bytes );
            success = true;
          }
        }
      }
      std::fclose( file );
      return success;
    }
#endif
  } // namespace

  bool ResourceFiles::Load() noexcept
  {
    Clear();

    nn::mii::ResourceInfo info;
    info.SetDefault()
      .SetShapeQuality( nn::mii::ShapeQuality_Middle )
      .SetTextureQuality( nn::mii::TextureQuality_Low )
      .SetGammaType( nn::mii::GammaType_Srgb );

#if defined( APOLLO_PLATFORM_NX )
    if ( !nn::mii::InitializeResourceLoader().IsSuccess() )
    {
      diagnostics::Write( diagnostics::Level::Error, "Mii system resource loader failed." );
      return false;
    }
    m_TextureSize = nn::mii::GetResourceTextureSize( info );
    m_ShapeSize   = nn::mii::GetResourceShapeSize( info );

    if ( m_TextureSize == 0 || m_ShapeSize == 0 || m_TextureSize > MaximumResourceBytes ||
         m_ShapeSize > MaximumResourceBytes )
    {
      nn::mii::FinalizeResourceLoader();
      Clear();
      diagnostics::Write( diagnostics::Level::Error, "Mii system resources have invalid sizes." );
      return false;
    }
    m_Texture.reset( new ( std::nothrow ) std::uint8_t[ m_TextureSize ] );
    m_Shape.reset( new ( std::nothrow ) std::uint8_t[ m_ShapeSize ] );

    const bool loaded = m_Texture && m_Shape &&
                        nn::mii::LoadResourceTexture( m_Texture.get(), m_TextureSize, info ).IsSuccess() &&
                        nn::mii::LoadResourceShape( m_Shape.get(), m_ShapeSize, info ).IsSuccess();
    nn::mii::FinalizeResourceLoader();

    if ( !loaded )
    {
      Clear();
      diagnostics::Write( diagnostics::Level::Error, "Mii system resources could not be loaded." );
      return false;
    }
#else
    if ( !ReadSdkResource( "WinGenericTextureLowSRGB.dat", m_Texture, m_TextureSize ) ||
         !ReadSdkResource( "ShapeMid.dat", m_Shape, m_ShapeSize ) )
    {
      Clear();
      diagnostics::Write( diagnostics::Level::Error, "Mii SDK resources unavailable; check NINTENDO_SDK_ROOT." );
      return false;
    }
#endif

    m_ResourceObjectAlignment =
      nn::mii::Resource::CalculateMemoryAlignment( m_Texture.get(), m_TextureSize, m_Shape.get(), m_ShapeSize, info );
    m_ResourceObjectSize =
      nn::mii::Resource::CalculateMemorySize( m_Texture.get(), m_TextureSize, m_Shape.get(), m_ShapeSize, info );

    if ( m_ResourceObjectAlignment == 0 || m_ResourceObjectSize == 0 )
    {
      Clear();
      diagnostics::Write( diagnostics::Level::Error, "Mii resource layout calculation failed." );
      return false;
    }

    char message[ 160 ]{};
    std::snprintf( message,
                   sizeof( message ),
                   "Mii inputs ready: texture %llu, shape %llu, Resource object %llu bytes (alignment %llu).",
                   static_cast<unsigned long long>( m_TextureSize ),
                   static_cast<unsigned long long>( m_ShapeSize ),
                   static_cast<unsigned long long>( m_ResourceObjectSize ),
                   static_cast<unsigned long long>( m_ResourceObjectAlignment ) );
    diagnostics::Write( diagnostics::Level::Information, message );
    return true;
  }

  void ResourceFiles::Clear() noexcept
  {
    m_Texture.reset();
    m_Shape.reset();
    m_TextureSize             = 0;
    m_ShapeSize               = 0;
    m_ResourceObjectSize      = 0;
    m_ResourceObjectAlignment = 0;
  }
} // namespace apollo::mii
