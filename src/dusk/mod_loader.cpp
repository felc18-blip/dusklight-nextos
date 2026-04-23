#include "dusk/mod_loader.hpp"
#include "dusk/hook_system.hpp"
#include "dusk/logging.h"

#include <algorithm>
#include <cstdarg>
#include <filesystem>
#include <string>
#include <unordered_map>

#include "imgui.h"
#include "miniz.h"
#include "nlohmann/json.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

static void* pl_dlopen(const std::filesystem::path& p) {
    return LoadLibraryW(p.wstring().c_str());
}
static void* pl_dlsym(void* h, const char* name) {
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(h), name));
}
static void pl_dlclose(void* h) {
    FreeLibrary(static_cast<HMODULE>(h));
}
static std::string pl_dlerror() {
    char buf[256]{};
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                   GetLastError(), 0, buf, sizeof(buf), nullptr);
    std::string s = buf;
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n'))
        s.pop_back();
    return s;
}
static constexpr const char* k_libExt = ".dll";

#else
#include <dlfcn.h>
static void* pl_dlopen(const std::filesystem::path& p) {
    return dlopen(p.c_str(), RTLD_LAZY | RTLD_LOCAL);
}
static void* pl_dlsym(void* h, const char* name) {
    return dlsym(h, name);
}
static void pl_dlclose(void* h) {
    dlclose(h);
}
static std::string pl_dlerror() {
    const char* e = dlerror();
    return e ? e : "(unknown error)";
}
#if defined(__APPLE__)
static constexpr const char* k_libExt = ".dylib";
#else
static constexpr const char* k_libExt = ".so";
#endif
#endif

static dusk::LoadedMod* g_currentMod = nullptr;
static std::unordered_map<std::string, void*> g_services;

namespace dusk {
void* g_dusk_hook_current_mod = nullptr;
}

struct ModGuard {
    explicit ModGuard(dusk::LoadedMod* m) {
        g_currentMod = m;
        dusk::g_dusk_hook_current_mod = m;
    }
    ~ModGuard() {
        g_currentMod = nullptr;
        dusk::g_dusk_hook_current_mod = nullptr;
    }
};

static const char* modName() {
    return g_currentMod ? g_currentMod->name.c_str() : "mod";
}

static void cb_log_info(const char* fmt, ...) {
    va_list ap, ap2; va_start(ap, fmt); va_copy(ap2, ap);
    std::string s(vsnprintf(nullptr, 0, fmt, ap2), '\0'); va_end(ap2);
    vsnprintf(s.data(), s.size() + 1, fmt, ap); va_end(ap);
    DuskLog.info("[{}] {}", modName(), s);
}

static void cb_log_warn(const char* fmt, ...) {
    va_list ap, ap2; va_start(ap, fmt); va_copy(ap2, ap);
    std::string s(vsnprintf(nullptr, 0, fmt, ap2), '\0'); va_end(ap2);
    vsnprintf(s.data(), s.size() + 1, fmt, ap); va_end(ap);
    DuskLog.warn("[{}] {}", modName(), s);
}

static void cb_log_error(const char* fmt, ...) {
    va_list ap, ap2; va_start(ap, fmt); va_copy(ap2, ap);
    std::string s(vsnprintf(nullptr, 0, fmt, ap2), '\0'); va_end(ap2);
    vsnprintf(s.data(), s.size() + 1, fmt, ap); va_end(ap);
    DuskLog.error("[{}] {}", modName(), s);
}

static void* cb_load_resource(const char* relative_path, size_t* out_size) {
    if (out_size)
        *out_size = 0;
    if (!g_currentMod || !relative_path)
        return nullptr;

    mz_zip_archive zip{};
    if (!mz_zip_reader_init_file(&zip, g_currentMod->mod_path.c_str(), 0)) {
        DuskLog.warn("[{}] load_resource: could not open {}", g_currentMod->name,
                     g_currentMod->mod_path);
        return nullptr;
    }
    std::string entry = std::string("res/") + relative_path;
    size_t sz = 0;
    void* data = mz_zip_reader_extract_file_to_heap(&zip, entry.c_str(), &sz, 0);
    mz_zip_reader_end(&zip);
    if (!data) {
        DuskLog.warn("[{}] load_resource: '{}' not found in zip", g_currentMod->name, entry);
        return nullptr;
    }
    if (out_size)
        *out_size = sz;
    return data;
}

static void cb_free_resource(void* data) {
    mz_free(data);
}

static void cb_register_tab_content(void (*draw_fn)(void*), void* userdata) {
    if (g_currentMod && draw_fn)
        g_currentMod->tab_content.push_back({draw_fn, userdata});
}

static void cb_service_publish(const char* name, void* ptr) {
    if (name) {
        g_services[name] = ptr;
    }
}

