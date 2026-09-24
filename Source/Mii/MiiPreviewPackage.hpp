#pragma once

#if !defined( APOLLO_PLATFORM_WINDOWS )
  #error "MiiPreviewPackage is available only on Windows."
#endif

#include "Mii/MiiResources.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace apollo::mii
{
  class PreviewPackage final
  {
  public:
    struct Texture
    {
      std::uint32_t        type{};
      std::uint32_t        width{};
      std::uint32_t        height{};
      const std::uint8_t * pixels{};
    };

    struct Part
    {
      std::uint32_t        drawType{};
      std::uint32_t        modulateType{};
      std::uint32_t        cullMode{};
      std::uint32_t        textureType{ 0xffffffffu };
      std::uint32_t        vertexCount{};
      std::uint32_t        indexCount{};
      float                colors[ 3 ][ 3 ]{};
      const std::uint8_t * positions{};
      const std::uint8_t * uvs{};
      const std::uint8_t * indices{};
    };

    [[nodiscard]] bool Load( const ResourceFiles & resources ) noexcept;
    void               Clear() noexcept;

    [[nodiscard]] bool            IsReady() const noexcept;
    [[nodiscard]] std::uint32_t   TextureCount() const noexcept;
    [[nodiscard]] std::uint32_t   PartCount() const noexcept;
    [[nodiscard]] const Texture * GetTexture( std::uint32_t type ) const noexcept;
    [[nodiscard]] const Part *    GetPart( std::uint32_t index ) const noexcept;

  private:
    [[nodiscard]] bool Parse( const ResourceFiles & resources, size_t fileBytes ) noexcept;

    std::unique_ptr<std::uint8_t[]> m_Data{};
    Texture                         m_Textures[ 5 ]{};
    Part                            m_Parts[ 9 ]{};
    std::uint32_t                   m_TextureCount{};
    std::uint32_t                   m_PartCount{};
  };
} // namespace apollo::mii
