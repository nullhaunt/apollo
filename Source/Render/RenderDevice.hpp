#pragma once

#include "Render/RenderTypes.hpp"

namespace apollo::render
{
  // The platform backend owns its native device and presentation surface.
  // Calls are made on the render thread. Implementations must not allocate
  // during steady-state frame calls.
  class RenderDevice
  {
  public:
    virtual ~RenderDevice() noexcept = default;

    RenderDevice( const RenderDevice & )             = delete;
    RenderDevice & operator=( const RenderDevice & ) = delete;

    [[nodiscard]] virtual Backend     GetBackend() const noexcept = 0;
    [[nodiscard]] virtual DeviceState GetState() const noexcept   = 0;

    // The configured backend must match GetBackend(). Success moves
    // Uninitialized to Ready. Shutdown is safe after partial initialization.
    [[nodiscard]] virtual Result Initialize( const DeviceConfig & config ) noexcept = 0;

    // Rebuild presentation resources when the output size changes or a frame
    // reports SurfaceOutOfDate. Requires Ready; never runs inside an open frame.
    [[nodiscard]] virtual Result Resize( const PresentationConfig & config ) noexcept = 0;

    // Success opens one frame. Recoverable surface results leave it closed.
    [[nodiscard]] virtual Result BeginFrame() noexcept = 0;

    // Valid only after a successful BeginFrame. Clears the current backbuffer.
    [[nodiscard]] virtual Result ClearBackbuffer( ClearColor color ) noexcept = 0;

    // Closes the frame even when presentation fails. The next frame may begin
    // only after a recoverable surface result has been handled.
    [[nodiscard]] virtual Result EndFrame() noexcept = 0;

    virtual void Shutdown() noexcept = 0;

  protected:
    RenderDevice() noexcept = default;
  };
} // namespace apollo::render
