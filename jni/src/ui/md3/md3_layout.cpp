#include "ui/md3.h"
#include "ui/md3_layout.h"
#include "ui/md3_motion.h"
#include "ui/md3_icons.h"
#include "md3_internal.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace aimgui::md3 {
namespace {
float px(float v) { return detail::Dp(v); }
float widthPx(float width) { return width > 0 ? px(width) : ImMax(1.0f, ImGui::GetContentRegionAvail().x); }
ImU32 col(ImVec4 color, float alpha = 1) { color.w *= alpha; return ImGui::GetColorU32(color); }
const char* endText(const char* text) { return text ? ImGui::FindRenderedTextEnd(text) : nullptr; }
ImVec2 measure(const char* text, float size) {
    return text ? detail::Font()->CalcTextSizeA(px(size), FLT_MAX, 0, text, endText(text)) : ImVec2(0, 0);
}
void textAt(ImDrawList* dl, ImVec2 pos, const char* text, float size, ImVec4 color,
            const ImRect* clip = nullptr, float alpha = 1) {
    if (!text || !*text) return;
    ImVec4 bounds;
    if (clip) bounds = ImVec4(clip->Min.x, clip->Min.y, clip->Max.x, clip->Max.y);
    dl->AddText(detail::Font(), px(size), pos, col(color, alpha), text, endText(text), 0,
                clip ? &bounds : nullptr);
}
void centered(ImDrawList* dl, ImRect rect, const char* label, float size, ImVec4 color, float alpha = 1) {
    if (!label || !*label) return;
    const ImVec2 ts = detail::Measure(label, size, true);
    ImVec4 bounds(rect.Min.x, rect.Min.y, rect.Max.x, rect.Max.y);
    dl->AddText(detail::Font(true), px(size), ImVec2(rect.GetCenter().x - ts.x * .5f, rect.GetCenter().y - ts.y * .5f),
                col(color, alpha), label, endText(label), 0, &bounds);
}
void centeredIcon(ImDrawList* dl, ImRect rect, const char* icon, float size,
                  ImVec4 color, float alpha = 1) {
    if (!icons::DrawSymbol(dl, rect.GetCenter(), px(size), col(color, alpha), icon))
        centered(dl, rect, icon, size, color, alpha);
}
bool interactive(const char* id, ImRect rect, bool enabled, bool* hovered, bool* held) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    *hovered = *held = false;
    ImGui::BeginDisabled(!enabled);
    const ImGuiID itemId = window->GetID(id);
    ImGui::ItemSize(rect);
    bool pressed = false;
    if (ImGui::ItemAdd(rect, itemId)) {
        pressed = ImGui::ButtonBehavior(rect, itemId, hovered, held);
    }
    ImGui::EndDisabled();
    return pressed;
}
bool parentEnabled() { return !(GImGui->CurrentItemFlags & ImGuiItemFlags_Disabled); }
void stateLayer(ImDrawList* dl, ImRect rect, ImVec4 color, bool hovered, bool held,
                bool enabled, float rounding = 0, ImDrawFlags flags = 0) {
    const float opacity = detail::Animate(GImGui->LastItemData.ID, 20, enabled ? (held ? .12f : hovered ? .08f : 0.f) : 0.f);
    if (opacity > .001f) dl->AddRectFilled(rect.Min, rect.Max, col(color, opacity), rounding, flags);
    detail::RippleLayer(GImGui->LastItemData.ID, rect, color, rounding, held, enabled, flags);
    if (enabled) ImGui::RenderNavCursor(rect, GImGui->LastItemData.ID, ImGuiNavRenderCursorFlags_None, rounding);
}
void shadow(ImDrawList* dl, ImRect rect, float radius) {
    dl->AddRectFilled(ImVec2(rect.Min.x, rect.Min.y + px(2)), ImVec2(rect.Max.x, rect.Max.y + px(3)),
                      IM_COL32(0, 0, 0, 18), radius);
    dl->AddRectFilled(ImVec2(rect.Min.x, rect.Min.y + px(1)), ImVec2(rect.Max.x, rect.Max.y + px(1)),
                      IM_COL32(0, 0, 0, 20), radius);
}
ImVec4 cardColor(CardStyle style) {
    const auto& t = GetTheme();
    return style == CardStyle::Elevated ? t.surfaceContainerLow : style == CardStyle::Filled ? t.surfaceContainerHighest : t.surface;
}
void drawBadge(ImDrawList* dl, ImVec2 center, int count) {
    const auto& t = GetTheme();
    if (count < 0) { dl->AddCircleFilled(center, px(3), col(t.error)); return; }
    char number[16];
    if (count > 999) std::snprintf(number, sizeof(number), "999+");
    else std::snprintf(number, sizeof(number), "%d", count);
    float w = ImMax(px(16), measure(number, 11).x + px(8));
    ImRect rect(ImVec2(center.x - w * .5f, center.y - px(8)), ImVec2(center.x + w * .5f, center.y + px(8)));
    dl->AddRectFilled(rect.Min, rect.Max, col(t.error), px(8));
    centered(dl, rect, number, 11, t.onError);
}
void check(ImDrawList* dl, ImVec2 center, ImVec4 color) {
    dl->AddLine(ImVec2(center.x-px(5),center.y), ImVec2(center.x-px(1),center.y+px(4)), col(color), px(2));
    dl->AddLine(ImVec2(center.x-px(1),center.y+px(4)), ImVec2(center.x+px(6),center.y-px(4)), col(color), px(2));
}
bool smallAction(const char* id, ImRect rect, const char* label, ImVec4 color, bool closeIcon = false, float fontSize = 14) {
    bool hovered, held;
    bool pressed = interactive(id, rect, true, &hovered, &held);
    auto* dl = ImGui::GetWindowDrawList();
    stateLayer(dl, rect, color, hovered, held, parentEnabled(), rect.GetHeight()*.5f);
    if (closeIcon) {
        const auto c = rect.GetCenter();
        dl->AddLine(ImVec2(c.x-px(5),c.y-px(5)),ImVec2(c.x+px(5),c.y+px(5)),col(color),px(2));
        dl->AddLine(ImVec2(c.x-px(5),c.y+px(5)),ImVec2(c.x+px(5),c.y-px(5)),col(color),px(2));
    } else centeredIcon(dl, rect, label, fontSize, color);
    return pressed;
}

