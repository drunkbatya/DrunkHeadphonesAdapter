package com.drunkbatya.drunkheadphonesadapter

import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbDeviceConnection
import android.hardware.usb.UsbInterface
import android.hardware.usb.UsbManager
import java.io.Closeable
import java.io.IOException
import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlinx.coroutines.delay

private const val BOOTLOADER_VENDOR_ID = 0x0483
private const val BOOTLOADER_PRODUCT_ID = 0xDF11
private const val DFU_INTERFACE_CLASS = 0xFE
private const val DFU_FUNCTIONAL_DESCRIPTOR_TYPE = 0x21
private const val DFU_FUNCTIONAL_DESCRIPTOR_TRANSFER_SIZE_OFFSET = 5

private const val REQUEST_TYPE_TO_DEVICE = 0x21
private const val REQUEST_TYPE_FROM_DEVICE = 0xA1
private const val REQUEST_SEND_BLOCK = 1
private const val REQUEST_RECEIVE_BLOCK = 2
private const val REQUEST_GET_STATUS = 3
private const val REQUEST_CLEAR_STATUS = 4
private const val REQUEST_ABORT = 6

private const val STATUS_SIZE = 6
private const val STATUS_OK = 0
private const val STATE_READY_FOR_NEXT_BLOCK = 5
private const val STATE_ERROR = 10

private const val COMMAND_BLOCK = 0
private const val FIRST_DATA_BLOCK = 2
private const val COMMAND_SET_ADDRESS = 0x21
private const val COMMAND_ERASE_PAGE = 0x41
private const val COMMAND_SIZE = 5

private const val CONTROL_TIMEOUT_MS = 5000
private const val STATUS_POLLS_BEFORE_RESET = 3

fun findBootloaderDevice(usbManager: UsbManager): UsbDevice? =
  usbManager.deviceList.values.firstOrNull {
    it.vendorId == BOOTLOADER_VENDOR_ID && it.productId == BOOTLOADER_PRODUCT_ID
  }

