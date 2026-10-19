/* Driveclub (CUSA00003): system library functions the Bloodborne runtime never needed.
 *
 * Resolved last (after the shadPS4 HLE table), so nothing here shadows an existing
 * implementation. Hardware and services a PC does not have (the USB wheel driver, Rudp,
 * live streaming, the companion app, Facebook and other dialogs) report "nothing there"
 * the way a PS4 without them would. Reference for behaviour: shadPS4 and
 * BlackLineInteractive/Driveclub-Port (GPL-2.0-or-later). */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include "runtime.h"
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

/* Stubs keep the guest signatures as documentation of the contract. */
#pragma GCC diagnostic ignored "-Wunused-parameter"

#define DC_ERROR_GENERIC ((int32_t)0x80020001)  /* SCE_KERNEL_ERROR_EPERM */
#define DC_ERROR_NOENT ((int32_t)0x80020002)
#define DC_ERROR_BUSY ((int32_t)0x80020010)
#define POSIX_EINVAL 22
#define POSIX_EBUSY 16
#define POSIX_EAGAIN 35
#define POSIX_EPERM 1

static int dc_trace = -1;
static void trace(const char *what) {
    if (dc_trace < 0) dc_trace = getenv("BB_DC_TRACE") && *getenv("BB_DC_TRACE") == '1';
    if (dc_trace) printf("DC: %s\n", what);
}

/* --- Generic results -------------------------------------------------------------------- */
static ABI int32_t ok(void) { return 0; }
static ABI int32_t fail_generic(void) { return DC_ERROR_GENERIC; }
static ABI int32_t one(void) { return 1; }
/* Common dialogs: SCE_COMMON_DIALOG_STATUS_FINISHED, as if the player closed them. */
static ABI int32_t dialog_finished(void) { return 3; }
static ABI int32_t dialog_none(void) { return 0; }  /* SCE_COMMON_DIALOG_STATUS_NONE */
static ABI int32_t dialog_result(int32_t *result) {
    if (result) result[1] = 1;  /* { mode, result = canceled } */
    return 0;
}

/* --- libkernel -------------------------------------------------------------------------- */
static ABI int32_t kernel_sleep(uint32_t seconds) {
#ifdef _WIN32
    Sleep(seconds * 1000u);
#else
    sleep(seconds);
#endif
    return 0;
}
static ABI int64_t kernel_getdents(int32_t fd, char *buffer, uint64_t size) {
    return runtime_file_getdents(fd, buffer, size, NULL);
}
/* Every module the game asks for is linked into the image at startup (link_modules.py)
 * and already started, so loading only hands back a handle. */
static ABI int32_t kernel_load_start_module(const char *path, uint64_t args, const void *argp,
                                            uint32_t flags, const void *option, int32_t *result) {
    if (result) *result = 0;
    const char *base = path ? strrchr(path, '/') : NULL;
    base = base ? base + 1 : path;
    printf("DC: sceKernelLoadStartModule(%s): already linked\n", path ? path : "(null)");
    uint32_t hash = 0x40;
    for (const char *c = base ? base : ""; *c; ++c) hash = hash * 31 + (unsigned char)*c;
    return (int32_t)(0x100 + (hash & 0xfff));
}
static ABI int32_t kernel_get_open_psid(uint8_t *id) {
    if (id) for (int i = 0; i < 16; ++i) id[i] = (uint8_t)(0x5a ^ i * 17);
    return 0;
}