// NewFrame's native navigation can close a child menu before BeginDialog runs.
// Capture the original top popup so that one Escape never dismisses two levels.
// Hooks are owned by the ImGui context and freed on its shutdown (no global map).
struct PopupSnapshot { ImGuiID top = 0; ImGuiID active = 0; };
void snapshotHook(ImGuiContext* context, ImGuiContextHook* hook) {
    auto* state = static_cast<PopupSnapshot*>(hook->UserData);
    state->top = context->OpenPopupStack.empty() ? 0 : context->OpenPopupStack.back().PopupId;
    state->active = context->ActiveId;
}
void snapshotShutdown(ImGuiContext*, ImGuiContextHook* hook) { delete static_cast<PopupSnapshot*>(hook->UserData); }
PopupSnapshot& popupSnapshot() {
    auto& g = *GImGui;
    for (auto& hook : g.Hooks)
        if (hook.Callback == snapshotHook && hook.Type == ImGuiContextHookType_NewFramePre)
            return *static_cast<PopupSnapshot*>(hook.UserData);
    auto* state = new PopupSnapshot;
    ImGuiContextHook hook;
    hook.Type = ImGuiContextHookType_NewFramePre; hook.Callback = snapshotHook; hook.UserData = state;
    ImGui::AddContextHook(&g, &hook);
    snapshotHook(&g, &hook);
    hook.Type = ImGuiContextHookType_Shutdown; hook.Callback = snapshotShutdown;
    ImGui::AddContextHook(&g, &hook);
    return *state;
}
bool escapeDismissesCurrent() {
    auto& s = popupSnapshot();
    auto* w = ImGui::GetCurrentWindow();
    return s.top == w->PopupId && s.active == 0 && ImGui::IsKeyPressed(ImGuiKey_Escape, false);
}
bool scrimDismissesCurrent() {
    auto& g = *GImGui;
    auto* window = ImGui::GetCurrentWindow();
    return g.OpenPopupStack.Size == g.BeginPopupStack.Size &&
           ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
           !window->Rect().Contains(ImGui::GetMousePos());
}
bool preparePopup(const char* id, bool* open) {
    popupSnapshot();
    if (!open) return false;
    if (!*open) {
        const int level = GImGui->BeginPopupStack.Size;
        if (level < GImGui->OpenPopupStack.Size && GImGui->OpenPopupStack[level].PopupId == ImGui::GetID(id))
            ImGui::ClosePopupToLevel(level, true);
        return false;
    }
    if (!ImGui::IsPopupOpen(id)) ImGui::OpenPopup(id);
    return true;
}
}

bool BeginCard(const char* id, ImVec2 size, CardStyle style) {
    const float width = widthPx(size.x), height = size.y > 0 ? px(size.y) : 0;
    if (style == CardStyle::Elevated && height > 0) {
        const auto p = ImGui::GetCursorScreenPos();
        shadow(ImGui::GetWindowDrawList(), ImRect(p, ImVec2(p.x+width,p.y+height)), px(12));
    }
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, px(12));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, style == CardStyle::Outlined ? px(1) : 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(16),px(16)));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, cardColor(style));
    ImGui::PushStyleColor(ImGuiCol_Border, GetTheme().outlineVariant);
    ImGuiChildFlags flags = ImGuiChildFlags_AlwaysUseWindowPadding;
    if (style == CardStyle::Outlined) flags |= ImGuiChildFlags_Borders;
    if (height == 0) flags |= ImGuiChildFlags_AutoResizeY;
    return ImGui::BeginChild(id, ImVec2(width,height), flags);
}
void EndCard() { ImGui::EndChild(); ImGui::PopStyleColor(2); ImGui::PopStyleVar(3); }