class DfuDevice private constructor(
  val device: UsbDevice,
  private val connection: UsbDeviceConnection,
  private val memoryRegions: List<UsbInterface>,
  val transferSize: Int,
) : Closeable {
  private class Status(val code: Int, val pollTimeoutMs: Long, val state: Int)

  private val interfaceNumber = memoryRegions.first().id

  fun selectMemoryRegion(alternateSetting: Int) {
    val region = memoryRegions.first { it.alternateSetting == alternateSetting }
    if (!connection.setInterface(region)) throw IOException("Select memory region $alternateSetting failed")
  }

  fun returnToIdle() {
    val request = if (readStatus().state == STATE_ERROR) REQUEST_CLEAR_STATUS else REQUEST_ABORT
    sendRequest(request, 0, ByteArray(0))
  }

  suspend fun erasePage(address: Int) {
    sendCommand(COMMAND_ERASE_PAGE, address)
  }

  suspend fun writeMemory(address: Int, data: ByteArray) {
    setAddress(address)
    sendBlock(FIRST_DATA_BLOCK, data)
    awaitBlockProcessed("Write at 0x%08X".format(address))
  }

  suspend fun writeMemoryAndAwaitReset(address: Int, data: ByteArray) {
    setAddress(address)
    sendBlock(FIRST_DATA_BLOCK, data)
    repeat(STATUS_POLLS_BEFORE_RESET) {
      val status = runCatching { readStatus() }.getOrNull() ?: return
      delay(status.pollTimeoutMs)
    }
  }

  suspend fun readMemory(address: Int, size: Int, onBlockRead: () -> Unit = {}): ByteArray {
    setAddress(address)
    abort()
    val memory = ByteArray(size)
    for ((index, offset) in (0 until size step transferSize).withIndex()) {
      val block = ByteArray(minOf(transferSize, size - offset))
      val length = connection.controlTransfer(
        REQUEST_TYPE_FROM_DEVICE, REQUEST_RECEIVE_BLOCK, FIRST_DATA_BLOCK + index, interfaceNumber,
        block, block.size, CONTROL_TIMEOUT_MS,
      )
      if (length != block.size) {
        throw IOException("Read at 0x%08X: %d of %d bytes".format(address + offset, length, block.size))
      }
      block.copyInto(memory, offset)
      onBlockRead()
    }
    abort()
    return memory
  }

  override fun close() {
    connection.releaseInterface(memoryRegions.first())
    connection.close()
  }

  private suspend fun setAddress(address: Int) {
    sendCommand(COMMAND_SET_ADDRESS, address)
  }

  private fun abort() {
    sendRequest(REQUEST_ABORT, 0, ByteArray(0))
  }

  private suspend fun sendCommand(command: Int, address: Int) {
    val commandBytes = ByteBuffer.allocate(COMMAND_SIZE).order(ByteOrder.LITTLE_ENDIAN)
      .put(command.toByte()).putInt(address).array()
    sendBlock(COMMAND_BLOCK, commandBytes)
    awaitBlockProcessed("Command 0x%02X at 0x%08X".format(command, address))
  }

  private fun sendBlock(blockNumber: Int, data: ByteArray) {
    sendRequest(REQUEST_SEND_BLOCK, blockNumber, data)
  }

  private suspend fun awaitBlockProcessed(step: String) {
    while (true) {
      val status = readStatus()
      if (status.code != STATUS_OK) throw IOException("$step: DFU status ${status.code}, state ${status.state}")
      if (status.state == STATE_READY_FOR_NEXT_BLOCK) return
      delay(status.pollTimeoutMs)
    }
  }

  private fun readStatus(): Status {
    val response = ByteArray(STATUS_SIZE)
    val length = connection.controlTransfer(
      REQUEST_TYPE_FROM_DEVICE, REQUEST_GET_STATUS, 0, interfaceNumber, response, response.size, CONTROL_TIMEOUT_MS,
    )
    if (length != STATUS_SIZE) throw IOException("DFU status: $length of $STATUS_SIZE bytes")
    val pollTimeoutMs = ByteBuffer.wrap(byteArrayOf(response[1], response[2], response[3], 0))
      .order(ByteOrder.LITTLE_ENDIAN).int
    return Status(response[0].toInt(), pollTimeoutMs.toLong(), response[4].toInt())
  }

  private fun sendRequest(request: Int, value: Int, data: ByteArray) {
    val length = connection.controlTransfer(
      REQUEST_TYPE_TO_DEVICE, request, value, interfaceNumber, data, data.size, CONTROL_TIMEOUT_MS,
    )
    if (length != data.size) throw IOException("DFU request $request: $length of ${data.size} bytes")
  }

  companion object {
    fun open(usbManager: UsbManager, device: UsbDevice): DfuDevice {
      val memoryRegions = (0 until device.interfaceCount).map(device::getInterface)
        .filter { it.interfaceClass == DFU_INTERFACE_CLASS }
      if (memoryRegions.isEmpty()) throw IOException("DFU interface not found on ${device.deviceName}")
      val connection = usbManager.openDevice(device) ?: throw IOException("Open ${device.deviceName} failed")
      val transferSize = readTransferSize(connection.rawDescriptors)
      if (transferSize == null || !connection.claimInterface(memoryRegions.first(), true)) {
        connection.close()
        throw IOException("Open DFU interface on ${device.deviceName} failed")
      }
      return DfuDevice(device, connection, memoryRegions, transferSize)
    }

    private fun readTransferSize(descriptors: ByteArray): Int? {
      var offset = 0
      while (offset + 1 < descriptors.size) {
        if (descriptors[offset + 1].toInt() == DFU_FUNCTIONAL_DESCRIPTOR_TYPE) {
          return ByteBuffer.wrap(descriptors, offset + DFU_FUNCTIONAL_DESCRIPTOR_TRANSFER_SIZE_OFFSET, 2)
            .order(ByteOrder.LITTLE_ENDIAN).short.toInt() and 0xFFFF
        }
        offset += descriptors[offset].toInt() and 0xFF
      }
      return null
    }
  }
}
