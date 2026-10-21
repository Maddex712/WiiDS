package com.driftdroid.android

/**
 * Inserts one standalone Mii record (the 74-byte "Mii Data" exchange format - offsets confirmed
 * by decoding a real .rcd export byte-for-byte: name at 0x02..0x15 in UTF-16BE, creator name in
 * the last 20 bytes ending exactly at byte 74) into an existing RFL_DB.dat, rather than requiring
 * the player to already have a full replacement database.
 *
 * Container layout (RFL_DB.dat's "My Miis" table) confirmed against SuperFromND/rfl_mii_extractor
 * (github.com/SuperFromND/rfl_mii_extractor/blob/main/src/main.go), a working, independent
 * open-source RFL_DB.dat parser: 4-byte header, then exactly 100 Mii slots of 74 bytes each
 * starting at offset 4 - the same 74-byte record layout as the standalone exchange format, just
 * packed back-to-back with no per-slot wrapper.
 */
internal object MiiDatabaseEditor {
    const val MII_RECORD_BYTES = 74
    const val DATABASE_BYTES = 779_968

    private const val TABLE_HEADER_BYTES = 4
    private const val SLOT_COUNT = 100
    private const val MAGIC = "RNOD"
    // Same offset/algorithm as KartPad's KartPadMiiStorage.isValidDatabase - CRC-16/CCITT-XMODEM
    // (poly 0x1021, no reflection) over every byte before this offset, stored big-endian here.
    private const val CRC_OFFSET = 0x1F1DE

    sealed class Result {
        data class Success(val database: ByteArray, val slotIndex: Int) : Result()
        data class Failure(val message: String) : Result()
    }

    fun isValidMiiRecord(record: ByteArray): Boolean =
        record.size == MII_RECORD_BYTES && record.any { it != 0.toByte() }

    fun isValidDatabase(database: ByteArray): Boolean {
        if (database.size != DATABASE_BYTES) return false
        if (String(database, 0, 4, Charsets.US_ASCII) != MAGIC) return false
        val stored = ((database[CRC_OFFSET].toInt() and 0xff) shl 8) or (database[CRC_OFFSET + 1].toInt() and 0xff)
        return stored == crc16(database, CRC_OFFSET)
    }

    /** Finds the first empty (all-zero) "My Miis" slot and writes [record] into it, then
     * recomputes and rewrites the whole-database CRC so the result stays a genuinely valid
     * database - not just one that happens to work in our own runtime. Does not mutate the input
     * array; returns a new one. */
    fun insertMii(database: ByteArray, record: ByteArray): Result {
        if (!isValidMiiRecord(record)) {
            return Result.Failure("that doesn't look like a valid single-Mii (.rcd) file")
        }
        if (!isValidDatabase(database)) {
            return Result.Failure("the existing Mii database failed validation - it may be corrupt")
        }

        val updated = database.copyOf()
        var freeSlot = -1
        for (slot in 0 until SLOT_COUNT) {
            val offset = TABLE_HEADER_BYTES + slot * MII_RECORD_BYTES
            var empty = true
            for (i in 0 until MII_RECORD_BYTES) {
                if (updated[offset + i] != 0.toByte()) {
                    empty = false
                    break
                }
            }
            if (empty) {
                freeSlot = slot
                break
            }
        }
        if (freeSlot < 0) {
            return Result.Failure("no free Mii slot - all 100 \"My Miis\" slots are already used")
        }

        val offset = TABLE_HEADER_BYTES + freeSlot * MII_RECORD_BYTES
        System.arraycopy(record, 0, updated, offset, MII_RECORD_BYTES)

        val newCrc = crc16(updated, CRC_OFFSET)
        updated[CRC_OFFSET] = ((newCrc shr 8) and 0xff).toByte()
        updated[CRC_OFFSET + 1] = (newCrc and 0xff).toByte()

        return Result.Success(updated, freeSlot)
    }

    private fun crc16(data: ByteArray, length: Int): Int {
        var crc = 0
        for (index in 0 until length) {
            crc = crc xor ((data[index].toInt() and 0xff) shl 8)
            repeat(8) {
                crc =
                    if ((crc and 0x8000) != 0) {
                        (crc shl 1) xor 0x1021
                    } else {
                        crc shl 1
                    }
                crc = crc and 0xffff
            }
        }
        return crc
    }
}
