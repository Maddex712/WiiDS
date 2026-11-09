#include <archive.h>
#include <archive_entry.h>
#include <jni.h>
#include <stdint.h>

static struct archive *reader(jlong handle) {
    return (struct archive *)(intptr_t)handle;
}

static void fail(JNIEnv *env, const char *message) {
    jclass io = (*env)->FindClass(env, "java/io/IOException");
    if (io != NULL) (*env)->ThrowNew(env, io, message);
}

JNIEXPORT jlong JNICALL Java_com_driftdroid_android_CatalogArchive_open(
        JNIEnv *env, jclass type, jstring path, jboolean rar) {
    (void)type;
    const char *name = (*env)->GetStringUTFChars(env, path, NULL);
    if (name == NULL) return 0;
    struct archive *a = archive_read_new();
    int status = ARCHIVE_FATAL;
    if (a != NULL) {
        status = rar ? archive_read_support_format_rar5(a) : archive_read_support_format_7zip(a);
        if (status == ARCHIVE_OK) status = archive_read_open_filename(a, name, 65536);
    }
    (*env)->ReleaseStringUTFChars(env, path, name);
    if (status != ARCHIVE_OK) {
        if (a != NULL) archive_read_free(a);
        fail(env, "Could not open the creator archive.");
        return 0;
    }
    return (jlong)(intptr_t)a;
}

JNIEXPORT jstring JNICALL Java_com_driftdroid_android_CatalogArchive_next(
        JNIEnv *env, jclass type, jlong handle, jlongArray details) {
    (void)type;
    struct archive_entry *entry;
    int status = archive_read_next_header(reader(handle), &entry);
    if (status == ARCHIVE_EOF) return NULL;
    if (status != ARCHIVE_OK || archive_entry_is_encrypted(entry) > 0) {
        fail(env, "Could not read the creator archive.");
        return NULL;
    }
    const char *path = archive_entry_pathname_utf8(entry);
    if (path == NULL) {
        fail(env, "The creator archive contains an invalid filename.");
        return NULL;
    }
    jlong metadata[] = { archive_entry_size(entry),
                         archive_entry_filetype(entry) == AE_IFREG && archive_entry_hardlink(entry) == NULL };
    (*env)->SetLongArrayRegion(env, details, 0, 2, metadata);
    return (*env)->NewStringUTF(env, path);
}

JNIEXPORT jint JNICALL Java_com_driftdroid_android_CatalogArchive_read(
        JNIEnv *env, jclass type, jlong handle, jbyteArray buffer) {
    (void)type;
    unsigned char bytes[16384];
    la_ssize_t n = archive_read_data(reader(handle), bytes, sizeof bytes);
    if (n < 0) {
        fail(env, "A texture in the creator archive is damaged.");
        return -1;
    }
    if (n > 0) (*env)->SetByteArrayRegion(env, buffer, 0, (jsize)n, (const jbyte *)bytes);
    return (jint)n;
}

JNIEXPORT void JNICALL Java_com_driftdroid_android_CatalogArchive_close(
        JNIEnv *env, jclass type, jlong handle) {
    (void)env;
    (void)type;
    archive_read_free(reader(handle));
}
