#pragma once

#include <string>

namespace aimgui::sysclip {

// Reads the Android clipboard over Binder.
//
// Reading only. Writing was built and then removed — see the note at the end.
//
// There is no shell command and no dump() on ClipboardService, so the only door
// is a raw transaction. Three things make that practical rather than reckless:
//
//   Ordinals. IClipboard.aidl has the same method order and the same signatures
//   on Android 14, 15 and 16, so getPrimaryClip is transaction 4 on all of them
//   rather than a moving target. ClipDescription's parcel layout is likewise
//   byte-identical across those releases.
//
//   Identity. ClipboardService resolves the caller from Binder.getCallingUid()
//   and then requires that uid to own the package name it was handed, so root
//   cannot simply claim to be anyone. But shell holds
//   READ_CLIPBOARD_IN_BACKGROUND, so the transaction is sent from a child that
//   drops to uid 2000 and calls itself com.android.shell, which is then true of
//   it. fork alone cannot get there — libbinder's atfork handler poisons
//   ProcessState in the child and the UI process has already initialised it
//   through libgui — so the child re-execs, which replaces the address space
//   and leaves the binder in the new image clean.
//
//   Blast radius. That child is also where a parcel walk that does not match
//   would run off the end. It dies alone and the caller sees a failure.
//
bool ReadText(std::string* out);

// Why the last call failed, for the UI to show rather than leaving the user
// guessing. Empty when the last call succeeded.
const char* LastError();

// Re-entry point for the helper process. main() must call this before anything
// else and return its value when it is not -1.
int RunHelperMain(int argc, char** argv);

// ─── Why there is no WriteText ───────────────────────────────────────────
//
// setPrimaryClip was implemented and never worked. It is recorded here because
// the next person to try will otherwise repeat all of it.
//
// The service always answered EX_BAD_PARCELABLE with "Parcel data not fully
// consumed, unread size: 28" — it read a whole valid ClipData and found 28
// bytes to spare. What was ruled out, each with evidence from the device rather
// than by reasoning:
//
//   * The field layout. Every offset the writer produced was compared against
//     the offsets a successful read walks on a clip the device itself made, and
//     they agreed field for field; the only differences were content lengths.
//   * The AIDL. Android 16's IClipboard.aidl is identical to 14, 15 and main,
//     so both the ordinal and the signature are right.
//   * Fields being skipped. Growing one field at a time by a known amount left
//     the shortfall at exactly 28 in every case — label, mime string, a second
//     mime entry, item text. A field the service does not read would have moved
//     it. Removing the mime list did not move it either, and dropping a typed
//     object made the service misread the next argument entirely, which places
//     it firmly at five.
//
// So the service reads every field, at the sizes sent, and still ends 28 bytes
// short. That is self-contradictory, which means the wrong assumption is
// somewhere none of these probes can see — most likely inside what
// libbinder_ndk emits around the payload rather than in the payload itself.
// Reading works through exactly the same machinery, so it is specific to the
// write direction.
//
// Anyone picking this up: instrument the transaction at the binder level rather
// than the parcel level. The payload has been exhausted.

} // namespace aimgui::sysclip