bool Card(const char* id, const char* headline, const char* supporting, ImVec2 size, CardStyle style, bool enabled) {
    auto p = ImGui::GetCursorScreenPos();
    ImRect rect(p, ImVec2(p.x+widthPx(size.x),p.y+px(ImMax(48.0f,size.y))));
    bool hovered, held;
    enabled = enabled && parentEnabled();
    const bool pressed = interactive(id, rect, enabled, &hovered, &held);
    auto* dl = ImGui::GetWindowDrawList(); const auto& t = GetTheme();
    if (style == CardStyle::Elevated && enabled) shadow(dl,rect,px(12));
    dl->AddRectFilled(rect.Min,rect.Max,col(cardColor(style)),px(12));
    if (style == CardStyle::Outlined) dl->AddRect(rect.Min,rect.Max,col(t.outlineVariant),px(12),px(1));
    stateLayer(dl,rect,t.onSurface,hovered,held,enabled,px(12));
    const ImRect clip(ImVec2(p.x+px(16),p.y+px(12)),ImVec2(rect.Max.x-px(16),rect.Max.y-px(12)));
    textAt(dl,clip.Min,headline,16,t.onSurface,&clip,enabled?1:.38f);
    if (supporting) textAt(dl,ImVec2(clip.Min.x,clip.Min.y+px(28)),supporting,14,t.onSurfaceVariant,&clip,enabled?1:.38f);
    return pressed;
}
void Divider(float width, float inset) {
    auto p = ImGui::GetCursorScreenPos();
    const float w = widthPx(width); const float i = ImClamp(px(inset),0.0f,w*.5f);
    ImGui::Dummy(ImVec2(w,px(1)));
    ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x+i,p.y+.5f),ImVec2(p.x+w-i,p.y+.5f),col(GetTheme().outlineVariant),px(1));
}
bool ListItem(const char* headline,const char* supporting,const char* trailing,const char* leading,bool selected,bool enabled,int lines,float width) {
    lines = ImClamp(lines,1,3); if(supporting && lines==1) lines=2;
    auto p=ImGui::GetCursorScreenPos();
    ImRect rect(p,ImVec2(p.x+widthPx(width),p.y+px(lines==1?56:lines==2?72:88)));
    bool hovered,held; enabled=enabled&&parentEnabled();
    const bool pressed=interactive(headline,rect,enabled,&hovered,&held);
    auto* dl=ImGui::GetWindowDrawList();const auto& t=GetTheme();
    dl->AddRectFilled(rect.Min,rect.Max,col(selected?t.secondaryContainer:t.surface));
    stateLayer(dl,rect,t.onSurface,hovered,held,enabled);
    float left=p.x+px(16), right=rect.Max.x-px(16), a=enabled?1:.38f;
    if(leading && *leading) { centered(dl,ImRect(ImVec2(left,p.y),ImVec2(left+px(24),rect.Max.y)),leading,24,t.onSurfaceVariant,a);left+=px(40); }
    if(trailing && *trailing) {auto ts=measure(trailing,12); textAt(dl,ImVec2(right-ts.x,rect.GetCenter().y-ts.y*.5f),trailing,12,t.onSurfaceVariant,&rect,a);right-=ts.x+px(16);}
    const ImRect clip(ImVec2(left,p.y+px(8)),ImVec2(ImMax(left,right),rect.Max.y-px(8)));
    float textY=lines==1?rect.GetCenter().y-px(8):p.y+px(lines==2?15:12);
    textAt(dl,ImVec2(left,textY),headline,16,t.onSurface,&clip,a);
    if(supporting && right>left) {
        // The supporting string may contain a newline; clip enforces 2/3-line height.
        ImVec4 bounds(clip.Min.x,textY+px(24),clip.Max.x,clip.Max.y);
        dl->AddText(detail::Font(),px(14),ImVec2(left,textY+px(24)),col(t.onSurfaceVariant,a),supporting,endText(supporting),right-left,&bounds);
    }
    return pressed;
}
void Badge(int count) {
    drawBadge(ImGui::GetWindowDrawList(),ImVec2(ImGui::GetItemRectMax().x-px(4),ImGui::GetItemRectMin().y+px(4)),count);
}

