#include "java_bridge.h"

#include <android/log.h>
#include <dlfcn.h>
#include <jni.h>
#include <mutex>

#define LOG_TAG "AImGui"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace aimgui::java_bridge {
namespace {
using GetCreatedVMs = jint (*)(JavaVM**, jsize, jsize*);
std::mutex g_mu;
JavaVM* g_vm = nullptr;
void* g_art = nullptr;
std::string g_error;

void ClearException(JNIEnv* e, const char* where) {
    if (!e->ExceptionCheck()) return;
    e->ExceptionDescribe();
    e->ExceptionClear();
    g_error = where;
}

JNIEnv* Env(bool* attached) {
    *attached = false;
    if (!g_vm) {
        g_art = dlopen("libart.so", RTLD_NOW | RTLD_LOCAL);
        if (!g_art) { g_error = "libart unavailable"; return nullptr; }
        auto get = reinterpret_cast<GetCreatedVMs>(dlsym(g_art, "JNI_GetCreatedJavaVMs"));
        jsize count = 0;
        if (!get || get(&g_vm, 1, &count) != JNI_OK || count < 1 || !g_vm) {
            g_error = "no running Java VM"; return nullptr;
        }
    }
    JNIEnv* env = nullptr;
    const jint state = g_vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    if (state == JNI_EDETACHED) {
        if (g_vm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
            g_error = "AttachCurrentThread failed"; return nullptr;
        }
        *attached = true;
    } else if (state != JNI_OK) {
        g_error = "GetEnv failed"; return nullptr;
    }
    return env;
}

jobject Context(JNIEnv* e) {
    jclass at = e->FindClass("android/app/ActivityThread");
    if (!at) { ClearException(e, "ActivityThread unavailable"); return nullptr; }
    jmethodID current = e->GetStaticMethodID(at, "currentApplication", "()Landroid/app/Application;");
    jobject app = current ? e->CallStaticObjectMethod(at, current) : nullptr;
    e->DeleteLocalRef(at);
    if (!app) { ClearException(e, "no current Application"); return nullptr; }
    return app;
}

jobject Service(JNIEnv* e, jobject context, const char* name) {
    jclass cc = e->GetObjectClass(context);
    jmethodID mid = e->GetMethodID(cc, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
    jstring n = e->NewStringUTF(name);
    jobject service = mid ? e->CallObjectMethod(context, mid, n) : nullptr;
    e->DeleteLocalRef(n); e->DeleteLocalRef(cc);
    if (!service) ClearException(e, "system service unavailable");
    return service;
}
}

bool Available() {
    std::lock_guard<std::mutex> lk(g_mu);
    bool attached = false; JNIEnv* e = Env(&attached);
    if (attached && g_vm) g_vm->DetachCurrentThread();
    return e != nullptr;
}

const char* LastError() { std::lock_guard<std::mutex> lk(g_mu); return g_error.c_str(); }

bool SetClipboard(const char* text) {
    std::lock_guard<std::mutex> lk(g_mu);
    bool attached = false; JNIEnv* e = Env(&attached); if (!e) return false;
    bool ok = false; jobject ctx = Context(e);
    if (ctx) {
        jobject cm = Service(e, ctx, "clipboard");
        jclass cd = e->FindClass("android/content/ClipData");
        jmethodID make = cd ? e->GetStaticMethodID(cd, "newPlainText", "(Ljava/lang/CharSequence;Ljava/lang/CharSequence;)Landroid/content/ClipData;") : nullptr;
        jmethodID set = cm ? e->GetMethodID(e->GetObjectClass(cm), "setPrimaryClip", "(Landroid/content/ClipData;)V") : nullptr;
        jstring label = e->NewStringUTF("AImGui");
        jstring value = e->NewStringUTF(text ? text : "");
        jobject clip = make ? e->CallStaticObjectMethod(cd, make, label, value) : nullptr;
        if (set && clip) { e->CallVoidMethod(cm, set, clip); ok = !e->ExceptionCheck(); }
        e->DeleteLocalRef(label); e->DeleteLocalRef(value); if (clip) e->DeleteLocalRef(clip);
        if (cd) e->DeleteLocalRef(cd); if (cm) e->DeleteLocalRef(cm); e->DeleteLocalRef(ctx);
        if (!ok) ClearException(e, "setPrimaryClip failed");
    }
    if (attached && g_vm) g_vm->DetachCurrentThread();
    return ok;
}

bool GetClipboard(std::string* out) {
    if (!out) return false;
    std::lock_guard<std::mutex> lk(g_mu);
    bool attached = false; JNIEnv* e = Env(&attached); if (!e) return false;
    bool ok = false; jobject ctx = Context(e);
    if (ctx) {
        jobject cm = Service(e, ctx, "clipboard");
        jclass c = cm ? e->GetObjectClass(cm) : nullptr;
        jmethodID get = c ? e->GetMethodID(c, "getPrimaryClip", "()Landroid/content/ClipData;") : nullptr;
        jobject clip = get ? e->CallObjectMethod(cm, get) : nullptr;
        if (clip) {
            jclass cc = e->GetObjectClass(clip);
            jmethodID count = e->GetMethodID(cc, "getItemCount", "()I");
            jmethodID item = e->GetMethodID(cc, "getItemAt", "(I)Landroid/content/ClipData$Item;");
            if (count && item && e->CallIntMethod(clip, count) > 0) {
                jobject ci = e->CallObjectMethod(clip, item, 0);
                jclass ic = ci ? e->GetObjectClass(ci) : nullptr;
                jmethodID coerce = ic ? e->GetMethodID(ic, "coerceToText", "(Landroid/content/Context;)Ljava/lang/CharSequence;") : nullptr;
                jobject seq = coerce ? e->CallObjectMethod(ci, coerce, ctx) : nullptr;
                if (seq) {
                    jclass oc = e->GetObjectClass(seq);
                    jmethodID str = e->GetMethodID(oc, "toString", "()Ljava/lang/String;");
                    jstring s = str ? (jstring)e->CallObjectMethod(seq, str) : nullptr;
                    if (s) { const char* p = e->GetStringUTFChars(s, nullptr); *out = p ? p : ""; if (p) e->ReleaseStringUTFChars(s, p); ok = true; e->DeleteLocalRef(s); }
                    e->DeleteLocalRef(oc); e->DeleteLocalRef(seq);
                }
                if (ic) e->DeleteLocalRef(ic); if (ci) e->DeleteLocalRef(ci);
            }
            e->DeleteLocalRef(cc); e->DeleteLocalRef(clip);
        }
        if (c) e->DeleteLocalRef(c); if (cm) e->DeleteLocalRef(cm); e->DeleteLocalRef(ctx);
        if (!ok) ClearException(e, "getPrimaryClip failed");
    }
    if (attached && g_vm) g_vm->DetachCurrentThread();
    return ok;
}
bool ShowInputMethod() {
    std::lock_guard<std::mutex> lk(g_mu);
    bool attached = false; JNIEnv* e = Env(&attached); if (!e) return false;
    bool ok = false;
    jobject ctx = Context(e);
    if (ctx) {
        jobject imm = Service(e, ctx, "input_method");
        jclass wg = e->FindClass("android/view/WindowManagerGlobal");
        jmethodID gi = wg ? e->GetStaticMethodID(wg, "getInstance", "()Landroid/view/WindowManagerGlobal;") : nullptr;
        jobject global = gi ? e->CallStaticObjectMethod(wg, gi) : nullptr;
        jmethodID gv = global ? e->GetMethodID(e->GetObjectClass(global), "getViews", "()[Landroid/view/View;") : nullptr;
        jobjectArray views = gv ? (jobjectArray)e->CallObjectMethod(global, gv) : nullptr;
        jclass ic = imm ? e->GetObjectClass(imm) : nullptr;
        jmethodID show = ic ? e->GetMethodID(ic, "showSoftInput", "(Landroid/view/View;I)Z") : nullptr;
        if (views && show) {
            const jsize n = e->GetArrayLength(views);
            for (jsize i = 0; i < n && !ok; ++i) {
                jobject view = e->GetObjectArrayElement(views, i);
                if (!view) continue;
                jclass vc = e->GetObjectClass(view);
                jmethodID focus = e->GetMethodID(vc, "hasFocus", "()Z");
                if (focus && e->CallBooleanMethod(view, focus))
                    ok = e->CallBooleanMethod(imm, show, view, 0) == JNI_TRUE;
                e->DeleteLocalRef(vc); e->DeleteLocalRef(view);
            }
        }
        if (ic) e->DeleteLocalRef(ic); if (views) e->DeleteLocalRef(views);
        if (global) e->DeleteLocalRef(global); if (wg) e->DeleteLocalRef(wg);
        if (imm) e->DeleteLocalRef(imm); e->DeleteLocalRef(ctx);
        if (!ok) ClearException(e, "no focused Java View for IME");
    }
    if (attached && g_vm) g_vm->DetachCurrentThread();
    return ok;
}
}
