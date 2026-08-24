#include "qingying/app/application.hpp"

#include <Windows.h>

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/,
                    PWSTR /*lpCmdLine*/, int /*nCmdShow*/) {
  qingying::Application app(hInstance);
  return app.run();
}