bool Tabs(const char* id,int* selected,const char* const* labels,int count,float width,bool primary,const bool* enabled) {
    if(!selected||!labels||count<=0) return false;
    ImGui::PushID(id); ImGui::BeginGroup();
    auto p=ImGui::GetCursorScreenPos();const float w=widthPx(width),h=px(48),segment=w/count;
    const auto& t=GetTheme();auto* dl=ImGui::GetWindowDrawList();bool changed=false;
    bool navigationHandled=false;
    dl->AddRectFilled(p,ImVec2(p.x+w,p.y+h),col(t.surface));
    dl->AddLine(ImVec2(p.x,p.y+h-px(.5f)),ImVec2(p.x+w,p.y+h-px(.5f)),col(t.outlineVariant),px(1));
    for(int i=0;i<count;i++) {
        ImGui::PushID(i);ImRect rect(ImVec2(p.x+i*segment,p.y),ImVec2(p.x+(i+1)*segment,p.y+h));
        ImGui::SetCursorScreenPos(rect.Min);bool hover,held;const bool active=(!enabled||enabled[i])&&parentEnabled();
        if(interactive("tab",rect,active,&hover,&held)&&*selected!=i){*selected=i;changed=true;}
        if(active&&!navigationHandled&&ImGui::IsItemFocused()) {
            int next=i, direction=0;
            if(ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) direction=-1;
            else if(ImGui::IsKeyPressed(ImGuiKey_RightArrow)) direction=1;
            else if(ImGui::IsKeyPressed(ImGuiKey_Home)) {next=count-1;direction=1;}
            else if(ImGui::IsKeyPressed(ImGuiKey_End)) {next=0;direction=-1;}
            if(direction) {
                for(int attempt=0;attempt<count;++attempt) {
                    next=(next+direction+count)%count;
                    if(!enabled||enabled[next]) break;
                }
                navigationHandled=true;
                ImGui::NavMoveRequestCancel();
                if(next!=i) {
                    changed|=*selected!=next;*selected=next;
                    ImGui::PopID();ImGui::PushID(next);
                    const ImGuiID nextId=ImGui::GetID("tab");
                    ImGui::PopID();ImGui::PushID(i);
                    const ImRect nextRect(ImVec2(p.x+next*segment,p.y),ImVec2(p.x+(next+1)*segment,p.y+h));
                    ImGuiWindow* window=ImGui::GetCurrentWindow();
                    ImGui::SetNavID(nextId,ImGuiNavLayer_Main,GImGui->CurrentFocusScopeId,ImGui::WindowRectAbsToRel(window,nextRect));
                    ImGui::SetNavCursorVisibleAfterMove();
                }
            }
        }
        stateLayer(dl,rect,t.onSurface,hover,held,active);
        centered(dl,rect,labels[i],14,*selected==i?(primary?t.primary:t.onSurface):t.onSurfaceVariant,active?1:.38f);
        // Material Web primary indicators span the label content (medium font),
        // while secondary indicators span the entire tab.
        if(*selected==i){float iw=primary?ImMin(detail::Measure(labels[i],14,true).x,ImMax(0.f,segment-px(16))):segment;
            dl->AddRectFilled(ImVec2(rect.GetCenter().x-iw*.5f,rect.Max.y-px(primary?3:2)),ImVec2(rect.GetCenter().x+iw*.5f,rect.Max.y),col(t.primary),px(primary?3:0),ImDrawFlags_RoundTop);}
        ImGui::PopID();
    }
    ImGui::SetCursorScreenPos(p);ImGui::Dummy(ImVec2(w,h));ImGui::EndGroup();ImGui::PopID();return changed;
}

namespace {
bool segments(const char* id,int* single,std::uint64_t* multi,const char* const* labels,int count,float width,bool allowEmpty,const bool* enabled) {
    if(!labels||count<=0||count>64||(!single&&!multi))return false;
    ImGui::PushID(id);ImGui::BeginGroup();auto p=ImGui::GetCursorScreenPos();
    const float w=widthPx(width),h=px(40),sw=w/count;auto* dl=ImGui::GetWindowDrawList();const auto& t=GetTheme();bool changed=false;
    for(int i=0;i<count;i++) {
        ImGui::PushID(i);ImRect rect(ImVec2(p.x+i*sw,p.y),ImVec2(p.x+(i+1)*sw,p.y+h));ImGui::SetCursorScreenPos(rect.Min);
        bool hover,held;bool active=(!enabled||enabled[i])&&parentEnabled();bool on=single?*single==i:(*multi&(std::uint64_t(1)<<i))!=0;
        if(interactive("segment",rect,active,&hover,&held)) {
            if(single){if(*single!=i){*single=i;changed=true;}}
            else {auto next=*multi^(std::uint64_t(1)<<i);if(allowEmpty||next){*multi=next;changed=true;}}
            on=single?*single==i:(*multi&(std::uint64_t(1)<<i))!=0;
        }
        ImDrawFlags corners=count==1?ImDrawFlags_RoundAll:i==0?ImDrawFlags_RoundLeft:i==count-1?ImDrawFlags_RoundRight:ImDrawFlags_RoundNone;
        dl->AddRectFilled(rect.Min,rect.Max,col(on?t.secondaryContainer:t.surface),h*.5f,corners);
        stateLayer(dl,rect,on?t.onSecondaryContainer:t.onSurface,hover,held,active,h*.5f,corners);
        if(i>0)dl->AddLine(rect.Min,ImVec2(rect.Min.x,rect.Max.y),col(t.outline),px(1));
        const auto ts=measure(labels[i],14);float total=ts.x+(on?px(26):0);float x=rect.GetCenter().x-total*.5f;
        const ImRect clip(ImVec2(rect.Min.x+px(8),rect.Min.y),ImVec2(rect.Max.x-px(8),rect.Max.y));
        if(on&&sw>=total+px(16)){check(dl,ImVec2(x+px(9),rect.GetCenter().y),t.onSecondaryContainer);x+=px(26);}else x=rect.GetCenter().x-ts.x*.5f;
        textAt(dl,ImVec2(x,rect.GetCenter().y-ts.y*.5f),labels[i],14,on?t.onSecondaryContainer:t.onSurface,&clip,active?1:.38f);
        ImGui::PopID();
    }
    dl->AddRect(p,ImVec2(p.x+w,p.y+h),col(t.outline),h*.5f,px(1));
    ImGui::SetCursorScreenPos(p);ImGui::Dummy(ImVec2(w,h));ImGui::EndGroup();ImGui::PopID();return changed;
}
}
bool SegmentedButtons(const char* id,int* selected,const char* const* labels,int count,float width,const bool* enabled){return segments(id,selected,nullptr,labels,count,width,false,enabled);}
bool MultiSegmentedButtons(const char* id,std::uint64_t* selected,const char* const* labels,int count,float width,bool allowEmpty,const bool* enabled){return segments(id,nullptr,selected,labels,count,width,allowEmpty,enabled);}

