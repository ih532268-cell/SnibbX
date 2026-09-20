/*
 * android_log.c - in-app log for the Android port of snibbetracker.
 *
 * Goals:
 *  - collect every message (our own + printf + SDL log) in a ring buffer
 *  - mirror to logcat
 *  - persist the buffer to a file, so that the Java side can show it on the
 *    NEXT launch even when the native code crashed (SIGSEGV etc.)
 *
 * Only compiled for Android. All functions are async-signal-unsafe EXCEPT
 * crash_handler, which restricts itself to open/write/close.
 */
#ifdef __ANDROID__

#include <android/log.h>
#include <jni.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <SDL.h>

#include "android_log.h"

#define LOG_CAP (64 * 1024)

static char g_buf[LOG_CAP];
static size_t g_len = 0;            /* bytes currently valid in g_buf */
static char g_path[512] = "";       /* file we persist to */
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;

static void append_raw(const char *s, size_t n)
{
    if (n >= LOG_CAP) {                 /* keep only the tail */
        s += n - (LOG_CAP - 1);
        n = LOG_CAP - 1;
    }
    if (g_len + n >= LOG_CAP) {         /* drop oldest data */
        size_t drop = (g_len + n) - (LOG_CAP - 1);
        if (drop > g_len) drop = g_len;
        memmove(g_buf, g_buf + drop, g_len - drop);
        g_len -= drop;
    }
    memcpy(g_buf + g_len, s, n);
    g_len += n;
}

void alog_write(const char *fmt, ...)
{
    char line[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof(line) - 2, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t)n > sizeof(line) - 2) n = (int)sizeof(line) - 2;
    line[n++] = '\n';
    line[n] = '\0';

    __android_log_write(ANDROID_LOG_INFO, "snibbe", line);

    pthread_mutex_lock(&g_mu);
    append_raw(line, (size_t)n);
    pthread_mutex_unlock(&g_mu);
}

static void sdl_log_cb(void *ud, int cat, SDL_LogPriority pri, const char *msg)
{
    (void)ud;
    alog_write("[SDL c%d p%d] %s", cat, (int)pri, msg ? msg : "(null)");
}

/* Persist current buffer. Uses only open/write/close (safe in a signal handler). */
static void flush_to_file(void)
{
    if (!g_path[0]) return;
    int fd = open(g_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return;
    size_t off = 0;
    while (off < g_len) {
        ssize_t w = write(fd, g_buf + off, g_len - off);
        if (w <= 0) break;
        off += (size_t)w;
    }
    close(fd);
}

void alog_flush(void)
{
    pthread_mutex_lock(&g_mu);
    flush_to_file();
    pthread_mutex_unlock(&g_mu);
}

static const char *sig_name(int s)
{
    switch (s) {
    case SIGSEGV: return "SIGSEGV (bad memory access)";
    case SIGABRT: return "SIGABRT (abort / failed assert)";
    case SIGBUS:  return "SIGBUS (bus error)";
    case SIGFPE:  return "SIGFPE (arithmetic error)";
    case SIGILL:  return "SIGILL (illegal instruction)";
    default:      return "signal";
    }
}

static struct sigaction g_old[32];

static void crash_handler(int sig, siginfo_t *info, void *ctx)
{
    /* Deliberately no locking / malloc here: we may be crashing inside them. */
    char msg[160];
    int n = snprintf(msg, sizeof(msg),
                     "\n*** NATIVE CRASH: %s, fault addr=%p ***\n",
                     sig_name(sig), info ? info->si_addr : NULL);
    if (n > 0 && (size_t)n < LOG_CAP - g_len)
        append_raw(msg, (size_t)n);
    flush_to_file();

    /* Restore the previous handler and re-raise so Android still records the crash. */
    if (sig >= 0 && sig < 32)
        sigaction(sig, &g_old[sig], NULL);
    raise(sig);
    (void)ctx;
}

void alog_init(const char *persist_path)
{
    if (persist_path)
        snprintf(g_path, sizeof(g_path), "%s", persist_path);

    alog_write("=== snibbetracker-android log start ===");
    SDL_LogSetOutputFunction(sdl_log_cb, NULL);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = crash_handler;
    sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
    sigemptyset(&sa.sa_mask);
    int sigs[] = { SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL };
    for (unsigned i = 0; i < sizeof(sigs) / sizeof(sigs[0]); i++)
        sigaction(sigs[i], &sa, &g_old[sigs[i]]);
}

/* ---- JNI: called from Java (org.libsdl.app.SnibbeLog) ---- */

JNIEXPORT jstring JNICALL
Java_org_libsdl_app_SnibbeLog_nativeGetLog(JNIEnv *env, jclass cls)
{
    (void)cls;
    static char tmp[LOG_CAP + 1];
    pthread_mutex_lock(&g_mu);
    memcpy(tmp, g_buf, g_len);
    tmp[g_len] = '\0';
    pthread_mutex_unlock(&g_mu);
    /* NewStringUTF wants modified-UTF8; log text is ASCII in practice, but be safe. */
    for (size_t i = 0; i < g_len; i++)
        if ((unsigned char)tmp[i] >= 0x80) tmp[i] = '?';
    return (*env)->NewStringUTF(env, tmp);
}

JNIEXPORT void JNICALL
Java_org_libsdl_app_SnibbeLog_nativeSetLogPath(JNIEnv *env, jclass cls, jstring jpath)
{
    (void)cls;
    const char *p = (*env)->GetStringUTFChars(env, jpath, NULL);
    if (p) {
        snprintf(g_path, sizeof(g_path), "%s", p);
        (*env)->ReleaseStringUTFChars(env, jpath, p);
    }
}

#endif /* __ANDROID__ */
