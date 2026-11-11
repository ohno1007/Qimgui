/* SPDX-License-Identifier: MIT */
#include "qk_ui.h"

#include "qk_arena.h"
#include "qk_math.h"

#include "imgui.h"
#include "ui/md3.h"
#include "ui/md3_layout.h"
#include "ui/md3_showcase.h"
#include "core/font_data.h"

namespace md = aimgui::md3;

namespace {

/* Growable array over the arena. ImGui's own buffers live there too, so the
 * frame never calls a kernel allocator.
 */
template <class T>
class Buffer {
public:
    void reset() { count_ = 0; }
    void release() {
        qk_arena_free(data_);
        data_ = nullptr;
        count_ = capacity_ = 0;
    }
    bool reserve(u32 wanted) {
        if (wanted <= capacity_)
            return true;
        u32 capacity = capacity_ ? capacity_ : 256;
        while (capacity < wanted)
            capacity *= 2;
        T* grown = (T*)qk_arena_alloc((size_t)capacity * sizeof(T));
        if (!grown)
            return false;
        for (u32 i = 0; i < count_; i++)
            grown[i] = data_[i];
        qk_arena_free(data_);
        data_ = grown;
        capacity_ = capacity;
        return true;
    }
    bool push(const T& value) {
        if (!reserve(count_ + 1))
            return false;
        data_[count_++] = value;
        return true;
    }
    T* data() { return data_; }
    const T* data() const { return data_; }
    u32 size() const { return count_; }

private:
    T* data_ = nullptr;
    u32 count_ = 0, capacity_ = 0;
};

constexpr int kMaxTextures = 8;

struct TextureSlot {
    ImTextureData* owner = nullptr;
    qk_texture description{};
};

struct UiState {
    bool ready = false;
    s32 width = 0, height = 0;
    float scale = 1.f;
    bool dark = false;
    bool shift = false;
    ImFont* font = nullptr;
    u64 frames = 0;
    Buffer<qk_vertex> vertices;
    Buffer<u32> indices;
    Buffer<qk_command> commands;
    TextureSlot slots[kMaxTextures];
    qk_texture textures[kMaxTextures];
    qk_draw_data draw{};
};

UiState ui;

void* ArenaAlloc(size_t bytes, void*) { return qk_arena_alloc(bytes); }
void ArenaFree(void* pointer, void*) { qk_arena_free(pointer); }

/* Linux input codes, spelled out so this file builds for the host too. */
enum {
    KEY_ESC = 1, KEY_1 = 2, KEY_0 = 11, KEY_MINUS = 12, KEY_EQUAL = 13,
    KEY_BACKSPACE = 14, KEY_TAB = 15, KEY_Q = 16, KEY_ENTER = 28,
    KEY_LEFTCTRL = 29, KEY_A = 30, KEY_LEFTSHIFT = 42, KEY_Z = 44,
    KEY_SLASH = 53, KEY_RIGHTSHIFT = 54, KEY_SPACE = 57, KEY_HOME = 102,
    KEY_UP = 103, KEY_LEFT = 105, KEY_RIGHT = 106, KEY_END = 107,
    KEY_DOWN = 108, KEY_DELETE = 111,
};

/* US layout, unshifted then shifted, for the rows the kernel UI can receive
 * from a hardware keyboard. The device has no soft keyboard here.
 */
struct KeyEntry {
    u32 code;
    ImGuiKey key;
    char plain, shifted;
};

const KeyEntry kKeys[] = {
    {KEY_ESC, ImGuiKey_Escape, 0, 0},
    {KEY_BACKSPACE, ImGuiKey_Backspace, 0, 0},
    {KEY_TAB, ImGuiKey_Tab, 0, 0},
    {KEY_ENTER, ImGuiKey_Enter, 0, 0},
    {KEY_SPACE, ImGuiKey_Space, ' ', ' '},
    {KEY_HOME, ImGuiKey_Home, 0, 0},
    {KEY_END, ImGuiKey_End, 0, 0},
    {KEY_DELETE, ImGuiKey_Delete, 0, 0},
    {KEY_UP, ImGuiKey_UpArrow, 0, 0},
    {KEY_DOWN, ImGuiKey_DownArrow, 0, 0},
    {KEY_LEFT, ImGuiKey_LeftArrow, 0, 0},
    {KEY_RIGHT, ImGuiKey_RightArrow, 0, 0},
    {KEY_MINUS, ImGuiKey_Minus, '-', '_'},
    {KEY_EQUAL, ImGuiKey_Equal, '=', '+'},
    {KEY_SLASH, ImGuiKey_Slash, '/', '?'},
    {KEY_LEFTSHIFT, ImGuiKey_LeftShift, 0, 0},
    {KEY_RIGHTSHIFT, ImGuiKey_RightShift, 0, 0},
    {KEY_LEFTCTRL, ImGuiKey_LeftCtrl, 0, 0},
};

const char kDigitRow[] = "1234567890";
const char kDigitShifted[] = "!@#$%^&*()";
/* Scan-code order of the three letter rows. */
const char kRowQ[] = "qwertyuiop";
const char kRowA[] = "asdfghjkl";
const char kRowZ[] = "zxcvbnm";

bool TranslateKey(u32 code, ImGuiKey* key, char* character)
{
    *key = ImGuiKey_None;
    *character = 0;
    for (const KeyEntry& entry : kKeys) {
        if (entry.code != code)
            continue;
        *key = entry.key;
        *character = ui.shift ? entry.shifted : entry.plain;
        return true;
    }
    if (code >= KEY_1 && code <= KEY_0) {
        const int index = (int)(code - KEY_1);
        *key = (ImGuiKey)(ImGuiKey_0 + (index == 9 ? 0 : index + 1));
        *character = ui.shift ? kDigitShifted[index] : kDigitRow[index];
        return true;
    }
    if (code >= KEY_Q && code < KEY_Q + 10) {
        *character = kRowQ[code - KEY_Q];
    } else if (code >= KEY_A && code < KEY_A + 9) {
        *character = kRowA[code - KEY_A];
    } else if (code >= KEY_Z && code < KEY_Z + 7) {
        *character = kRowZ[code - KEY_Z];
    } else {
        return false;
    }
    *key = (ImGuiKey)(ImGuiKey_A + (*character - 'a'));
    if (ui.shift)
        *character = (char)(*character - 'a' + 'A');
    return true;
}

void Feed(const qk_ui_event& event)
{
    ImGuiIO& io = ImGui::GetIO();

    switch (event.type) {
    case QK_EVENT_POINTER_DOWN:
        io.AddMousePosEvent((float)event.x, (float)event.y);
        io.AddMouseButtonEvent(0, true);
        break;
    case QK_EVENT_POINTER_MOVE:
        io.AddMousePosEvent((float)event.x, (float)event.y);
        break;
    case QK_EVENT_POINTER_UP:
        io.AddMousePosEvent((float)event.x, (float)event.y);
        io.AddMouseButtonEvent(0, false);
        break;
    case QK_EVENT_SCROLL:
        io.AddMouseWheelEvent((float)event.x, (float)event.y);
        break;
    case QK_EVENT_KEY: {
        ImGuiKey key = ImGuiKey_None;
        char character = 0;

        if (event.code == KEY_LEFTSHIFT || event.code == KEY_RIGHTSHIFT)
            ui.shift = event.down != 0;
        if (!TranslateKey(event.code, &key, &character))
            break;
        if (key != ImGuiKey_None)
            io.AddKeyEvent(key, event.down != 0);
        if (event.down && character)
            io.AddInputCharacter((unsigned int)(unsigned char)character);
        break;
    }
    default:
        break;
    }
}

void Header()
{
    const md::Theme &theme = md::GetTheme();

    /* The embedded font is Latin only: the kernel has no access to the
     * device's CJK fonts, so this UI stays in English. See docs/KERNEL_PORT.md.
     */
    ImGui::PushFont(ImGui::GetFont(), 22.f * theme.scale);
    ImGui::TextUnformatted("Qimgui in kernel space");
    ImGui::PopFont();
    md::Divider();
    /* The gallery owns the dark-theme switch and its own category tabs. */
}

void Content()
{
    md::PushTheme();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("qimgui-kernel", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);
    Header();
    /* The gallery owns its own category tabs and page state. */
    md::Showcase(-1);
    ImGui::End();
    md::PopTheme();
}

int SlotFor(ImTextureData* texture)
{
    for (int slot = 1; slot < kMaxTextures; slot++)
        if (ui.slots[slot].owner == texture)
            return slot;
    for (int slot = 1; slot < kMaxTextures; slot++)
        if (!ui.slots[slot].owner) {
            ui.slots[slot].owner = texture;
            return slot;
        }
    return -1;
}

/* ImGui asks the backend to create, update and destroy its atlas textures. The
 * rasteriser reads ImGui's own pixels, so an update is just an acknowledgement.
 */
bool SyncTextures()
{
    for (ImTextureData* texture : ImGui::GetPlatformIO().Textures) {
        if (texture->Status == ImTextureStatus_WantDestroy) {
            const int slot = (int)(intptr_t)texture->GetTexID();

            if (slot > 0 && slot < kMaxTextures && ui.slots[slot].owner == texture)
                ui.slots[slot].owner = nullptr;
            texture->SetTexID(ImTextureID_Invalid);
            texture->SetStatus(ImTextureStatus_Destroyed);
            continue;
        }
        if (texture->Status != ImTextureStatus_WantCreate &&
            texture->Status != ImTextureStatus_WantUpdates)
            continue;
        const int slot = SlotFor(texture);
        if (slot < 0)
            return false;
        texture->SetTexID((ImTextureID)(intptr_t)slot);
        texture->SetStatus(ImTextureStatus_OK);
    }
    for (int slot = 0; slot < kMaxTextures; slot++) {
        ImTextureData* owner = ui.slots[slot].owner;
        qk_texture& description = ui.textures[slot];

        description = qk_texture{};
        if (!owner || owner->Status != ImTextureStatus_OK || !owner->GetPixels())
            continue;
        description.pixels = (const u8*)owner->GetPixels();
        description.width = owner->Width;
        description.height = owner->Height;
        description.pitch = (u32)owner->GetPitch();
        description.format = owner->Format == ImTextureFormat_Alpha8 ? QK_TEXTURE_ALPHA8
                                                                     : QK_TEXTURE_RGBA32;
        ui.slots[slot].description = description;
    }
    return true;
}

s32 ToFixed(float value, int bits)
{
    const float scaled = value * (float)(1 << bits);

    /* Round to nearest so a half-pixel coordinate does not drift. */
    return (s32)qk_roundf(scaled);
}

/* One converted copy of a list's vertices per texture it uses: ImGui's UVs are
 * normalised, the rasteriser's are texels.
 */
struct VertexRange {
    int slot;
    u32 base;
};

bool Translate(const ImDrawData* data)
{
    ui.vertices.reset();
    ui.indices.reset();
    ui.commands.reset();

    for (const ImDrawList* list : data->CmdLists) {
        VertexRange ranges[kMaxTextures];
        u32 range_count = 0;

        for (const ImDrawCmd& command : list->CmdBuffer) {
            if (command.UserCallback || !command.ElemCount)
                continue;
            const int slot = (int)(intptr_t)command.GetTexID();
            if (slot < 0 || slot >= kMaxTextures)
                return false;
            const qk_texture& texture = ui.textures[slot];
            u32 base = 0;
            bool found = false;

            for (u32 i = 0; i < range_count; i++)
                if (ranges[i].slot == slot) {
                    base = ranges[i].base;
                    found = true;
                    break;
                }
            if (!found) {
                base = ui.vertices.size();
                if (!ui.vertices.reserve(base + (u32)list->VtxBuffer.Size))
                    return false;
                const float width = texture.width > 0 ? (float)texture.width : 1.f;
                const float height = texture.height > 0 ? (float)texture.height : 1.f;

                for (const ImDrawVert& source : list->VtxBuffer) {
                    qk_vertex vertex;

                    vertex.x = ToFixed(source.pos.x, QK_SUBPIXEL_BITS);
                    vertex.y = ToFixed(source.pos.y, QK_SUBPIXEL_BITS);
                    vertex.u = ToFixed(source.uv.x * width, QK_UV_BITS);
                    vertex.v = ToFixed(source.uv.y * height, QK_UV_BITS);
                    vertex.color = source.col;
                    if (!ui.vertices.push(vertex))
                        return false;
                }
                if (range_count < kMaxTextures)
                    ranges[range_count++] = VertexRange{slot, base};
            }

            qk_command out;
            out.index_offset = ui.indices.size();
            out.index_count = command.ElemCount;
            out.texture = texture.format == QK_TEXTURE_NONE ? 0u : (u32)slot;
            out.clip.x0 = (s32)qk_floorf(command.ClipRect.x);
            out.clip.y0 = (s32)qk_floorf(command.ClipRect.y);
            out.clip.x1 = (s32)qk_ceilf(command.ClipRect.z);
            out.clip.y1 = (s32)qk_ceilf(command.ClipRect.w);
            if (!ui.indices.reserve(out.index_offset + command.ElemCount))
                return false;
            for (unsigned int i = 0; i < command.ElemCount; i++) {
                const u32 index = base + command.VtxOffset +
                                  list->IdxBuffer[command.IdxOffset + i];

                if (index >= ui.vertices.size() || !ui.indices.push(index))
                    return false;
            }
            if (!ui.commands.push(out))
                return false;
        }
    }

    ui.draw.vertices = ui.vertices.data();
    ui.draw.vertex_count = ui.vertices.size();
    ui.draw.indices = ui.indices.data();
    ui.draw.index_count = ui.indices.size();
    ui.draw.commands = ui.commands.data();
    ui.draw.command_count = ui.commands.size();
    ui.draw.textures = ui.textures;
    ui.draw.texture_count = kMaxTextures;
    return true;
}

} // namespace