bool Navigation(const char* id,int* selected,const NavigationItem* items,int count,NavigationStyle style,float width) {
    if(!selected||!items||count<=0)return false;
    ImGui::PushID(id);ImGui::BeginGroup();auto p=ImGui::GetCursorScreenPos();
    const bool bar=style==NavigationStyle::Bar,rail=style==NavigationStyle::Rail;
    const float w=width>0?px(width):rail?px(80):style==NavigationStyle::Drawer?ImMin(widthPx(0),px(360)):widthPx(0);
    const float itemH=px(bar?80:rail?72:56),h=bar?itemH:count*itemH+px(16);
    auto* dl=ImGui::GetWindowDrawList();const auto& t=GetTheme();bool changed=false;
    dl->AddRectFilled(p,ImVec2(p.x+w,p.y+h),col(bar?t.surfaceContainer:t.surface));
    for(int i=0;i<count;i++) {
        ImGui::PushID(i);ImRect r(bar?ImVec2(p.x+w/count*i,p.y):ImVec2(p.x,p.y+px(8)+i*itemH),bar?ImVec2(p.x+w/count*(i+1),p.y+itemH):ImVec2(p.x+w,p.y+px(8)+(i+1)*itemH));
        ImGui::SetCursorScreenPos(r.Min);bool hover,held;const bool active=items[i].enabled&&parentEnabled();
        if(interactive("destination",r,active,&hover,&held)&&*selected!=i){*selected=i;changed=true;}
        bool on=*selected==i;ImRect indicator;
        if(bar||rail){float iw=ImMin(px(bar?64:56),r.GetWidth()-px(8));indicator=ImRect(ImVec2(r.GetCenter().x-iw*.5f,r.Min.y+px(12)),ImVec2(r.GetCenter().x+iw*.5f,r.Min.y+px(44)));}
        else indicator=ImRect(ImVec2(r.Min.x+px(12),r.Min.y),ImVec2(r.Max.x-px(12),r.Max.y));
        if(on)dl->AddRectFilled(indicator.Min,indicator.Max,col(t.secondaryContainer),px(28));
        stateLayer(dl,indicator,on?t.onSecondaryContainer:t.onSurface,hover,held,active,px(28));
        if(bar||rail){
            if(items[i].icon) centeredIcon(dl,indicator,items[i].icon,24,on?t.onSecondaryContainer:t.onSurfaceVariant,active?1:.38f);
            else centered(dl,indicator,items[i].label,14,on?t.onSecondaryContainer:t.onSurfaceVariant,active?1:.38f);
            centered(dl,ImRect(ImVec2(r.Min.x+px(4),r.Min.y+px(48)),ImVec2(r.Max.x-px(4),r.Min.y+px(68))),items[i].label,12,on?t.onSurface:t.onSurfaceVariant,active?1:.38f);
            if(items[i].badge)drawBadge(dl,ImVec2(indicator.GetCenter().x+px(14),indicator.Min.y+px(5)),items[i].badge);
        }else{
            float x=indicator.Min.x+px(16);
            if(items[i].icon){centeredIcon(dl,ImRect(ImVec2(x,r.Min.y),ImVec2(x+px(24),r.Max.y)),items[i].icon,24,on?t.onSecondaryContainer:t.onSurfaceVariant,active?1:.38f);x+=px(36);}
            ImRect clip(ImVec2(x,r.Min.y),ImVec2(indicator.Max.x-px(items[i].badge?48:16),r.Max.y));
            textAt(dl,ImVec2(x,r.GetCenter().y-px(7)),items[i].label,14,on?t.onSecondaryContainer:t.onSurfaceVariant,&clip,active?1:.38f);
            if(items[i].badge)drawBadge(dl,ImVec2(indicator.Max.x-px(28),r.GetCenter().y),items[i].badge);
        }
        ImGui::PopID();
    }
    ImGui::SetCursorScreenPos(p);ImGui::Dummy(ImVec2(w,h));ImGui::EndGroup();ImGui::PopID();return changed;
}

