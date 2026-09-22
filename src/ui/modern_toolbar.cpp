#include "qingying/ui/modern_toolbar.hpp"

#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <cwchar>

#include <commctrl.h>
#include <objidl.h>
#include <gdiplus.h>

namespace qingying {

namespace {

class PenGuard
{
 public:
  PenGuard(HDC hdc, int width, COLORREF color) : m_hdc(hdc)
  {
    LOGBRUSH brush{};
    brush.lbStyle = BS_SOLID;
    brush.lbColor = color;
    m_pen = ExtCreatePen(PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_SQUARE |
                             PS_JOIN_MITER,
                         (std::max)(1, width), &brush, 0, nullptr);
    if (m_pen != nullptr)
    {
      m_old = SelectObject(m_hdc, m_pen);
    }
  }

  ~PenGuard()
  {
    if (m_pen != nullptr)
    {
      SelectObject(m_hdc, m_old);
      DeleteObject(m_pen);
    }
  }

  PenGuard(const PenGuard&) = delete;
  PenGuard& operator=(const PenGuard&) = delete;

  bool ok() const
  {
    return m_pen != nullptr;
  }

 private:
  HDC m_hdc{nullptr};
  HPEN m_pen{nullptr};
  HGDIOBJ m_old{nullptr};
};

int iconHalfExtent(const RECT& cell)
{
  const int w = cell.right - cell.left;
  const int h = cell.bottom - cell.top;
  const int cell_size = (std::min)(w, h);
  return (std::max)(7, (cell_size * 9) / 28);
}

Gdiplus::Color toGdiplus(COLORREF color)
{
  return Gdiplus::Color(255, GetRValue(color), GetGValue(color),
                        GetBValue(color));
}

class GdiplusOnce
{
 public:
  GdiplusOnce()
  {
    Gdiplus::GdiplusStartupInput input;
    m_ok = (Gdiplus::GdiplusStartup(&m_token, &input, nullptr) == Gdiplus::Ok);
  }

  GdiplusOnce(const GdiplusOnce&) = delete;
  GdiplusOnce& operator=(const GdiplusOnce&) = delete;

  bool ok() const
  {
    return m_ok;
  }