static void* cb_service_get(const char* name) {
    if (!name) {
        return nullptr;
    }
    auto it = g_services.find(name);
    return it != g_services.end() ? it->second : nullptr;
}

static void cb_register_menu_item(void (*draw_fn)(void*), void* userdata) {
    if (g_currentMod && draw_fn)
        g_currentMod->menu_items.push_back({draw_fn, userdata});
}

static void api_hook_pre(void* addr, int32_t (*fn)(void* args)) {
    dusk::hookRegisterPre(addr, g_currentMod, fn);
}

static void api_hook_post(void* addr, void (*fn)(void* args)) {
    dusk::hookRegisterPost(addr, g_currentMod, fn);
}

static void api_hook_replace(void* addr, void (*fn)(void* args)) {
    dusk::hookSetReplace(addr, g_currentMod, fn);
}

namespace dusk {

ModLoader& ModLoader::instance() {
    static ModLoader inst;
    return inst;
}

void ModLoader::buildAPI(LoadedMod& mod) {
    mod.api.api_version = DUSK_MOD_API_VERSION;
    mod.api.mod_dir = mod.dir.c_str();
    mod.api.log_info = cb_log_info;
    mod.api.log_warn = cb_log_warn;
    mod.api.log_error = cb_log_error;
    mod.api.load_resource = cb_load_resource;
    mod.api.free_resource = cb_free_resource;
    mod.api.register_tab_content = cb_register_tab_content;
    mod.api.register_menu_item = cb_register_menu_item;
    mod.api.hook_install = hookInstallByAddr;
    mod.api.hook_pre = api_hook_pre;
    mod.api.hook_post = api_hook_post;
    mod.api.hook_replace = api_hook_replace;
    mod.api.hook_dispatch_pre = hookDispatchPre;
    mod.api.hook_dispatch_post = hookDispatchPost;
    mod.api.service_publish = cb_service_publish;
    mod.api.service_get     = cb_service_get;
}

void ModLoader::tryLoadDusk(const std::filesystem::path& modPath) {
    namespace fs = std::filesystem;

    std::string metaName, metaVersion, metaAuthor, metaDescription;
    {
        mz_zip_archive zip{};
        if (mz_zip_reader_init_file(&zip, modPath.string().c_str(), 0)) {
            size_t jsonSize = 0;
            void* jsonData = mz_zip_reader_extract_file_to_heap(&zip, "mod.json", &jsonSize, 0);
            mz_zip_reader_end(&zip);
            if (jsonData) {
                try {
                    std::string jsonStr(static_cast<char*>(jsonData), jsonSize);
                    mz_free(jsonData);
                    jsonData = nullptr;
                    auto j = nlohmann::json::parse(jsonStr);
                    metaName = j.value("name", "");
                    metaVersion = j.value("version", "");
                    metaAuthor = j.value("author", "");
                    metaDescription = j.value("description", "");
                } catch (const std::exception& e) {
                    mz_free(jsonData);
                    DuskLog.warn("ModLoader: bad mod.json in {}: {}", modPath.filename().string(),
                                 e.what());
                }
            }
        }
    }

    mz_zip_archive zip{};
    if (!mz_zip_reader_init_file(&zip, modPath.string().c_str(), 0)) {
        DuskLog.error("ModLoader: failed to open {}", modPath.filename().string());
        return;
    }

    std::string dllEntry;
    for (mz_uint i = 0, n = mz_zip_reader_get_num_files(&zip); i < n; ++i) {
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(&zip, i, &stat))
            continue;
        if (mz_zip_reader_is_file_a_directory(&zip, i))
            continue;
        if (fs::path(stat.m_filename).extension() == k_libExt) {
            dllEntry = stat.m_filename;
            break;
        }
    }

    if (dllEntry.empty()) {
        mz_zip_reader_end(&zip);
        DuskLog.warn("ModLoader: no *{} found in {} — skipping", k_libExt,
                     modPath.filename().string());
        return;
    }

    const fs::path cacheDir = fs::path("mods") / ".cache" / modPath.stem();
    std::error_code ec;
    fs::create_directories(cacheDir, ec);

    const fs::path dllCachePath = cacheDir / fs::path(dllEntry).filename();
    if (!mz_zip_reader_extract_file_to_file(&zip, dllEntry.c_str(), dllCachePath.string().c_str(),
                                            0))
    {
        mz_zip_reader_end(&zip);
        DuskLog.error("ModLoader: failed to extract {} from {}", dllEntry,
                      modPath.filename().string());
        return;
    }
    mz_zip_reader_end(&zip);

    void* handle = pl_dlopen(dllCachePath);
    if (!handle) {
        DuskLog.error("ModLoader: failed to open {}: {}", dllCachePath.string(), pl_dlerror());
        return;
    }

