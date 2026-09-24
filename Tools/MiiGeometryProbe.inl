// Included only in a generated, ignored copy of the SDK's Generic Mii sample.
// This probes public CharModel draw buffers; it is not an Apollo runtime renderer.

#include <cstdint>
#include <cstdio>
#include <limits>

namespace
{
  struct ApolloGeometryHeader
  {
    std::uint32_t magic{ 0x474D5041 }; // APMG
    std::uint32_t version{ 1 };
    std::uint32_t partCount{};
    std::uint32_t reserved{};
  };

  struct ApolloGeometryPart
  {
    std::uint32_t drawType{};
    std::uint32_t modulateType{};
    std::uint32_t cullMode{};
    std::uint32_t hasTexture{};
    std::uint32_t positionBytes{};
    std::uint32_t uvBytes{};
    std::uint32_t indexCount{};
    float         colors[ 3 ][ 3 ]{};
  };

  static_assert( sizeof( ApolloGeometryHeader ) == 16 );
  static_assert( sizeof( ApolloGeometryPart ) == 64 );

  bool ApolloSelectDefaultMii()
  {
    static_assert( sizeof( nn::mii::CharInfo ) == 88 );

    nn::mii::Database database;
    if ( !database.Initialize().IsSuccess() )
    {
      return false;
    }

    nn::mii::CharInfoElement elements[ 16 ]{};
    int                      count   = 0;
    const bool               fetched = database.Get( &count, elements, 16, nn::mii::SourceFlag_Default ).IsSuccess();
    database.Finalize();

    if ( !fetched || count <= 0 || count > 16 )
    {
      return false;
    }

    for ( int index = 0; index < count; ++index )
    {
      if ( nn::mii::CharInfoAccessor( elements[ index ].info ).IsValid() )
      {
        g_CharInfo = elements[ index ].info;

        std::FILE * file = nullptr;
        if ( fopen_s( &file, "MiiCharInfoProbe.bin", "wb" ) != 0 || file == nullptr )
        {
          return false;
        }

        const bool saved = std::fwrite( &g_CharInfo, sizeof( g_CharInfo ), 1, file ) == 1;
        if ( std::fclose( file ) != 0 || !saved )
        {
          std::remove( "MiiCharInfoProbe.bin" );
          return false;
        }

        std::printf( "Apollo Mii probe selected Generic default %d of %d.\n", index, count );
        return true;
      }
    }

    return false;
  }

  bool ApolloWriteBuffer( std::FILE * file, const nn::gfx::Buffer * buffer, size_t size )
  {
    if ( size == 0 )
    {
      return true;
    }

    if ( buffer == nullptr )
    {
      return false;
    }

    const void * mapped = buffer->Map();
    if ( mapped == nullptr )
    {
      return false;
    }

    const bool written = std::fwrite( mapped, 1, size, file ) == size;
    buffer->Unmap();
    return written;
  }

  bool ApolloWriteIndices( std::FILE * file, const nn::gfx::Buffer * buffer, int count, size_t vertexCount )
  {
    if ( buffer == nullptr )
    {
      return false;
    }

    const auto * indices = buffer->Map<std::uint16_t>();
    if ( indices == nullptr )
    {
      return false;
    }

    bool valid = true;
    for ( int index = 0; index < count; ++index )
    {
      if ( indices[ index ] >= vertexCount )
      {
        valid = false;
        break;
      }
    }

    if ( valid )
    {
      valid = std::fwrite( indices, sizeof( std::uint16_t ), static_cast<size_t>( count ), file ) ==
              static_cast<size_t>( count );
    }

    buffer->Unmap();
    return valid;
  }

