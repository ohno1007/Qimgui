#pragma once

namespace aimgui {

// Clipboard.
//
// Pasting reads the real Android clipboard over Binder; see clipboard_system.h
// for how it gets there and what it costs. Copying writes a file at a known
// path and nothing else — the write direction of the Binder route was built,
// never worked, and was removed, with the findings recorded there.
//
// The file is the way text leaves this process, and the way it can be put in
// without the Android clipboard being involved at all:
//
//     echo -n "text" > /data/local/tmp/aimgui.clip     # readable by a paste
//     cat /data/local/tmp/aimgui.clip                  # take what was copied
//
// It is re-read on every paste rather than cached, so text placed there while
// the process runs is picked up without a restart.
namespace clipboard {

const char* Path();

// Whether the last Get reached the Android clipboard rather than the file, and
// why it did not. The UI shows this: a paste that quietly came from somewhere
// other than where the user copied is the worst possible failure for this
// feature. Set is always the file, so it always reports false.
bool        UsedSystem();
const char* SystemError();

// Installs the handlers on the current ImGui context. Call after the context
// exists; ImGui's own in-memory fallback is replaced, not wrapped.
void Install();

// Direct access, for the on-screen paste button — a phone has no Ctrl+V.
void        Set(const char* text);
const char* Get();   // owned by the clipboard, valid until the next call

} // namespace clipboard
} // namespace aimgui
