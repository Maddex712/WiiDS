package com.driftdroid.android

import java.io.ByteArrayOutputStream
import java.io.IOException
import java.io.InputStream
import java.io.OutputStream

internal object BoundedInput {
    fun copy(input: InputStream, output: OutputStream, limit: Long): Long {
        val buffer = ByteArray(32 * 1024)
        var total = 0L
        while (true) {
            val request = minOf(buffer.size.toLong(), limit - total + 1).toInt()
            val read = input.read(buffer, 0, request)
            if (read < 0) return total
            if (read == 0) throw IOException("The input stopped responding.")
            if (read > limit - total) throw IOException("The selected file exceeds the size limit.")
            output.write(buffer, 0, read)
            total += read
        }
    }

    fun bytes(input: InputStream, limit: Int, exact: Boolean = false): ByteArray {
        val output = ByteArrayOutputStream(minOf(limit, 32 * 1024))
        val size = copy(input, output, limit.toLong())
        if (exact && size != limit.toLong()) throw IOException("Expected a $limit-byte file.")
        return output.toByteArray()
    }
}