 private:
  ULONG_PTR m_token{0};
  bool m_ok{false};
};

bool ensureGdiplus()
{
  static GdiplusOnce session;
  return session.ok();
}

constexpr float kIconStrokePx = 1.6f;
constexpr float kIconConfirmStrokePx = 2.0f;
constexpr bool kUseSvgPenIcon = true;
constexpr bool kUseSvgGeometryIcon = true;
constexpr bool kUseSvgEditIcon = true;
constexpr bool kUseSvgUndoIcon = true;
constexpr bool kUseSvgTextIcon = true;
constexpr bool kUseSvgMosaicIcon = true;
constexpr bool kUseSvgStrokeWidthIcon = true;
constexpr bool kUseSvgPinIcon = true;
constexpr bool kUseSvgMoveIcon = true;
constexpr bool kUseSvgEyedropperIcon = true;
constexpr int kSvgFigureMaxPoints = 192;

// 由 qrc/pencil-minus.svg 路径展平得到。kUseSvgPenIcon 设为 false 即可退回旧画笔。
const Gdiplus::PointF kPenSvgPoints[] = {
    {786.240f, 320.000f}, {783.927f, 300.814f}, {777.208f, 282.695f},
    {766.452f, 266.641f}, {752.251f, 253.534f}, {735.387f, 244.097f},
    {716.789f, 238.848f}, {697.480f, 238.078f}, {678.522f, 241.827f},
    {660.960f, 249.891f}, {645.760f, 261.824f}, {209.024f, 698.624f},
    {209.024f, 814.976f}, {325.376f, 814.976f}, {762.112f, 378.240f},
    {766.502f, 373.491f}, {770.506f, 368.412f}, {774.100f, 363.035f},
    {777.260f, 357.393f}, {779.968f, 351.520f}, {782.207f, 345.452f},
    {783.963f, 339.228f}, {785.225f, 332.885f}, {785.986f, 326.462f},
    {786.240f, 320.000f}, {863.040f, 320.000f}, {862.547f, 332.483f},
    {861.077f, 344.889f}, {858.639f, 357.141f}, {855.248f, 369.165f},
    {850.925f, 380.886f}, {845.696f, 392.232f}, {839.594f, 403.133f},
    {832.656f, 413.522f}, {824.925f, 423.335f}, {816.448f, 432.512f},
    {368.448f, 880.512f}, {366.236f, 882.559f}, {363.870f, 884.427f},
    {361.365f, 886.103f}, {358.736f, 887.578f}, {356.000f, 888.842f},
    {353.173f, 889.887f}, {350.272f, 890.708f}, {347.317f, 891.298f},
    {344.324f, 891.655f}, {341.312f, 891.776f}, {170.624f, 891.776f},
    {164.617f, 891.303f}, {158.758f, 889.897f}, {153.191f, 887.591f},
    {148.053f, 884.442f}, {143.471f, 880.529f}, {139.558f, 875.947f},
    {136.409f, 870.809f}, {134.103f, 865.242f}, {132.697f, 859.383f},
    {132.224f, 853.376f}, {132.224f, 682.752f}, {132.414f, 678.937f},
    {132.976f, 675.190f}, {133.898f, 671.536f}, {135.168f, 668.000f},
    {136.774f, 664.608f}, {138.704f, 661.386f}, {140.946f, 658.359f},
    {143.488f, 655.552f}, {591.488f, 207.552f}, {620.847f, 184.386f},
    {654.804f, 168.717f}, {691.482f, 161.410f}, {728.851f, 162.870f},
    {764.847f, 173.016f}, {797.478f, 191.288f}, {824.940f, 216.674f},
    {845.715f, 247.771f}, {858.653f, 282.860f}, {863.040f, 320.000f},
    {576.000f, 238.980f}, {572.988f, 239.098f}, {569.994f, 239.453f},
    {567.037f, 240.042f}, {564.136f, 240.862f}, {561.308f, 241.907f},
    {558.571f, 243.170f}, {555.942f, 244.645f}, {553.437f, 246.322f},
    {551.071f, 248.191f}, {548.860f, 250.240f}, {544.944f, 254.817f},
    {541.793f, 259.950f}, {539.485f, 265.514f}, {538.077f, 271.370f},
    {537.604f, 277.375f}, {538.077f, 283.380f}, {539.485f, 289.236f},
    {541.793f, 294.800f}, {544.944f, 299.933f}, {548.860f, 304.510f},
    {719.490f, 475.200f}, {729.217f, 482.185f}, {740.621f, 485.836f},
    {752.596f, 485.798f}, {763.978f, 482.076f}, {773.661f, 475.031f},
    {780.704f, 465.347f}, {784.424f, 453.964f}, {784.460f, 441.989f},
    {780.807f, 430.585f}, {773.820f, 420.860f}, {603.140f, 250.240f},
    {600.929f, 248.191f}, {598.563f, 246.322f}, {596.058f, 244.645f},
    {593.429f, 243.170f}, {590.692f, 241.907f}, {587.864f, 240.862f},
    {584.963f, 240.042f}, {582.006f, 239.453f}, {579.012f, 239.098f},
    {576.000f, 238.980f},
};
constexpr int kPenSvgFigureCounts[] = {25, 53, 43};
constexpr int kPenSvgOutlineBegin = 0;
constexpr int kPenSvgOutlineEnd = 2;
constexpr int kPenSvgFacetBegin = 2;
constexpr int kPenSvgFacetEnd = 3;
constexpr float kPenSvgMinX = 132.224f;
constexpr float kPenSvgMinY = 161.410f;
constexpr float kPenSvgMaxX = 863.040f;
constexpr float kPenSvgMaxY = 891.776f;

// 由 qrc/Checkbox.svg 路径展平得到。kUseSvgGeometryIcon 设为 false 即可退回旧几何图标。
const Gdiplus::PointF kGeometrySvgPoints[] = {
    {768.000f, 819.200f}, {768.000f, 896.000f}, {256.000f, 896.000f},
    {256.000f, 819.200f}, {768.000f, 819.200f}, {819.200f, 768.000f},
    {819.200f, 256.000f}, {818.570f, 247.991f}, {816.694f, 240.178f},
    {813.620f, 232.756f}, {809.422f, 225.905f}, {804.204f, 219.796f},
    {798.095f, 214.578f}, {791.244f, 210.380f}, {783.822f, 207.306f},
    {776.009f, 205.430f}, {768.000f, 204.800f}, {256.000f, 204.800f},
    {247.991f, 205.430f}, {240.178f, 207.306f}, {232.756f, 210.380f},
    {225.905f, 214.578f}, {219.796f, 219.796f}, {214.578f, 225.905f},
    {210.380f, 232.756f}, {207.306f, 240.178f}, {205.430f, 247.991f},
    {204.800f, 256.000f}, {204.800f, 768.000f}, {205.430f, 776.009f},
    {207.306f, 783.822f}, {210.380f, 791.244f}, {214.578f, 798.095f},
    {219.796f, 804.204f}, {225.905f, 809.422f}, {232.756f, 813.620f},
    {240.178f, 816.694f}, {247.991f, 818.570f}, {256.000f, 819.200f},
    {256.000f, 896.000f}, {235.976f, 894.424f}, {216.446f, 889.735f},
    {197.889f, 882.049f}, {180.763f, 871.554f}, {165.490f, 858.510f},
    {152.446f, 843.237f}, {141.951f, 826.111f}, {134.265f, 807.554f},
    {129.576f, 788.024f}, {128.000f, 768.000f}, {128.000f, 256.000f},
    {129.576f, 235.976f}, {134.265f, 216.446f}, {141.951f, 197.889f},
    {152.446f, 180.763f}, {165.490f, 165.490f}, {180.763f, 152.446f},
    {197.889f, 141.951f}, {216.446f, 134.265f}, {235.976f, 129.576f},
    {256.000f, 128.000f}, {768.000f, 128.000f}, {788.024f, 129.576f},
    {807.554f, 134.265f}, {826.111f, 141.951f}, {843.237f, 152.446f},
    {858.510f, 165.490f}, {871.554f, 180.763f}, {882.049f, 197.889f},
    {889.735f, 216.446f}, {894.424f, 235.976f}, {896.000f, 256.000f},
    {896.000f, 768.000f}, {894.424f, 788.024f}, {889.735f, 807.554f},
    {882.049f, 826.111f}, {871.554f, 843.237f}, {858.510f, 858.510f},
    {843.237f, 871.554f}, {826.111f, 882.049f}, {807.554f, 889.735f},
    {788.024f, 894.424f}, {768.000f, 896.000f}, {768.000f, 819.200f},
    {776.009f, 818.570f}, {783.822f, 816.694f}, {791.244f, 813.620f},
    {798.095f, 809.422f}, {804.204f, 804.204f}, {809.422f, 798.095f},
    {813.620f, 791.244f}, {816.694f, 783.822f}, {818.570f, 776.009f},
    {819.200f, 768.000f},
};
constexpr int kGeometrySvgFigureCounts[] = {5, 89};
constexpr float kGeometrySvgMinX = 128.0f;
constexpr float kGeometrySvgMinY = 128.0f;
constexpr float kGeometrySvgMaxX = 896.0f;
constexpr float kGeometrySvgMaxY = 896.0f;

// 由 qrc/edit.svg 路径展平得到。kUseSvgEditIcon 设为 false 即可退回旧编辑图标。
const Gdiplus::PointF kEditSvgPoints[] = {
    {132.288f, 789.376f}, {132.288f, 362.688f}, {140.083f, 323.501f},
    {162.280f, 290.280f}, {195.501f, 268.083f}, {234.688f, 260.288f},
    {298.688f, 260.288f}, {313.383f, 263.211f}, {325.841f, 271.535f},
    {334.165f, 283.993f}, {337.088f, 298.688f}, {334.165f, 313.383f},
    {325.841f, 325.841f}, {313.383f, 334.165f}, {298.688f, 337.088f},
    {234.688f, 337.088f}, {226.777f, 338.341f}, {219.641f, 341.977f},
    {213.977f, 347.641f}, {210.341f, 354.777f}, {209.088f, 362.688f},
    {209.088f, 789.376f}, {209.423f, 793.514f}, {210.393f, 797.445f},
    {211.944f, 801.115f}, {214.026f, 804.470f}, {216.584f, 807.456f},
    {219.567f, 810.020f}, {222.921f, 812.108f}, {226.594f, 813.665f},
    {230.534f, 814.639f}, {234.688f, 814.976f}, {661.376f, 814.976f},
    {669.287f, 813.723f}, {676.423f, 810.087f}, {682.087f, 804.423f},
    {685.723f, 797.287f}, {686.976f, 789.376f}, {686.976f, 725.376f},
    {690.510f, 711.351f}, {698.974f, 699.623f}, {711.171f, 691.849f},
    {725.376f, 689.130f}, {739.581f, 691.849f}, {751.778f, 699.623f},
    {760.242f, 711.351f}, {763.776f, 725.376f}, {763.776f, 789.376f},
    {755.981f, 828.563f}, {733.784f, 861.784f}, {700.563f, 883.981f},
    {661.376f, 891.776f}, {234.688f, 891.776f}, {195.501f, 883.981f},
    {162.280f, 861.784f}, {140.083f, 828.563f}, {132.288f, 789.376f},
    {857.600f, 217.600f}, {853.626f, 198.062f}, {842.507f, 181.513f},
    {825.921f, 170.448f}, {806.370f, 166.539f}, {786.804f, 170.375f},
    {770.176f, 181.376f}, {770.176f, 181.440f}, {422.400f, 527.936f},
    {422.400f, 601.600f}, {496.000f, 601.600f}, {842.560f, 253.888f},
    {842.624f, 253.824f}, {848.989f, 246.059f}, {853.716f, 237.202f},
    {856.623f, 227.592f}, {857.600f, 217.600f}, {934.400f, 217.600f},
    {931.959f, 242.581f}, {924.689f, 266.605f}, {912.872f, 288.748f},
    {896.960f, 308.160f}, {896.896f, 308.096f}, {539.136f, 667.136f},
    {533.322f, 671.914f}, {526.688f, 675.466f}, {519.488f, 677.656f},
    {512.000f, 678.400f}, {384.000f, 678.400f}, {369.305f, 675.477f},
    {356.847f, 667.153f}, {348.523f, 654.695f}, {345.600f, 640.000f},
    {345.600f, 512.000f}, {346.335f, 504.495f}, {348.522f, 497.278f},
    {352.077f, 490.627f}, {356.864f, 484.800f}, {715.904f, 127.104f},
    {757.427f, 99.366f}, {806.404f, 89.627f}, {855.380f, 99.369f},
    {896.901f, 127.109f}, {924.649f, 168.626f}, {934.400f, 217.600f},
    {655.552f, 186.240f}, {665.262f, 179.173f}, {676.683f, 175.456f},
    {688.693f, 175.456f}, {700.114f, 179.173f}, {709.824f, 186.240f},
    {837.824f, 314.240f}, {846.131f, 326.688f}, {849.041f, 341.367f},
    {846.113f, 356.042f}, {837.792f, 368.480f}, {825.344f, 376.787f},
    {810.665f, 379.697f}, {795.990f, 376.769f}, {783.552f, 368.448f},
    {655.552f, 240.448f}, {648.485f, 230.738f}, {644.768f, 219.317f},
    {644.768f, 207.307f}, {648.485f, 195.886f}, {655.552f, 186.176f},
};
constexpr int kEditSvgFigureCounts[] = {57, 17, 28, 21};
constexpr int kEditSvgPageBegin = 0;
constexpr int kEditSvgPageEnd = 1;
constexpr int kEditSvgPencilBegin = 1;
constexpr int kEditSvgPencilEnd = 3;
constexpr int kEditSvgSlashBegin = 3;
constexpr int kEditSvgSlashEnd = 4;
constexpr float kEditSvgMinX = 132.288f;
constexpr float kEditSvgMinY = 89.627f;
constexpr float kEditSvgMaxX = 934.400f;
constexpr float kEditSvgMaxY = 891.776f;

// 由 qrc/撤回色块.svg 路径展平得到。kUseSvgUndoIcon 设为 false 即可退回旧撤销图标。
const Gdiplus::PointF kUndoSvgPoints[] = {
    {64.000f, 347.552f}, {320.000f, 128.000f}, {320.000f, 576.000f},
    {265.472f, 896.000f}, {265.472f, 784.000f}, {643.296f, 784.000f},
    {719.833f, 768.776f}, {784.717f, 725.421f}, {828.072f, 660.537f},
    {843.296f, 584.000f}, {828.072f, 507.463f}, {784.717f, 442.579f},
    {719.833f, 399.224f}, {643.296f, 384.000f}, {240.000f, 384.000f},
    {240.000f, 272.000f}, {643.296f, 272.000f}, {693.906f, 276.083f},
    {741.915f, 287.905f}, {786.681f, 306.824f}, {827.562f, 332.196f},
    {863.916f, 363.380f}, {895.100f, 399.734f}, {920.472f, 440.615f},
    {939.391f, 485.381f}, {951.213f, 533.390f}, {955.296f, 584.000f},
    {951.213f, 634.610f}, {939.391f, 682.619f}, {920.472f, 727.385f},
    {895.100f, 768.266f}, {863.916f, 804.620f}, {827.562f, 835.804f},
    {786.681f, 861.176f}, {741.915f, 880.095f}, {693.906f, 891.917f},
    {643.296f, 896.000f}, {265.472f, 896.000f},
};
constexpr int kUndoSvgFigureCounts[] = {3, 35};
constexpr int kUndoSvgArrowBegin = 0;
constexpr int kUndoSvgArrowEnd = 1;
constexpr int kUndoSvgBodyBegin = 1;
constexpr float kUndoSvgMinX = 64.000f;
constexpr float kUndoSvgMinY = 128.000f;
constexpr float kUndoSvgMaxX = 955.296f;
constexpr float kUndoSvgMaxY = 896.000f;

// 由 qrc/文字.svg 路径展平得到。kUseSvgTextIcon 设为 false 即可退回旧文字图标。
const Gdiplus::PointF kTextSvgPoints[] = {
    {64.100f, 66.000f}, {64.100f, 369.700f}, {103.600f, 368.400f},
    {109.828f, 336.244f}, {119.898f, 306.366f}, {133.125f, 278.935f},
    {148.829f, 254.115f}, {166.325f, 232.075f}, {184.931f, 212.981f},
    {203.965f, 196.999f}, {222.742f, 184.298f}, {240.582f, 175.042f},
    {256.800f, 169.400f}, {273.273f, 166.369f}, {291.692f, 164.676f},
    {311.191f, 164.081f}, {330.904f, 164.344f}, {349.962f, 165.225f},
    {367.500f, 166.484f}, {382.649f, 167.881f}, {394.544f, 170.129f},
    {402.316f, 170.500f}, {405.100f, 170.500f}, {405.064f, 188.050f},
    {404.965f, 235.873f}, {404.819f, 306.731f}, {404.642f, 393.386f},
    {404.450f, 488.600f}, {404.258f, 585.134f}, {404.081f, 675.749f},
    {403.935f, 753.207f}, {403.836f, 810.270f}, {403.800f, 839.700f},
    {402.140f, 852.180f}, {397.633f, 863.186f}, {390.991f, 872.756f},
    {382.926f, 880.927f}, {374.150f, 887.738f}, {365.374f, 893.225f},
    {357.309f, 897.427f}, {350.667f, 900.382f}, {346.160f, 902.127f},
    {344.500f, 902.700f}, {265.500f, 904.000f}, {264.300f, 959.400f},
    {757.400f, 959.400f}, {757.400f, 905.300f}, {667.200f, 905.300f},
    {655.454f, 902.789f}, {644.731f, 897.944f}, {635.081f, 891.377f},
    {626.554f, 883.700f}, {619.200f, 875.525f}, {613.070f, 867.464f},
    {608.215f, 860.129f}, {604.685f, 854.132f}, {602.530f, 850.085f},
    {601.800f, 848.600f}, {602.900f, 164.100f}, {604.719f, 163.800f},
    {610.039f, 163.056f}, {618.652f, 162.102f}, {630.350f, 161.172f},
    {644.925f, 160.500f}, {662.170f, 160.320f}, {681.878f, 160.866f},
    {703.841f, 162.372f}, {727.851f, 165.072f}, {753.700f, 169.200f},
    {779.927f, 178.900f}, {804.963f, 196.724f}, {828.438f, 220.518f},
    {849.982f, 248.128f}, {869.225f, 277.400f}, {885.798f, 306.180f},
    {899.332f, 332.314f}, {909.457f, 353.648f}, {915.803f, 368.028f},
    {918.000f, 373.300f}, {957.500f, 373.300f}, {957.500f, 66.000f},
    {64.100f, 66.000f},
};
constexpr int kTextSvgFigureCounts[] = {82};
constexpr float kTextSvgMinX = 64.100f;
constexpr float kTextSvgMinY = 66.000f;
constexpr float kTextSvgMaxX = 957.500f;
constexpr float kTextSvgMaxY = 959.400f;

// 由 qrc/马赛克.svg 路径展平得到。kUseSvgMosaicIcon 设为 false 即可退回旧马赛克图标。
const Gdiplus::PointF kMosaicSvgPoints[] = {
    {0.000f, 0.000f}, {0.000f, 1024.000f}, {1024.000f, 1024.000f},
    {1024.000f, 0.000f}, {0.000f, 0.000f}, {672.160f, 989.856f},
    {672.160f, 673.504f}, {352.192f, 673.504f}, {352.192f, 989.856f},
    {34.144f, 989.856f}, {34.144f, 672.192f}, {350.944f, 672.192f},
    {350.944f, 352.192f}, {34.144f, 352.192f}, {34.144f, 34.112f},
    {352.192f, 34.112f}, {352.192f, 350.880f}, {672.160f, 350.880f},
    {672.160f, 34.144f}, {989.856f, 34.144f}, {989.856f, 352.224f},
    {672.096f, 352.224f}, {672.096f, 672.224f}, {989.856f, 672.224f},
    {989.856f, 989.888f}, {672.160f, 989.888f}, {672.160f, 989.856f},
};
constexpr int kMosaicSvgFigureCounts[] = {5, 22};
constexpr float kMosaicSvgMinX = 0.000f;
constexpr float kMosaicSvgMinY = 0.000f;
constexpr float kMosaicSvgMaxX = 1024.000f;
constexpr float kMosaicSvgMaxY = 1024.000f;

// 由 qrc/粗细.svg 路径展平得到。kUseSvgStrokeWidthIcon 设为 false 即可退回旧粗细图标。
const Gdiplus::PointF kStrokeWidthSvgPoints[] = {
    {128.000f, 725.333f}, {896.000f, 725.333f}, {896.000f, 640.000f},
    {128.000f, 640.000f}, {128.000f, 725.333f}, {128.000f, 853.333f},
    {896.000f, 853.333f}, {896.000f, 810.667f}, {128.000f, 810.667f},
    {128.000f, 853.333f}, {128.000f, 554.667f}, {896.000f, 554.667f},
    {896.000f, 426.667f}, {128.000f, 426.667f}, {128.000f, 554.667f},
    {128.000f, 170.667f}, {128.000f, 341.333f}, {896.000f, 341.333f},
    {896.000f, 170.667f}, {128.000f, 170.667f},
};
constexpr int kStrokeWidthSvgFigureCounts[] = {5, 5, 5, 5};
constexpr float kStrokeWidthSvgMinX = 128.000f;
constexpr float kStrokeWidthSvgMinY = 170.667f;
constexpr float kStrokeWidthSvgMaxX = 896.000f;
constexpr float kStrokeWidthSvgMaxY = 853.333f;

// 由 qrc/钉.svg 路径展平得到。kUseSvgPinIcon 设为 false 即可退回旧钉图图标。
const Gdiplus::PointF kPinSvgPoints[] = {
    {715.737f, 64.315f}, {933.140f, 281.954f}, {945.578f, 296.540f},
    {955.581f, 312.893f}, {962.900f, 330.610f}, {967.356f, 349.254f},
    {968.839f, 368.366f}, {967.313f, 387.474f}, {962.814f, 406.108f},
    {955.454f, 423.808f}, {945.415f, 440.138f}, {932.943f, 454.695f},
    {753.388f, 633.620f}, {753.782f, 634.644f}, {763.927f, 671.470f},
    {768.289f, 708.303f}, {767.236f, 745.005f}, {761.137f, 781.435f},
    {750.360f, 817.453f}, {735.273f, 852.920f}, {716.244f, 887.695f},
    {693.642f, 921.639f}, {686.828f, 930.855f}, {677.363f, 941.425f},
    {666.232f, 950.223f}, {653.762f, 956.991f}, {640.319f, 961.531f},
    {626.299f, 963.708f}, {612.113f, 963.460f}, {598.178f, 960.793f},
    {584.902f, 955.785f}, {572.677f, 948.584f}, {561.861f, 939.402f},
    {345.718f, 722.983f}, {96.414f, 972.327f}, {85.445f, 980.296f},
    {72.551f, 984.486f}, {58.993f, 984.486f}, {46.099f, 980.296f},
    {35.131f, 972.327f}, {27.162f, 961.359f}, {22.973f, 948.465f},
    {22.973f, 934.907f}, {27.162f, 922.013f}, {35.131f, 911.045f},
    {284.475f, 661.662f}, {56.438f, 433.428f}, {47.424f, 422.854f},
    {40.304f, 410.922f}, {35.281f, 397.968f}, {32.495f, 384.355f},
    {32.025f, 370.469f}, {33.884f, 356.699f}, {38.019f, 343.435f},
    {44.315f, 331.048f}, {52.593f, 319.889f}, {62.622f, 310.272f},
    {100.136f, 283.187f}, {137.872f, 262.066f}, {175.632f, 246.587f},
    {213.218f, 236.431f}, {250.432f, 231.275f}, {287.073f, 230.801f},
    {322.945f, 234.685f}, {357.849f, 242.609f}, {362.023f, 243.870f},
    {543.350f, 63.961f}, {557.935f, 51.598f}, {574.271f, 41.664f},
    {591.957f, 34.402f}, {610.561f, 29.990f}, {629.625f, 28.536f},
    {648.682f, 30.077f}, {667.265f, 34.574f}, {684.918f, 41.916f},
    {701.209f, 51.925f}, {715.737f, 64.354f}, {715.737f, 64.315f},
    {604.396f, 125.440f}, {403.495f, 324.923f}, {399.435f, 328.485f},
    {394.962f, 331.514f}, {390.147f, 333.963f}, {385.065f, 335.794f},
    {379.795f, 336.978f}, {374.418f, 337.497f}, {369.018f, 337.343f},
    {363.680f, 336.519f}, {358.486f, 335.036f}, {353.516f, 332.918f},
    {351.616f, 332.082f}, {349.702f, 331.276f}, {347.776f, 330.501f},
    {345.838f, 329.757f}, {343.888f, 329.043f}, {341.927f, 328.361f},
    {339.955f, 327.710f}, {337.973f, 327.091f}, {335.982f, 326.503f},
    {333.982f, 325.947f}, {309.774f, 320.396f}, {284.910f, 317.546f},
    {259.471f, 317.631f}, {233.541f, 320.886f}, {207.201f, 327.546f},
    {180.533f, 337.844f}, {153.620f, 352.015f}, {126.543f, 370.294f},
    {120.517f, 375.020f}, {620.190f, 875.244f}, {624.010f, 870.124f},
    {642.004f, 842.864f}, {656.913f, 815.295f}, {668.479f, 787.561f},
    {676.446f, 759.808f}, {680.556f, 732.180f}, {680.552f, 704.823f},
    {676.178f, 677.882f}, {667.175f, 651.500f}, {663.276f, 643.190f},
    {661.151f, 638.200f}, {659.666f, 632.983f}, {658.845f, 627.621f},
    {658.700f, 622.199f}, {659.234f, 616.802f}, {660.438f, 611.513f},
    {662.293f, 606.416f}, {664.771f, 601.591f}, {667.832f, 597.114f},
    {671.429f, 593.054f}, {871.778f, 393.374f}, {875.399f, 389.149f},
    {878.314f, 384.411f}, {880.451f, 379.274f}, {881.759f, 373.867f},
    {882.204f, 368.322f}, {881.776f, 362.775f}, {880.486f, 357.363f},
    {878.364f, 352.220f}, {875.464f, 347.472f}, {871.857f, 343.237f},
    {654.454f, 125.598f}, {650.236f, 121.987f}, {645.506f, 119.079f},
    {640.380f, 116.946f}, {634.984f, 115.640f}, {629.450f, 115.192f},
    {623.913f, 115.614f}, {618.511f, 116.895f}, {613.375f, 119.004f},
    {608.631f, 121.889f}, {604.396f, 125.479f}, {604.396f, 125.440f},
};
constexpr int kPinSvgFigureCounts[] = {78, 75};
constexpr float kPinSvgMinX = 22.973f;
constexpr float kPinSvgMinY = 28.536f;
constexpr float kPinSvgMaxX = 968.839f;
constexpr float kPinSvgMaxY = 984.486f;

// 由 qrc/移动.svg 路径展平得到。kUseSvgMoveIcon 设为 false 即可退回旧移动图标。
const Gdiplus::PointF kMoveSvgPoints[] = {
    {1017.900f, 501.700f}, {849.200f, 365.000f}, {846.970f, 363.518f},
    {844.621f, 362.555f},  {842.213f, 362.081f}, {839.804f, 362.070f},
    {837.455f, 362.494f},  {835.225f, 363.325f}, {833.175f, 364.535f},
    {831.363f, 366.096f},  {829.850f, 367.981f}, {828.695f, 370.162f},
    {827.959f, 372.611f},  {827.700f, 375.300f}, {827.700f, 469.800f},
    {554.200f, 469.800f},  {554.200f, 196.400f}, {648.700f, 196.400f},
    {651.368f, 196.141f},  {653.803f, 195.405f}, {655.977f, 194.250f},
    {657.859f, 192.737f},  {659.423f, 190.925f}, {660.638f, 188.875f},
    {661.475f, 186.645f},  {661.907f, 184.296f}, {661.905f, 181.887f},
    {661.438f, 179.479f},  {660.480f, 177.130f}, {659.000f, 174.900f},
    {522.300f, 6.100f},    {520.884f, 4.610f},   {519.309f, 3.392f},
    {517.606f, 2.444f},    {515.807f, 1.767f},   {513.945f, 1.360f},
    {512.050f, 1.225f},    {510.155f, 1.360f},   {508.293f, 1.767f},
    {506.494f, 2.444f},    {504.791f, 3.392f},   {503.216f, 4.610f},
    {501.800f, 6.100f},    {365.000f, 174.800f}, {363.518f, 177.030f},
    {362.555f, 179.379f},  {362.081f, 181.788f}, {362.070f, 184.196f},
    {362.494f, 186.545f},  {363.325f, 188.775f}, {364.535f, 190.825f},
    {366.096f, 192.637f},  {367.981f, 194.150f}, {370.162f, 195.305f},
    {372.611f, 196.041f},  {375.300f, 196.300f}, {469.800f, 196.300f},
    {469.800f, 469.700f},  {196.400f, 469.700f}, {196.400f, 375.200f},
    {196.141f, 372.532f},  {195.404f, 370.097f}, {194.248f, 367.923f},
    {192.733f, 366.041f},  {190.918f, 364.477f}, {188.862f, 363.262f},
    {186.626f, 362.425f},  {184.267f, 361.993f}, {181.845f, 361.995f},
    {179.421f, 362.462f},  {177.053f, 363.420f}, {174.800f, 364.900f},
    {6.100f, 501.700f},    {4.610f, 503.116f},   {3.392f, 504.691f},
    {2.444f, 506.394f},    {1.767f, 508.193f},   {1.360f, 510.055f},
    {1.225f, 511.950f},    {1.360f, 513.845f},   {1.767f, 515.707f},
    {2.444f, 517.506f},    {3.392f, 519.209f},   {4.610f, 520.784f},
    {6.100f, 522.200f},    {174.800f, 659.000f}, {177.032f, 660.482f},
    {179.386f, 661.445f},  {181.803f, 661.919f}, {184.222f, 661.930f},
    {186.583f, 661.506f},  {188.825f, 660.675f}, {190.888f, 659.465f},
    {192.711f, 657.904f},  {194.234f, 656.019f}, {195.397f, 653.838f},
    {196.139f, 651.389f},  {196.400f, 648.700f}, {196.400f, 554.200f},
    {469.800f, 554.200f},  {469.800f, 827.600f}, {375.300f, 827.600f},
    {372.632f, 827.859f},  {370.197f, 828.595f}, {368.023f, 829.750f},
    {366.141f, 831.263f},  {364.577f, 833.075f}, {363.362f, 835.125f},
    {362.525f, 837.355f},  {362.093f, 839.704f}, {362.095f, 842.112f},
    {362.562f, 844.521f},  {363.520f, 846.870f}, {365.000f, 849.100f},
    {501.800f, 1017.800f}, {503.216f, 1019.290f}, {504.791f, 1020.508f},
    {506.494f, 1021.456f}, {508.293f, 1022.133f}, {510.155f, 1022.540f},
    {512.050f, 1022.675f}, {513.945f, 1022.540f}, {515.807f, 1022.133f},
    {517.606f, 1021.456f}, {519.309f, 1020.508f}, {520.884f, 1019.290f},
    {522.300f, 1017.800f}, {659.000f, 849.200f}, {660.480f, 846.970f},
    {661.438f, 844.621f},  {661.905f, 842.213f}, {661.907f, 839.804f},
    {661.475f, 837.455f},  {660.637f, 835.225f}, {659.423f, 833.175f},
    {657.859f, 831.363f},  {655.977f, 829.850f}, {653.803f, 828.695f},
    {651.368f, 827.959f},  {648.700f, 827.700f}, {554.200f, 827.700f},
    {554.200f, 554.200f},  {827.600f, 554.200f}, {827.600f, 648.700f},
    {827.859f, 651.368f},  {828.595f, 653.803f}, {829.750f, 655.977f},
    {831.263f, 657.859f},  {833.075f, 659.423f}, {835.125f, 660.638f},
    {837.355f, 661.475f},  {839.704f, 661.907f}, {842.112f, 661.905f},
    {844.521f, 661.438f},  {846.870f, 660.480f}, {849.100f, 659.000f},
    {1017.800f, 522.200f}, {1019.313f, 520.805f}, {1020.550f, 519.244f},
    {1021.514f, 517.548f}, {1022.204f, 515.752f}, {1022.620f, 513.887f},
    {1022.762f, 511.987f}, {1022.632f, 510.086f}, {1022.230f, 508.215f},
    {1021.555f, 506.408f}, {1020.608f, 504.698f}, {1019.390f, 503.117f},
    {1017.900f, 501.700f},
};
constexpr int kMoveSvgFigureCounts[] = {169};
constexpr float kMoveSvgMinX = 1.225f;
constexpr float kMoveSvgMinY = 1.225f;
constexpr float kMoveSvgMaxX = 1022.762f;
constexpr float kMoveSvgMaxY = 1022.675f;

// 由 qrc/取色器.svg 路径展平得到。kUseSvgEyedropperIcon 设为 false 即可退回旧吸管。
const Gdiplus::PointF kEyedropperSvgPoints[] = {
    {1017.640f, 165.366f}, {1017.056f, 155.839f}, {1015.354f, 146.385f},
    {1012.609f, 137.081f}, {1008.894f, 128.000f}, {1004.286f, 119.217f},
    {998.857f, 110.807f}, {992.683f, 102.845f}, {985.839f, 95.404f},
    {922.236f, 31.801f}, {906.832f, 19.280f}, {889.640f, 10.335f},
    {871.255f, 4.969f}, {852.273f, 3.180f}, {833.292f, 4.969f},
    {814.907f, 10.335f}, {797.714f, 19.280f}, {782.311f, 31.801f},
    {642.385f, 178.087f}, {572.422f, 108.124f}, {432.497f, 248.050f},
    {489.739f, 305.292f}, {101.764f, 686.907f}, {92.807f, 696.745f},
    {84.969f, 707.180f}, {78.174f, 718.211f}, {72.348f, 729.839f},
    {67.416f, 742.062f}, {63.304f, 754.882f}, {59.938f, 768.298f},
    {57.242f, 782.311f}, {31.801f, 807.752f}, {15.106f, 827.925f},
    {3.180f, 849.888f}, {-3.975f, 873.043f}, {-6.360f, 896.795f},
    {-3.975f, 920.547f}, {3.180f, 943.702f}, {15.106f, 965.665f},
    {31.801f, 985.839f}, {51.975f, 1002.534f}, {73.938f, 1014.460f},
    {97.093f, 1021.615f}, {120.845f, 1024.000f}, {144.596f, 1021.615f},
    {167.752f, 1014.460f}, {189.714f, 1002.534f}, {209.888f, 985.839f},
    {235.329f, 960.398f}, {249.342f, 959.528f}, {262.758f, 957.019f},
    {275.578f, 953.019f}, {287.801f, 947.677f}, {299.429f, 941.143f},
    {310.460f, 933.565f}, {320.894f, 925.093f}, {330.733f, 915.876f},
    {718.708f, 527.901f}, {775.950f, 585.143f}, {915.876f, 445.217f},
    {845.913f, 375.255f}, {985.839f, 235.329f}, {992.683f, 227.888f},
    {998.857f, 219.925f}, {1004.286f, 211.516f}, {1008.894f, 202.733f},
    {1012.609f, 193.652f}, {1015.354f, 184.348f}, {1017.056f, 174.894f},
    {1017.640f, 165.366f}, {267.130f, 852.273f}, {259.702f, 858.559f},
    {251.826f, 863.205f}, {243.652f, 866.360f}, {235.329f, 868.174f},
    {227.006f, 868.795f}, {218.832f, 868.373f}, {210.957f, 867.056f},
    {203.528f, 864.994f}, {146.286f, 922.236f}, {140.969f, 926.410f},
    {134.758f, 929.391f}, {127.950f, 931.180f}, {120.845f, 931.776f},
    {113.739f, 931.180f}, {106.932f, 929.391f}, {100.720f, 926.410f},
    {95.404f, 922.236f}, {91.230f, 916.919f}, {88.248f, 910.708f},
    {86.460f, 903.901f}, {85.863f, 896.795f}, {86.460f, 889.689f},
    {88.248f, 882.882f}, {91.230f, 876.671f}, {95.404f, 871.354f},
    {152.646f, 814.112f}, {149.019f, 806.683f}, {147.478f, 798.807f},
    {147.727f, 790.634f}, {149.466f, 782.311f}, {152.398f, 773.988f},
    {156.224f, 765.814f}, {160.646f, 757.938f}, {165.366f, 750.509f},
    {553.342f, 362.534f}, {655.106f, 464.298f}, {267.130f, 852.273f},
};
constexpr int kEyedropperSvgFigureCounts[] = {70, 38};
constexpr float kEyedropperSvgMinX = -6.360f;
constexpr float kEyedropperSvgMinY = 3.180f;
constexpr float kEyedropperSvgMaxX = 1017.640f;
constexpr float kEyedropperSvgMaxY = 1024.000f;

struct SvgGlyph
{
  const Gdiplus::PointF* points;
  int point_count;
  const int* figure_counts;
  int figure_count;
  float min_x;
  float min_y;
  float max_x;
  float max_y;
};

void drawLegacyPenIcon(Gdiplus::Graphics& graphics, Gdiplus::Pen& pen, float cx,
                       float cy, float s)
{
  const Gdiplus::PointF curve[3] = {
      {cx - s + 1.5f, cy + s - 2.5f},
      {cx - 0.5f, cy + 0.5f},
      {cx + s - 2.5f, cy - s + 2.5f},
  };
  graphics.DrawCurve(&pen, curve, 3);
  graphics.DrawLine(&pen, cx + s - 4.5f, cy - s + 1.0f, cx + s - 1.5f,
                    cy - s + 4.0f);
}

void drawLegacyGeometryIcon(Gdiplus::Graphics& graphics, Gdiplus::Pen& pen,
                            float cx, float cy, float s)
{
  graphics.DrawRectangle(&pen, cx - s + 1.0f, cy - s + 2.0f, 2.0f * s - 3.0f,
                         2.0f * s - 4.0f);
}

void drawLegacyEditIcon(Gdiplus::Graphics& graphics, Gdiplus::Pen& pen, float cx,
                        float cy, float s)
{
  graphics.DrawLine(&pen, cx - s + 1.5f, cy + s - 1.5f, cx + s - 3.0f,
                    cy - s + 3.0f);
  graphics.DrawLine(&pen, cx + s - 5.0f, cy - s + 1.5f, cx + s - 1.5f,
                    cy - s + 5.0f);
  graphics.DrawLine(&pen, cx + s - 1.5f, cy - s + 5.0f, cx + s - 3.0f,
                    cy - s + 3.0f);
  graphics.DrawLine(&pen, cx - s + 1.5f, cy + s - 1.5f, cx - s + 4.0f, cy + s);
  graphics.DrawLine(&pen, cx - s + 1.5f, cy + s - 1.5f, cx - s, cy + s - 3.0f);
}

void drawLegacyTextIcon(Gdiplus::Graphics& graphics, Gdiplus::Pen& pen, float cx,
                        float cy, float s)
{
  graphics.DrawLine(&pen, cx - s + 1.5f, cy - s + 2.5f, cx + s - 1.5f,
                    cy - s + 2.5f);
  graphics.DrawLine(&pen, cx - s + 1.5f, cy - s + 2.5f, cx - s + 1.5f,
                    cy - s + 5.5f);
  graphics.DrawLine(&pen, cx + s - 1.5f, cy - s + 2.5f, cx + s - 1.5f,
                    cy - s + 5.5f);
  graphics.DrawLine(&pen, cx, cy - s + 2.5f, cx, cy + s - 2.0f);
}

void drawLegacyUndoIcon(Gdiplus::Graphics& graphics, Gdiplus::Pen& pen, float cx,
                        float cy, float s)
{
  graphics.DrawArc(&pen, cx - s + 1.0f, cy - s + 2.0f, 2.0f * s - 2.0f,
                   2.0f * s - 3.0f, 40.0f, 230.0f);
  graphics.DrawLine(&pen, cx - s + 1.0f, cy - 0.5f, cx - 2.0f, cy - s + 3.0f);
  graphics.DrawLine(&pen, cx - s + 1.0f, cy - 0.5f, cx - 1.0f, cy + 3.5f);
}

void drawLegacyRedoIcon(Gdiplus::Graphics& graphics, Gdiplus::Pen& pen, float cx,
                        float cy, float s)
{
  graphics.DrawArc(&pen, cx - s + 1.0f, cy - s + 2.0f, 2.0f * s - 2.0f,
                   2.0f * s - 3.0f, -90.0f, 230.0f);
  graphics.DrawLine(&pen, cx + s - 1.0f, cy - 0.5f, cx + 2.0f, cy - s + 3.0f);
  graphics.DrawLine(&pen, cx + s - 1.0f, cy - 0.5f, cx + 1.0f, cy + 3.5f);
}

void drawLegacyMosaicIcon(Gdiplus::Graphics& graphics, Gdiplus::Pen& pen,
                          float cx, float cy, float s)
{
  const float tile = (std::max)(3.2f, s * 0.55f);
  const float gap = 1.4f;
  const float origin_x = cx - tile - gap * 0.5f;
  const float origin_y = cy - tile - gap * 0.5f;
  graphics.DrawRectangle(&pen, origin_x, origin_y, tile, tile);
  graphics.DrawRectangle(&pen, origin_x + tile + gap, origin_y, tile, tile);
  graphics.DrawRectangle(&pen, origin_x, origin_y + tile + gap, tile, tile);
  graphics.DrawRectangle(&pen, origin_x + tile + gap, origin_y + tile + gap,
                         tile, tile);
}

void drawLegacyStrokeWidthIcon(Gdiplus::Graphics& graphics,
                               const Gdiplus::SolidBrush& brush, float cx,
                               float cy, float s)
{
  const float half = s - 1.5f;
  const float bar_w = half * 2.0f;
  const float left = cx - half;
  const float heights[4] = {s * 0.18f, s * 0.28f, s * 0.12f, s * 0.08f};
  const float tops[4] = {cy - s + 1.2f, cy - s * 0.35f, cy + s * 0.22f,
                         cy + s - 2.4f};
  for (int i = 0; i < 4; ++i)
  {
    graphics.FillRectangle(&brush, left, tops[i], bar_w, heights[i]);
  }
}

void drawLegacyPinIcon(Gdiplus::Graphics& graphics, Gdiplus::Pen& pen, float cx,
                       float cy, float s)
{
  graphics.DrawEllipse(&pen, cx - 3.0f, cy - s, 6.0f, 6.0f);
  graphics.DrawLine(&pen, cx - s + 2.0f, cy - 2.0f, cx + s - 2.0f, cy - 2.0f);
  graphics.DrawLine(&pen, cx, cy - 2.0f, cx, cy + s);
}

void drawLegacyEyedropperIcon(Gdiplus::Graphics& graphics, Gdiplus::Pen& pen,
                              float cx, float cy, float s)
{
  graphics.DrawLine(&pen, cx - s + 2.0f, cy + s - 2.0f, cx + s - 2.0f,
                    cy - s + 2.0f);
  graphics.DrawEllipse(&pen, cx + s - 5.5f, cy - s + 0.5f, 4.5f, 4.5f);
  graphics.DrawLine(&pen, cx - s + 1.0f, cy + s - 4.5f, cx - s + 5.0f,
                    cy + s - 0.5f);
}

void drawLegacyMoveIcon(Gdiplus::Graphics& graphics, Gdiplus::Pen& pen, float cx,
                        float cy, float s)
{
  graphics.DrawLine(&pen, cx, cy - s, cx, cy + s);
  graphics.DrawLine(&pen, cx - s, cy, cx + s, cy);
  graphics.DrawLine(&pen, cx, cy - s, cx - 2.5f, cy - s + 3.5f);
  graphics.DrawLine(&pen, cx, cy - s, cx + 2.5f, cy - s + 3.5f);
  graphics.DrawLine(&pen, cx, cy + s, cx - 2.5f, cy + s - 3.5f);
  graphics.DrawLine(&pen, cx, cy + s, cx + 2.5f, cy + s - 3.5f);
  graphics.DrawLine(&pen, cx - s, cy, cx - s + 3.5f, cy - 2.5f);
  graphics.DrawLine(&pen, cx - s, cy, cx - s + 3.5f, cy + 2.5f);
  graphics.DrawLine(&pen, cx + s, cy, cx + s - 3.5f, cy - 2.5f);
  graphics.DrawLine(&pen, cx + s, cy, cx + s - 3.5f, cy + 2.5f);
}

Gdiplus::PointF mapSvgPoint(const Gdiplus::PointF& src, float min_x, float min_y,
                            float origin_x, float origin_y, float scale)
{
  Gdiplus::PointF dst;
  dst.X = origin_x + (src.X - min_x) * scale;
  dst.Y = origin_y + (src.Y - min_y) * scale;
  return dst;
}

bool fillSvgFigures(Gdiplus::Graphics& graphics, const Gdiplus::SolidBrush& brush,
                    const SvgGlyph& glyph, float origin_x, float origin_y,
                    float scale, int begin_figure, int end_figure)
{
  if (glyph.points == nullptr || glyph.figure_counts == nullptr ||
      begin_figure < 0 || end_figure > glyph.figure_count ||
      begin_figure >= end_figure)
  {
    return false;
  }

  Gdiplus::GraphicsPath path;
  path.SetFillMode(Gdiplus::FillModeAlternate);
  int offset = 0;
  for (int figure = 0; figure < glyph.figure_count; ++figure)
  {
    const int count = glyph.figure_counts[figure];
    if (count <= 1 || count > kSvgFigureMaxPoints ||
        offset > glyph.point_count - count)
    {
      return false;
    }
    if (figure >= begin_figure && figure < end_figure)
    {
      Gdiplus::PointF mapped[kSvgFigureMaxPoints];
      for (int i = 0; i < count; ++i)
      {
        mapped[i] = mapSvgPoint(glyph.points[offset + i], glyph.min_x,
                                glyph.min_y, origin_x, origin_y, scale);
      }
      if (path.AddPolygon(mapped, count) != Gdiplus::Ok)
      {
        return false;
      }
    }
    offset += count;
  }
  return graphics.FillPath(&brush, &path) == Gdiplus::Ok;
}

void layoutSvgGlyph(const SvgGlyph& glyph, float cx, float cy, float s,
                    float& origin_x, float& origin_y, float& scale)
{
  const float content_w = glyph.max_x - glyph.min_x;
  const float content_h = glyph.max_y - glyph.min_y;
  const float content = (content_w > content_h) ? content_w : content_h;
  scale = (2.0f * s) / content;
  origin_x = cx - content_w * scale * 0.5f;
  origin_y = cy - content_h * scale * 0.5f;
}

SvgGlyph makePenSvgGlyph()
{
  SvgGlyph glyph{};
  glyph.points = kPenSvgPoints;
  glyph.point_count =
      static_cast<int>(sizeof(kPenSvgPoints) / sizeof(kPenSvgPoints[0]));
  glyph.figure_counts = kPenSvgFigureCounts;
  glyph.figure_count = static_cast<int>(sizeof(kPenSvgFigureCounts) /
                                        sizeof(kPenSvgFigureCounts[0]));
  glyph.min_x = kPenSvgMinX;
  glyph.min_y = kPenSvgMinY;
  glyph.max_x = kPenSvgMaxX;
  glyph.max_y = kPenSvgMaxY;
  return glyph;
}

SvgGlyph makeGeometrySvgGlyph()
{
  SvgGlyph glyph{};
  glyph.points = kGeometrySvgPoints;
  glyph.point_count = static_cast<int>(sizeof(kGeometrySvgPoints) /
                                       sizeof(kGeometrySvgPoints[0]));
  glyph.figure_counts = kGeometrySvgFigureCounts;
  glyph.figure_count = static_cast<int>(sizeof(kGeometrySvgFigureCounts) /
                                        sizeof(kGeometrySvgFigureCounts[0]));
  glyph.min_x = kGeometrySvgMinX;
  glyph.min_y = kGeometrySvgMinY;
  glyph.max_x = kGeometrySvgMaxX;
  glyph.max_y = kGeometrySvgMaxY;
  return glyph;
}

SvgGlyph makeEditSvgGlyph()
{
  SvgGlyph glyph{};
  glyph.points = kEditSvgPoints;
  glyph.point_count =
      static_cast<int>(sizeof(kEditSvgPoints) / sizeof(kEditSvgPoints[0]));
  glyph.figure_counts = kEditSvgFigureCounts;
  glyph.figure_count = static_cast<int>(sizeof(kEditSvgFigureCounts) /
                                        sizeof(kEditSvgFigureCounts[0]));
  glyph.min_x = kEditSvgMinX;
  glyph.min_y = kEditSvgMinY;
  glyph.max_x = kEditSvgMaxX;
  glyph.max_y = kEditSvgMaxY;
  return glyph;
}

SvgGlyph makeUndoSvgGlyph()
{
  SvgGlyph glyph{};
  glyph.points = kUndoSvgPoints;
  glyph.point_count =
      static_cast<int>(sizeof(kUndoSvgPoints) / sizeof(kUndoSvgPoints[0]));
  glyph.figure_counts = kUndoSvgFigureCounts;
  glyph.figure_count = static_cast<int>(sizeof(kUndoSvgFigureCounts) /
                                        sizeof(kUndoSvgFigureCounts[0]));
  glyph.min_x = kUndoSvgMinX;
  glyph.min_y = kUndoSvgMinY;
  glyph.max_x = kUndoSvgMaxX;
  glyph.max_y = kUndoSvgMaxY;
  return glyph;
}

SvgGlyph makeTextSvgGlyph()
{
  SvgGlyph glyph{};
  glyph.points = kTextSvgPoints;
  glyph.point_count =
      static_cast<int>(sizeof(kTextSvgPoints) / sizeof(kTextSvgPoints[0]));
  glyph.figure_counts = kTextSvgFigureCounts;
  glyph.figure_count = static_cast<int>(sizeof(kTextSvgFigureCounts) /
                                        sizeof(kTextSvgFigureCounts[0]));
  glyph.min_x = kTextSvgMinX;
  glyph.min_y = kTextSvgMinY;
  glyph.max_x = kTextSvgMaxX;
  glyph.max_y = kTextSvgMaxY;
  return glyph;
}

SvgGlyph makeMosaicSvgGlyph()
{
  SvgGlyph glyph{};
  glyph.points = kMosaicSvgPoints;
  glyph.point_count =
      static_cast<int>(sizeof(kMosaicSvgPoints) / sizeof(kMosaicSvgPoints[0]));
  glyph.figure_counts = kMosaicSvgFigureCounts;
  glyph.figure_count = static_cast<int>(sizeof(kMosaicSvgFigureCounts) /
                                        sizeof(kMosaicSvgFigureCounts[0]));
  glyph.min_x = kMosaicSvgMinX;
  glyph.min_y = kMosaicSvgMinY;
  glyph.max_x = kMosaicSvgMaxX;
  glyph.max_y = kMosaicSvgMaxY;
  return glyph;
}

SvgGlyph makeStrokeWidthSvgGlyph()
{
  SvgGlyph glyph{};
  glyph.points = kStrokeWidthSvgPoints;
  glyph.point_count = static_cast<int>(sizeof(kStrokeWidthSvgPoints) /
                                       sizeof(kStrokeWidthSvgPoints[0]));
  glyph.figure_counts = kStrokeWidthSvgFigureCounts;
  glyph.figure_count = static_cast<int>(sizeof(kStrokeWidthSvgFigureCounts) /
                                        sizeof(kStrokeWidthSvgFigureCounts[0]));
  glyph.min_x = kStrokeWidthSvgMinX;
  glyph.min_y = kStrokeWidthSvgMinY;
  glyph.max_x = kStrokeWidthSvgMaxX;
  glyph.max_y = kStrokeWidthSvgMaxY;
  return glyph;
}

SvgGlyph makePinSvgGlyph()
{
  SvgGlyph glyph{};
  glyph.points = kPinSvgPoints;
  glyph.point_count =
      static_cast<int>(sizeof(kPinSvgPoints) / sizeof(kPinSvgPoints[0]));
  glyph.figure_counts = kPinSvgFigureCounts;
  glyph.figure_count = static_cast<int>(sizeof(kPinSvgFigureCounts) /
                                        sizeof(kPinSvgFigureCounts[0]));
  glyph.min_x = kPinSvgMinX;
  glyph.min_y = kPinSvgMinY;
  glyph.max_x = kPinSvgMaxX;
  glyph.max_y = kPinSvgMaxY;
  return glyph;
}

SvgGlyph makeMoveSvgGlyph()
{
  SvgGlyph glyph{};
  glyph.points = kMoveSvgPoints;
  glyph.point_count =
      static_cast<int>(sizeof(kMoveSvgPoints) / sizeof(kMoveSvgPoints[0]));
  glyph.figure_counts = kMoveSvgFigureCounts;
  glyph.figure_count = static_cast<int>(sizeof(kMoveSvgFigureCounts) /
                                        sizeof(kMoveSvgFigureCounts[0]));
  glyph.min_x = kMoveSvgMinX;
  glyph.min_y = kMoveSvgMinY;
  glyph.max_x = kMoveSvgMaxX;
  glyph.max_y = kMoveSvgMaxY;
  return glyph;
}

SvgGlyph makeEyedropperSvgGlyph()
{
  SvgGlyph glyph{};
  glyph.points = kEyedropperSvgPoints;
  glyph.point_count = static_cast<int>(sizeof(kEyedropperSvgPoints) /
                                       sizeof(kEyedropperSvgPoints[0]));
  glyph.figure_counts = kEyedropperSvgFigureCounts;
  glyph.figure_count = static_cast<int>(sizeof(kEyedropperSvgFigureCounts) /
                                        sizeof(kEyedropperSvgFigureCounts[0]));
  glyph.min_x = kEyedropperSvgMinX;
  glyph.min_y = kEyedropperSvgMinY;
  glyph.max_x = kEyedropperSvgMaxX;
  glyph.max_y = kEyedropperSvgMaxY;
  return glyph;
}

bool drawSvgPenIcon(Gdiplus::Graphics& graphics, const Gdiplus::SolidBrush& brush,
                    float cx, float cy, float s)
{
  const SvgGlyph glyph = makePenSvgGlyph();
  float origin_x = 0.0f;
  float origin_y = 0.0f;
  float scale = 0.0f;
  layoutSvgGlyph(glyph, cx, cy, s, origin_x, origin_y, scale);

  // SVG 里是两个独立 path：铅笔轮廓（含镂空）+ 笔头切面，需分开填充。
  return fillSvgFigures(graphics, brush, glyph, origin_x, origin_y, scale,
                        kPenSvgOutlineBegin, kPenSvgOutlineEnd) &&
         fillSvgFigures(graphics, brush, glyph, origin_x, origin_y, scale,
                        kPenSvgFacetBegin, kPenSvgFacetEnd);
}

bool drawSvgGeometryIcon(Gdiplus::Graphics& graphics,
                         const Gdiplus::SolidBrush& brush, float cx, float cy,
                         float s)
{
  const SvgGlyph glyph = makeGeometrySvgGlyph();
  float origin_x = 0.0f;
  float origin_y = 0.0f;
  float scale = 0.0f;
  layoutSvgGlyph(glyph, cx, cy, s, origin_x, origin_y, scale);
  return fillSvgFigures(graphics, brush, glyph, origin_x, origin_y, scale, 0,
                        glyph.figure_count);
}

bool drawSvgFilledGlyph(Gdiplus::Graphics& graphics,
                        const Gdiplus::SolidBrush& brush, const SvgGlyph& glyph,
                        float cx, float cy, float s)
{
  float origin_x = 0.0f;
  float origin_y = 0.0f;
  float scale = 0.0f;
  layoutSvgGlyph(glyph, cx, cy, s, origin_x, origin_y, scale);
  return fillSvgFigures(graphics, brush, glyph, origin_x, origin_y, scale, 0,
                        glyph.figure_count);
}

bool drawSvgEditIcon(Gdiplus::Graphics& graphics, const Gdiplus::SolidBrush& brush,
                     float cx, float cy, float s)
{
  const SvgGlyph glyph = makeEditSvgGlyph();
  float origin_x = 0.0f;
  float origin_y = 0.0f;
  float scale = 0.0f;
  layoutSvgGlyph(glyph, cx, cy, s, origin_x, origin_y, scale);
  // 纸张、铅笔镂空、斜杠是三段独立填充，不能一次 Alternate 否则斜杠会把笔身挖空。
  return fillSvgFigures(graphics, brush, glyph, origin_x, origin_y, scale,
                        kEditSvgPageBegin, kEditSvgPageEnd) &&
         fillSvgFigures(graphics, brush, glyph, origin_x, origin_y, scale,
                        kEditSvgPencilBegin, kEditSvgPencilEnd) &&
         fillSvgFigures(graphics, brush, glyph, origin_x, origin_y, scale,
                        kEditSvgSlashBegin, kEditSvgSlashEnd);
}

bool drawSvgUndoIcon(Gdiplus::Graphics& graphics, const Gdiplus::SolidBrush& brush,
                     float cx, float cy, float s)
{
  const SvgGlyph glyph = makeUndoSvgGlyph();
  float origin_x = 0.0f;
  float origin_y = 0.0f;
  float scale = 0.0f;
  layoutSvgGlyph(glyph, cx, cy, s, origin_x, origin_y, scale);
  // 箭头与环形是两块实心，分开填避免 Alternate 在交叠处挖洞。
  return fillSvgFigures(graphics, brush, glyph, origin_x, origin_y, scale,
                        kUndoSvgArrowBegin, kUndoSvgArrowEnd) &&
         fillSvgFigures(graphics, brush, glyph, origin_x, origin_y, scale,
                        kUndoSvgBodyBegin, glyph.figure_count);
}

bool drawSvgTextIcon(Gdiplus::Graphics& graphics, const Gdiplus::SolidBrush& brush,
                     float cx, float cy, float s)
{
  return drawSvgFilledGlyph(graphics, brush, makeTextSvgGlyph(), cx, cy, s);
}

bool drawSvgMosaicIcon(Gdiplus::Graphics& graphics, const Gdiplus::SolidBrush& brush,
                       float cx, float cy, float s)
{
  // 外框与十字镂空必须一次 Alternate 填充，才能得到四格马赛克。
  return drawSvgFilledGlyph(graphics, brush, makeMosaicSvgGlyph(), cx, cy, s);
}

bool drawSvgStrokeWidthIcon(Gdiplus::Graphics& graphics,
                            const Gdiplus::SolidBrush& brush, float cx, float cy,
                            float s)
{
  return drawSvgFilledGlyph(graphics, brush, makeStrokeWidthSvgGlyph(), cx, cy,
                            s);
}

bool drawSvgPinIcon(Gdiplus::Graphics& graphics, const Gdiplus::SolidBrush& brush,
                    float cx, float cy, float s)
{
  // 外轮廓与内部镂空必须一次 Alternate 填充，才能得到图钉空心。
  return drawSvgFilledGlyph(graphics, brush, makePinSvgGlyph(), cx, cy, s);
}

bool drawSvgMoveIcon(Gdiplus::Graphics& graphics, const Gdiplus::SolidBrush& brush,
                     float cx, float cy, float s)
{
  return drawSvgFilledGlyph(graphics, brush, makeMoveSvgGlyph(), cx, cy, s);
}

bool drawSvgEyedropperIcon(Gdiplus::Graphics& graphics,
                           const Gdiplus::SolidBrush& brush, float cx, float cy,
                           float s)
{
  // 外轮廓与吸头内腔必须一次 Alternate 填充，才能得到吸管镂空。
  return drawSvgFilledGlyph(graphics, brush, makeEyedropperSvgGlyph(), cx, cy,
                            s);
}

void configureIconGraphics(Gdiplus::Graphics& graphics)
{
  graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
  graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
  graphics.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);
}

void addRoundRectPath(Gdiplus::GraphicsPath& path, const Gdiplus::RectF& box,
                      float radius)
{
  const float width = box.Width;
  const float height = box.Height;
  if (width <= 0.0f || height <= 0.0f)
  {
    return;
  }

  float corner = radius;
  const float max_corner = (std::min)(width, height) * 0.5f;
  if (corner > max_corner)
  {
    corner = max_corner;
  }
  if (corner < 0.5f)
  {
    path.AddRectangle(box);
    return;
  }

  const float diameter = corner * 2.0f;
  path.AddArc(box.X, box.Y, diameter, diameter, 180.0f, 90.0f);
  path.AddArc(box.X + width - diameter, box.Y, diameter, diameter, 270.0f,
              90.0f);
  path.AddArc(box.X + width - diameter, box.Y + height - diameter, diameter,
              diameter, 0.0f, 90.0f);
  path.AddArc(box.X, box.Y + height - diameter, diameter, diameter, 90.0f,
              90.0f);
  path.CloseFigure();
}

void fillRoundRectGraphics(Gdiplus::Graphics& graphics, const RECT& rect,
                           COLORREF fill, COLORREF border, int radius)
{
  const float width = static_cast<float>(rect.right - rect.left);
  const float height = static_cast<float>(rect.bottom - rect.top);
  if (width <= 0.0f || height <= 0.0f)
  {
    return;
  }

  configureIconGraphics(graphics);
  // 收缩 0.5px，让 1px 描边落在像素中心，圆弧不被裁成台阶。
  const Gdiplus::RectF box(
      static_cast<float>(rect.left) + 0.5f, static_cast<float>(rect.top) + 0.5f,
      (std::max)(1.0f, width - 1.0f), (std::max)(1.0f, height - 1.0f));
  Gdiplus::GraphicsPath path;
  addRoundRectPath(path, box, static_cast<float>(radius));

  const Gdiplus::SolidBrush brush(toGdiplus(fill));
  graphics.FillPath(&brush, &path);
  Gdiplus::Pen pen(toGdiplus(border), 1.0f);
  pen.SetLineJoin(Gdiplus::LineJoinRound);
  graphics.DrawPath(&pen, &path);
}

void fillRoundRectGdiFallback(HDC hdc, const RECT& rect, COLORREF fill,
                              COLORREF border, int radius)
{
  const int width = rect.right - rect.left;
  const int height = rect.bottom - rect.top;
  const int half = (std::min)(width, height) / 2;
  const int safe_radius = (std::max)(0, (std::min)(radius, half));
  const HBRUSH brush = CreateSolidBrush(fill);
  const HPEN pen = CreatePen(PS_SOLID, 1, border);
  if (brush == nullptr || pen == nullptr)
  {
    if (brush != nullptr)
    {
      DeleteObject(brush);
    }
    if (pen != nullptr)
    {
      DeleteObject(pen);
    }
    return;
  }

  const HGDIOBJ old_brush = SelectObject(hdc, brush);
  const HGDIOBJ old_pen = SelectObject(hdc, pen);
  RoundRect(hdc, rect.left, rect.top, rect.right, rect.bottom, safe_radius * 2,
            safe_radius * 2);
  SelectObject(hdc, old_pen);
  SelectObject(hdc, old_brush);
  DeleteObject(pen);
  DeleteObject(brush);
}

void makeIconPen(Gdiplus::Pen& pen)
{
  pen.SetLineCap(Gdiplus::LineCapRound, Gdiplus::LineCapRound,
                 Gdiplus::DashCapRound);
  pen.SetLineJoin(Gdiplus::LineJoinRound);
}

void lineTo(HDC hdc, int x, int y)
{
  LineTo(hdc, x, y);
}

void copyWide(wchar_t* dest, std::size_t dest_chars, const wchar_t* source)
{
  if (dest == nullptr || dest_chars == 0)
  {
    return;
  }
  dest[0] = L'\0';
  if (source == nullptr)
  {
    return;
  }
  wcsncpy_s(dest, dest_chars, source, _TRUNCATE);
}

}  // namespace

