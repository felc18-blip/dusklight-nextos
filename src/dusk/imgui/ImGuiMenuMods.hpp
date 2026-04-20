#pragma once

namespace dusk {

class ImGuiMenuMods {
public:
    void draw();

    void showModsWindow();

private:
    bool m_showWindow = false;
};

} // namespace dusk
