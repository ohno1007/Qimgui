import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.graphics.PixelFormat;
import android.net.Credentials;
import android.net.LocalServerSocket;
import android.net.LocalSocket;
import android.os.Handler;
import android.os.Looper;
import android.os.Process;
import android.system.Os;
import android.util.Base64;
import android.util.Log;
import android.view.Gravity;
import android.view.WindowManager;
import android.view.inputmethod.InputMethodManager;
import android.widget.EditText;

import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.InputStreamReader;
import java.io.OutputStreamWriter;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

/** Optional helper for the standalone native executable. No network listener. */
public final class ImeHost {
    private static final int MAX_LINE = 96 * 1024;
    private static String socketName;
    private static String token;
    private static int parentPid;
    private static Context context;
    private static EditText editor;
    private static WindowManager windows;
    private static Handler main;

    private static Context context() throws Exception {
        if (context != null) return context;
        Class<?> activityThread = Class.forName("android.app.ActivityThread");
        java.lang.reflect.Method current = activityThread.getDeclaredMethod("currentApplication");
        current.setAccessible(true);
        context = (Context) current.invoke(null);
        if (context == null) {
            java.lang.reflect.Method systemMain = activityThread.getDeclaredMethod("systemMain");
            systemMain.setAccessible(true);
            Object thread = systemMain.invoke(null);
            java.lang.reflect.Method getContext = activityThread.getDeclaredMethod("getSystemContext");
            getContext.setAccessible(true);
            context = (Context) getContext.invoke(thread);
        }
        if (context == null) throw new IllegalStateException("system context unavailable");
        return context;
    }

    private static void show(String initial) throws Exception {
        Context ctx = context();
        if (editor == null) {
            windows = (WindowManager) ctx.getSystemService(Context.WINDOW_SERVICE);
            EditText candidate = new EditText(ctx);
            candidate.setSingleLine(false);
            candidate.setHint("输入文本后，返回 Qimgui 点击“应用输入”");
            candidate.setTextSize(18);
            candidate.setTextColor(0xfff2f2f2);
            candidate.setBackgroundColor(0xf0202328);
            int padding = (int) (16 * ctx.getResources().getDisplayMetrics().density);
            candidate.setPadding(padding, padding, padding, padding);
            WindowManager.LayoutParams params = new WindowManager.LayoutParams(
                    WindowManager.LayoutParams.MATCH_PARENT, padding * 7,
                    WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY,
                    WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL,
                    PixelFormat.TRANSLUCENT);
            // ALT_FOCUSABLE_IM would explicitly prevent this editor using IME.
            params.gravity = Gravity.BOTTOM;
            params.softInputMode = WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE;
            params.setTitle("Qimgui input");
            windows.addView(candidate, params);
            editor = candidate;
        }
        editor.setText(initial);
        editor.setSelection(editor.length());
        editor.requestFocus();
        final EditText target = editor;
        target.post(() -> {
            if (editor == target) {
                InputMethodManager ime = (InputMethodManager) context.getSystemService(Context.INPUT_METHOD_SERVICE);
                ime.showSoftInput(target, InputMethodManager.SHOW_IMPLICIT);
            }
        });
    }

    private static void hide() {
        if (editor == null) return;
        InputMethodManager ime = (InputMethodManager) context.getSystemService(Context.INPUT_METHOD_SERVICE);
        ime.hideSoftInputFromWindow(editor.getWindowToken(), 0);
        windows.removeViewImmediate(editor);
        editor = null;
    }

    private static String encode(String value) {
        byte[] bytes = value.getBytes(StandardCharsets.UTF_8);
        if (bytes.length > 64 * 1024) throw new IllegalArgumentException("text too large");
        return Base64.encodeToString(bytes, Base64.NO_WRAP);
    }

    private static String decode(String value) {
        return new String(Base64.decode(value, Base64.DEFAULT), StandardCharsets.UTF_8);
    }