bool prepareModernToolbarRendering() noexcept
{
  return ensureGdiplus();
}

void fillRoundRect(HDC hdc, const RECT& rect, COLORREF fill, COLORREF border,
                   int radius)
{
  if (hdc == nullptr)
  {
    return;
  }
  if (!ensureGdiplus())
  {
    fillRoundRectGdiFallback(hdc, rect, fill, border, radius);
    return;
  }

  Gdiplus::Graphics graphics(hdc);
  fillRoundRectGraphics(graphics, rect, fill, border, radius);
}

HBITMAP createTopDownArgbDib(int width, int height, void** bits)
{
  if (bits == nullptr || width <= 0 || height <= 0)
  {
    return nullptr;
  }
  *bits = nullptr;
  BITMAPINFO info{};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = width;
  info.bmiHeader.biHeight = -height;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  return CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, bits, nullptr, 0);
}

void applyColorKeyAlpha(void* bits, int width, int height, COLORREF key)
{
  if (bits == nullptr || width <= 0 || height <= 0)
  {
    return;
  }
  const std::uint32_t key_rgb =
      static_cast<std::uint32_t>(GetBValue(key)) |
      (static_cast<std::uint32_t>(GetGValue(key)) << 8) |
      (static_cast<std::uint32_t>(GetRValue(key)) << 16);
  std::uint32_t* pixels = static_cast<std::uint32_t*>(bits);
  const int count = width * height;
  for (int i = 0; i < count; ++i)
  {
    const std::uint32_t rgb = pixels[static_cast<std::size_t>(i)] & 0x00FFFFFFu;
    if (rgb == key_rgb)
    {
      pixels[static_cast<std::size_t>(i)] = 0;
    }
    else
    {
      pixels[static_cast<std::size_t>(i)] |= 0xFF000000u;
    }
  }
}

