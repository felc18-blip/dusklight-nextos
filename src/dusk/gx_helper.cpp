#include "dusk/gx_helper.h"

#ifdef TARGET_PC
GXTexObjRAII::~GXTexObjRAII() { GXDestroyTexObj(this); }
void GXTexObjRAII::reset() { GXDestroyTexObj(this); }
#endif

GXScopedDebugGroup::GXScopedDebugGroup(const char* text) {
    GXPushDebugGroup(text);
}
GXScopedDebugGroup::~GXScopedDebugGroup() {
    GXPopDebugGroup();
}
