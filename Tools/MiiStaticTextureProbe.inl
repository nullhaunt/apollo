// Included only in a generated, ignored copy of the SDK's Generic Mii sample.
// The Generic SDK uses GL4. Read its public texture views before exiting the offline tool.

#include <nn/gll/gll_Gl.h>

#undef glBindTexture
#undef glGetError
#undef glGetIntegerv
#undef glGetTexImage
#undef glGetTexLevelParameteriv
#undef glPixelStorei

#include <cstdint>
#include <cstdio>
#include <vector>

namespace
{
  using ApolloBindBuffer = void( APIENTRY * )( GLenum, GLuint );
  using ApolloIsBuffer   = GLboolean( APIENTRY * )( GLuint );

  bool ApolloWriteViewTexture( const nn::gfx::TextureView & view,
                               const char *                 fileName,
                               std::FILE *                  report,
                               ApolloBindBuffer             bindBuffer,
                               ApolloIsBuffer               isBuffer )
  {
    const nn::gfx::TextureView::DataType & data = nn::gfx::AccessorToData( view );
    std::fprintf( report, "  GL handle %u target 0x%x\n", data.hTexture, data.target );
    if ( data.hTexture == 0 || data.target != GL_TEXTURE_2D )
    {
      return false;
    }

    constexpr GLenum PackState[] = { GL_PACK_ALIGNMENT,
                                     GL_PACK_ROW_LENGTH,
                                     GL_PACK_SKIP_ROWS,
                                     GL_PACK_SKIP_PIXELS,
                                     GL_PACK_IMAGE_HEIGHT,
                                     GL_PACK_SKIP_IMAGES,
                                     GL_PACK_SWAP_BYTES,
                                     GL_PACK_LSB_FIRST };
    GLint            previousPack[ 8 ]{};
    for ( int index = 0; index < 8; ++index )
    {
      glGetIntegerv( PackState[ index ], &previousPack[ index ] );
    }

    std::fprintf( report,
                  "  prior pack alignment %d row length %d skip rows %d skip pixels %d\n",
                  previousPack[ 0 ],
                  previousPack[ 1 ],
                  previousPack[ 2 ],
                  previousPack[ 3 ] );

    GLint previousBinding    = 0;
    GLint previousPackBuffer = 0;
    glGetIntegerv( GL_TEXTURE_BINDING_2D, &previousBinding );
    glGetIntegerv( GL_PIXEL_PACK_BUFFER_BINDING, &previousPackBuffer );
    const GLenum priorError = glGetError();
    std::fprintf( report, "  prior GL error 0x%x, pack buffer %d\n", priorError, previousPackBuffer );
    glBindTexture( GL_TEXTURE_2D, data.hTexture );
    bindBuffer( GL_PIXEL_PACK_BUFFER, 0 );

    for ( int index = 0; index < 8; ++index )
    {
      glPixelStorei( PackState[ index ], index == 0 ? 1 : 0 );
    }

    GLint width  = 0;
    GLint height = 0;
    glGetTexLevelParameteriv( GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width );
    glGetTexLevelParameteriv( GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height );
    std::fprintf( report, "  dimensions %d x %d, GL error 0x%x\n", width, height, glGetError() );

    bool written = false;
    if ( width > 0 && height > 0 && width <= 2048 && height <= 2048 )
    {
      const size_t              pixelBytes = static_cast<size_t>( width ) * static_cast<size_t>( height ) * 4;
      std::vector<std::uint8_t> pixels( pixelBytes );

      glGetTexImage( GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data() );
      const GLenum readError = glGetError();
      std::fprintf( report, "  readback GL error 0x%x\n", readError );
      if ( readError == GL_NO_ERROR )
      {
        std::FILE * file = nullptr;
        if ( fopen_s( &file, fileName, "wb" ) == 0 && file != nullptr )
        {
          ApolloTextureHeader header{};
          header.width      = static_cast<std::uint32_t>( width );
          header.height     = static_cast<std::uint32_t>( height );
          header.pixelBytes = static_cast<std::uint32_t>( pixelBytes );
          written           = std::fwrite( &header, sizeof( header ), 1, file ) == 1 &&
                    std::fwrite( pixels.data(), 1, pixelBytes, file ) == pixelBytes;
          written = std::fclose( file ) == 0 && written;
        }
      }
    }

    for ( int index = 0; index < 8; ++index )
    {
      glPixelStorei( PackState[ index ], previousPack[ index ] );
    }
    if ( previousPackBuffer == 0 || isBuffer( static_cast<GLuint>( previousPackBuffer ) ) == GL_TRUE )
    {
      bindBuffer( GL_PIXEL_PACK_BUFFER, static_cast<GLuint>( previousPackBuffer ) );
    }
    else
    {
      // The Generic queue can leave a deleted readback buffer name bound.
      std::fprintf( report, "  skipped stale SDK pack buffer %d\n", previousPackBuffer );
    }
    glBindTexture( GL_TEXTURE_2D, static_cast<GLuint>( previousBinding ) );
    const GLenum restoreError = glGetError();
    std::fprintf( report, "  restore GL error 0x%x\n", restoreError );

    if ( !written )
    {
      std::remove( fileName );
    }

    return written;
  }

