#pragma once

#include <cstdint>

namespace dusk {

void hookInstallByAddr(void* fn_addr, void* tramp_fn, void** orig_store);

void hookRegisterPre (void* fn_addr, void* mod, int32_t (*fn)(void* args));
void hookRegisterPost(void* fn_addr, void* mod, void    (*fn)(void* args));
void hookSetReplace  (void* fn_addr, void* mod, void    (*fn)(void* args));

bool hookDispatchPre (void* fn_addr, void* args);
void hookDispatchPost(void* fn_addr, void* args);

void hookClearMod(void* mod);

} // namespace dusk
