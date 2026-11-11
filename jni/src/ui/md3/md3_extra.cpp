#include "ui/md3_extra.h"
#include "md3_internal.h"

namespace aimgui::md3 {
using namespace detail;

bool Carousel(const char* id, int* selected, const CarouselItem* items, int count,
              float width, float height, float itemWidth) {
    if (!selected || !items || count <= 0) return false;
    const Theme& t = GetTheme();
    const bool disabled = Disabled();
    const float w = width > 0 ? Dp(width) : ImGui::GetContentRegionAvail().x;
    const float h = Dp(std::max(80.f, height));
    const float cardW = Dp(std::max(80.f, itemWidth));
    bool changed = false;
    bool navigationHandled = false;
    ImGui::PushID(id);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0,0));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(Dp(8),0));
    ImGui::BeginChild("viewport", ImVec2(w,h), ImGuiChildFlags_None,
                      ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoBackground);
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    auto* storage = ImGui::GetStateStorage();
    for (int i=0; i<count; ++i) {
        ImGui::PushID(i);
        if (i) ImGui::SameLine();
        const auto item = Item("card", ImVec2(cardW,h-ImGui::GetStyle().ScrollbarSize));
        const ImGuiID startKey = ImGui::GetID("drag-start");
        const ImGuiID dragKey = ImGui::GetID("dragged");
        if (ImGui::IsItemActivated()) {
            storage->SetFloat(startKey, ImGui::GetScrollX());
            storage->SetBool(dragKey, false);
        }
        if (!disabled && item.held && ImGui::IsMouseDragging(ImGuiMouseButton_Left, Dp(6))) {
            storage->SetBool(dragKey, true);
            ImGui::SetScrollX(storage->GetFloat(startKey) - ImGui::GetMouseDragDelta().x);
        }
        if (!disabled && item.pressed && !storage->GetBool(dragKey)) {
            changed |= *selected != i; *selected = i;
        }
        if (!disabled && !navigationHandled && ImGui::IsItemFocused()) {
            int next = i;
            if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) next = std::max(0,i-1);
            if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) next = std::min(count-1,i+1);
            if (next != i) {
                changed |= *selected != next; *selected=next;
                // Handle one move per frame, even though the destination may
                // still be visited by this loop. Cancel ImGui's pending spatial
                // move so it cannot apply a second move on the following frame.
                navigationHandled = true;
                ImGui::NavMoveRequestCancel();
                ImGui::PopID(); ImGui::PushID(next);
                const ImGuiID nextId = ImGui::GetID("card");
                ImGui::PopID(); ImGui::PushID(i);
                const ImVec2 offset((next - i) * (cardW + Dp(8)), 0);
                const ImRect nextRect(Add(item.rect.Min, offset), Add(item.rect.Max, offset));
                ImGui::SetNavID(nextId, ImGuiNavLayer_Main, 0,
                               ImGui::WindowRectAbsToRel(window, nextRect));
                ImGui::SetScrollX(next * (cardW + Dp(8)));
            }
        }
        if (item.visible) {
            auto* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(item.rect.Min,item.rect.Max,Color(t.secondaryContainer),Dp(28));
            if (items[i].image.GetTexID() != ImTextureID_Invalid) {
                dl->AddImageRounded(items[i].image,item.rect.Min,item.rect.Max,
                    ImVec2(0,0),ImVec2(1,1),IM_COL32_WHITE,Dp(28));
                dl->AddRectFilled(item.rect.Min,item.rect.Max,IM_COL32(0,0,0,100),Dp(28));
            }
            const bool hasImage=items[i].image.GetTexID()!=ImTextureID_Invalid;
            const ImVec4 fg=hasImage?ImVec4(1,1,1,1):t.onSecondaryContainer;
            const float textY=item.rect.Max.y-Dp(items[i].supporting?64:40);
            dl->PushClipRect(Add(item.rect.Min,ImVec2(Dp(16),Dp(8))),Sub(item.rect.Max,ImVec2(Dp(16),Dp(8))),true);
            Text(ImVec2(item.rect.Min.x+Dp(16),textY),items[i].title?items[i].title:"",fg,20);
            if(items[i].supporting) Text(ImVec2(item.rect.Min.x+Dp(16),textY+Dp(28)),items[i].supporting,fg,14);
            dl->PopClipRect();
            StateLayer(item,fg,Dp(28));
            if(*selected==i) dl->AddRect(Add(item.rect.Min,ImVec2(Dp(2),Dp(2))),Sub(item.rect.Max,ImVec2(Dp(2),Dp(2))),Color(t.primary),Dp(26),Dp(2));
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopID();
    return changed;
}

int BottomAppBar(const char* id,const char* const* actions,int actionCount,
                 const char* fabIcon,float width) {
    const Theme& t=GetTheme();
    const float w=width>0?Dp(width):ImGui::GetContentRegionAvail().x;
    const ImVec2 start=ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddRectFilled(start,Add(start,ImVec2(w,Dp(80))),Color(t.surfaceContainer));
    ImGui::PushID(id);
    ImGui::BeginGroup();
    ImGui::SetCursorScreenPos(Add(start,ImVec2(Dp(4),Dp(16))));
    int result=-1;
    const float available=w-Dp(fabIcon?80:8);
    for(int i=0; actions && i<std::max(0,actionCount) && Dp(48*(i+1))<=available; ++i) {
        ImGui::PushID(i);
        if(i) ImGui::SameLine(0,0);
        if(IconButton("action",actions[i])) result=i;
        ImGui::PopID();
    }
    if(fabIcon && w>=Dp(80)) {
        ImGui::SetCursorScreenPos(ImVec2(start.x+w-Dp(72),start.y+Dp(12)));
        if(Fab("fab",fabIcon)) result=actionCount;
    }
    ImGui::SetCursorScreenPos(start);
    ImGui::Dummy(ImVec2(w,Dp(80)));
    ImGui::EndGroup();
    ImGui::PopID();
    return result;
}

bool RichTooltip(const char* id,const char* title,const char* supporting,
                 const char* action,float width) {
    // A popup is used so its action remains reachable by pointer and keyboard.
    const bool hovered=ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal);
    const bool focused=ImGui::IsItemFocused();
    const ImVec2 anchor=ImGui::GetItemRectMax();
    ImGui::PushID(id);
    if(hovered || (focused && ImGui::IsKeyPressed(ImGuiKey_F1))) ImGui::OpenPopup("rich-tooltip");
    ImGui::SetNextWindowPos(Add(anchor,ImVec2(0,Dp(8))),ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(Dp(std::max(160.f,width)),0));
    ImGui::PushStyleColor(ImGuiCol_PopupBg,GetTheme().surfaceContainerHigh);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding,Dp(12));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(Dp(16),Dp(16)));
    bool activated=false;
    if(ImGui::BeginPopup("rich-tooltip",ImGuiWindowFlags_NoMove)) {
        if(title) ImGui::TextUnformatted(title);
        if(supporting) { ImGui::PushTextWrapPos(0); ImGui::TextUnformatted(supporting); ImGui::PopTextWrapPos(); }
        if(action && Button(action,ButtonStyle::Text)) {activated=true;ImGui::CloseCurrentPopup();}
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    ImGui::PopID();
    return activated;
}
}