extern "C" int qk_ui_init(const struct qk_ui_config* config)
{
    if (!config || config->width <= 0 || config->height <= 0 ||
        config->width > QK_MAX_DIMENSION || config->height > QK_MAX_DIMENSION)
        return -22;
    if (ui.ready)
        return -16; /* -EBUSY */

    /* ImGui dereferences whatever its allocator returns, so a region that
     * cannot hold a frame is refused here rather than faulting mid-frame.
     */
    struct qk_arena_stats arena;
    qk_arena_get_stats(&arena);
    if (arena.capacity + arena.reserve + 4096 < QK_ARENA_MINIMUM || qk_arena_exhausted())
        return -12; /* -ENOMEM */

    ImGui::SetAllocatorFunctions(ArenaAlloc, ArenaFree, nullptr);
    if (!ImGui::CreateContext())
        return -12; /* -ENOMEM */

    ui.width = config->width;
    ui.height = config->height;
    ui.dark = config->dark != 0;
    ui.scale = (float)(config->scale_percent < 50 ? 100 :
                       (config->scale_percent > 300 ? 300 : config->scale_percent)) / 100.f;

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.DisplaySize = ImVec2((float)ui.width, (float)ui.height);
    io.DeltaTime = 1.f / 60.f;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures |
                       ImGuiBackendFlags_RendererHasVtxOffset;
    /* No cursor and no clipboard owner inside the kernel. */
    io.MouseDrawCursor = false;

    ImFontConfig font{};
    font.FontDataOwnedByAtlas = false;
    ui.font = io.Fonts->AddFontFromMemoryTTF(
        (void*)aimgui::font_data::kUiFont, (int)sizeof(aimgui::font_data::kUiFont),
        16.f * ui.scale, &font);
    if (!ui.font) {
        ImGui::DestroyContext();
        return -12;
    }
    md::SetFonts(ui.font, ui.font);
    md::SetTheme(ui.dark, ui.scale);
    if (qk_arena_exhausted()) {
        ImGui::DestroyContext();
        return -12;
    }
    ui.ready = true;
    return 0;
}

