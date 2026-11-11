#include "ui/md3.h"
#include "ui/md3_layout.h"

namespace aimgui::md3 {
void DrawLayoutGallery() {
    static int tab = 0, segment = 0, destination = 0;
    static std::uint64_t multi = 1;
    static bool dialog = false, fullscreen = false, sheet = false, snackbar = false;
    static float timeout = 0;
    static const char* labels[] = {"Music", "Podcasts", "Books"};
    static const NavigationItem destinations[] = {{"Inbox", "1", 3}, {"Sent", "2"}, {"Saved", "3"}};
    ImGui::PushID("md3-layout-gallery");
    TopAppBar("appbar", "Your collection", AppBarStyle::Small, "<");
    Tabs("tabs", &tab, labels, 3);
    ImGui::Spacing();
    SegmentedButtons("single", &segment, labels, 3);
    ImGui::Spacing();
    MultiSegmentedButtons("multi", &multi, labels, 3);
    ImGui::Spacing();
    Navigation("navigation", &destination, destinations, 3);
    ImGui::Spacing();
    Card("card", "A place for your ideas", "Cards group related information.", ImVec2(0, 104));
    ListItem("A new arrival", "Supporting text", "9:41", "+", true);
    Divider();
    ListItem("Saved for later", "A second line of supporting text", "12", "+");
    ImGui::Spacing();
    LinearProgress("progress", .65f);
    ImGui::Spacing();
    CircularProgress("spinner", -1);
    if (Button("Dialog")) dialog = true;
    Tooltip("Open a basic dialog");
    ImGui::SameLine();
    if (Button("Full screen", ButtonStyle::Outlined)) fullscreen = true;
    ImGui::SameLine();
    if (Button("Sheet", ButtonStyle::Outlined)) sheet = true;
    if (Button("Show snackbar", ButtonStyle::Text)) { snackbar = true; timeout = 6; }
    Snackbar("snackbar", "Changes saved", &snackbar, &timeout, "Undo");
    if (BeginDialog("dialog", &dialog, "Save your changes?")) {
        ImGui::TextWrapped("Your changes will be available next time you open this collection.");
        if (Button("Cancel", ButtonStyle::Text)) dialog = false;
        ImGui::SameLine();
        if (Button("Save")) dialog = false;
        EndDialog();
    }
    if (BeginDialog("fullscreen", &fullscreen, "Edit collection", true)) {
        ImGui::TextWrapped("Full-screen dialogs provide more room for a focused task.");
        if (Button("Done")) fullscreen = false;
        EndDialog();
    }
    if (BeginSheet("sheet", &sheet)) {
        ImGui::TextUnformatted("Collection options");
        if (ListItem("Share", "Make this collection available")) sheet = false;
        if (ListItem("Download", "Save for offline use")) sheet = false;
        EndSheet();
    }
    ImGui::PopID();
}
}
