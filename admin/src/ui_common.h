#pragma once
#include <windows.h>

#include <string>
#include <vector>

namespace pvnc {

// --- Premium dark palette (matches the viewer) ---
namespace theme {
constexpr COLORREF kWindow = RGB(0x1E, 0x1E, 0x1E);
constexpr COLORREF kPanel = RGB(0x25, 0x25, 0x26);
constexpr COLORREF kEdit = RGB(0x2D, 0x2D, 0x30);
constexpr COLORREF kText = RGB(0xE6, 0xE6, 0xE6);
constexpr COLORREF kMuted = RGB(0x9A, 0x9A, 0x9E);
constexpr COLORREF kAccent = RGB(0x3B, 0x82, 0xF6);
}  // namespace theme

// Shared cached brushes.
HBRUSH windowBrush();
HBRUSH panelBrush();
HBRUSH editBrush();

// Turns on the immersive dark title bar for `hwnd` (no-op on older Windows).
void enableDarkTitleBar(HWND hwnd);

// Centralized WM_CTLCOLOR* handler for dark controls. Returns a brush (as
// LRESULT) when `msg` is a CTLCOLOR message it handles, or 0 otherwise; set
// `handled` accordingly.
LRESULT darkCtlColor(UINT msg, WPARAM wParam, bool& handled);

// Applies the default GUI font to every child of `parent`.
void applyGuiFont(HWND parent);

// Creates a themed child "panel" window that routes its children's WM_COMMAND
// ids to `fn(owner, id)` and paints the dark background + control colors. Child
// controls are created with the returned HWND as their parent.
using PanelCommandFn = void (*)(void* owner, int id);
HWND createPanelWindow(HWND parent, void* owner, PanelCommandFn fn);

// --- Control creators (child windows) ---
HWND makeLabel(HWND parent, const wchar_t* text, int x, int y, int w, int h);
HWND makeButton(HWND parent, int id, const wchar_t* text, int x, int y, int w, int h);
HWND makeEdit(HWND parent, int id, int x, int y, int w, int h, DWORD extra = 0);
HWND makeGroup(HWND parent, const wchar_t* text, int x, int y, int w, int h);
HWND makeListView(HWND parent, int id, int x, int y, int w, int h);
HWND makeCheckbox(HWND parent, int id, const wchar_t* text, int x, int y, int w, int h);

// Adds a column to a report-mode list view.
void lvAddColumn(HWND lv, int index, const wchar_t* title, int width);
// Appends a row; returns its index. Extra subitem texts set columns 1..n.
int lvAddRow(HWND lv, const std::wstring& col0, const std::vector<std::wstring>& more = {});

std::wstring getWindowText(HWND w);

// Standard message boxes with the app title.
void info(HWND owner, const std::wstring& msg);
void warn(HWND owner, const std::wstring& msg);

// --- Modal prompts (return false on Cancel) ---
struct Field {
  std::wstring label;
  std::wstring value;  // in: default, out: entered
  bool password = false;
};
// Shows a labeled form with OK/Cancel. Values are read back into `fields`.
bool promptFields(HWND owner, const wchar_t* title, std::vector<Field>& fields);

// Convenience single-line prompt.
bool promptText(HWND owner, const wchar_t* title, const wchar_t* label, std::wstring& value);

// Folder picker (SHBrowseForFolder). Returns false on cancel.
bool pickFolder(HWND owner, const wchar_t* title, std::wstring& out);

// File open/save dialogs. `filter` is a double-NUL terminated pair list.
bool openFileDialog(HWND owner, const wchar_t* title, const wchar_t* filter, std::wstring& out);
bool saveFileDialog(HWND owner, const wchar_t* title, const wchar_t* filter,
                    const wchar_t* defName, std::wstring& out);

}  // namespace pvnc
