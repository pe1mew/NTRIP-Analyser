/**
 * @file jni_glue.c
 * @brief JNI marshalling for @ref ntrip_bridge.h -- and nothing else.
 *
 * Every function here converts Java types to C types, calls one bridge
 * function, and converts back.  No logic lives in this file by design:
 * it is the only code in the project that cannot be compiled without an
 * NDK, so anything placed here escapes desktop testing (see
 * ntrip_bridge.h).  If a change needs a decision made, it belongs in
 * ntrip_bridge.c.
 *
 * The bridge handle crosses the boundary as a jlong.  The Kotlin side
 * holds it in one object with a matching lifetime, so it is never a raw
 * pointer in reachable Kotlin code.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * @copyright Apache License 2.0 with Commons Clause (see LICENSE for details)
 */

#include <jni.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "ntrip_bridge.h"

/*
 * The externals are declared in a companion object, so the obvious
 * symbol name is `..._NtripBridge_00024Companion_nativeOpen` -- and that
 * is wrong.  `@JvmStatic` promotes them to static methods on the
 * *enclosing* class, and the JVM resolves the name from where the method
 * ends up, not from where it was written.  Dropping `@JvmStatic` would
 * make the `$Companion` form correct instead; the two must agree.
 *
 * Confirming the symbols exist is not enough to catch this -- they do
 * exist, under the name nothing looks up.  The check that matters is
 * that the app calls a native method without an UnsatisfiedLinkError.
 */
#define JNI_FN(name) \
    Java_nl_pe1mew_ntripanalyser_NtripBridge_##name

/** @brief Fetch a UTF-8 copy of @p s; returns "" for null. */
static const char *str_in(JNIEnv *env, jstring s)
{
    return s ? (*env)->GetStringUTFChars(env, s, NULL) : NULL;
}

static void str_free(JNIEnv *env, jstring s, const char *c)
{
    if (s && c) (*env)->ReleaseStringUTFChars(env, s, c);
}

JNIEXPORT jlong JNICALL
JNI_FN(nativeOpen)(JNIEnv *env, jobject thiz,
                   jstring caster, jint port, jstring mountpoint,
                   jstring user, jstring password,
                   jdouble lat, jdouble lon, jboolean sendGga, jboolean watch)
{
    (void)thiz;
    const char *c_caster = str_in(env, caster);
    const char *c_mount  = str_in(env, mountpoint);
    const char *c_user   = str_in(env, user);
    const char *c_pass   = str_in(env, password);

    NtripBridge *b = bridge_open(c_caster ? c_caster : "", (int)port,
                                 c_mount ? c_mount : "",
                                 c_user ? c_user : "",
                                 c_pass ? c_pass : "",
                                 (double)lat, (double)lon,
                                 sendGga == JNI_TRUE, watch == JNI_TRUE);

    str_free(env, caster, c_caster);
    str_free(env, mountpoint, c_mount);
    str_free(env, user, c_user);
    str_free(env, password, c_pass);
    return (jlong)(intptr_t)b;
}

JNIEXPORT jlong JNICALL
JNI_FN(nativeOpenFile)(JNIEnv *env, jobject thiz, jstring path, jboolean watch)
{
    (void)thiz;
    const char *c_path = str_in(env, path);
    NtripBridge *b = bridge_open_file(c_path ? c_path : "", watch == JNI_TRUE);
    str_free(env, path, c_path);
    return (jlong)(intptr_t)b;
}

JNIEXPORT jint JNICALL
JNI_FN(nativePump)(JNIEnv *env, jobject thiz, jlong handle,
                   jint timeoutMs, jdouble nowS)
{
    (void)env; (void)thiz;
    return (jint)bridge_pump((NtripBridge *)(intptr_t)handle,
                             (int)timeoutMs, (double)nowS);
}

JNIEXPORT jstring JNICALL
JNI_FN(nativeSnapshotJson)(JNIEnv *env, jobject thiz, jlong handle)
{
    (void)thiz;
    /* Heap, not stack: an Android thread's stack is far smaller than a
     * desktop's, and 16 kB there is a real risk. */
    const size_t cap = 16384;
    char *buf = (char *)malloc(cap);
    if (!buf) return NULL;

    int n = bridge_snapshot_json((NtripBridge *)(intptr_t)handle, buf, cap);
    jstring out = (n < 0) ? NULL : (*env)->NewStringUTF(env, buf);
    free(buf);
    return out;
}

JNIEXPORT jint JNICALL
JNI_FN(nativeOverall)(JNIEnv *env, jobject thiz, jlong handle)
{
    (void)env; (void)thiz;
    return (jint)bridge_overall((const NtripBridge *)(intptr_t)handle);
}

JNIEXPORT void JNICALL
JNI_FN(nativeSetPosition)(JNIEnv *env, jobject thiz, jlong handle,
                          jdouble lat, jdouble lon)
{
    (void)env; (void)thiz;
    bridge_set_position((NtripBridge *)(intptr_t)handle,
                        (double)lat, (double)lon);
}

JNIEXPORT void JNICALL
JNI_FN(nativeClose)(JNIEnv *env, jobject thiz, jlong handle)
{
    (void)env; (void)thiz;
    bridge_close((NtripBridge *)(intptr_t)handle);
}