  bool ApolloExportViewTextures()
  {
    std::FILE * report = nullptr;
    if ( fopen_s( &report, "MiiViewProbe.txt", "wb" ) != 0 || report == nullptr )
    {
      return false;
    }

    const auto & queueData        = static_cast<const nn::gfx::Queue::DataType &>( nn::gfx::AccessorToData( g_Queue ) );
    const HDC    deviceContext    = static_cast<HDC>( queueData.hDc.ptr );
    const HGLRC  renderingContext = static_cast<HGLRC>( queueData.renderingContext.hGlRc.ptr );
    const HDC    priorDeviceContext    = wglGetCurrentDC();
    const HGLRC  priorRenderingContext = wglGetCurrentContext();

    std::fprintf( report,
                  "queue GL context %p, current GL context %p\n",
                  static_cast<void *>( renderingContext ),
                  static_cast<void *>( priorRenderingContext ) );
    std::fflush( report );
    if ( renderingContext == nullptr || deviceContext == nullptr || !wglMakeCurrent( deviceContext, renderingContext ) )
    {
      std::fprintf( report, "could not bind the Generic queue context\n" );
      std::fclose( report );
      return false;
    }

    const auto bindBuffer = reinterpret_cast<ApolloBindBuffer>( wglGetProcAddress( "glBindBuffer" ) );
    const auto isBuffer   = reinterpret_cast<ApolloIsBuffer>( wglGetProcAddress( "glIsBuffer" ) );
    std::fprintf( report, "GL buffer functions available %d\n", bindBuffer != nullptr && isBuffer != nullptr ? 1 : 0 );
    std::fflush( report );
    if ( bindBuffer == nullptr || isBuffer == nullptr )
    {
      wglMakeCurrent( priorDeviceContext, priorRenderingContext );
      std::fclose( report );
      return false;
    }

    bool valid = true;
    for ( int index = 0; index < nn::mii::CharModel::TextureType_End; ++index )
    {
      const auto   type = static_cast<nn::mii::CharModel::TextureType>( index );
      const auto * view = g_CharModel.GetTextureView( type, 0 );
      if ( view == nullptr )
      {
        std::fprintf( report, "view %d absent\n", index );
        continue;
      }

      char fileName[ 64 ]{};
      std::snprintf( fileName, sizeof( fileName ), "MiiView%dProbe.aptx", index );
      const bool exported = ApolloWriteViewTexture( *view, fileName, report, bindBuffer, isBuffer );
      std::fprintf( report, "view %d %s\n", index, exported ? "exported" : "failed" );
      valid = valid && exported;
    }

    wglMakeCurrent( priorDeviceContext, priorRenderingContext );
    std::fclose( report );
    return valid;
  }
} // namespace
