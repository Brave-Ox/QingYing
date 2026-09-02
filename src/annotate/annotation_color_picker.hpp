#pragma once

class AnnotationEditorHost;

namespace qingying {

bool colorPickerIsVisible(const AnnotationEditorHost* data);
void hideColorPicker(AnnotationEditorHost* data, bool apply);
void destroyColorPicker(AnnotationEditorHost* data);
void showColorPicker(AnnotationEditorHost* data);
bool hitTestCurrentColorSwatch(const AnnotationEditorHost* data, int x, int y);
bool handleColorPickerEyedropperClick(AnnotationEditorHost* data, int x, int y);

}  // namespace qingying
