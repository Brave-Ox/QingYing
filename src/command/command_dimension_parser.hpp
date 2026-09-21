#pragma once

#include <string>

namespace qingying::command_detail {

struct Dimensions {
  int width{0};
  int height{0};
};

bool parseCenteredCropDimensions(const std::wstring& utterance,
                                 Dimensions* dimensions);

}  // namespace qingying::command_detail