void promoteRgbToOpaqueAlpha(void* bits, int width, int height)
{
  if (bits == nullptr || width <= 0 || height <= 0)
  {
    return;
  }
  std::uint32_t* pixels = static_cast<std::uint32_t*>(bits);
  const int count = width * height;
  for (int i = 0; i < count; ++i)
  {
    std::uint32_t& pixel = pixels[static_cast<std::size_t>(i)];
    if ((pixel & 0xFF000000u) == 0 && (pixel & 0x00FFFFFFu) != 0)
    {
      pixel |= 0xFF000000u;
    }
  }
}

bool presentLayeredArgbWindow(HWND hwnd, HDC src_dc, int width, int height)
{
  if (hwnd == nullptr || src_dc == nullptr || width <= 0 || height <= 0)
  {
    return false;
  }

  RECT window_rect{};
  if (GetWindowRect(hwnd, &window_rect) == FALSE)
  {
    return false;
  }
  POINT dst{window_rect.left, window_rect.top};
  POINT src{0, 0};
  SIZE size{width, height};
  BLENDFUNCTION blend{};
  blend.BlendOp = AC_SRC_OVER;
  blend.SourceConstantAlpha = 255;
  blend.AlphaFormat = AC_SRC_ALPHA;
  return UpdateLayeredWindow(hwnd, nullptr, &dst, &size, src_dc, &src, 0,
                             &blend, ULW_ALPHA) != FALSE;
}

