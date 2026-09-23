#include "Application/Application.hpp"
#include "Platform/Diagnostics.hpp"

extern "C" void nnMain()
{
  apollo::Application application;
  const auto          status = application.Run();

  if ( status != apollo::ApplicationExitStatus::Success )
  {
    apollo::diagnostics::Write( apollo::diagnostics::Level::Error, "Apollo terminated with an error." );
  }
}
