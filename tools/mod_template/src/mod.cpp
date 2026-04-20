#include "d/actor/d_a_alink.h"
#include "dusk/hook.hpp"
#include "dusk/mod_api.h"
#include "imgui.h"
#include "m_Do/m_Do_controller_pad.h"

#include <string>

static int TickCount = 0;
static std::string TextContents;

static int32_t on_posMove_pre(void* args) {
    if (!mDoCPd_c::getHoldR(PAD_1))
        return 0;
    daAlink_c* link = dusk::arg<daAlink_c*>(args, 0);
    link->shape_angle.y -= 2048;
    dusk::g_api->log_info("ROTATING %d", link->shape_angle.y);
    return 0;
}

static void DrawTabContent(void*) {
    daAlink_c* link = daAlink_getAlinkActorClass();
    if (link) {
        ImGui::Text("Y angle: %d", (int)link->shape_angle.y);
        ImGui::Spacing();
        if (ImGui::Button("Reset rotation")) {
            link->shape_angle.y = 0;
        }
    }
    if (!TextContents.empty()) {
        ImGui::Separator();
        ImGui::TextUnformatted(TextContents.c_str());
    }
}

static void DrawMenuItem(void*) {
    if (ImGui::MenuItem("Reset rotation")) {
        daAlink_c* link = daAlink_getAlinkActorClass();
        if (link) {
            link->shape_angle.y = 0;
        }
    }
}

extern "C" {

void dusk_mod_set_imgui_ctx(void* ctx) {
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(ctx));
}

void mod_init(DuskModAPI* api) {
    api->log_info("Test Mod initializing...");

    dusk::init(api);
    dusk::hookAddPre<&daAlink_c::posMove>(on_posMove_pre);

    size_t size = 0;
    void* data = api->load_resource("text.txt", &size);
    if (data) {
        TextContents.assign(static_cast<char*>(data), size);
        api->free_resource(data);
        api->log_info("Loaded text.txt (%zu bytes)", size);
    } else {
        api->log_warn("Failed to load text.txt");
    }

    api->register_tab_content(DrawTabContent, nullptr);
    api->register_menu_item(DrawMenuItem, nullptr);
    api->log_info("Test Mod ready. Mod folder: %s", api->mod_dir);
}

void mod_tick(DuskModAPI* api) {
    ++TickCount;
}

void mod_cleanup(DuskModAPI* api) {
    api->log_info("Test Mod unloading after %d ticks.", TickCount);
}

}