bool drawToolbarBarOnArgbBits(void* bits, int width, int height,
                              const RECT& rect)
{
  return drawToolbarBarOnArgbBits(bits, width, height, rect,
                                  DefaultModernToolbarMetrics);
}

bool drawToolbarBarOnArgbBits(
    void* bits, int width, int height, const RECT& rect,
    const ModernToolbarMetrics& metrics)
{
  if (bits == nullptr || width <= 0 || height <= 0 || !ensureGdiplus())
  {
    return false;
  }

  Gdiplus::Bitmap bitmap(width, height, width * 4, PixelFormat32bppPARGB,
                         static_cast<BYTE*>(bits));
  Gdiplus::Graphics graphics(&bitmap);
  const ModernToolbarColors colors = DefaultModernToolbarColors;
  RECT shadow = rect;
  OffsetRect(&shadow, 0, 1);
  fillRoundRectGraphics(graphics, shadow, colors.bar_shadow, colors.bar_shadow,
                        metrics.corner_radius);
  fillRoundRectGraphics(graphics, rect, colors.bar_fill, colors.bar_border,
                        metrics.corner_radius);
  return true;
}

void fillToolbarColorKey(HDC hdc, const RECT& rect)
{
  if (hdc == nullptr)
  {
    return;
  }
  const HBRUSH brush = CreateSolidBrush(kToolbarColorKey);
  if (brush == nullptr)
  {
    return;
  }
  FillRect(hdc, &rect, brush);
  DeleteObject(brush);
}

