package com.drunkbatya.drunkheadphonesadapter

import java.io.IOException
import java.nio.ByteBuffer
import java.nio.ByteOrder

private const val FLASH_REGION = 0
private const val FLASH_ADDRESS = 0x08000000
private const val FLASH_PAGE_SIZE = 2048
private const val FLASH_PROGRAM_UNIT = 8
private const val FLASH_ERASED_BYTE = 0xFF.toByte()

private const val OPTION_BYTES_REGION = 1
private const val OPTION_BYTES_ADDRESS = 0x1FFF7800
private const val OPTION_BYTES_SIZE = 128
private const val OPTR_OFFSET = 0
private const val OPTR_COMPLEMENT_OFFSET = 4
private const val OPTR_REQUIRED_MASK = 0x07300000
private const val OPTR_REQUIRED_VALUE = 0x07100000

suspend fun DfuDevice.writeFirmware(image: ByteArray, onProgress: (Float) -> Unit) {
  selectMemoryRegion(FLASH_REGION)
  returnToIdle()

  val pageCount = (image.size + FLASH_PAGE_SIZE - 1) / FLASH_PAGE_SIZE
  val (firstPageBlocks, otherBlocks) = (0 until image.size step transferSize).partition { it < FLASH_PAGE_SIZE }
  val stepCount = pageCount + (firstPageBlocks.size + otherBlocks.size) * 2
  var stepsDone = 0
  val reportStepDone = {
    stepsDone++
    onProgress(stepsDone.toFloat() / stepCount)
  }

  for (page in 0 until pageCount) {
    erasePage(FLASH_ADDRESS + page * FLASH_PAGE_SIZE)
    reportStepDone()
  }
  for (offset in otherBlocks + firstPageBlocks) {
    writeMemory(FLASH_ADDRESS + offset, flashBlock(image, offset))
    reportStepDone()
  }
  val flashImage = readMemory(FLASH_ADDRESS, image.size, reportStepDone)
  if (!flashImage.contentEquals(image)) {
    erasePage(FLASH_ADDRESS)
    throw IOException("Flash content differs from image")
  }
}

suspend fun DfuDevice.startFirmware() {
  selectMemoryRegion(FLASH_REGION)
  returnToIdle()
  writeMemoryAndAwaitReset(FLASH_ADDRESS, ByteArray(0))
}

suspend fun DfuDevice.writeRequiredOptionBytes(): Int? {
  selectMemoryRegion(OPTION_BYTES_REGION)
  val optionBytes = ByteBuffer.wrap(readMemory(OPTION_BYTES_ADDRESS, OPTION_BYTES_SIZE)).order(ByteOrder.LITTLE_ENDIAN)
  val optr = optionBytes.getInt(OPTR_OFFSET)
  val optrComplement = optionBytes.getInt(OPTR_COMPLEMENT_OFFSET)
  if (optrComplement != optr.inv()) {
    throw IOException("Option bytes OPTR 0x%08X, complement 0x%08X".format(optr, optrComplement))
  }
  if (optr and OPTR_REQUIRED_MASK == OPTR_REQUIRED_VALUE) return null

  val requiredOptr = (optr and OPTR_REQUIRED_MASK.inv()) or OPTR_REQUIRED_VALUE
  optionBytes.putInt(OPTR_OFFSET, requiredOptr)
  optionBytes.putInt(OPTR_COMPLEMENT_OFFSET, requiredOptr.inv())
  writeMemoryAndAwaitReset(OPTION_BYTES_ADDRESS, optionBytes.array())
  return requiredOptr
}

private fun DfuDevice.flashBlock(image: ByteArray, offset: Int): ByteArray {
  val size = minOf(transferSize, image.size - offset)
  val alignedSize = (size + FLASH_PROGRAM_UNIT - 1) / FLASH_PROGRAM_UNIT * FLASH_PROGRAM_UNIT
  return ByteArray(alignedSize) { FLASH_ERASED_BYTE }.also { image.copyInto(it, 0, offset, offset + size) }
}
