#pragma once

#include "annotate/annotation_editor_paint.h"

namespace qingying {

void destroyInlineEdit(AnnotationEditorHost* data);
bool measureAnnotationText(HDC hdc, const Annotation& annotation, SIZE& out_size);
void fillTextHitBounds(HWND hwnd, Annotation& annotation);
void commitInlineText(AnnotationEditorHost* data);
void cancelInlineText(AnnotationEditorHost* data);
void resetTextGesture(AnnotationEditorHost* data);
void clearTextSelection(AnnotationEditorHost* data);
void selectTextAnnotation(AnnotationEditorHost* data, HWND hwnd,
                          std::size_t index, int click_x, int click_y);
bool isTextDoubleClick(const AnnotationEditorHost* data, std::size_t hit,
                       int x, int y);
bool deleteSelectedTextAnnotation(AnnotationEditorHost* data);
void placeInlineEditCaret(HWND edit, int caret, bool scroll_to_caret);
void layoutInlineEdit(AnnotationEditorHost* data);
void applyInlineEditVisual(AnnotationEditorHost* data);
bool registerInlineEditHostClass(HINSTANCE instance);
LRESULT CALLBACK inlineEditHostWndProc(HWND hwnd, UINT msg, WPARAM wparam,
                                       LPARAM lparam);
HBRUSH ensureInlineEditKeyBrush(AnnotationEditorHost* data);
void updateTextGesture(AnnotationEditorHost* data, int x, int y);
void tryPromoteInlineEditToDrag(AnnotationEditorHost* data, int client_x,
                                int client_y);
void selectFontSizeInCombo(AnnotationEditorHost* data, int font_size);
void measureTextHitSize(HDC hdc, const Annotation& annotation, int& out_width,
                        int& out_height);
void paintLiveInlineText(HDC hdc,
                         const AnnotationEditorPaintSnapshot& snapshot);
void paintInlineEditFrame(HDC hdc,
                          const AnnotationEditorPaintSnapshot& snapshot);
AnnotationEditorTextChrome makeTextChrome(const AnnotationEditorHost* data,
                                          const Annotation& annotation,
                                          bool use_drag_position);
AnnotationEditorTextChrome makeTextChrome(
    const AnnotationEditorPaintSnapshot& snapshot,
    const Annotation& annotation, bool use_drag_position);
std::size_t hitTestTextAnnotation(AnnotationEditorHost* data, int x, int y);
bool isCtrlZKey(WPARAM key);
LRESULT CALLBACK inlineEditSubclassProc(HWND hwnd, UINT msg, WPARAM wparam,
                                        LPARAM lparam);
void invalidateInlineEditRegion(const AnnotationEditorHost* data);
void beginInlineText(AnnotationEditorHost* data, HWND hwnd, int x, int y,
                     std::size_t edit_index);
void beginOrEditTextAt(AnnotationEditorHost* data, HWND hwnd, int x, int y);
void finishTextGesture(AnnotationEditorHost* data, HWND hwnd, int x, int y);

}  // namespace qingying