void applyToolbarColorKey(HWND hwnd)
{
  if (hwnd == nullptr)
  {
    return;
  }
  const LONG_PTR ex_style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
  (void)SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex_style | WS_EX_LAYERED);
  (void)SetLayeredWindowAttributes(hwnd, kToolbarColorKey, 0, LWA_COLORKEY);
}

void drawToolbarBar(HDC hdc, const RECT& rect)
{
  drawToolbarBar(hdc, rect, DefaultModernToolbarMetrics);
}

void drawToolbarBar(HDC hdc, const RECT& rect,
                    const ModernToolbarMetrics& metrics)
{
  if (hdc == nullptr)
  {
    return;
  }

  const ModernToolbarColors colors = DefaultModernToolbarColors;
  RECT shadow = rect;
  OffsetRect(&shadow, 0, 1);
  fillRoundRect(hdc, shadow, colors.bar_shadow, colors.bar_shadow,
                metrics.corner_radius);
  fillRoundRect(hdc, rect, colors.bar_fill, colors.bar_border,
                metrics.corner_radius);
}

void drawToolbarIcon(HDC hdc, const RECT& cell, ToolbarIconKind kind,
                     COLORREF color)
{
  if (hdc == nullptr || !ensureGdiplus())
  {
    return;
  }

  Gdiplus::Graphics graphics(hdc);
  configureIconGraphics(graphics);

  const Gdiplus::Color ink = toGdiplus(color);
  const float stroke =
      (kind == ToolbarIconKind::Confirm) ? kIconConfirmStrokePx : kIconStrokePx;
  Gdiplus::Pen pen(ink, stroke);
  makeIconPen(pen);
  Gdiplus::SolidBrush brush(ink);

  const float cx =
      (static_cast<float>(cell.left) + static_cast<float>(cell.right)) * 0.5f;
  const float cy =
      (static_cast<float>(cell.top) + static_cast<float>(cell.bottom)) * 0.5f;
  const float s = static_cast<float>(iconHalfExtent(cell));

  switch (kind)
  {
    case ToolbarIconKind::Copy:
      graphics.DrawRectangle(&pen, cx - s + 3.0f, cy - s, 2.0f * s - 5.0f,
                             2.0f * s - 5.0f);
      graphics.DrawRectangle(&pen, cx - s, cy - s + 4.0f, 2.0f * s - 5.0f,
                             2.0f * s - 5.0f);
      break;
    case ToolbarIconKind::Save:
      graphics.DrawLine(&pen, cx, cy - s, cx, cy + 1.0f);
      graphics.DrawLine(&pen, cx - 3.5f, cy - 2.5f, cx, cy + 1.0f);
      graphics.DrawLine(&pen, cx + 3.5f, cy - 2.5f, cx, cy + 1.0f);
      graphics.DrawLine(&pen, cx - s, cy + 3.0f, cx - s, cy + s);
      graphics.DrawLine(&pen, cx - s, cy + s, cx + s, cy + s);
      graphics.DrawLine(&pen, cx + s, cy + s, cx + s, cy + 3.0f);
      break;
    case ToolbarIconKind::Edit:
      if (!kUseSvgEditIcon ||
          !drawSvgEditIcon(graphics, brush, cx, cy, s))
      {
        drawLegacyEditIcon(graphics, pen, cx, cy, s);
      }
      break;
    case ToolbarIconKind::Pin:
      if (!kUseSvgPinIcon || !drawSvgPinIcon(graphics, brush, cx, cy, s))
      {
        drawLegacyPinIcon(graphics, pen, cx, cy, s);
      }
      break;
    case ToolbarIconKind::Move:
      if (!kUseSvgMoveIcon || !drawSvgMoveIcon(graphics, brush, cx, cy, s))
      {
        drawLegacyMoveIcon(graphics, pen, cx, cy, s);
      }
      break;
    case ToolbarIconKind::Eyedropper:
      if (!kUseSvgEyedropperIcon ||
          !drawSvgEyedropperIcon(graphics, brush, cx, cy, s))
      {
        drawLegacyEyedropperIcon(graphics, pen, cx, cy, s);
      }
      break;
    case ToolbarIconKind::Rectangle:
      drawLegacyGeometryIcon(graphics, pen, cx, cy, s);
      break;
    case ToolbarIconKind::Geometry:
      if (!kUseSvgGeometryIcon ||
          !drawSvgGeometryIcon(graphics, brush, cx, cy, s))
      {
        drawLegacyGeometryIcon(graphics, pen, cx, cy, s);
      }
      break;
    case ToolbarIconKind::Ellipse:
      graphics.DrawEllipse(&pen, cx - s + 1.0f, cy - s + 2.0f, 2.0f * s - 3.0f,
                           2.0f * s - 4.0f);
      break;
    case ToolbarIconKind::Fill:
      graphics.FillRectangle(&brush, cx - s + 2.0f, cy - s + 3.0f,
                             2.0f * s - 4.0f, 2.0f * s - 6.0f);
      break;
    case ToolbarIconKind::LineSolid:
    case ToolbarIconKind::LineDashed:
    case ToolbarIconKind::LineDotted:
    {
      Gdiplus::Pen line_pen(ink, kIconStrokePx);
      makeIconPen(line_pen);
      if (kind == ToolbarIconKind::LineDashed)
      {
        line_pen.SetDashStyle(Gdiplus::DashStyleDash);
      }
      else if (kind == ToolbarIconKind::LineDotted)
      {
        line_pen.SetDashStyle(Gdiplus::DashStyleDot);
      }
      graphics.DrawLine(&line_pen, cx - s + 1.0f, cy, cx + s - 1.0f, cy);
      break;
    }
    case ToolbarIconKind::Arrow:
    {
      const float x0 = cx - s + 1.5f;
      const float y0 = cy + s - 2.0f;
      const float x1 = cx + s - 2.0f;
      const float y1 = cy - s + 2.0f;
      graphics.DrawLine(&pen, x0, y0, x1, y1);
      Gdiplus::GraphicsPath head;
      head.AddLine(x1, y1, x1 - 7.0f, y1 + 1.8f);
      head.AddLine(x1 - 7.0f, y1 + 1.8f, x1 - 1.8f, y1 + 7.0f);
      head.CloseFigure();
      graphics.FillPath(&brush, &head);
      break;
    }
    case ToolbarIconKind::Pen:
      if (!kUseSvgPenIcon ||
          !drawSvgPenIcon(graphics, brush, cx, cy, s))
      {
        drawLegacyPenIcon(graphics, pen, cx, cy, s);
      }
      break;
    case ToolbarIconKind::Mosaic:
      if (!kUseSvgMosaicIcon ||
          !drawSvgMosaicIcon(graphics, brush, cx, cy, s))
      {
        drawLegacyMosaicIcon(graphics, pen, cx, cy, s);
      }
      break;
    case ToolbarIconKind::StrokeWidth:
      if (!kUseSvgStrokeWidthIcon ||
          !drawSvgStrokeWidthIcon(graphics, brush, cx, cy, s))
      {
        drawLegacyStrokeWidthIcon(graphics, brush, cx, cy, s);
      }
      break;
    case ToolbarIconKind::Text:
      if (!kUseSvgTextIcon ||
          !drawSvgTextIcon(graphics, brush, cx, cy, s))
      {
        drawLegacyTextIcon(graphics, pen, cx, cy, s);
      }
      break;
    case ToolbarIconKind::Undo:
      if (!kUseSvgUndoIcon ||
          !drawSvgUndoIcon(graphics, brush, cx, cy, s))
      {
        drawLegacyUndoIcon(graphics, pen, cx, cy, s);
      }
      break;
    case ToolbarIconKind::Redo:
      drawLegacyRedoIcon(graphics, pen, cx, cy, s);
      break;
    case ToolbarIconKind::Confirm:
      graphics.DrawLine(&pen, cx - s + 2.0f, cy + 0.5f, cx - 0.5f,
                        cy + s - 4.0f);
      graphics.DrawLine(&pen, cx - 0.5f, cy + s - 4.0f, cx + s - 2.0f,
                        cy - s + 3.5f);
      break;
    case ToolbarIconKind::Cancel:
      graphics.DrawLine(&pen, cx - s + 3.0f, cy - s + 3.0f, cx + s - 3.0f,
                        cy + s - 3.0f);
      graphics.DrawLine(&pen, cx + s - 3.0f, cy - s + 3.0f, cx - s + 3.0f,
                        cy + s - 3.0f);
      break;
    case ToolbarIconKind::LongShot:
      graphics.DrawRectangle(&pen, cx - s + 2.0f, cy - s + 0.5f, 2.0f * s - 4.0f,
                             2.0f * s - 6.0f);
      graphics.DrawLine(&pen, cx, cy + s - 4.5f, cx, cy + s - 0.5f);
      graphics.DrawLine(&pen, cx - 3.0f, cy + s - 3.0f, cx, cy + s - 0.5f);
      graphics.DrawLine(&pen, cx + 3.0f, cy + s - 3.0f, cx, cy + s - 0.5f);
      break;
    case ToolbarIconKind::Pause:
      graphics.FillRectangle(&brush, cx - s + 3.0f, cy - s + 2.0f, 3.0f,
                             2.0f * s - 4.0f);
      graphics.FillRectangle(&brush, cx + s - 6.0f, cy - s + 2.0f, 3.0f,
                             2.0f * s - 4.0f);
      break;
    case ToolbarIconKind::Resume:
    {
      Gdiplus::GraphicsPath play;
      play.AddLine(cx - s + 3.0f, cy - s + 2.0f, cx - s + 3.0f, cy + s - 2.0f);
      play.AddLine(cx - s + 3.0f, cy + s - 2.0f, cx + s - 2.0f, cy);
      play.CloseFigure();
      graphics.FillPath(&brush, &play);
      break;
    }
    case ToolbarIconKind::Stop:
      graphics.FillRectangle(&brush, cx - s + 3.0f, cy - s + 3.0f, 2.0f * s - 6.0f,
                             2.0f * s - 6.0f);
      break;
    default:
      break;
  }
}

