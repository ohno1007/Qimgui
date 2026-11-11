#pragma once
#include "imgui.h"
#include <cstddef>

namespace aimgui::md3 {

struct Date { int year = 2026; int month = 1; int day = 1; };
struct DateRange { Date start; Date end; };
struct Time { int hour = 12; int minute = 0; };

bool IsLeapYear(int year);
int DaysInMonth(int year, int month);
bool IsValidDate(Date date);
int CompareDates(Date lhs, Date rhs);
Date ClampDate(Date date);

// Widths use logical dp. Returned true only when the value changes.
bool Select(const char* label, int* selected, const char* const* options,
            int count, float width = 280.0f, bool outlined = true,
            bool enabled = true, const char* supporting = nullptr,
            bool error = false);
bool SearchBar(const char* label, char* buffer, std::size_t capacity,
               const char* const* suggestions = nullptr, int count = 0,
               float width = 320.0f, bool enabled = true);

// OpenMenu is called at the same ID scope as BeginMenuPopup. End only if true.
void OpenMenu(const char* id);
bool BeginMenuPopup(const char* id, float minimumWidth = 196.0f);
void EndMenuPopup();
bool BeginSubMenu(const char* label, bool enabled = true);
void EndSubMenu();
bool MenuItem(const char* label, const char* shortcut = nullptr,
              bool selected = false, bool enabled = true);
void MenuDivider();

// Inline pickers, so the caller may place them in a card, dialog or sheet.
// Gregorian dates are restricted to years 1..9999. view state is per widget ID.
bool DatePicker(const char* id, Date* date, bool enabled = true);
// First click starts a range; the second finishes it (earlier dates are sorted).
bool DateRangePicker(const char* id, DateRange* range, bool enabled = true);
// Clock face plus editable hour/minute fields. Time always stores 24-hour values.
bool TimePicker(const char* id, Time* time, bool use24Hour = true,
                bool enabled = true);

void DrawSelectionGallery();
}