/* --- POSIX semaphores and rwlocks (libScePosix) ----------------------------------------- */
/* Guest sem_t and pthread_rwlock_t are pointer-sized handles. */
typedef struct { pthread_mutex_t lock; pthread_cond_t cond; uint32_t count; } HostSem;
static ABI int32_t posix_sem_init(HostSem **sem, int32_t shared, uint32_t value) {
    if (!sem) return -1;
    HostSem *s = calloc(1, sizeof(*s));
    if (!s) return -1;
    pthread_mutex_init(&s->lock, NULL);
    pthread_cond_init(&s->cond, NULL);
    s->count = value;
    *sem = s;
    return 0;
}
static ABI int32_t posix_sem_destroy(HostSem **sem) {
    if (!sem || !*sem) return -1;
    pthread_mutex_destroy(&(*sem)->lock);
    pthread_cond_destroy(&(*sem)->cond);
    free(*sem);
    *sem = NULL;
    return 0;
}
static ABI int32_t posix_sem_wait(HostSem **sem) {
    if (!sem || !*sem) return -1;
    HostSem *s = *sem;
    pthread_mutex_lock(&s->lock);
    while (!s->count) pthread_cond_wait(&s->cond, &s->lock);
    --s->count;
    pthread_mutex_unlock(&s->lock);
    return 0;
}
static ABI int32_t posix_sem_trywait(HostSem **sem) {
    if (!sem || !*sem) return -1;
    HostSem *s = *sem;
    pthread_mutex_lock(&s->lock);
    int32_t r = s->count ? (--s->count, 0) : -1;
    pthread_mutex_unlock(&s->lock);
    if (r) *runtime_errno() = POSIX_EAGAIN;
    return r;
}
static ABI int32_t posix_sem_post(HostSem **sem) {
    if (!sem || !*sem) return -1;
    HostSem *s = *sem;
    pthread_mutex_lock(&s->lock);
    ++s->count;
    pthread_cond_signal(&s->cond);
    pthread_mutex_unlock(&s->lock);
    return 0;
}
static ABI int32_t posix_sem_getvalue(HostSem **sem, int32_t *value) {
    if (!sem || !*sem || !value) return -1;
    pthread_mutex_lock(&(*sem)->lock);
    *value = (int32_t)(*sem)->count;
    pthread_mutex_unlock(&(*sem)->lock);
    return 0;
}

/* PS4 (FreeBSD libthr) rwlocks: a thread that already holds a read lock gets another one even
 * while a writer waits; other new readers queue behind the writer. MinGW's pthread_rwlock makes
 * every reader wait for a waiting writer, so a recursive read deadlocked against a writer
 * (Driveclub froze mid-race: the main thread in wrlock, a worker holding the lock in rdlock). */
typedef struct DcRwlock {
    pthread_mutex_t m;
    pthread_cond_t readers_cv, writers_cv;
    unsigned readers, writers_waiting;
    int writer;
} DcRwlock;
static _Thread_local unsigned read_locks_held; /* libthr's curthread->rdlock_count */
static pthread_mutex_t rwlock_init_lock = PTHREAD_MUTEX_INITIALIZER;
/* Static initializers leave the guest handle NULL: create the host lock on first use. */
static DcRwlock *rwlock(DcRwlock **handle) {
    if (!handle) return NULL;
    DcRwlock *l = __atomic_load_n(handle, __ATOMIC_ACQUIRE);
    if (l) return l;
    pthread_mutex_lock(&rwlock_init_lock);
    if (!*handle) {
        l = calloc(1, sizeof(*l));
        if (l) {
            pthread_mutex_init(&l->m, NULL);
            pthread_cond_init(&l->readers_cv, NULL);
            pthread_cond_init(&l->writers_cv, NULL);
        }
        __atomic_store_n(handle, l, __ATOMIC_RELEASE);
    }
    l = *handle;
    pthread_mutex_unlock(&rwlock_init_lock);
    return l;
}
static ABI int32_t posix_rwlock_init(DcRwlock **handle, const void *attr) {
    (void)attr;
    if (!handle) return POSIX_EINVAL;
    *handle = NULL;
    return rwlock(handle) ? 0 : POSIX_EINVAL;
}
static ABI int32_t posix_rwlock_destroy(DcRwlock **handle) {
    if (!handle || !*handle) return 0;
    DcRwlock *l = *handle;
    pthread_cond_destroy(&l->readers_cv);
    pthread_cond_destroy(&l->writers_cv);
    pthread_mutex_destroy(&l->m);
    free(l);
    *handle = NULL;
    return 0;
}
static int32_t rw_read(DcRwlock **h, int attempt) {
    DcRwlock *l = rwlock(h);
    if (!l) return POSIX_EINVAL;
    pthread_mutex_lock(&l->m);
    while (l->writer || (l->writers_waiting && !read_locks_held)) {
        if (attempt) { pthread_mutex_unlock(&l->m); return POSIX_EBUSY; }
        pthread_cond_wait(&l->readers_cv, &l->m);
    }
    ++l->readers;
    ++read_locks_held;
    pthread_mutex_unlock(&l->m);
    return 0;
}
static int32_t rw_write(DcRwlock **h, int attempt) {
    DcRwlock *l = rwlock(h);
    if (!l) return POSIX_EINVAL;
    pthread_mutex_lock(&l->m);
    if (attempt && (l->writer || l->readers)) { pthread_mutex_unlock(&l->m); return POSIX_EBUSY; }
    ++l->writers_waiting;
    while (l->writer || l->readers) pthread_cond_wait(&l->writers_cv, &l->m);
    --l->writers_waiting;
    l->writer = 1;
    pthread_mutex_unlock(&l->m);
    return 0;
}
static ABI int32_t posix_rwlock_rdlock(DcRwlock **h) { return rw_read(h, 0); }
static ABI int32_t posix_rwlock_wrlock(DcRwlock **h) { return rw_write(h, 0); }
static ABI int32_t posix_rwlock_tryrdlock(DcRwlock **h) { return rw_read(h, 1); }
static ABI int32_t posix_rwlock_trywrlock(DcRwlock **h) { return rw_write(h, 1); }
static ABI int32_t posix_rwlock_unlock(DcRwlock **h) {
    DcRwlock *l = rwlock(h);
    if (!l) return POSIX_EINVAL;
    pthread_mutex_lock(&l->m);
    if (l->writer) {
        l->writer = 0;
    } else if (l->readers) {
        --l->readers;
        if (read_locks_held) --read_locks_held;
    } else {
        pthread_mutex_unlock(&l->m);
        return POSIX_EPERM;
    }
    if (!l->readers && l->writers_waiting) pthread_cond_signal(&l->writers_cv);
    pthread_cond_broadcast(&l->readers_cv);
    pthread_mutex_unlock(&l->m);
    return 0;
}

