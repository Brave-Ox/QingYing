#pragma once

#include "qingying/action/image.hpp"
#include "qingying/pin/pin_window.hpp"

#include <memory>
#include <vector>

namespace qingying {

class PinManager {
 public:
  ~PinManager();

  bool show(const Image& image);
  void closeAll();
  int count() const;

 private:
  void onWindowClosed(PinWindow* window);

  std::vector<std::unique_ptr<PinWindow>> windows_;
};

}  // namespace qingying
