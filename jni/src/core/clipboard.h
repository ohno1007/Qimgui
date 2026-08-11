#pragma once

namespace aimgui {

// Clipboard, backed by a file.
//
// The Android clipboard proper is out of reach from here, and not for want of
// permission. ClipboardService has no shell command and implements no dump(),
// so there is nothing to shell out to; the only door is a raw Binder
// transaction whose ordinal moves between releases, carrying a ClipData
// parcelable that has to be marshalled by hand — and its access check starts
// with mAppOps.checkPackage(uid, callingPackage), which wants a package
// identity this process does not have. Reads additionally require the caller
// to be the focused app, the default IME, or to hold
// READ_CLIPBOARD_IN_BACKGROUND.
//
// So this is a plain file at a known path instead. Inside the UI it behaves
// exactly like a clipboard — copy here, paste there. Outside it, the path is
// the bridge: anything that can write a file can put text in, and anything
// that can read one can take it out.
//
//     echo -n "text" > /data/local/tmp/aimgui.clip     # paste into the UI
//     cat /data/local/tmp/aimgui.clip                  # take what was copied
//
// The file is read on every paste rather than cached, so text put there while
// the process is running is picked up without a restart.
namespace clipboard {

const char* Path();

// Installs the handlers on the current ImGui context. Call after the context
// exists; ImGui's own in-memory fallback is replaced, not wrapped.
void Install();

// Direct access, for the on-screen copy/paste buttons — a phone has no Ctrl+C.
void        Set(const char* text);
const char* Get();   // owned by the clipboard, valid until the next call

} // namespace clipboard
} // namespace aimgui