/* pthread_detach: scePthreadDetach's contract with a POSIX error number. */
static ABI int32_t posix_detach(void *thread) {
    typedef int32_t (ABI *Detach)(void *);
    static Detach detach;
    if (!detach) detach = (Detach)runtime_resolve("4qGrR6eoP9Y#p#J", 0);
    int32_t r = detach ? detach(thread) : 0;
    return r ? r & 0xffff : 0;
}

/* --- USB (Logitech wheel driver logiWheel.prx): no devices ------------------------------- */
static ABI int64_t usbd_get_device_list(void ***list) {
    static void *empty[1];
    if (list) *list = empty;
    return 0;
}
static ABI int32_t usbd_no_device(void) { return (int32_t)0x80240004; } /* SCE_USBD_ERROR_NO_DEVICE */

/* --- Rudp (online): initialise, but never connect ---------------------------------------- */
static ABI int32_t rudp_context(void) { return 1; }
static ABI int32_t rudp_would_block(void) { return (int32_t)0x80770012; } /* SCE_RUDP_ERROR_WOULDBLOCK */

/* --- Live streaming / companion app / screenshots: off ----------------------------------- */
static ABI int32_t streaming_status(uint32_t *status) {
    if (status) memset(status, 0, 16);
    return 0;
}
static ABI int32_t companion_no_event(void) { return (int32_t)0x80E40008; } /* NO_EVENT */

/* --- Trophies ---------------------------------------------------------------------------- */
static ABI int32_t trophy_unlock_state(int32_t context, int32_t handle, uint32_t *flags, uint32_t *count) {
    if (flags) memset(flags, 0, 16);  /* SceNpTrophyFlagArray: 128 bits */
    if (count) *count = 0;
    return 0;
}

/* --- PlayGo: the whole game is installed ------------------------------------------------- */
static ABI int32_t playgo_todo(int32_t handle, void *list, uint32_t count, uint32_t *entries) {
    if (entries) *entries = 0;
    return 0;
}
static ABI int32_t playgo_eta(int32_t handle, const uint16_t *ids, uint32_t count, int64_t *eta) {
    if (eta) *eta = 0;
    return 0;
}
static ABI int32_t playgo_speed(int32_t handle, int32_t *speed) {
    if (speed) *speed = 2;  /* SCE_PLAYGO_INSTALL_SPEED_TRICKLE..FULL: full */
    return 0;
}

/* --- AppContent: add-on info/mount live in runtime_content.c; plenty of download space ----------------------------- */
static ABI int32_t download_space(int32_t format, uint64_t *kb) {
    if (kb) *kb = 4ull << 20;  /* 4 GiB */
    return 0;
}

/* --- Images: decoding is left to the game's fallbacks for now ---------------------------- */
static ABI int32_t image_unsupported(void) { return (int32_t)0x80690001; }
static ABI int32_t image_memory_size(void) { return 64 * 1024; }

