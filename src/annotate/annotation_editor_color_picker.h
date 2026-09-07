#pragma once

#include "annotate/annotation_editor_host.hpp"

namespace qingying {

bool colorPickerIsVisible(const AnnotationEditorHost* data);
void hideColorPicker(AnnotationEditorHost* data, bool apply);
void destroyColorPicker(AnnotationEditorHost* data);
void showColorPicker(AnnotationEditorHost* data);
bool hitTestCurrentColorSwatch(const AnnotationEditorHost* data, int x, int y);
void setColorPickerEyedropping(AnnotationEditorHost* data, bool enabled);
void setColorPickerEyedropperCursor();
bool handleColorPickerEyedropperMove(AnnotationEditorHost* data, int x, int y);
bool handleColorPickerEyedropperClick(AnnotationEditorHost* data, int x, int y);

}  // namespace qingying
