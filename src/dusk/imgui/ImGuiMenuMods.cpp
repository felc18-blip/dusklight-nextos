#include "ImGuiMenuMods.hpp"

#include "ImGuiConsole.hpp"
#include "dusk/mod_loader.hpp"
#include "imgui.h"

namespace dusk {

void ImGuiMenuMods::draw() {
    const auto& mods = ModLoader::instance().mods();
    if (mods.empty()) return;

    if (ImGui::BeginMenu("Mods")) {
        if (ImGui::MenuItem("Mod Manager", nullptr, m_showWindow)) {
            m_showWindow = !m_showWindow;
        }

        for (const auto& mod : mods) {
            if (mod.menu_items.empty()) continue;
            ImGui::Separator();
            if (ImGui::BeginMenu(mod.name.c_str())) {
                for (const auto& item : mod.menu_items) {
                    ModLoader::callDrawCallback(mod, item);
                }
                ImGui::EndMenu();
            }
        }

        ImGui::EndMenu();
    }
}

void ImGuiMenuMods::showModsWindow() {
    if (!m_showWindow) return;

    ImGui::SetNextWindowSize(ImVec2(520, 420), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Mod Manager", &m_showWindow)) {
        ImGui::End();
        return;
    }

    const auto& mods = ModLoader::instance().mods();
    if (mods.empty()) {
        ImGuiTextCenter("No mods loaded.");
        ImGui::End();
        return;
    }

    if (ImGui::BeginTabBar("##ModsOuter")) {
        for (const auto& mod : mods) {
            const std::string tabLabel = mod.name + (mod.active ? "" : " [disabled]");

            if (ImGui::BeginTabItem(tabLabel.c_str())) {
                ImGui::Text("Version: %s", mod.version.c_str());
                ImGui::Text("Author: %s", mod.author.c_str());
                ImGui::Text("Status: %s", mod.active ? "Active" : "Disabled");
                ImGui::Text("Path: %s", mod.mod_path.c_str());

                if (!mod.description.empty()) {
                    ImGui::Separator();
                    ImGui::TextWrapped("%s", mod.description.c_str());
                }

                for (const auto& cb : mod.tab_content) {
                    ImGui::Separator();
                    ModLoader::callDrawCallback(mod, cb);
                }

                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
}

} // namespace dusk