/* --- Misc services ----------------------------------------------------------------------- */
static ABI int32_t safe_area(float *info) {
    if (info) { info[0] = 1.0f; memset(info + 1, 0, 128 - sizeof(float)); }
    return 0;
}
static ABI int32_t save_mount_info(const void *mount_point, uint64_t *info) {
    if (info) { info[0] = 1u << 20; info[1] = 1u << 20; }  /* blocks, free blocks */
    return 0;
}
static ABI int32_t audio_in_silent(void) { return 0; }
static ABI int32_t rtc_parse_unsupported(void) { return (int32_t)0x80B50002; }

static const RuntimeExport exports[] = {
    {"sceKernelSleep", kernel_sleep}, {"sceKernelGetdents", kernel_getdents},
    {"sceKernelLoadStartModule", kernel_load_start_module}, {"sceKernelStopUnloadModule", ok},
    {"sceKernelSetPrtAperture", ok}, {"sceKernelSetVirtualRangeName", ok}, {"sceKernelFutimes", ok},
    {"sceKernelGetOpenPsId", kernel_get_open_psid},

    {"sem_init", posix_sem_init}, {"sem_destroy", posix_sem_destroy}, {"sem_wait", posix_sem_wait},
    {"sem_trywait", posix_sem_trywait}, {"sem_post", posix_sem_post}, {"sem_getvalue", posix_sem_getvalue},
    {"pthread_rwlock_init", posix_rwlock_init}, {"pthread_rwlock_destroy", posix_rwlock_destroy},
    {"pthread_rwlock_rdlock", posix_rwlock_rdlock}, {"pthread_rwlock_wrlock", posix_rwlock_wrlock},
    {"pthread_rwlock_tryrdlock", posix_rwlock_tryrdlock}, {"pthread_rwlock_trywrlock", posix_rwlock_trywrlock},
    {"pthread_rwlock_unlock", posix_rwlock_unlock}, {"pthread_detach", posix_detach},

    {"sceUsbdInit", ok}, {"sceUsbdExit", ok}, {"sceUsbdGetDeviceList", usbd_get_device_list},
    {"sceUsbdFreeDeviceList", ok}, {"sceUsbdHandleEventsTimeout", ok}, {"sceUsbdCheckConnected", usbd_no_device},
    {"sceUsbdOpen", usbd_no_device}, {"sceUsbdClose", ok}, {"sceUsbdGetDeviceDescriptor", usbd_no_device},
    {"sceUsbdGetConfigDescriptor", usbd_no_device}, {"sceUsbdFreeConfigDescriptor", ok},
    {"sceUsbdClaimInterface", usbd_no_device}, {"sceUsbdReleaseInterface", ok},
    {"sceUsbdKernelDriverActive", ok}, {"sceUsbdAttachKernelDriver", ok}, {"sceUsbdSetConfiguration", usbd_no_device},
    {"sceUsbdAllocTransfer", ok}, {"sceUsbdFreeTransfer", ok}, {"sceUsbdSubmitTransfer", usbd_no_device},
    {"sceUsbdFillControlSetup", ok}, {"sceUsbdFillControlTransfer", ok}, {"sceUsbdFillInterruptTransfer", ok},
    {"sceUsbdControlTransfer", usbd_no_device}, {"sceUsbdControlTransferGetData", ok},
    {"sceUsbdGetBusNumber", ok}, {"sceUsbdGetDeviceAddress", ok},

    {"sceRudpInit", ok}, {"sceRudpEnd", ok}, {"sceRudpCreateContext", rudp_context},
    {"sceRudpTerminate", ok}, {"sceRudpBind", ok}, {"sceRudpInitiate", fail_generic},
    {"sceRudpProcessEvents", ok}, {"sceRudpNetReceived", ok}, {"sceRudpSetEventHandler", ok},
    {"sceRudpSetOption", ok}, {"sceRudpGetOption", ok}, {"sceRudpGetStatus", ok},
    {"sceRudpRead", rudp_would_block}, {"sceRudpWrite", rudp_would_block}, {"sceRudpGetSizeWritable", ok},

    {"sceGameLiveStreamingInitialize", ok}, {"sceGameLiveStreamingTerminate", ok},
    {"sceGameLiveStreamingEnableLiveStreaming", ok}, {"sceGameLiveStreamingPermitLiveStreaming", ok},
    {"sceGameLiveStreamingPermitServerSideRecording", ok}, {"sceGameLiveStreamingPostSocialMessage", ok},
    {"sceGameLiveStreamingGetCurrentStatus", streaming_status}, {"sceGameLiveStreamingGetProgramInfo", fail_generic},
    {"sceCompanionHttpdStart", ok}, {"sceCompanionHttpdStop", ok}, {"sceCompanionHttpdTerminate", ok},
    {"sceCompanionHttpdInitialize", ok}, {"sceCompanionHttpdGetEvent", companion_no_event},
    {"sceCompanionHttpdRegisterRequestCallback2", ok}, {"sceCompanionHttpdUnregisterRequestCallback", ok},
    {"sceScreenShotEnable", ok}, {"sceScreenShotDisable", ok}, {"sceVideoRecordingSetInfo", ok},

    {"sceNpTrophyDestroyContext", ok}, {"sceNpTrophyDestroyHandle", ok},
    {"sceNpTrophyGetTrophyUnlockState", trophy_unlock_state},

    {"scePlayGoClose", ok}, {"scePlayGoTerminate", ok}, {"scePlayGoPrefetch", ok},
    {"scePlayGoGetToDoList", playgo_todo}, {"scePlayGoGetEta", playgo_eta},
    {"scePlayGoGetInstallSpeed", playgo_speed},

    {"sceAppContentAddcontMount", (void *)runtime_addcont_mount}, {"sceAppContentAddcontUnmount", ok},
    {"sceAppContentGetAddcontInfo", (void *)runtime_addcont_info}, {"sceAppContentDownloadDataFormat", ok},
    {"sceAppContentDownloadDataGetAvailableSpaceKb", download_space},

    {"sceJpegDecCreate", image_unsupported}, {"sceJpegDecDelete", ok}, {"sceJpegDecDecode", image_unsupported},
    {"sceJpegDecParseHeader", image_unsupported}, {"sceJpegDecQueryMemorySize", image_memory_size},
    {"scePngDecCreate", image_unsupported}, {"scePngDecDelete", ok}, {"scePngDecDecode", image_unsupported},
    {"scePngDecParseHeader", image_unsupported}, {"scePngDecQueryMemorySize", image_memory_size},
    {"scePngEncCreate", image_unsupported}, {"scePngEncDelete", ok}, {"scePngEncEncode", image_unsupported},
    {"scePngEncQueryMemorySize", image_memory_size},

    {"sceGameCustomDataDialogInitialize", ok}, {"sceGameCustomDataDialogTerminate", ok},
    {"sceGameCustomDataDialogOpen", ok}, {"sceGameCustomDataDialogClose", ok},
    {"sceGameCustomDataDialogUpdateStatus", dialog_finished}, {"sceGameCustomDataDialogGetResult", dialog_result},
    {"sceNpSnsFacebookDialogInitialize", ok}, {"sceNpSnsFacebookDialogTerminate", ok},
    {"sceNpSnsFacebookDialogOpen", ok}, {"sceNpSnsFacebookDialogClose", ok},
    {"sceNpSnsFacebookDialogUpdateStatus", dialog_finished}, {"sceNpSnsFacebookDialogGetResult", dialog_result},
    {"sceErrorDialogInitialize", ok}, {"sceErrorDialogTerminate", ok}, {"sceErrorDialogOpen", ok},
    {"sceErrorDialogClose", ok}, {"sceErrorDialogUpdateStatus", dialog_finished},
    {"sceMsgDialogClose", ok}, {"sceMsgDialogGetResult", dialog_result},
    {"sceSaveDataDialogClose", ok}, {"sceSaveDataDialogGetResult", dialog_result},
    {"sceSaveDataDialogIsReadyToDisplay", one}, {"sceSaveDataDialogProgressBarSetValue", ok},
    {"sceNpCommerceDialogGetResult", dialog_result}, {"sceNpCommerceDialogGetStatus", dialog_none},
    {"sceNpProfileDialogClose", ok}, {"sceCommonDialogIsUsed", ok},

    {"sceSystemServiceGetDisplaySafeAreaInfo", safe_area},
    {"sceSaveDataGetMountInfo", save_mount_info},
    {"sceAudioInGetSilentState", audio_in_silent},
    {"sceRtcParseRFC3339", rtc_parse_unsupported},
};

uintptr_t runtime_dc_resolve(const char *name) {
    uintptr_t found = runtime_font_resolve(name);
    if (!found) found = RUNTIME_LOOKUP(exports, name);
    if (found) trace(runtime_symbol(name));
    return found;
}
