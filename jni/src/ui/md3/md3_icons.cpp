#include "ui/md3_icons.h"

#include <cmath>
#include <cstring>

namespace aimgui::md3::icons {
namespace {
constexpr float kPi = 3.14159265358979323846f;
enum class Symbol {
    Home, Widgets, Settings, Speed, Info, Menu, Close, Minimize, Sun, Moon, Expand,
    FormatBold, FormatItalic, FormatUnderlined, FormatColorText, FormatColorFill,
    PersonAdd, Share, Alarm, Clock, Timer, Stopwatch, Play, Pause, Reset, Plus
};

struct Canvas {
    ImDrawList* draw;
    ImVec2 origin;
    float unit;
    ImU32 color;

    ImVec2 Point(float x, float y) const { return ImVec2(origin.x + x * unit, origin.y + y * unit); }
    void Line(float x1, float y1, float x2, float y2) const {
        const ImVec2 a = Point(x1, y1), b = Point(x2, y2);
        draw->AddLine(a, b, color, 2.f * unit);
        draw->AddCircleFilled(a, unit, color, 12);
        draw->AddCircleFilled(b, unit, color, 12);
    }
    void Circle(float x, float y, float radius) const {
        draw->AddCircle(Point(x, y), radius * unit, color, 32, 2.f * unit);
    }
    void Stroke(bool closed = false) const {
        draw->PathStroke(color, 2.f * unit, closed ? ImDrawFlags_Closed : 0);
    }
    void To(float x, float y) const { draw->PathLineTo(Point(x, y)); }
};
} // namespace

bool DrawSymbol(ImDrawList* draw, ImVec2 center, float size, ImU32 color, const char* token) {
    if (!token || token[0] != '@') return false;
    Symbol symbol;
    if (!std::strcmp(token, Home)) symbol = Symbol::Home;
    else if (!std::strcmp(token, Widgets)) symbol = Symbol::Widgets;
    else if (!std::strcmp(token, Settings)) symbol = Symbol::Settings;
    else if (!std::strcmp(token, Speed)) symbol = Symbol::Speed;
    else if (!std::strcmp(token, Info)) symbol = Symbol::Info;
    else if (!std::strcmp(token, Menu)) symbol = Symbol::Menu;
    else if (!std::strcmp(token, Close)) symbol = Symbol::Close;
    else if (!std::strcmp(token, Minimize)) symbol = Symbol::Minimize;
    else if (!std::strcmp(token, Sun)) symbol = Symbol::Sun;
    else if (!std::strcmp(token, Moon)) symbol = Symbol::Moon;
    else if (!std::strcmp(token, Expand)) symbol = Symbol::Expand;
    else if (!std::strcmp(token, FormatBold)) symbol = Symbol::FormatBold;
    else if (!std::strcmp(token, FormatItalic)) symbol = Symbol::FormatItalic;
    else if (!std::strcmp(token, FormatUnderlined)) symbol = Symbol::FormatUnderlined;
    else if (!std::strcmp(token, FormatColorText)) symbol = Symbol::FormatColorText;
    else if (!std::strcmp(token, FormatColorFill)) symbol = Symbol::FormatColorFill;
    else if (!std::strcmp(token, PersonAdd)) symbol = Symbol::PersonAdd;
    else if (!std::strcmp(token, Share)) symbol = Symbol::Share;
    else if (!std::strcmp(token, Alarm)) symbol = Symbol::Alarm;
    else if (!std::strcmp(token, Clock)) symbol = Symbol::Clock;
    else if (!std::strcmp(token, Timer)) symbol = Symbol::Timer;
    else if (!std::strcmp(token, Stopwatch)) symbol = Symbol::Stopwatch;
    else if (!std::strcmp(token, Play)) symbol = Symbol::Play;
    else if (!std::strcmp(token, Pause)) symbol = Symbol::Pause;
    else if (!std::strcmp(token, Reset)) symbol = Symbol::Reset;
    else if (!std::strcmp(token, Plus)) symbol = Symbol::Plus;
    else return false;
    if (!draw || !std::isfinite(size) || !std::isfinite(center.x) || !std::isfinite(center.y) ||
        size <= 0 || !(color & IM_COL32_A_MASK)) return true;

    const Canvas c{draw, ImVec2(center.x - size * .5f, center.y - size * .5f), size / 24.f, color};
    switch (symbol) {
    case Symbol::Home:
        c.To(3, 10); c.To(12, 3); c.To(21, 10); c.Stroke();
        c.To(5, 9); c.To(5, 21); c.To(10, 21); c.To(10, 15);
        c.To(14, 15); c.To(14, 21); c.To(19, 21); c.To(19, 9); c.Stroke();
        break;
    case Symbol::Widgets:
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 2; ++x)
                draw->AddRect(c.Point(4.f + x * 10.f, 4.f + y * 10.f),
                              c.Point(10.f + x * 10.f, 10.f + y * 10.f), color,
                              c.unit, 2.f * c.unit);
        break;
    case Symbol::Settings:
        for (int i = 0; i < 32; ++i) {
            const float angle = i * kPi / 16.f - kPi * .5f;
            const float radius = (i % 4 == 1 || i % 4 == 2) ? 10.f : 8.f;
            c.To(12.f + std::cos(angle) * radius, 12.f + std::sin(angle) * radius);
        }
        c.Stroke(true);
        c.Circle(12, 12, 3.25f);
        break;
    case Symbol::Speed:
        draw->PathArcTo(c.Point(12, 13), 9.f * c.unit, kPi * .75f, kPi * 2.25f, 32);
        c.Stroke();
        c.Line(12, 13, 17, 8);
        draw->AddCircleFilled(c.Point(12, 13), 1.7f * c.unit, color, 16);
        break;
    case Symbol::Info:
        c.Circle(12, 12, 9);
        c.Line(12, 11, 12, 17);
        draw->AddCircleFilled(c.Point(12, 7), c.unit, color, 16);
        break;
    case Symbol::Menu:
        c.Line(4, 6, 20, 6); c.Line(4, 12, 20, 12); c.Line(4, 18, 20, 18);
        break;
    case Symbol::Close:
        c.Line(6, 6, 18, 18); c.Line(6, 18, 18, 6);
        break;
    case Symbol::Minimize:
        c.Line(4, 12, 20, 12);
        break;
    case Symbol::Sun:
        c.Circle(12, 12, 4);
        for (int i = 0; i < 8; ++i) {
            const float angle = i * kPi / 4.f, x = std::cos(angle), y = std::sin(angle);
            c.Line(12 + 8 * x, 12 + 8 * y, 12 + 10 * x, 12 + 10 * y);
        }
        break;
    case Symbol::Moon:
        c.To(14.5f, 3.5f);
        draw->PathBezierCubicCurveTo(c.Point(12.5f, 7), c.Point(13.3f, 12.2f), c.Point(17, 14));
        draw->PathBezierCubicCurveTo(c.Point(18.3f, 14.7f), c.Point(19.6f, 15), c.Point(21, 14.8f));
        draw->PathBezierCubicCurveTo(c.Point(19.6f, 19.7f), c.Point(14.5f, 22.2f), c.Point(9.8f, 20.6f));
        draw->PathBezierCubicCurveTo(c.Point(5.1f, 19.3f), c.Point(2.5f, 14.3f), c.Point(3.8f, 9.6f));
        draw->PathBezierCubicCurveTo(c.Point(5.1f, 5), c.Point(9.7f, 2.4f), c.Point(14.5f, 3.5f));
        c.Stroke(true);
        break;
    case Symbol::Expand:
        c.To(4, 10); c.To(4, 4); c.To(10, 4); c.Stroke();
        c.To(14, 4); c.To(20, 4); c.To(20, 10); c.Stroke();
        c.To(20, 14); c.To(20, 20); c.To(14, 20); c.Stroke();
        c.To(10, 20); c.To(4, 20); c.To(4, 14); c.Stroke();
        break;
    case Symbol::FormatBold:
        c.Line(7, 4, 7, 20);
        c.To(7, 4); c.To(12.5f, 4);
        draw->PathBezierCubicCurveTo(c.Point(18, 4), c.Point(18, 11.5f), c.Point(12.5f, 11.5f));
        c.To(7, 11.5f); c.Stroke();
        c.To(12.5f, 11.5f);
        draw->PathBezierCubicCurveTo(c.Point(19.5f, 11.5f), c.Point(19.5f, 20), c.Point(12.5f, 20));
        c.To(7, 20); c.Stroke();
        break;
    case Symbol::FormatItalic:
        c.Line(10, 4, 19, 4); c.Line(5, 20, 14, 20); c.Line(15, 4, 9, 20);
        break;
    case Symbol::FormatUnderlined:
        c.To(6, 4); c.To(6, 11.5f);
        draw->PathBezierCubicCurveTo(c.Point(6, 19), c.Point(18, 19), c.Point(18, 11.5f));
        c.To(18, 4); c.Stroke(); c.Line(5, 21, 19, 21);
        break;
    case Symbol::FormatColorText:
        c.Line(6, 16, 12, 3); c.Line(12, 3, 18, 16); c.Line(8.5f, 11, 15.5f, 11);
        draw->AddRectFilled(c.Point(4, 20), c.Point(20, 23), color, .5f * c.unit);
        break;
    case Symbol::FormatColorFill:
        c.Line(7, 3, 11, 7);
        c.To(10, 5); c.To(17, 12); c.To(10, 19); c.To(3, 12); c.Stroke(true);
        c.Line(4, 12, 16, 12);
        // The small drop distinguishes the fill bucket from a diamond swatch.
        c.To(20, 12);
        draw->PathBezierCubicCurveTo(c.Point(21, 14), c.Point(22.5f, 15.5f), c.Point(22.5f, 17));
        draw->PathBezierCubicCurveTo(c.Point(22.5f, 20.5f), c.Point(17.5f, 20.5f), c.Point(17.5f, 17));
        draw->PathBezierCubicCurveTo(c.Point(17.5f, 15.5f), c.Point(19, 14), c.Point(20, 12));
        draw->PathFillConvex(color);
        break;
    case Symbol::PersonAdd:
        c.Circle(8, 7, 3);
        c.To(2, 20); c.To(2, 18);
        draw->PathBezierCubicCurveTo(c.Point(2, 12), c.Point(14, 12), c.Point(14, 18));
        c.To(14, 20); c.Stroke(true);
        c.Line(16, 10, 22, 10); c.Line(19, 7, 19, 13);
        break;
    case Symbol::Share:
        c.Line(8.2f, 10.7f, 15.8f, 6.3f); c.Line(8.2f, 13.3f, 15.8f, 17.7f);
        c.Circle(6, 12, 2.5f); c.Circle(18, 5, 2.5f); c.Circle(18, 19, 2.5f);
        break;
    case Symbol::Alarm:
        c.Circle(12, 13, 8);
        c.Line(12, 8, 12, 13); c.Line(12, 13, 15.5f, 15.5f);
        c.Line(2.5f, 5.5f, 5.5f, 2.5f);
        c.Line(18.5f, 2.5f, 21.5f, 5.5f);
        c.Line(6, 19.5f, 4.5f, 21.5f);
        c.Line(18, 19.5f, 19.5f, 21.5f);
        break;
    case Symbol::Clock:
        c.Circle(12, 12, 9);
        c.Line(12, 6, 12, 12); c.Line(12, 12, 16, 15);
        break;
    case Symbol::Timer:
        draw->AddRectFilled(c.Point(5, 2), c.Point(19, 4), color);
        draw->AddRectFilled(c.Point(5, 20), c.Point(19, 22), color);
        // Each half is convex; splitting at the waist avoids treating the
        // complete concave hourglass silhouette as a convex polygon.
        c.To(7, 4); c.To(17, 4); c.To(17, 8);
        c.To(13.5f, 12); c.To(10.5f, 12); c.To(7, 8);
        draw->PathFillConvex(color);
        c.To(10.5f, 12); c.To(13.5f, 12); c.To(17, 16);
        c.To(17, 20); c.To(7, 20); c.To(7, 16);
        draw->PathFillConvex(color);
        break;
    case Symbol::Stopwatch:
        c.Circle(12, 14, 7);
        c.Line(10, 2, 14, 2); c.Line(12, 2, 12, 5);
        c.Line(17.5f, 8, 19.5f, 6);
        c.Line(12, 9, 12, 14);
        break;
    case Symbol::Play:
        draw->AddTriangleFilled(c.Point(7, 4), c.Point(20, 12), c.Point(7, 20), color);
        break;
    case Symbol::Pause:
        draw->AddRectFilled(c.Point(6, 4), c.Point(10, 20), color, .5f * c.unit);
        draw->AddRectFilled(c.Point(14, 4), c.Point(18, 20), color, .5f * c.unit);
        break;
    case Symbol::Reset:
        draw->PathArcTo(c.Point(12, 13), 8.f * c.unit, -kPi * .75f, kPi * .95f, 36);
        c.Stroke();
        c.Line(6.3f, 7.3f, 6.3f, 3);
        c.Line(6.3f, 7.3f, 10.6f, 7.3f);
        break;
    case Symbol::Plus:
        c.Line(5, 12, 19, 12); c.Line(12, 5, 12, 19);
        break;
    }
    return true;
}

} // namespace aimgui::md3::icons
