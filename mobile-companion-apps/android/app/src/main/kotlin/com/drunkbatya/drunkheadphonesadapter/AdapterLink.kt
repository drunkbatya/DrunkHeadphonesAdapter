package com.drunkbatya.drunkheadphonesadapter

import adapter.RpcRequest
import adapter.RpcResponse
import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.hardware.usb.UsbConstants
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbDeviceConnection
import android.hardware.usb.UsbEndpoint
import android.hardware.usb.UsbInterface
import android.hardware.usb.UsbManager
import com.squareup.wire.ProtoAdapter
import java.io.Closeable
import java.io.IOException
import kotlin.coroutines.resume
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext

private const val ADAPTER_VENDOR_ID = 0x1209
private const val ADAPTER_PRODUCT_ID = 0x0001
private const val ADAPTER_PRODUCT_NAME = "DrunkHeadphonesAdapter"
private const val ACTION_USB_PERMISSION = "com.drunkbatya.drunkheadphonesadapter.USB_PERMISSION"
private const val WRITE_TIMEOUT_MS = 100
private const val READ_POLL_MS = 50
private const val VARINT32_SIZE_MAX = 5

fun findAdapterDevice(usbManager: UsbManager): UsbDevice? =
  usbManager.deviceList.values.firstOrNull {
    it.vendorId == ADAPTER_VENDOR_ID && it.productId == ADAPTER_PRODUCT_ID && it.productName == ADAPTER_PRODUCT_NAME
  }

suspend fun requestUsbPermission(context: Context, usbManager: UsbManager, device: UsbDevice): Boolean {
  if (usbManager.hasPermission(device)) return true
  return suspendCancellableCoroutine { continuation ->
    val receiver = object : BroadcastReceiver() {
      override fun onReceive(receiverContext: Context, intent: Intent) {
        context.unregisterReceiver(this)
        continuation.resume(intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false))
      }
    }
    context.registerReceiver(receiver, IntentFilter(ACTION_USB_PERMISSION), Context.RECEIVER_NOT_EXPORTED)
    continuation.invokeOnCancellation { context.unregisterReceiver(receiver) }
    val intent = Intent(ACTION_USB_PERMISSION).setPackage(context.packageName)
    usbManager.requestPermission(device, PendingIntent.getBroadcast(context, 0, intent, PendingIntent.FLAG_MUTABLE))
  }
}

class AdapterLink private constructor(
  val device: UsbDevice,
  private val connection: UsbDeviceConnection,
  private val dataInterface: UsbInterface,
  private val inEndpoint: UsbEndpoint,
  private val outEndpoint: UsbEndpoint,
) : Closeable {
  private val mutex = Mutex()
  private var nextRequestId = 1
  private var rxBytes = ByteArray(0)

  suspend fun request(request: RpcRequest): RpcResponse = mutex.withLock {
    withContext(Dispatchers.IO) {
      val requestId = nextRequestId++
      val body = RpcRequest.ADAPTER.encode(request.copy(request_id = requestId))
      val frame = ProtoAdapter.UINT32.encode(body.size) + body
      val written = connection.bulkTransfer(outEndpoint, frame, frame.size, WRITE_TIMEOUT_MS)
      if (written != frame.size) throw IOException("Bulk write ${device.deviceName}: $written of ${frame.size} bytes")
      var response: RpcResponse
      do {
        response = RpcResponse.ADAPTER.decode(readFrame())
      } while (response.request_id != requestId)
      response
    }
  }

  override fun close() {
    connection.releaseInterface(dataInterface)
    connection.close()
  }

  private suspend fun readFrame(): ByteArray {
    val packet = ByteArray(inEndpoint.maxPacketSize)
    while (true) {
      takeFrame()?.let { return it }
      currentCoroutineContext().ensureActive()
      val length = connection.bulkTransfer(inEndpoint, packet, packet.size, READ_POLL_MS)
      if (length > 0) rxBytes += packet.copyOf(length)
    }
  }

  private fun takeFrame(): ByteArray? {
    var bodySize = 0
    for (i in 0 until minOf(rxBytes.size, VARINT32_SIZE_MAX)) {
      val byte = rxBytes[i].toInt()
      bodySize = bodySize or ((byte and 0x7F) shl (7 * i))
      if (byte and 0x80 != 0) continue
      val frameEnd = i + 1 + bodySize
      if (rxBytes.size < frameEnd) return null
      val body = rxBytes.copyOfRange(i + 1, frameEnd)
      rxBytes = rxBytes.copyOfRange(frameEnd, rxBytes.size)
      return body
    }
    return null
  }

  companion object {
    fun open(usbManager: UsbManager, device: UsbDevice): AdapterLink {
      val dataInterface = (0 until device.interfaceCount).map(device::getInterface)
        .firstOrNull { it.interfaceClass == UsbConstants.USB_CLASS_CDC_DATA }
        ?: throw IOException("CDC data interface not found on ${device.deviceName}")
      val bulkEndpoints = (0 until dataInterface.endpointCount).map(dataInterface::getEndpoint)
        .filter { it.type == UsbConstants.USB_ENDPOINT_XFER_BULK }
      val inEndpoint = bulkEndpoints.firstOrNull { it.direction == UsbConstants.USB_DIR_IN }
        ?: throw IOException("Bulk IN endpoint not found on interface ${dataInterface.id}")
      val outEndpoint = bulkEndpoints.firstOrNull { it.direction == UsbConstants.USB_DIR_OUT }
        ?: throw IOException("Bulk OUT endpoint not found on interface ${dataInterface.id}")
      val connection = usbManager.openDevice(device)
        ?: throw IOException("Open ${device.deviceName} failed")
      if (!connection.claimInterface(dataInterface, true)) {
        connection.close()
        throw IOException("Claim interface ${dataInterface.id} on ${device.deviceName} failed")
      }
      return AdapterLink(device, connection, dataInterface, inEndpoint, outEndpoint)
    }
  }
}
