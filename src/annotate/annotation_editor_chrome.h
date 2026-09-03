#pragma once

#include "annotate/annotation_editor_paint.h"

namespace qingying {

void positionOwnedPopup(HWND popup, HWND owner, int client_x, int client_y,
                        int width, int height);
void fillSizeCombo(AnnotationEditorHost* data);
void syncSizeFromCombo(AnnotationEditorHost* data);
void bindSizeComboTooltip(AnnotationEditorHost* data);
void bindPropertyBarTooltips(AnnotationEditorHost* data);
COLORREF colorBgraToRef(const ColorBgra& color);
bool colorsMatch(const ColorBgra& left, const ColorBgra& right);
void syncStyleFromAnnotation(AnnotationEditorHost* data,
                             const Annotation& annotation);
void applyLiveTextStyle(AnnotationEditorHost* data);
RECT toWinRect(const AnnotationEditorRect& rect);
RECT takeToolbarButtonRect(int& x, int y);
RECT takeToolbarSizedRect(int& x, int y, int width);
void skipToolbarDivider(int& x);
void resetPropertyBarRects(AnnotationEditorHost* data);
void syncGeometryButton(AnnotationEditorHost* data);
void layoutEditorChrome(HWND hwnd, AnnotationEditorHost* data);
void layoutPropertyBar(HWND hwnd, AnnotationEditorHost* data);
void resizeEditorChrome(AnnotationEditorHost* data);
int hitTestColorSwatch(const AnnotationEditorHost* data, int x, int y);
bool hitTestCurrentColorSwatch(const AnnotationEditorHost* data, int x, int y);
bool hitTestSizeCombo(const AnnotationEditorHost* data, int x, int y);
void refreshInlineEditFont(AnnotationEditorHost* data);
void applyPickedSizeValue(AnnotationEditorHost* data, bool mosaic, int value);
bool handleSizeComboWheel(AnnotationEditorHost* data, int delta);
void pickSizeFromOverlayMenu(AnnotationEditorHost* data);
bool hitTestStrokeChip(const AnnotationEditorHost* data, int x, int y);
int hitTestShapeToggle(const AnnotationEditorHost* data, int x, int y);
bool hitTestFill(const AnnotationEditorHost* data, int x, int y);
int hitTestLineStyle(const AnnotationEditorHost* data, int x, int y);
void handlePropertyBarClick(AnnotationEditorHost* data, int x, int y);
bool pointerHitsStyleChrome(const AnnotationEditorHost* data);
bool createButtons(HWND hwnd, AnnotationEditorHost* data);
void invalidateToolbar(AnnotationEditorHost* data);
bool hitTestChromeBar(const AnnotationEditorHost* data, int x, int y);
void bindEditorTooltips(AnnotationEditorHost* data);
RECT unionChromeRects(const RECT& main_bar, const RECT& property_bar);
void invalidateChromeMove(HWND hwnd, const RECT& old_rect,
                          const RECT& new_rect);
void beginChromeDrag(AnnotationEditorHost* data, HWND hwnd, int x, int y);
void updateChromeDrag(AnnotationEditorHost* data, int x, int y);
void endChromeDrag(AnnotationEditorHost* data);
int hitTestEditorToolbar(const AnnotationEditorHost* data, int x, int y);
void handleToolbarItemClick(AnnotationEditorHost* data, UINT id);
void handleToolCommand(AnnotationEditorHost* data, UINT id);
void paintEditorToolbar(HDC hdc,
                        const AnnotationEditorPaintSnapshot& snapshot,
                        bool draw_shell);
void paintPropertyBar(HDC hdc,
                      const AnnotationEditorPaintSnapshot& snapshot,
                      bool draw_shell);

}  // namespace qingying