int TopAppBar(const char* id,const char* title,AppBarStyle style,const char* navigationIcon,const char* const* actions,int actionCount,float width) {
    ImGui::PushID(id); ImGui::BeginGroup();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = widthPx(width);
    const bool expanded = style == AppBarStyle::Medium || style == AppBarStyle::Large;
    const float h = px(style == AppBarStyle::Medium ? 112 : style == AppBarStyle::Large ? 152 : 64);
    auto* dl = ImGui::GetWindowDrawList(); const auto& t = GetTheme();
    dl->AddRectFilled(p,ImVec2(p.x+w,p.y+h),col(t.surface));
    int result = -1;
    float left = p.x + ImMin(px(16),w), right = p.x + ImMax(0.f,w-px(8));
    // Keep each action's hit rectangle inside this bar. On narrow surfaces,
    // navigation has priority; then the first actions that fit are shown.
    if (navigationIcon && w >= px(56)) {
        const ImRect rect(ImVec2(p.x+px(4),p.y+px(8)),ImVec2(p.x+px(52),p.y+px(56)));
        ImGui::SetCursorScreenPos(rect.Min);
        if (smallAction("navigation",rect,navigationIcon,t.onSurface,false,24)) result=-2;
        left=p.x+px(64);
    }
    const int availableActions = static_cast<int>(ImMax(0.f,right-left)/px(48));
    const int visibleActions = actions ? ImMin(ImMax(0,actionCount),availableActions) : 0;
    for (int i=visibleActions-1;i>=0;--i) {
        ImGui::PushID(i);
        const ImRect rect(ImVec2(right-px(48),p.y+px(8)),ImVec2(right,p.y+px(56)));
        ImGui::SetCursorScreenPos(rect.Min);
        if (smallAction("action",rect,actions[i],t.onSurfaceVariant,false,24)) result=i;
        right-=px(48); ImGui::PopID();
    }
    const float fontSize=style==AppBarStyle::Large?36:style==AppBarStyle::Medium?28:22;
    const float titleLeft=expanded?p.x+ImMin(px(16),w):ImMin(left,p.x+w);
    const float titleRight=expanded?p.x+ImMax(0.f,w-px(16)):right;
    const ImRect titleRect(ImVec2(titleLeft,expanded?p.y+px(64):p.y),
                          ImVec2(ImMax(titleLeft,titleRight),expanded?p.y+h-px(12):p.y+px(64)));
    if (titleRect.GetWidth()>0) {
        if (style==AppBarStyle::CenterAligned) {
            const auto size=measure(title,fontSize);
            const float x=ImClamp(p.x+(w-size.x)*.5f,titleRect.Min.x,ImMax(titleRect.Min.x,titleRect.Max.x-size.x));
            textAt(dl,ImVec2(x,titleRect.GetCenter().y-size.y*.5f),title,fontSize,t.onSurface,&titleRect);
        } else {
            textAt(dl,ImVec2(titleRect.Min.x,titleRect.GetCenter().y-px(fontSize)*.5f),title,fontSize,t.onSurface,&titleRect);
        }
    }
    ImGui::SetCursorScreenPos(p); ImGui::Dummy(ImVec2(w,h)); ImGui::EndGroup(); ImGui::PopID();
    return result;
}

