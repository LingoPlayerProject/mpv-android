#include <jni.h>

#include <mpv/client.h>

#include "globals.h"
#include "jni_utils.h"
#include "log.h"
#include "data_source.h"

#define DATA_SOURCE_CB_THREAD_NAME "data_source_cb"


/**
 * Read callback used to implement a custom stream. The semantics of the
 * callback match read(2) in blocking mode. Short reads are allowed (you can
 * return less bytes than requested, and libmpv will retry reading the rest
 * with another call). If no data can be immediately read, the callback must
 * block until there is new data. A return of 0 will be interpreted as final
 * EOF, although libmpv might retry the read, or seek to a different position.
 *
 * @param cookie opaque cookie identifying the stream,
 *               returned from mpv_stream_cb_open_fn
 * @param buf buffer to read data into
 * @param size of the buffer
 * @return number of bytes read into the buffer
 * @return 0 on EOF
 * @return -1 on error
 */
int64_t mpv_data_source_cb_read_fn(void *cookie, char *buf, uint64_t nbytes) {
#define __MIN(a, b) ( ((a) < (b)) ? (a) : (b) )
    JNIEnv * env = jni_get_env(DATA_SOURCE_CB_THREAD_NAME);
    if (env == NULL) {
        return -1;
    }
    jobject source = (jobject) cookie;
    jbyteArray array = NULL;

    int64_t ret = 0;
    int64_t read_size = 0;
    jbyte *elements = NULL;
    while (nbytes > 0) {
        //Read in batches to reduce peak memory usage in Java
        int tmp_size = __MIN((jint) nbytes, 1024 * 1024);
        array = env->NewByteArray((jsize) tmp_size);
        if (!array) {
            ALOGE("jni NewByteArray failed");
            break;
        }
        read_size = env->CallIntMethod(source, mpv_MPVDataSource_read, array, (jint) tmp_size);
        if (env->ExceptionCheck()) {
            ALOGE("DataSource read exception");
            ret = -1;
            break;
        }
        if (read_size < 0 || read_size > nbytes) {
            ALOGE("DataSource read returned invalid len %ld", read_size);
            ret = -1;
            break;
        }
        if (read_size == 0) {
            ALOGV("DataSource read EOF");
            break;
        }
        elements = env->GetByteArrayElements(array, 0);
        if (elements == NULL) {
            ALOGE("Failed to get byte array elements");
            ret = -1;
            break;
        }
        memcpy(buf + ret, elements, read_size);
        ret += read_size;
        nbytes -= read_size;
        env->ReleaseByteArrayElements(array, elements, 0);
        elements = NULL;
        env->DeleteLocalRef(array);
        array = NULL;
    }

    if (env->ExceptionCheck())
    {
        env->ExceptionDescribe();
        env->ExceptionClear();
    }
    if (elements)
    {
        env->ReleaseByteArrayElements(array, elements, 0);
    }
    if (array)
    {
        env->DeleteLocalRef(array);
    }
    return ret;
#undef __MIN
}

/**
 * Seek callback used to implement a custom stream.
 *
 * Note that mpv will issue a seek to position 0 immediately after opening. This
 * is used to test whether the stream is seekable (since seekability might
 * depend on the URI contents, not just the protocol). Return
 * MPV_ERROR_UNSUPPORTED if seeking is not implemented for this stream. This
 * seek also serves to establish the fact that streams start at position 0.
 *
 * This callback can be NULL, in which it behaves as if always returning
 * MPV_ERROR_UNSUPPORTED.
 *
 * @param cookie opaque cookie identifying the stream,
 *               returned from mpv_stream_cb_open_fn
 * @param offset target absolute stream position
 * @return the resulting offset of the stream
 *         MPV_ERROR_UNSUPPORTED or MPV_ERROR_GENERIC if the seek failed
 */
int64_t mpv_data_source_cb_seek_fn(void *cookie, int64_t offset) {
        JNIEnv * env = jni_get_env(DATA_SOURCE_CB_THREAD_NAME);
        if (env == NULL) {
            return MPV_ERROR_GENERIC;
        }
        jobject source = (jobject) cookie;

        env->CallVoidMethod(source, mpv_MPVDataSource_seek, (jlong) offset);
        if (env->ExceptionCheck())
        {
            ALOGE("DataSource seek error");
            env->ExceptionDescribe();
            env->ExceptionClear();
            return MPV_ERROR_GENERIC;
        }

        return MPV_ERROR_SUCCESS;
}

/**
 * Size callback used to implement a custom stream.
 *
 * Return MPV_ERROR_UNSUPPORTED if no size is known.
 *
 * This callback can be NULL, in which it behaves as if always returning
 * MPV_ERROR_UNSUPPORTED.
 *
 * @param cookie opaque cookie identifying the stream,
 *               returned from mpv_stream_cb_open_fn
 * @return the total size in bytes of the stream
 */
