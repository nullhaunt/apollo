#include "Core/Identifier.hpp"
#include "World/WorldPosition.hpp"

#include <type_traits>

namespace
{
  using namespace apollo;

  static_assert( sizeof( AssetId ) == sizeof( u64 ) );
  static_assert( sizeof( EntityGuid ) == sizeof( u64 ) * 2 );
  static_assert( sizeof( WorldCell ) == sizeof( s32 ) * 3 );
  static_assert( sizeof( LocalPosition ) == sizeof( f32 ) * 3 );
  static_assert( sizeof( WorldPosition ) == sizeof( WorldCell ) + sizeof( LocalPosition ) );

  static_assert( std::is_trivially_copyable_v<AssetId> );
  static_assert( std::is_trivially_copyable_v<EntityGuid> );
  static_assert( std::is_trivially_copyable_v<WorldPosition> );

  static_assert( !AssetId{}.IsValid() );
  static_assert( !EntityGuid{}.IsValid() );
  static_assert( AssetId{ 1 }.IsValid() );
  static_assert( EntityGuid{ 0, 1 }.IsValid() );
} // namespace