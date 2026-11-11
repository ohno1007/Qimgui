/* SPDX-License-Identifier: MIT */
/* The application layer inside the kernel: the same AImGui main window the
 * userspace build draws, its persisted settings, and the stubs that stand in
 * for the Android-only services it expects.
 */
#ifndef QK_APP_H
#define QK_APP_H

#ifdef __cplusplus
extern "C" {
#endif

enum qk_app_mode {
    QK_APP_MAIN = 0,     /* AImGui's shell: five pages, navigation, dialogs. */
    QK_APP_GALLERY,      /* The MD3 component gallery on its own. */
};

/* Called once, before the first frame. settings_text is the settings file's
 * contents, already read by the caller: reading a file sleeps, and this runs
 * inside the floating-point region, which may not. settings_path is kept for
 * saving, which happens between frames.
 */
void qk_app_init(int mode, int dark, int scale_percent, const char *settings_path,
                 const char *settings_text);
/* Draws one frame's worth of UI. Inside the ImGui frame and the FP region. */
void qk_app_draw(void);
/* True once the user has asked the window to close. */
int qk_app_wants_exit(void);
/* Writes the settings file if anything the user can change has changed.
 * Touches the filesystem, so it runs outside the floating-point region.
 * Returns 1 if it wrote, 0 if there was nothing to write, or a negative errno.
 */
int qk_app_save_settings(void);

#ifdef __cplusplus
}
#endif

#endif /* QK_APP_H */