int64_t mpv_data_source_cb_size_fn(void *cookie) {
    JNIEnv * env = jni_get_env(DATA_SOURCE_CB_THREAD_NAME);
    if (env == NULL) {
        return MPV_ERROR_GENERIC;
    }
    jobject source = (jobject) cookie;

    jlong size = env->CallLongMethod(source, mpv_MPVDataSource_size);
    if (env->ExceptionCheck()) {
        ALOGE("DataSource get size error");
        env->ExceptionDescribe();
        env->ExceptionClear();
        return 0;
    }

    return size;
}

/**
 * Close callback used to implement a custom stream.
 *
 * @param cookie opaque cookie identifying the stream,
 *               returned from mpv_stream_cb_open_fn
 */
void mpv_data_source_cb_close_fn(void *cookie) {
    JNIEnv * env = jni_get_env(DATA_SOURCE_CB_THREAD_NAME);
    if (env == NULL) {
        return;
    }
    jobject source = (jobject) cookie;

    env->CallVoidMethod(source, mpv_MPVDataSource_close);
    if (env->ExceptionCheck()) {
        ALOGE("DataSource close with exception");
        env->ExceptionDescribe();
        env->ExceptionClear();
    }
    env->DeleteGlobalRef(source);
}

/**
 * Cancel callback used to implement a custom stream.
 *
 * This callback is used to interrupt any current or future read and seek
 * operations. It will be called from a separate thread than the demux
 * thread, and should not block.
 *
 * This callback can be NULL.
 *
 * Available since API 1.106.
 *
 * @param cookie opaque cookie identifying the stream,
 *               returned from mpv_stream_cb_open_fn
 */
void mpv_data_source_cb_cancel_fn(void *cookie) {
    JNIEnv * env = jni_get_env(DATA_SOURCE_CB_THREAD_NAME);
    if (env == NULL) {
        return;
    }
    jobject source = (jobject) cookie;

    env->CallVoidMethod(source, mpv_MPVDataSource_cancel);
    if (env->ExceptionCheck()) {
        ALOGE("DataSource cancel with exception");
        env->ExceptionDescribe();
        env->ExceptionClear();
    }
}

/**
 * Open callback used to implement a custom read-only (ro) stream. The user
 * must set the callback fields in the passed info struct. The cookie field
 * also can be set to store state associated to the stream instance.
 *
 * Note that the info struct is valid only for the duration of this callback.
 * You can't change the callbacks or the pointer to the cookie at a later point.
 *
 * Each stream instance created by the open callback can have different
 * callbacks.
 *
 * The close_fn callback will terminate the stream instance. The pointers to
 * your callbacks and cookie will be discarded, and the callbacks will not be
 * called again.
 *
 * @param user_data opaque user data provided via mpv_stream_cb_add()
 * @param uri name of the stream to be opened (with protocol prefix)
 * @param info fields which the user should fill
 * @return 0 on success, MPV_ERROR_LOADING_FAILED if the URI cannot be opened.
 */
int mpv_open_data_source_fn(void *user_data, char *uri,
                               mpv_stream_cb_info *info) {
    JNIEnv * env = jni_get_env(DATA_SOURCE_CB_THREAD_NAME);
    if (env == NULL) {
        ALOGE("failed to get JNIEnv");
        return MPV_ERROR_LOADING_FAILED;
    }
    ALOGV("calling MPVLib openDataSource...");
    jstring juri = env->NewStringUTF(uri);
    if (!juri) {
        ALOGE("mpv jni create juri failed");
        return MPV_ERROR_LOADING_FAILED;
    }
    jobject data_source = env->CallStaticObjectMethod(mpv_MPVLib, mpv_MPVLib_openDataSource, juri);
    env->DeleteLocalRef(juri);
    ALOGV("calling MPVLib openDataSource success");
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        return MPV_ERROR_LOADING_FAILED;
    }
    if (data_source == NULL) {
        ALOGE("openDataSource returns null");
        return MPV_ERROR_LOADING_FAILED;
    }
    jobject ref = env->NewGlobalRef(data_source);
    env->DeleteLocalRef(data_source);
    if (ref == NULL) {
        ALOGE("failed to create data source global ref");
        return MPV_ERROR_LOADING_FAILED;
    }
    ALOGV("open data source success");
    info->cookie = ref;
    info->read_fn = mpv_data_source_cb_read_fn;
    info->seek_fn = mpv_data_source_cb_seek_fn;
    info->size_fn = mpv_data_source_cb_size_fn;
    info->close_fn = mpv_data_source_cb_close_fn;
    info->cancel_fn = mpv_data_source_cb_cancel_fn;
    return MPV_ERROR_SUCCESS;
}