  bool ApolloWritePart( std::FILE * file, const nn::mii::DrawParam & draw, int drawType )
  {
    const size_t positionBytes = draw.GetBufferSize( nn::mii::DrawParam::AttributeType_Position );
    const size_t uvBytes       = draw.IsValidAttribute( nn::mii::DrawParam::AttributeType_Uv )
                                   ? draw.GetBufferSize( nn::mii::DrawParam::AttributeType_Uv )
                                   : 0;
    const int    indexCount    = draw.GetIndexCount();
    const size_t vertexCount   = positionBytes / nn::mii::DrawParam::PositionStride;

    if ( positionBytes == 0 || positionBytes % nn::mii::DrawParam::PositionStride != 0 ||
         uvBytes % nn::mii::DrawParam::UvStride != 0 ||
         ( uvBytes != 0 && uvBytes / nn::mii::DrawParam::UvStride != vertexCount ) || indexCount <= 0 ||
         indexCount % 3 != 0 || vertexCount > 65536 || positionBytes > 16 * 1024 * 1024 || uvBytes > 16 * 1024 * 1024 ||
         indexCount > 8 * 1024 * 1024 || positionBytes > std::numeric_limits<std::uint32_t>::max() ||
         uvBytes > std::numeric_limits<std::uint32_t>::max() )
    {
      return false;
    }

    ApolloGeometryPart part{};
    part.drawType      = static_cast<std::uint32_t>( drawType );
    part.modulateType  = static_cast<std::uint32_t>( draw.GetModulateType() );
    part.cullMode      = static_cast<std::uint32_t>( draw.GetCullMode() );
    part.hasTexture    = draw.IsValidTexture() ? 1u : 0u;
    part.positionBytes = static_cast<std::uint32_t>( positionBytes );
    part.uvBytes       = static_cast<std::uint32_t>( uvBytes );
    part.indexCount    = static_cast<std::uint32_t>( indexCount );

    for ( int color = 0; color < 3; ++color )
    {
      const auto * value = draw.GetConstantColor( color );
      if ( value != nullptr )
      {
        part.colors[ color ][ 0 ] = value->r;
        part.colors[ color ][ 1 ] = value->g;
        part.colors[ color ][ 2 ] = value->b;
      }
    }

    return std::fwrite( &part, sizeof( part ), 1, file ) == 1 &&
           ApolloWriteBuffer( file, draw.GetAttribute( nn::mii::DrawParam::AttributeType_Position ), positionBytes ) &&
           ApolloWriteBuffer( file, draw.GetAttribute( nn::mii::DrawParam::AttributeType_Uv ), uvBytes ) &&
           ApolloWriteIndices( file, draw.GetIndexBuffer(), indexCount, vertexCount );
  }

  bool ApolloExportMiiGeometry()
  {
    std::FILE * file = nullptr;
    if ( fopen_s( &file, "MiiGeometryProbe.apmg", "wb" ) != 0 || file == nullptr )
    {
      return false;
    }

    ApolloGeometryHeader header{};
    bool                 valid = std::fwrite( &header, sizeof( header ), 1, file ) == 1;

    for ( int index = 0; valid && index < nn::mii::CharModel::DrawType_End; ++index )
    {
      const auto * draw = g_CharModel.GetDrawParam(
        static_cast<nn::mii::CharModel::DrawType>( index ), CreateModelType, CreateNoseType, 0 );
      if ( draw == nullptr )
      {
        continue;
      }

      valid = ApolloWritePart( file, *draw, index );

      if ( valid )
      {
        ++header.partCount;
      }
    }

    if ( valid && header.partCount != 0 )
    {
      valid = std::fseek( file, 0, SEEK_SET ) == 0 && std::fwrite( &header, sizeof( header ), 1, file ) == 1;
    }
    else
    {
      valid = false;
    }

    valid = std::fclose( file ) == 0 && valid;
    if ( !valid )
    {
      std::remove( "MiiGeometryProbe.apmg" );
    }

    std::printf( "Apollo Mii geometry probe: %s (%u parts).\n", valid ? "passed" : "failed", header.partCount );
    return valid;
  }
} // namespace
