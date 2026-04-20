#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "dusk/mod_api.h"

namespace dusk {

struct ModDrawCallback {
    void (*draw_fn)(void* userdata);
    void* userdata;
};

struct LoadedMod {
    std::string name;
    std::string version;
    std::string author;
    std::string description;
    std::string mod_path;
    std::string dir;

    void* handle = nullptr;
    bool  active = false;

    using FnInit        = void (*)(DuskModAPI*);
    using FnTick        = void (*)(DuskModAPI*);
    using FnCleanup     = void (*)(DuskModAPI*);
    using FnSetImguiCtx = void (*)(void*);

    FnInit        fn_init          = nullptr;
    FnTick        fn_tick          = nullptr;
    FnCleanup     fn_cleanup       = nullptr;
    FnSetImguiCtx fn_set_imgui_ctx = nullptr;

    DuskModAPI api{};

    std::vector<ModDrawCallback> tab_content;
    std::vector<ModDrawCallback> menu_items;
};

class ModLoader {
public:
    static ModLoader& instance();

    void setModsDir(std::filesystem::path dir) { m_modsDir = std::move(dir); }
    void init();
    void tick();
    void shutdown();

    const std::vector<LoadedMod>& mods() const { return m_mods; }
    static void callDrawCallback(const LoadedMod& mod, const ModDrawCallback& cb);

private:
    std::vector<LoadedMod> m_mods;
    std::filesystem::path  m_modsDir = "mods";
    bool                   m_initialized = false;

    void tryLoadDusk(const std::filesystem::path& modPath);
    void buildAPI(LoadedMod& mod);
};

} // namespace dusk
