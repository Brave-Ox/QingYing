#include "qingying/ui/toolbar_model.h"

namespace qingying {

const wchar_t* toolbarIconLabel(ToolbarIconKind kind)
{
  // 使用码点转义，避免源文件编码导致 Tooltip 显示乱码。
  switch (kind)
  {
    case ToolbarIconKind::Copy:
      return L"\x590D\x5236";
    case ToolbarIconKind::Save:
      return L"\x4E0B\x8F7D\x56FE\x7247";
    case ToolbarIconKind::Edit:
      return L"\x7F16\x8F91";
    case ToolbarIconKind::Pin:
      return L"\x9489\x56FE";
    case ToolbarIconKind::Rectangle:
      return L"\x77E9\x5F62";
    case ToolbarIconKind::Ellipse:
      return L"\x692D\x5706";
    case ToolbarIconKind::Geometry:
      return L"\x51E0\x4F55";
    case ToolbarIconKind::Fill:
      return L"\x586B\x5145";
    case ToolbarIconKind::LineSolid:
      return L"\x5B9E\x7EBF";
    case ToolbarIconKind::LineDashed:
      return L"\x865A\x7EBF";
    case ToolbarIconKind::LineDotted:
      return L"\x70B9\x7EBF";
    case ToolbarIconKind::Arrow:
      return L"\x7BAD\x5934";
    case ToolbarIconKind::Pen:
      return L"\x753B\x7B14";
    case ToolbarIconKind::Mosaic:
      return L"\x9A6C\x8D5B\x514B";
    case ToolbarIconKind::StrokeWidth:
      return L"\x7C97\x7EC6";
    case ToolbarIconKind::Text:
      return L"\x6587\x5B57";
    case ToolbarIconKind::Undo:
      return L"\x64A4\x9500";
    case ToolbarIconKind::Redo:
      return L"\x91CD\x505A";
    case ToolbarIconKind::Confirm:
      return L"\x5B8C\x6210";
    case ToolbarIconKind::Cancel:
      return L"\x53D6\x6D88";
    case ToolbarIconKind::Move:
      return L"\x79FB\x52A8";
    case ToolbarIconKind::Eyedropper:
      return L"\x53D6\x8272\x5668";
    case ToolbarIconKind::LongShot:
      return L"\x957F\x622A\x56FE";
    case ToolbarIconKind::Pause:
      return L"\x6682\x505C";
    case ToolbarIconKind::Resume:
      return L"\x7EE7\x7EED";
    case ToolbarIconKind::Stop:
      return L"\x505C\x6B62";
    default:
      return L"";
  }
}

const wchar_t* toolbarStrokePresetLabel(int index)
{
  switch (index)
  {
    case 0:
      return L"\x7EC6";
    case 1:
      return L"\x4E2D";
    case 2:
      return L"\x7C97";
    default:
      return L"";
  }
}

const wchar_t* toolbarColorPresetLabel(int index)
{
  switch (index)
  {
    case 0:
      return L"\x7EA2";
    case 1:
      return L"\x6A59";
    case 2:
      return L"\x9EC4";
    case 3:
      return L"\x7EFF";
    case 4:
      return L"\x9752";
    case 5:
      return L"\x84DD";
    case 6:
      return L"\x7D2B";
    case 7:
      return L"\x767D";
    default:
      return L"";
  }
}

const wchar_t* toolbarLineStyleLabel(int index)
{
  switch (index)
  {
    case 0:
      return L"\x5B9E\x7EBF";
    case 1:
      return L"\x865A\x7EBF";
    case 2:
      return L"\x70B9\x7EBF";
    case 3:
      return L"\x70B9\x5212\x7EBF";
    case 4:
      return L"\x53CC\x70B9\x5212\x7EBF";
    default:
      return L"";
  }
}

const wchar_t* toolbarArrowStyleLabel(int index)
{
  switch (index)
  {
    case 0:
      return L"\x672B\x7AEF\x7A7A\x5FC3\x7BAD\x5934";
    case 1:
      return L"\x8D77\x59CB\x7A7A\x5FC3\x7BAD\x5934";
    case 2:
      return L"\x53CC\x7AEF\x7A7A\x5FC3\x7BAD\x5934";
    case 3:
      return L"\x672B\x7AEF\x5B9E\x5FC3\x7BAD\x5934";
    case 4:
      return L"\x8D77\x59CB\x5B9E\x5FC3\x7BAD\x5934";
    case 5:
      return L"\x53CC\x7AEF\x5B9E\x5FC3\x7BAD\x5934";
    case 6:
      return L"\x672B\x7AEF\x7AD6\x7EBF";
    case 7:
      return L"\x8D77\x59CB\x7AD6\x7EBF";
    case 8:
      return L"\x53CC\x7AEF\x7AD6\x7EBF";
    default:
      return L"";
  }
}

int modernToolbarHeight(const ModernToolbarMetrics& metrics)
{
  return metrics.item_size + metrics.bar_padding * 2;
}

int modernToolbarWidth(int item_count, int extra_width,
                       const ModernToolbarMetrics& metrics)
{
  if (item_count < 0)
  {
    item_count = 0;
  }
  const int gaps = item_count > 0 ? item_count - 1 : 0;
  return metrics.bar_padding * 2 + item_count * metrics.item_size +
         gaps * metrics.gap + extra_width;
}

}  // namespace qingying
