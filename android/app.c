/* LocalStage for Android, in C.
   Starts the LocalStage server on a background thread and shows the presenter page in a
   WebView that fills the app (built through JNI, no Java code). Viewers join from their own
   phones' browsers. A PDF shared to the app ("Share" or "Open with") is handed to the
   presenter page, which renders it with the built-in PDF.js. */
#include <android/log.h>
#include <android/native_activity.h>
#include <android/window.h>
#include <errno.h>
#include <jni.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../src/server.h"

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "LocalStage", __VA_ARGS__)

static int server_started, server_failed;
static jobject main_activity; /* global ref to the activity showing the presenter page */
static int main_task_id = -1;

static void *server_thread(void *arg) {
    (void)arg;
    char *argv[] = {"localstage", NULL};
    int rc = localstage_main(1, argv);
    LOG("server exited with %d", rc);
    server_failed = 1;
    return NULL;
}

static int clear_exc(JNIEnv *env) {
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionDescribe(env); (*env)->ExceptionClear(env); return 1; }
    return 0;
}

static jobject call_obj(JNIEnv *env, jobject o, const char *name, const char *sig, ...) {
    jclass c = (*env)->GetObjectClass(env, o);
    jmethodID m = (*env)->GetMethodID(env, c, name, sig);
    if (!m) { clear_exc(env); return NULL; }
    va_list ap;
    va_start(ap, sig);
    jobject r = (*env)->CallObjectMethodV(env, o, m, ap);
    va_end(ap);
    if (clear_exc(env)) return NULL;
    return r;
}

static void call_void(JNIEnv *env, jobject o, const char *name, const char *sig, ...) {
    jclass c = (*env)->GetObjectClass(env, o);
    jmethodID m = (*env)->GetMethodID(env, c, name, sig);
    if (!m) { clear_exc(env); return; }
    va_list ap;
    va_start(ap, sig);
    (*env)->CallVoidMethodV(env, o, m, ap);
    va_end(ap);
    clear_exc(env);
}

/* Copy a shared PDF (content:// URI from the launching intent) to ls_shared_path. */
static int take_shared_pdf(JNIEnv *env, jobject act) {
    jobject intent = call_obj(env, act, "getIntent", "()Landroid/content/Intent;");
    if (!intent) return 0;
    jobject uri = call_obj(env, intent, "getData", "()Landroid/net/Uri;");
    if (!uri) {
        jstring key = (*env)->NewStringUTF(env, "android.intent.extra.STREAM");
        uri = call_obj(env, intent, "getParcelableExtra", "(Ljava/lang/String;)Landroid/os/Parcelable;", key);
    }
    if (!uri) return 0;
    jobject resolver = call_obj(env, act, "getContentResolver", "()Landroid/content/ContentResolver;");
    jobject in = resolver ? call_obj(env, resolver, "openInputStream", "(Landroid/net/Uri;)Ljava/io/InputStream;", uri) : NULL;
    if (!in) return 0;
    char tmp[620];
    snprintf(tmp, sizeof tmp, "%s.part", ls_shared_path);
    FILE *out = fopen(tmp, "wb");
    jbyteArray chunk = (*env)->NewByteArray(env, 65536);
    jclass isc = (*env)->GetObjectClass(env, in);
    jmethodID rd = (*env)->GetMethodID(env, isc, "read", "([B)I");
    long total = 0;
    int ok = out != NULL;
    while (ok) {
        jint n = (*env)->CallIntMethod(env, in, rd, chunk);
        if (clear_exc(env)) { ok = 0; break; }
        if (n < 0) break;
        jbyte *b = (*env)->GetByteArrayElements(env, chunk, NULL);
        if (fwrite(b, 1, (size_t)n, out) != (size_t)n) ok = 0;
        (*env)->ReleaseByteArrayElements(env, chunk, b, JNI_ABORT);
        total += n;
    }
    call_void(env, in, "close", "()V");
    if (out && fclose(out) != 0) ok = 0;
    if (!ok || total == 0 || rename(tmp, ls_shared_path) != 0) { remove(tmp); return 0; }
    LOG("received shared deck (%ld bytes)", total);
    return 1;
}

static jstring jstr(JNIEnv *env, const char *s) { return (*env)->NewStringUTF(env, s); }

