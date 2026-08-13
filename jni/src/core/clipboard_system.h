#pragma once

#include <string>

namespace aimgui::sysclip {

// Reads the Android clipboard over Binder. Reading only — see the note at the
// end for why there is no write.
//
// There is no shell command and no dump() on ClipboardService, so the only door
// is a raw transaction. Three things make that practical:
//
//   Ordinals. IClipboard.aidl has the same method order and signatures on
//   Android 14, 15 and 16, so getPrimaryClip is transaction 4 on all of them
//   rather than a moving target, and ClipDescription's parcel layout is
//   byte-identical across those releases.
//
//   Identity. ClipboardService resolves the caller from getCallingUid() and
//   requires that uid to own the package name it was handed, so root cannot
//   simply claim to be anyone. Shell holds READ_CLIPBOARD_IN_BACKGROUND, so the
//   transaction is sent from a child that drops to uid 2000 and calls itself
//   com.android.shell — truthfully. fork alone cannot get there: libbinder's
//   atfork handler poisons ProcessState in the child and libgui has already
//   initialised it, so the child re-execs and the new image starts clean.
//
//   Blast radius. That child is also where a parcel walk that did not match
//   would run off the end. It dies alone and the caller sees a failure.
bool ReadText(std::string* out);

// Why the last call failed, for the UI to show. Empty on success.
const char* LastError();

// Re-entry point for the helper process. main() must call this first and return
// its value when it is not -1.
int RunHelperMain(int argc, char** argv);

// ─── Why there is no WriteText ───────────────────────────────────────────
//
// setPrimaryClip was implemented and never worked. Recorded so the next attempt
// does not repeat it.
//
// The service always answered EX_BAD_PARCELABLE with "Parcel data not fully
// consumed, unread size: 28" — it read a whole valid ClipData and found 28 bytes
// to spare. Ruled out, each with device evidence rather than reasoning:
//
//   * Field layout: every offset the writer produced matched the offsets a
//     successful read walks on a clip the device made itself, field for field.
//   * The AIDL: Android 16's IClipboard.aidl is identical to 14, 15 and main.
//   * Skipped fields: growing one field at a time by a known amount left the
//     shortfall at exactly 28 every time — label, mime string, a second mime
//     entry, item text. Removing the mime list did not move it either, and
//     dropping a typed object made the service misread the next argument, which
//     places the count firmly at five.
//
// So the service reads every field at the size sent and still ends 28 bytes
// short, which is self-contradictory — the wrong assumption is somewhere none of
// these probes can see, most likely in what libbinder_ndk emits around the
// payload rather than in the payload. Reading works through the same machinery,
// so it is specific to the write direction.
//
// Anyone picking this up: instrument at the binder level, not the parcel level.
// The payload has been exhausted.

} // namespace aimgui::sysclip
