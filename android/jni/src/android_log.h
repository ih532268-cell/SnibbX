#ifndef SNIBBE_ANDROID_LOG_H
#define SNIBBE_ANDROID_LOG_H

#ifdef __ANDROID__
void alog_init(const char *persist_path);
void alog_write(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void alog_flush(void);
#else
#define alog_init(p)      ((void)0)
#define alog_write(...)   ((void)0)
#define alog_flush()      ((void)0)
#endif

#endif