constexpr int kChevronInsetRightPx = 7;
constexpr int kChevronInsetBottomPx = 6;
constexpr int kChevronArmPx = 3;
constexpr int kChevronDropPx = 2;

void drawToolbarItem(HDC hdc, const RECT& cell, ToolbarIconKind kind,
                     bool hovered, bool selected, bool enabled, bool accent,
                     bool grouped)
{
  drawToolbarItem(hdc, cell,
                  ToolbarItemModel{kind, hovered, selected, enabled, accent,
                                   grouped});
}

void drawToolbarItem(HDC hdc, const RECT& cell,
                     const ToolbarItemModel& model)
{
  drawToolbarItem(hdc, cell, model, DefaultModernToolbarMetrics);
}

void drawToolbarItem(HDC hdc, const RECT& cell,
                     const ToolbarItemModel& model,
                     const ModernToolbarMetrics& metrics)
{
  if (hdc == nullptr)
  {
    return;
  }

  const ModernToolbarColors colors = DefaultModernToolbarColors;
  const COLORREF button_fill =
      model.enabled ? colors.button_fill : colors.disabled_fill;
  fillRoundRect(hdc, cell, button_fill, button_fill, metrics.hover_radius);
  const bool highlight =
      model.enabled && (model.hovered || model.selected || model.accent);
  if (highlight)
  {
    const COLORREF fill = model.hovered ? colors.hover_fill
                                        : colors.selected_fill;
    fillRoundRect(hdc, cell, fill, fill, metrics.hover_radius);
  }

  COLORREF icon_color = model.enabled ? colors.icon : colors.icon_disabled;
  if (model.enabled && model.icon == ToolbarIconKind::Confirm)
  {
    icon_color = colors.confirm;
  }
  else if (model.enabled && model.icon == ToolbarIconKind::Cancel)
  {
    icon_color = colors.cancel;
  }
  drawToolbarIcon(hdc, cell, model.icon, icon_color);

  if (model.grouped && ensureGdiplus())
  {
    Gdiplus::Graphics graphics(hdc);
    configureIconGraphics(graphics);
    Gdiplus::Pen chevron(toGdiplus(icon_color), 1.4f);
    makeIconPen(chevron);
    const float chevron_x =
        static_cast<float>(cell.right - kChevronInsetRightPx);
    const float chevron_y =
        static_cast<float>(cell.bottom - kChevronInsetBottomPx);
    graphics.DrawLine(&chevron, chevron_x, chevron_y,
                      chevron_x + static_cast<float>(kChevronArmPx), chevron_y);
    graphics.DrawLine(&chevron, chevron_x + static_cast<float>(kChevronArmPx),
                      chevron_y,
                      chevron_x + static_cast<float>(kChevronArmPx) * 0.5f,
                      chevron_y + static_cast<float>(kChevronDropPx));
    graphics.DrawLine(&chevron, chevron_x + static_cast<float>(kChevronArmPx) * 0.5f,
                      chevron_y + static_cast<float>(kChevronDropPx), chevron_x,
                      chevron_y);
  }
}

