#pragma once

#include <string>

namespace aimgui::sysclip {

// The real Android clipboard, over Binder.
//
// There is no shell command and no dump() on ClipboardService, so the only
// door is a raw transaction. Three things make that practical rather than
// reckless:
//
//   Ordinals. IClipboard.aidl has the same method order and the same
//   signatures on Android 14, 15 and 16, so getPrimaryClip is transaction 4 on
//   all of them rather than a moving target. Same for ClipDescription's parcel
//   layout, which is byte-identical across those releases.
//
//   Identity. ClipboardService opens its access check with
//   mAppOps.checkPackage(uid, callingPackage), which throws unless the uid owns
//   the package named — so root cannot simply claim to be anyone. But shell
//   holds READ_CLIPBOARD_IN_BACKGROUND, and writes are permitted outright. So
//   the transaction is sent from a forked child that drops to uid 2000 and
//   calls itself com.android.shell, which is true of it.
//
//   Blast radius. That same child is where a parcel layout that does not match
//   what was read from AOSP walks off the end of a buffer. It dies alone; the
//   caller sees a failure and falls back.
//
// Available() only says the service was reachable and identified itself as
// IClipboard. Whether a given read is permitted is decided per call.
bool Available();

bool ReadText(std::string* out);
bool WriteText(const char* text);

// Re-entry point for the helper process. main() must call this before anything
// else and return its value when it is not -1.
//
// The transaction cannot be sent from the UI process. ClipboardService resolves
// the caller from Binder.getCallingUid() and then requires that uid to own the
// package it was handed — root naming com.android.shell fails that test, and
// the failure is not a refusal but a catch block that wipes the clipboard and
// returns null. So it goes from a process that really is shell.
//
// fork alone does not work: libbinder's atfork handler poisons ProcessState in
// the child and the UI process has already initialised it through libgui. exec
// replaces the address space, which is what makes the child's binder usable —
// and the privilege drop happens here, after the exec, because /data is not
// readable by shell and a child that dropped first could not exec at all.
int RunHelperMain(int argc, char** argv);

// Why the last call failed, for the UI to show rather than leaving the user
// guessing. Empty when the last call succeeded.
const char* LastError();

} // namespace aimgui::sysclip