static void show_presenter(ANativeActivity *activity) {
    JNIEnv *env = activity->env;
    jobject act = activity->clazz;

    /* Ask the server thread for a fresh one-use presenter link. */
    __atomic_store_n(&ls_link_ready, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&ls_link_request, 1, __ATOMIC_RELEASE);
    for (int i = 0; i < 300 && !__atomic_load_n(&ls_link_ready, __ATOMIC_ACQUIRE) && !server_failed; i++) usleep(10000);
    const char *url = __atomic_load_n(&ls_link_ready, __ATOMIC_ACQUIRE) ? ls_link
        : "data:text/html,<h2 style='font-family:sans-serif'>LocalStage could not start. Port 3000 may be in use by another app.</h2>";

    /* NativeActivity reserves the window for native drawing; hand it back to normal views. */
    jobject window = call_obj(env, act, "getWindow", "()Landroid/view/Window;");
    if (window) {
        call_void(env, window, "takeSurface", "(Landroid/view/SurfaceHolder$Callback2;)V", NULL);
        call_void(env, window, "takeInputQueue", "(Landroid/view/InputQueue$Callback;)V", NULL);
    }

    jclass wvc = (*env)->FindClass(env, "android/webkit/WebView");
    jobject wv = (*env)->NewObject(env, wvc, (*env)->GetMethodID(env, wvc, "<init>", "(Landroid/content/Context;)V"), act);
    if (clear_exc(env) || !wv) { LOG("WebView unavailable"); return; }
    jobject settings = call_obj(env, wv, "getSettings", "()Landroid/webkit/WebSettings;");
    if (settings) {
        call_void(env, settings, "setJavaScriptEnabled", "(Z)V", JNI_TRUE);
        call_void(env, settings, "setDomStorageEnabled", "(Z)V", JNI_TRUE);
        call_void(env, settings, "setMediaPlaybackRequiresUserGesture", "(Z)V", JNI_FALSE);
        jstring ua = call_obj(env, settings, "getUserAgentString", "()Ljava/lang/String;");
        const char *u = ua ? (*env)->GetStringUTFChars(env, ua, NULL) : "";
        char nua[1024];
        snprintf(nua, sizeof nua, "%s LocalStageApp", u);
        if (ua) (*env)->ReleaseStringUTFChars(env, ua, u);
        call_void(env, settings, "setUserAgentString", "(Ljava/lang/String;)V", jstr(env, nua));
    }
    /* A plain WebViewClient keeps page navigation inside the app. */
    jclass wcc = (*env)->FindClass(env, "android/webkit/WebViewClient");
    jobject client = (*env)->NewObject(env, wcc, (*env)->GetMethodID(env, wcc, "<init>", "()V"));
    if (!clear_exc(env) && client) call_void(env, wv, "setWebViewClient", "(Landroid/webkit/WebViewClient;)V", client);
    /* Lets the page's file button open Android's file picker (see Main.java). */
    jobject loader = call_obj(env, act, "getClassLoader", "()Ljava/lang/ClassLoader;");
    jclass ldc = (*env)->FindClass(env, "java/lang/ClassLoader");
    jclass chc = loader ? (*env)->CallObjectMethod(env, loader, (*env)->GetMethodID(env, ldc, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;"), jstr(env, "io.github.eendor.localstage.Main$Chooser")) : NULL;
    if (!clear_exc(env) && chc) {
        jobject ch = (*env)->NewObject(env, chc, (*env)->GetMethodID(env, chc, "<init>", "(Landroid/app/Activity;)V"), act);
        if (!clear_exc(env) && ch) call_void(env, wv, "setWebChromeClient", "(Landroid/webkit/WebChromeClient;)V", ch);
    } else LOG("file chooser unavailable");
    call_void(env, wv, "loadUrl", "(Ljava/lang/String;)V", jstr(env, url));
    call_void(env, act, "setContentView", "(Landroid/view/View;)V", wv);
}

static void on_destroy(ANativeActivity *activity) {
    JNIEnv *env = activity->env;
    if (main_activity && (*env)->IsSameObject(env, main_activity, activity->clazz)) {
        (*env)->DeleteGlobalRef(env, main_activity);
        main_activity = NULL;
        main_task_id = -1;
    }
}

JNIEXPORT void ANativeActivity_onCreate(ANativeActivity *activity, void *saved, size_t saved_size) {
    (void)saved; (void)saved_size;
    JNIEnv *env = activity->env;
    activity->callbacks->onDestroy = on_destroy;

    if (!server_started) {
        char dir[600];
        snprintf(dir, sizeof dir, "%s/tmp", activity->internalDataPath);
        mkdir(activity->internalDataPath, 0700);
        mkdir(dir, 0700);
        setenv("TMPDIR", dir, 1);
        snprintf(ls_shared_path, sizeof ls_shared_path, "%s/shared.pdf", activity->internalDataPath);
        remove(ls_shared_path);
        pthread_t t;
        if (pthread_create(&t, NULL, server_thread, NULL) == 0) { pthread_detach(t); server_started = 1; }
        else server_failed = 1;
    }

    int shared = take_shared_pdf(env, activity->clazz);
    if (shared && main_activity) {
        /* Already presenting: send the user back to that screen, which picks the file up. */
        jstring svc = jstr(env, "activity");
        jobject am = call_obj(env, activity->clazz, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;", svc);
        if (am && main_task_id >= 0) call_void(env, am, "moveTaskToFront", "(II)V", main_task_id, 0);
        ANativeActivity_finish(activity);
        return;
    }

    ANativeActivity_setWindowFlags(activity, AWINDOW_FLAG_KEEP_SCREEN_ON, 0);
    if (main_activity) (*env)->DeleteGlobalRef(env, main_activity);
    main_activity = (*env)->NewGlobalRef(env, activity->clazz);
    jclass ac = (*env)->GetObjectClass(env, activity->clazz);
    main_task_id = (*env)->CallIntMethod(env, activity->clazz, (*env)->GetMethodID(env, ac, "getTaskId", "()I"));
    clear_exc(env);
    show_presenter(activity);
}