JNIEXPORT jstring JNICALL
JNI_FN(nativeSourcetable)(JNIEnv *env, jobject thiz,
                          jstring caster, jint port,
                          jstring user, jstring password)
{
    (void)thiz;
    const char *c_caster = str_in(env, caster);
    const char *c_user   = str_in(env, user);
    const char *c_pass   = str_in(env, password);

    /* A national caster's table runs to tens of kilobytes. */
    /* A 1200-mountpoint caster serialises to roughly 150 kB, and the
     * builder refuses rather than truncates, which the app reports as
     * an unreachable caster. Transient, freed below. */
    const size_t cap = 1048576;
    char *buf = (char *)malloc(cap);
    jstring outv = NULL;

    if (buf) {
        int n = bridge_sourcetable_json(c_caster ? c_caster : "", (int)port,
                                        c_user ? c_user : "",
                                        c_pass ? c_pass : "", buf, cap);
        if (n >= 0) outv = (*env)->NewStringUTF(env, buf);
        free(buf);
    }

    str_free(env, caster, c_caster);
    str_free(env, user, c_user);
    str_free(env, password, c_pass);
    return outv;
}

JNIEXPORT jboolean JNICALL
JNI_FN(nativeOpenEph)(JNIEnv *env, jobject thiz, jlong handle,
                      jstring caster, jint port, jstring mountpoint,
                      jstring user, jstring password)
{
    (void)thiz;
    const char *c_caster = str_in(env, caster);
    const char *c_mount  = str_in(env, mountpoint);
    const char *c_user   = str_in(env, user);
    const char *c_pass   = str_in(env, password);

    jboolean ok = bridge_open_eph((NtripBridge *)(intptr_t)handle,
                                  c_caster ? c_caster : "", (int)port,
                                  c_mount ? c_mount : "",
                                  c_user ? c_user : "",
                                  c_pass ? c_pass : "") ? JNI_TRUE : JNI_FALSE;

    str_free(env, caster, c_caster);
    str_free(env, mountpoint, c_mount);
    str_free(env, user, c_user);
    str_free(env, password, c_pass);
    return ok;
}

JNIEXPORT jint JNICALL
JNI_FN(nativeEphCount)(JNIEnv *env, jobject thiz, jlong handle)
{
    (void)env; (void)thiz;
    return (jint)bridge_eph_count((const NtripBridge *)(intptr_t)handle);
}

/**
 * Render the sky plot into a caller-allocated int array, packed ARGB as
 * `Bitmap.setPixels` expects.  The array comes from Kotlin so the
 * bitmap can be reused across refreshes rather than reallocated every
 * second.
 */
JNIEXPORT jboolean JNICALL
JNI_FN(nativeSkyPixels)(JNIEnv *env, jobject thiz, jlong handle,
                        jintArray pixels, jint width, jint height)
{
    (void)thiz;
    if (!pixels || width < 100 || height < 100) return JNI_FALSE;
    if ((*env)->GetArrayLength(env, pixels) < width * height) return JNI_FALSE;

    size_t nbytes = (size_t)width * (size_t)height * 3;
    unsigned char *rgb = (unsigned char *)malloc(nbytes);
    if (!rgb) return JNI_FALSE;

    if (!bridge_sky_rgb((NtripBridge *)(intptr_t)handle, rgb, width, height)) {
        free(rgb);
        return JNI_FALSE;
    }

    jint *out = (*env)->GetIntArrayElements(env, pixels, NULL);
    if (!out) { free(rgb); return JNI_FALSE; }

    for (int i = 0; i < width * height; i++) {
        out[i] = (jint)(0xFF000000u |
                        ((uint32_t)rgb[i * 3 + 0] << 16) |
                        ((uint32_t)rgb[i * 3 + 1] <<  8) |
                        ((uint32_t)rgb[i * 3 + 2]));
    }

    (*env)->ReleaseIntArrayElements(env, pixels, out, 0);
    free(rgb);
    return JNI_TRUE;
}

JNIEXPORT jint JNICALL
JNI_FN(nativeEphFrames)(JNIEnv *env, jobject thiz, jlong handle)
{
    (void)env; (void)thiz;
    return (jint)bridge_eph_frames((const NtripBridge *)(intptr_t)handle);
}

JNIEXPORT jint JNICALL
JNI_FN(nativeLoadRinex)(JNIEnv *env, jobject thiz, jlong handle, jstring path)
{
    (void)thiz;
    const char *c_path = str_in(env, path);
    jint n = (jint)bridge_load_rinex((NtripBridge *)(intptr_t)handle,
                                     c_path ? c_path : "");
    str_free(env, path, c_path);
    return n;
}

JNIEXPORT jint JNICALL
JNI_FN(nativeCheckRinex)(JNIEnv *env, jobject thiz, jstring path)
{
    (void)thiz;
    const char *c_path = str_in(env, path);
    jint n = (jint)bridge_check_rinex(c_path ? c_path : "");
    str_free(env, path, c_path);
    return n;
}

JNIEXPORT jint JNICALL
JNI_FN(nativePlaceable)(JNIEnv *env, jobject thiz, jlong handle)
{
    (void)env; (void)thiz;
    int tracked = 0;
    int have = bridge_placeable((const NtripBridge *)(intptr_t)handle, &tracked);
    /* Two counts in one call: placeable in the low half, tracked in the
     * high half, so the policy needs a single crossing per second. */
    return (jint)((tracked << 16) | (have & 0xFFFF));
}

JNIEXPORT void JNICALL
JNI_FN(nativeCloseEph)(JNIEnv *env, jobject thiz, jlong handle)
{
    (void)env; (void)thiz;
    bridge_close_eph((NtripBridge *)(intptr_t)handle);
}
