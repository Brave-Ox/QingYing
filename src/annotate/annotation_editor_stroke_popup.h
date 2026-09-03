#pragma once

#include "annotate/annotation_editor_host.hpp"

namespace qingying {

int currentStrokeWidthPx(const AnnotationEditorHost* data);
void syncStrokePopupEdit(AnnotationEditorHost* data);
void applyStrokeFromPopupEdit(AnnotationEditorHost* data);
bool handleStrokePopupEditCommand(AnnotationEditorHost* data, UINT id, UINT code,
                                  HWND control);
void applyEditorStrokeWidth(AnnotationEditorHost* data, int width);
bool handleStrokeChipWheel(AnnotationEditorHost* data, int delta);
bool strokePopupIsVisible(const AnnotationEditorHost* data);
void hideStrokePopup(AnnotationEditorHost* data);
void destroyStrokePopup(AnnotationEditorHost* data);
void showStrokePopup(AnnotationEditorHost* data);
void applyStrokeFromPopupSlider(AnnotationEditorHost* data, int client_x);
LRESULT CALLBACK strokePopupWndProc(HWND hwnd, UINT msg, WPARAM wparam,
                                    LPARAM lparam);
bool registerStrokePopupClass(HINSTANCE instance);

}  // namespace qingying
