#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

namespace apollo::mii
{
  // CPU-side SDK inputs retained for later CharModel creation. No SDK binary
  // resource is copied into the Apollo repository or executable.
  class ResourceFiles final
  {
  public:
    [[nodiscard]] bool Load() noexcept;
    void               Clear() noexcept;

    [[nodiscard]] bool IsReady() const noexcept
    {
      return m_Texture != nullptr && m_Shape != nullptr;
    }
    [[nodiscard]] const void * TextureData() const noexcept
    {
      return m_Texture.get();
    }
    [[nodiscard]] const void * ShapeData() const noexcept
    {
      return m_Shape.get();
    }
    [[nodiscard]] size_t TextureSize() const noexcept
    {
      return m_TextureSize;
    }
    [[nodiscard]] size_t ShapeSize() const noexcept
    {
      return m_ShapeSize;
    }
    [[nodiscard]] size_t ResourceObjectSize() const noexcept
    {
      return m_ResourceObjectSize;
    }
    [[nodiscard]] size_t ResourceObjectAlignment() const noexcept
    {
      return m_ResourceObjectAlignment;
    }

  private:
    std::unique_ptr<std::uint8_t[]> m_Texture{};
    std::unique_ptr<std::uint8_t[]> m_Shape{};
    size_t                          m_TextureSize{};
    size_t                          m_ShapeSize{};
    size_t                          m_ResourceObjectSize{};
    size_t                          m_ResourceObjectAlignment{};
  };
} // namespace apollo::mii
