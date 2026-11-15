package com.drunkbatya.drunkheadphonesadapter

import java.nio.ByteBuffer
import java.nio.ByteOrder

private const val FIRMWARE_HEADER_OFFSET = 0x100
private const val FIRMWARE_HEADER_MAGIC = 0x46414844
private const val FIRMWARE_HEADER_VERSION_SIZE = 48
private const val FIRMWARE_HEADER_SIZE = 3 * Int.SIZE_BYTES + FIRMWARE_HEADER_VERSION_SIZE

class FirmwareImage(val bytes: ByteArray, val target: Int, val version: String)

fun parseFirmwareImage(bytes: ByteArray): FirmwareImage {
  val headerEnd = FIRMWARE_HEADER_OFFSET + FIRMWARE_HEADER_SIZE
  require(bytes.size >= headerEnd) { "File size ${bytes.size} bytes, header ends at $headerEnd" }
  val header = ByteBuffer.wrap(bytes, FIRMWARE_HEADER_OFFSET, FIRMWARE_HEADER_SIZE).order(ByteOrder.LITTLE_ENDIAN)
  val magic = header.int
  require(magic == FIRMWARE_HEADER_MAGIC) { "Header magic 0x%08X".format(magic) }
  val target = header.int
  val imageSize = header.int
  require(imageSize == bytes.size) { "Header image size $imageSize bytes, file size ${bytes.size} bytes" }
  val versionBytes = ByteArray(FIRMWARE_HEADER_VERSION_SIZE).also { header.get(it) }
  val version = String(versionBytes, Charsets.US_ASCII).substringBefore('\u0000')
  return FirmwareImage(bytes, target, version)
}