    private static String command(String request) throws Exception {
        if (request.startsWith("SHOW ")) { show(decode(request.substring(5))); return "OK"; }
        if (request.equals("HIDE")) { hide(); return "OK"; }
        if (request.equals("GET")) return "OK " + encode(editor == null ? "" : editor.getText().toString());
        ClipboardManager clipboard = (ClipboardManager) context().getSystemService(Context.CLIPBOARD_SERVICE);
        if (request.equals("CLIPGET")) {
            ClipData clip = clipboard.getPrimaryClip();
            CharSequence text = clip != null && clip.getItemCount() > 0
                    ? clip.getItemAt(0).coerceToText(context()) : "";
            return "OK " + encode(text == null ? "" : text.toString());
        }
        if (request.startsWith("CLIPSET ")) {
            clipboard.setPrimaryClip(ClipData.newPlainText("Qimgui", decode(request.substring(8))));
            return "OK";
        }
        throw new IllegalArgumentException("unknown command");
    }

    private static String dispatch(String request) throws Exception {
        CountDownLatch done = new CountDownLatch(1);
        String[] result = new String[1];
        Runnable task = () -> {
            try { result[0] = command(request); }
            catch (Throwable error) { result[0] = "ERR " + error.getClass().getSimpleName(); }
            finally { done.countDown(); }
        };
        if (!main.post(task)) throw new IllegalStateException("main looper unavailable");
        if (!done.await(600, TimeUnit.MILLISECONDS)) {
            main.removeCallbacks(task);
            throw new IllegalStateException("main looper timeout");
        }
        return result[0];
    }

    private static String readLine(BufferedReader reader) throws Exception {
        StringBuilder line = new StringBuilder();
        for (int i = 0; i <= MAX_LINE; ++i) {
            int c = reader.read();
            if (c == '\n') return line.toString();
            if (c < 0) throw new IllegalArgumentException("incomplete request");
            line.append((char) c);
        }
        throw new IllegalArgumentException("request too large");
    }

    private static void serve() {
        // This constructor binds an abstract Unix socket. Native code must match.
        try (LocalServerSocket server = new LocalServerSocket(socketName)) {
            for (;;) {
                try (LocalSocket client = server.accept()) {
                    client.setSoTimeout(1000);
                    Credentials peer = client.getPeerCredentials();
                    if (peer.getUid() != Process.myUid() || peer.getPid() != parentPid) continue;
                    BufferedReader reader = new BufferedReader(new InputStreamReader(client.getInputStream(), StandardCharsets.UTF_8));
                    BufferedWriter writer = new BufferedWriter(new OutputStreamWriter(client.getOutputStream(), StandardCharsets.UTF_8));
                    String request = readLine(reader);
                    String answer = request.startsWith(token + " ")
                            ? dispatch(request.substring(token.length() + 1)) : "ERR unauthorized";
                    writer.write(answer);
                    writer.write('\n');
                    writer.flush();
                } catch (Exception error) {
                    Log.w("QimguiIme", "Client request failed", error);
                }
            }
        } catch (Exception error) {
            Log.e("QimguiIme", "Host startup failed", error);
            System.exit(1);
        }
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 3 || args[1].length() != 64) throw new IllegalArgumentException("invalid host arguments");
        socketName = args[0]; token = args[1]; parentPid = Integer.parseInt(args[2]);
        if (Os.getppid() != parentPid) throw new IllegalArgumentException("parent mismatch");
        Looper.prepareMainLooper();
        main = new Handler(Looper.getMainLooper());
        Thread watchdog = new Thread(() -> {
            while (Os.getppid() == parentPid) {
                try { Thread.sleep(1000); } catch (InterruptedException ignored) { return; }
            }
            System.exit(0);
        }, "qimgui-parent-watch");
        watchdog.setDaemon(true);
        watchdog.start();
        new Thread(ImeHost::serve, "qimgui-ime-ipc").start();
        Looper.loop();
    }
}