void drawToolbarDivider(HDC hdc, int x, int top, int bottom)
{
  if (hdc == nullptr)
  {
    return;
  }

  const ModernToolbarColors colors = DefaultModernToolbarColors;
  const PenGuard pen(hdc, 1, colors.divider);
  if (!pen.ok())
  {
    return;
  }
  MoveToEx(hdc, x, top, nullptr);
  lineTo(hdc, x, bottom);
}

void drawToolbarHorizontalDivider(HDC hdc, int left, int right, int y)
{
  if (hdc == nullptr || right <= left)
  {
    return;
  }

  const ModernToolbarColors colors = DefaultModernToolbarColors;
  const PenGuard pen(hdc, 1, colors.divider);
  if (!pen.ok())
  {
    return;
  }
  MoveToEx(hdc, left, y, nullptr);
  lineTo(hdc, right, y);
}

HWND createToolbarTooltip(HWND owner)
{
  if (owner == nullptr)
  {
    return nullptr;
  }

  INITCOMMONCONTROLSEX icc{};
  icc.dwSize = sizeof(icc);
  icc.dwICC = ICC_WIN95_CLASSES;
  (void)InitCommonControlsEx(&icc);

  const HWND tooltip = CreateWindowExW(
      WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
      WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP, CW_USEDEFAULT, CW_USEDEFAULT,
      CW_USEDEFAULT, CW_USEDEFAULT, owner, nullptr, GetModuleHandleW(nullptr),
      nullptr);
  if (tooltip == nullptr)
  {
    return nullptr;
  }

  SetWindowPos(tooltip, HWND_TOPMOST, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  SendMessageW(tooltip, TTM_SETDELAYTIME, TTDT_INITIAL,
               DefaultModernToolbarMetrics.tooltip_delay_ms);
  SendMessageW(tooltip, TTM_SETMAXTIPWIDTH, 0, 240);
  return tooltip;
}

void bindToolbarTooltip(HWND tooltip, HWND owner, UINT id, const RECT& rect,
                        const wchar_t* text, wchar_t* storage,
                        std::size_t storage_chars)
{
  if (tooltip == nullptr || owner == nullptr || storage == nullptr ||
      storage_chars == 0)
  {
    return;
  }

  TOOLINFOW info{};
#ifdef TTTOOLINFOW_V2_SIZE
  info.cbSize = TTTOOLINFOW_V2_SIZE;
#else
  info.cbSize = sizeof(TOOLINFOW);
#endif
  info.uFlags = TTF_SUBCLASS | TTF_TRANSPARENT;
  info.hwnd = owner;
  info.uId = static_cast<UINT_PTR>(id);
  info.rect = rect;
  info.lpszText = storage;

  const bool hidden = (rect.right <= rect.left) || (rect.bottom <= rect.top);
  if (hidden || text == nullptr || text[0] == L'\0')
  {
    storage[0] = L'\0';
    SendMessageW(tooltip, TTM_DELTOOLW, 0, reinterpret_cast<LPARAM>(&info));
    return;
  }

  copyWide(storage, storage_chars, text);
  SendMessageW(tooltip, TTM_DELTOOLW, 0, reinterpret_cast<LPARAM>(&info));
  SendMessageW(tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&info));
}

}  // namespace qingying
