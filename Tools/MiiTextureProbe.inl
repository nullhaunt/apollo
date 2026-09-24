// Included only in a generated, ignored copy of the SDK's Generic Mii sample.
// Reads back the already generated faceline and normal-expression mask.

#include <cstdint>
#include <cstdio>

namespace
{
  struct ApolloTextureHeader
  {
    std::uint32_t magic{ 0x58545041 }; // APTX
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t pixelBytes{};
  };

  static_assert( sizeof( ApolloTextureHeader ) == 16 );

  bool ApolloWriteTexture(
    nn::gfx::Buffer & readback, const nn::gfx::Texture * source, int width, int height, const char * fileName )
  {
    if ( source == nullptr || width <= 0 || height <= 0 )
    {
      return false;
    }

    nn::gfx::BufferTextureCopyRegion region;
    region.SetDefault();
    region.SetBufferImageWidth( width );
    region.SetBufferImageHeight( height );
    region.EditTextureCopyRegion().SetWidth( width );
    region.EditTextureCopyRegion().SetHeight( height );

    g_CommandBuffer.Reset();
    g_CommandBuffer.AddControlMemory( g_pCommandBufferControlMemory, CommandBufferControlMemorySize );
    g_CommandBuffer.AddCommandMemory( &g_MemoryPool, g_CommandBufferMemoryPoolOffset, CommandBufferMemoryPoolSize );
    g_CommandBuffer.Begin();
    g_CommandBuffer.CopyImageToBuffer( &readback, source, region );
    g_CommandBuffer.End();
    g_Queue.ExecuteCommand( &g_CommandBuffer, nullptr );
    g_Queue.Sync();

    const size_t pixelBytes = static_cast<size_t>( width ) * static_cast<size_t>( height ) * 4;
    const void * pixels     = readback.Map();
    if ( pixels == nullptr )
    {
      return false;
    }

    readback.InvalidateMappedRange( 0, pixelBytes );

    std::FILE * file    = nullptr;
    bool        written = false;
    if ( fopen_s( &file, fileName, "wb" ) == 0 && file != nullptr )
    {
      ApolloTextureHeader header{};
      header.width      = static_cast<std::uint32_t>( width );
      header.height     = static_cast<std::uint32_t>( height );
      header.pixelBytes = static_cast<std::uint32_t>( pixelBytes );
      written           = std::fwrite( &header, sizeof( header ), 1, file ) == 1 &&
                std::fwrite( pixels, 1, pixelBytes, file ) == pixelBytes;
      written = std::fclose( file ) == 0 && written;
    }

    readback.Unmap();

    if ( !written )
    {
      std::remove( fileName );
    }

    return written;
  }

  bool ApolloExportGeneratedTextures()
  {
    constexpr int    Resolution    = 512;
    constexpr size_t ReadbackBytes = static_cast<size_t>( Resolution ) * Resolution * 4;

    nn::gfx::BufferInfo info;
    info.SetDefault();
    info.SetSize( ReadbackBytes );
    info.SetGpuAccessFlags( nn::gfx::GpuAccess_Write );

    const size_t alignment = nn::gfx::Buffer::GetBufferAlignment( &g_Device, info );
    if ( alignment == 0 )
    {
      return false;
    }

    const ptrdiff_t offset = nn::util::align_up( g_MemoryPoolOffset, alignment );
    if ( offset < 0 || static_cast<size_t>( offset ) > VisiblePoolMemorySize - ReadbackBytes )
    {
      return false;
    }

    nn::gfx::Buffer readback;
    readback.Initialize( &g_Device, info, &g_MemoryPool, offset, ReadbackBytes );

    const bool faceline = ApolloWriteTexture(
      readback, g_CharModel.GetFacelineTexture(), Resolution / 2, Resolution, "MiiFacelineProbe.aptx" );
    const bool mask =
      ApolloWriteTexture( readback, g_CharModel.GetMaskTexture( 0 ), Resolution, Resolution, "MiiMaskProbe.aptx" );

    readback.Finalize( &g_Device );
    std::printf(
      "Apollo Mii texture probe: faceline %s, mask %s.\n", faceline ? "passed" : "failed", mask ? "passed" : "failed" );
    return faceline && mask;
  }
} // namespace