void LinearProgress(const char* id,float value,float width){
    ImGui::PushID(id);auto p=ImGui::GetCursorScreenPos();float w=widthPx(width),h=px(4);ImGui::Dummy(ImVec2(w,h));
    // Pinned Material Web linear-progress tokens: highest surface track,
    // four-dp height and square ends for both track and active indicator.
    auto* dl=ImGui::GetWindowDrawList();const auto& t=GetTheme();dl->AddRectFilled(p,ImVec2(p.x+w,p.y+h),col(t.surfaceContainerHighest));
    float start=0,end=ImClamp(std::isfinite(value)?value:0.f,0.f,1.f);
    if(value<0){const auto& motion=GetMotionSettings();const float phase=(motion.reduced||motion.durationScale<=0.f)?.55f:std::fmod(static_cast<float>(ImGui::GetTime()/std::max(.01f,motion.durationScale))*.7f,1.f);start=ImMax(0.f,phase*1.4f-.4f);end=ImMin(1.f,phase*1.4f);}
    if(end>start)dl->AddRectFilled(ImVec2(p.x+w*start,p.y),ImVec2(p.x+w*end,p.y+h),col(t.primary));
    ImGui::PopID();
}
void CircularProgress(const char* id,float value,float diameter){
    // Material Web reserves 4dp inside the 48dp host: the visible circle is
    // 40dp across, with a 10% stroke (4dp at default size) and transparent track.
    ImGui::PushID(id);auto p=ImGui::GetCursorScreenPos();float d=px(ImMax(8.f,diameter)),inner=ImMax(0.f,d-px(8)),thickness=inner*.1f,radius=(inner-thickness)*.5f;ImGui::Dummy(ImVec2(d,d));
    auto* dl=ImGui::GetWindowDrawList();const auto& t=GetTheme();ImVec2 c(p.x+d*.5f,p.y+d*.5f);
    float begin=-IM_PI*.5f,span=ImClamp(std::isfinite(value)?value:0.f,0.f,1.f)*IM_PI*2;
    if(value<0){const auto& motion=GetMotionSettings();const float time=(motion.reduced||motion.durationScale<=0.f)?0.f:static_cast<float>(ImGui::GetTime()/std::max(.01f,motion.durationScale));begin=time*4;span=(.2f+.6f*(.5f+.5f*std::sin(time*2)))*IM_PI*2;}
    if(span>0&&inner>0){dl->PathArcTo(c,radius,begin,begin+span,64);dl->PathStroke(col(t.primary),thickness);}
    ImGui::PopID();
}
void Tooltip(const char* text){
    if(!text||!ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal|ImGuiHoveredFlags_Stationary))return;
    const auto& t=GetTheme();ImGui::PushStyleColor(ImGuiCol_PopupBg,t.inverseSurface);ImGui::PushStyleColor(ImGuiCol_Text,t.inverseOnSurface);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding,px(4));ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(px(8),px(4)));
    ImGui::BeginTooltip();ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+px(240));ImGui::TextUnformatted(text);ImGui::PopTextWrapPos();ImGui::EndTooltip();ImGui::PopStyleVar(2);ImGui::PopStyleColor(2);
}
bool Snackbar(const char* id,const char* message,bool* open,float* remainingSeconds,const char* action,float width,bool dismissible){
    if(!open||!*open)return false;
    ImGui::PushID(id);ImGui::BeginGroup();
    const auto p=ImGui::GetCursorScreenPos();const float w=widthPx(width);
    auto* dl=ImGui::GetWindowDrawList();const auto& t=GetTheme();
    // Keep every action inside the surface even in a narrow parent. Preserve
    // a minimum hit target, clipping long labels instead of overlapping it.
    const bool showDismiss=dismissible&&w>=px(48);
    const float availableAction=ImMax(0.f,w-px(8)-px(showDismiss?40:0));
    const float actionWidth=action&&*action&&availableAction>=px(40)
        ?ImMin(measure(action,14).x+px(24),availableAction):0;
    const float textWidth=ImMax(1.f,w-px(28)-actionWidth-px(showDismiss?40:0));
    const ImVec2 wrapped=message?detail::Font()->CalcTextSizeA(px(14),FLT_MAX,textWidth,message,endText(message)):ImVec2(0,0);
    const float h=px(wrapped.y>px(21)?68:48),centerY=p.y+h*.5f;
    dl->AddRectFilled(p,ImVec2(p.x+w,p.y+h),col(t.inverseSurface),px(4));
    float right=p.x+w-px(4);bool hit=false,focused=false;
    if(showDismiss){
        ImRect r(ImVec2(right-px(40),centerY-px(20)),ImVec2(right,centerY+px(20)));
        ImGui::SetCursorScreenPos(r.Min);
        if(smallAction("dismiss",r,nullptr,t.inverseOnSurface,true))*open=false;
        focused|=ImGui::IsItemFocused();right-=px(40);
    }
    if(actionWidth>0){
        ImRect r(ImVec2(right-actionWidth,centerY-px(20)),ImVec2(right,centerY+px(20)));
        ImGui::SetCursorScreenPos(r.Min);hit=smallAction("action",r,action,t.inversePrimary);
        focused|=ImGui::IsItemFocused();if(hit)*open=false;right-=actionWidth;
    }
    if(message){ImVec4 bounds(p.x+px(16),p.y+px(12),ImMax(p.x+px(16),right-px(8)),p.y+h-px(12));
        dl->AddText(detail::Font(),px(14),ImVec2(bounds.x,centerY-ImMin(wrapped.y,px(40))*.5f),col(t.inverseOnSurface),message,endText(message),textWidth,&bounds);}
    const bool hovered=ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)&&
        ImGui::IsMouseHoveringRect(p,ImVec2(p.x+w,p.y+h));
    // Never expire an action while the user is reading or navigating it. An
    // automatic initial NavId is not visible keyboard focus.
    if(*open&&remainingSeconds&&*remainingSeconds>0&&!hovered&&!(focused&&GImGui->NavCursorVisible)) {
        *remainingSeconds=ImMax(0.f,*remainingSeconds-ImGui::GetIO().DeltaTime);
        if(*remainingSeconds==0)*open=false;
    }
    ImGui::SetCursorScreenPos(p);ImGui::Dummy(ImVec2(w,h));ImGui::EndGroup();ImGui::PopID();return hit;
}