    LoadedMod mod;
    mod.mod_path = fs::absolute(modPath).string();
    mod.dir = fs::absolute(cacheDir).string();
    mod.handle = handle;
    mod.fn_init = reinterpret_cast<LoadedMod::FnInit>(pl_dlsym(handle, "mod_init"));
    mod.fn_tick = reinterpret_cast<LoadedMod::FnTick>(pl_dlsym(handle, "mod_tick"));
    mod.fn_cleanup = reinterpret_cast<LoadedMod::FnCleanup>(pl_dlsym(handle, "mod_cleanup"));
    mod.fn_set_imgui_ctx =
        reinterpret_cast<LoadedMod::FnSetImguiCtx>(pl_dlsym(handle, "dusk_mod_set_imgui_ctx"));

    if (!mod.fn_init || !mod.fn_tick) {
        DuskLog.error("ModLoader: {} missing mod_init or mod_tick — skipping",
                      fs::path(dllEntry).filename().string());
        pl_dlclose(handle);
        return;
    }

    mod.name = metaName.empty() ? modPath.stem().string() : metaName;
    mod.version = metaVersion.empty() ? "?" : metaVersion;
    mod.author = metaAuthor.empty() ? "unknown" : metaAuthor;
    mod.description = metaDescription;

    m_mods.push_back(std::move(mod));
    DuskLog.info("ModLoader: found '{}' v{} by {} ({})", m_mods.back().name, m_mods.back().version,
                 m_mods.back().author, modPath.filename().string());
}

void ModLoader::init() {
    if (m_initialized)
        return;
    m_initialized = true;

    namespace fs = std::filesystem;
    if (!fs::exists(m_modsDir)) {
        DuskLog.info("ModLoader: mods directory '{}' not found — mod loading skipped",
                     m_modsDir.string());
        return;
    }

    std::error_code ec;
    std::vector<fs::directory_entry> entries;
    for (auto& e : fs::directory_iterator(m_modsDir, ec))
        if (e.is_regular_file() && e.path().extension() == ".dusk")
            entries.push_back(e);
    std::sort(entries.begin(), entries.end(),
              [](const fs::directory_entry& a, const fs::directory_entry& b) {
                  return a.path().filename() < b.path().filename();
              });

    for (auto& entry : entries)
        tryLoadDusk(entry.path());

    if (m_mods.empty()) {
        DuskLog.info("ModLoader: no mods found");
        return;
    }

    DuskLog.info("ModLoader: initializing {} mod(s)...", m_mods.size());
    for (auto& mod : m_mods)
        buildAPI(mod);

    for (auto& mod : m_mods) {
        ModGuard guard(&mod);
        try {
            mod.fn_init(&mod.api);
            mod.active = true;
            DuskLog.info("ModLoader: '{}' initialized", mod.name);
        } catch (const std::exception& e) {
            DuskLog.error("ModLoader: exception in {}.mod_init(): {}", mod.name, e.what());
        } catch (...) {
            DuskLog.error("ModLoader: unknown exception in {}.mod_init()", mod.name);
        }
    }

    auto active =
        std::count_if(m_mods.begin(), m_mods.end(), [](const LoadedMod& m) { return m.active; });
    DuskLog.info("ModLoader: {}/{} mod(s) active", active, m_mods.size());
}

void ModLoader::tick() {
    for (auto& mod : m_mods) {
        if (!mod.active)
            continue;
        ModGuard guard(&mod);
        try {
            mod.fn_tick(&mod.api);
        } catch (const std::exception& e) {
            DuskLog.error("ModLoader: exception in {}.mod_tick(): {} — disabling", mod.name,
                          e.what());
            mod.active = false;
        } catch (...) {
            DuskLog.error("ModLoader: unknown exception in {}.mod_tick() — disabling", mod.name);
            mod.active = false;
        }
    }
}

void ModLoader::shutdown() {
    for (auto& mod : m_mods) {
        hookClearMod(&mod);
        if (mod.fn_cleanup) {
            ModGuard guard(&mod);
            try {
                mod.fn_cleanup(&mod.api);
            } catch (...) {
            }
        }
        if (mod.handle) {
            pl_dlclose(mod.handle);
            mod.handle = nullptr;
        }
    }
    m_mods.clear();
    g_services.clear();
    DuskLog.info("ModLoader: all mods unloaded");
}

void ModLoader::callDrawCallback(const LoadedMod& mod, const ModDrawCallback& cb) {
    if (mod.fn_set_imgui_ctx)
        mod.fn_set_imgui_ctx(ImGui::GetCurrentContext());
    cb.draw_fn(cb.userdata);
}

}  // namespace dusk