extern "C" void qk_ui_shutdown(void)
{
    if (!ui.ready)
        return;
    for (ImTextureData* texture : ImGui::GetPlatformIO().Textures) {
        texture->SetTexID(ImTextureID_Invalid);
        texture->SetStatus(ImTextureStatus_Destroyed);
    }
    ui.vertices.release();
    ui.indices.release();
    ui.commands.release();
    ImGui::DestroyContext();
    for (TextureSlot& slot : ui.slots)
        slot = TextureSlot{};
    ui.ready = false;
    ui.font = nullptr;
    ui.draw = qk_draw_data{};
}

extern "C" int qk_ui_resize(s32 width, s32 height)
{
    if (!ui.ready)
        return -19; /* -ENODEV */
    if (width <= 0 || height <= 0 || width > QK_MAX_DIMENSION || height > QK_MAX_DIMENSION)
        return -22;
    ui.width = width;
    ui.height = height;
    ImGui::GetIO().DisplaySize = ImVec2((float)width, (float)height);
    return 0;
}

extern "C" int qk_ui_frame(u64 elapsed_us, const struct qk_ui_event* events, u32 count,
                           const struct qk_draw_data** data)
{
    if (!ui.ready)
        return -19;
    if (count && !events)
        return -22;

    ImGuiIO& io = ImGui::GetIO();
    /* Clamp the step: a stalled worker must not jump every animation. */
    const u64 clamped = elapsed_us < 1000 ? 1000 : (elapsed_us > 100000 ? 100000 : elapsed_us);

    io.DisplaySize = ImVec2((float)ui.width, (float)ui.height);
    io.DeltaTime = (float)clamped / 1000000.f;
    for (u32 i = 0; i < count; i++)
        Feed(events[i]);

    ImGui::NewFrame();
    Content();
    ImGui::Render();
    /* The arena served an allocation from its reserve: this frame's buffers
     * overlap, so nothing it produced can be trusted. The caller tears the UI
     * down rather than rasterising garbage.
     */
    if (qk_arena_exhausted())
        return -12;
    if (!SyncTextures())
        return -12;
    if (!Translate(ImGui::GetDrawData()))
        return -12;
    ui.frames++;
    if (data)
        *data = &ui.draw;
    return 0;
}

extern "C" void qk_ui_get_stats(struct qk_ui_stats* stats)
{
    struct qk_arena_stats arena;

    if (!stats)
        return;
    qk_arena_get_stats(&arena);
    stats->vertices = ui.vertices.size();
    stats->indices = ui.indices.size();
    stats->commands = ui.commands.size();
    stats->textures = 0;
    for (int slot = 1; slot < kMaxTextures; slot++)
        if (ui.slots[slot].owner)
            stats->textures++;
    stats->frames = ui.frames;
    stats->arena_peak = arena.peak;
}