bool BeginDialog(const char* id,bool* open,const char* title,bool fullscreen,float width,bool dismissible){
    if(!preparePopup(id,open))return false;
    auto* viewport=ImGui::GetMainViewport();const auto& t=GetTheme();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x+viewport->WorkSize.x*.5f,viewport->WorkPos.y+viewport->WorkSize.y*.5f),ImGuiCond_Always,ImVec2(.5f,.5f));
    if(fullscreen)ImGui::SetNextWindowSize(viewport->WorkSize);
    else{float w=ImMin(px(ImMax(280.f,width)),ImMax(px(120),viewport->WorkSize.x-px(48)));ImGui::SetNextWindowSize(ImVec2(w,0));ImGui::SetNextWindowSizeConstraints(ImVec2(w,0),ImVec2(w,ImMax(px(96),viewport->WorkSize.y-px(48))));}
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,fullscreen?0:px(28));ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(px(24),px(24)));ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize,0);
    ImGui::PushStyleColor(ImGuiCol_PopupBg,t.surfaceContainerHigh);ImGui::PushStyleColor(ImGuiCol_Text,t.onSurface);
    ImGuiWindowFlags flags=ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove;
    if(!fullscreen)flags|=ImGuiWindowFlags_AlwaysAutoResize;
    if(!ImGui::BeginPopupModal(id,nullptr,flags)){ImGui::PopStyleColor(2);ImGui::PopStyleVar(3);return false;}
    if(dismissible&&(escapeDismissesCurrent()||scrimDismissesCurrent())){*open=false;ImGui::CloseCurrentPopup();EndDialog();return false;}
    auto p=ImGui::GetCursorScreenPos();const float avail=ImGui::GetContentRegionAvail().x;ImRect tr(p,ImVec2(p.x+avail-px(dismissible?40:0),p.y+px(40)));
    textAt(ImGui::GetWindowDrawList(),ImVec2(tr.Min.x,p.y+px(6)),title,24,t.onSurface,&tr);
    if(dismissible){ImRect r(ImVec2(p.x+avail-px(40),p.y),ImVec2(p.x+avail,p.y+px(40)));ImGui::SetCursorScreenPos(r.Min);if(smallAction("##dialogClose",r,nullptr,t.onSurfaceVariant,true)){*open=false;ImGui::CloseCurrentPopup();EndDialog();return false;}}
    ImGui::SetCursorScreenPos(p);ImGui::Dummy(ImVec2(avail,px(48)));return true;
}
void EndDialog(){ImGui::EndPopup();ImGui::PopStyleColor(2);ImGui::PopStyleVar(3);}
bool BeginSheet(const char* id,bool* open,SheetSide side,float extent,bool dismissible){
    if(!preparePopup(id,open))return false;
    auto* v=ImGui::GetMainViewport();const auto& t=GetTheme();
    const bool bottom=side==SheetSide::Bottom;float e=px(ImMax(96.f,extent));ImVec2 size=bottom?ImVec2(v->WorkSize.x,ImMin(e,v->WorkSize.y)):ImVec2(ImMin(e,v->WorkSize.x),v->WorkSize.y);
    ImVec2 pos=v->WorkPos;if(bottom)pos.y+=v->WorkSize.y-size.y;else if(side==SheetSide::Right)pos.x+=v->WorkSize.x-size.x;
    ImGui::SetNextWindowPos(pos);ImGui::SetNextWindowSize(size);ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,0);ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(px(24),px(24)));ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize,0);
    ImGui::PushStyleColor(ImGuiCol_PopupBg,ImVec4(0,0,0,0));ImGui::PushStyleColor(ImGuiCol_Text,t.onSurface);
    if(!ImGui::BeginPopupModal(id,nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove)){ImGui::PopStyleColor(2);ImGui::PopStyleVar(3);return false;}
    if(dismissible&&(escapeDismissesCurrent()||scrimDismissesCurrent())){*open=false;ImGui::CloseCurrentPopup();EndSheet();return false;}
    auto* dl=ImGui::GetWindowDrawList();auto* win=ImGui::GetCurrentWindow();ImDrawFlags corners=bottom?ImDrawFlags_RoundTop:side==SheetSide::Left?ImDrawFlags_RoundRight:ImDrawFlags_RoundLeft;
    dl->AddRectFilled(win->Pos,ImVec2(win->Pos.x+win->Size.x,win->Pos.y+win->Size.y),col(t.surfaceContainerLow),px(28),corners);
    if(bottom){
        auto c=ImVec2(win->Pos.x+win->Size.x*.5f,win->Pos.y+px(12));
        dl->AddRectFilled(ImVec2(c.x-px(16),c.y),ImVec2(c.x+px(16),c.y+px(4)),col(t.onSurfaceVariant,.4f),px(2));
        if(dismissible){
            const auto saved=ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos(ImVec2(c.x-px(40),win->Pos.y));
            ImGui::InvisibleButton("##sheetDrag",ImVec2(px(80),px(24)));
            const ImGuiID dragKey=win->GetID("##sheetDragDistance");
            if(ImGui::IsItemActivated())ImGui::GetStateStorage()->SetFloat(dragKey,0);
            if(ImGui::IsItemActive())ImGui::GetStateStorage()->SetFloat(dragKey,ImGui::GetMouseDragDelta(ImGuiMouseButton_Left).y);
            const bool dragDismiss=ImGui::IsItemDeactivated()&&ImGui::GetStateStorage()->GetFloat(dragKey)>px(48);
            ImGui::SetCursorScreenPos(saved);
            if(dragDismiss){*open=false;ImGui::CloseCurrentPopup();EndSheet();return false;}
        }
    }
    if(dismissible){auto p=ImGui::GetCursorScreenPos();float w=ImGui::GetContentRegionAvail().x;ImRect r(ImVec2(p.x+w-px(40),p.y),ImVec2(p.x+w,p.y+px(40)));ImGui::SetCursorScreenPos(r.Min);if(smallAction("##sheetClose",r,nullptr,t.onSurfaceVariant,true)){*open=false;ImGui::CloseCurrentPopup();EndSheet();return false;}ImGui::SetCursorScreenPos(p);ImGui::Dummy(ImVec2(w,px(40)));}
    return true;
}
void EndSheet(){EndDialog();}
}
