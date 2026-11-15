package com.drunkbatya.drunkheadphonesadapter

import adapter.ErrType
import adapter.RebootToBootloaderRequest
import adapter.RpcRequest
import adapter.VersionRequest
import adapter.VersionResponse
import android.app.Application
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbManager
import android.net.Uri
import android.util.Log
import androidx.annotation.StringRes
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import java.io.IOException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.TimeoutCancellationException
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeout

private const val TAG = "Adapter"
private const val CONNECT_TIMEOUT_MS = 1000L
private const val REQUEST_TIMEOUT_MS = 1000L
private const val REBOOT_TIMEOUT_MS = 10000L
private const val DEVICE_LIST_POLL_MS = 100L

enum class ConnectionState(@param:StringRes val label: Int) {
  Disconnected(R.string.connection_disconnected),
  Connecting(R.string.connection_connecting),
  Connected(R.string.connection_connected),
  Bootloader(R.string.connection_bootloader),
}

data class AdapterState(
  val connection: ConnectionState = ConnectionState.Disconnected,
  val firmware: VersionResponse? = null,
)

sealed interface UpdateState {
  data class Confirm(val image: FirmwareImage) : UpdateState
  data class Writing(val progress: Float) : UpdateState
  data object Rebooting : UpdateState
  data class Done(val version: String) : UpdateState
  data class Failed(@param:StringRes val title: Int, val reason: String) : UpdateState
}

class AdapterViewModel(application: Application) : AndroidViewModel(application) {
  private val usbManager = application.getSystemService(UsbManager::class.java)
  private val mutableState = MutableStateFlow(AdapterState())
  val state: StateFlow<AdapterState> = mutableState
  private val mutableUpdateState = MutableStateFlow<UpdateState?>(null)
  val updateState: StateFlow<UpdateState?> = mutableUpdateState
  private var adapterLink: AdapterLink? = null
  private var bootloaderDevice: UsbDevice? = null

  private val detachReceiver = object : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
      val device = intent.getParcelableExtra(UsbManager.EXTRA_DEVICE, UsbDevice::class.java)
      if (device == adapterLink?.device || device == bootloaderDevice) disconnect()
    }
  }

  init {
    application.registerReceiver(
      detachReceiver,
      IntentFilter(UsbManager.ACTION_USB_DEVICE_DETACHED),
      Context.RECEIVER_NOT_EXPORTED,
    )
    connect()
  }

  fun connect() {
    if (mutableState.value.connection != ConnectionState.Disconnected) return
    viewModelScope.launch {
      val adapterDevice = findAdapterDevice(usbManager)
      val bootloader = findBootloaderDevice(usbManager)
      when {
        adapterDevice != null -> connectAdapter(adapterDevice)
        bootloader != null -> connectBootloader(bootloader)
      }
    }
  }

  fun disconnect() {
    adapterLink?.close()
    adapterLink = null
    bootloaderDevice = null
    mutableState.value = AdapterState()
  }

  fun selectFirmwareFile(uri: Uri) {
    viewModelScope.launch {
      mutableUpdateState.value = try {
        val bytes = withContext(Dispatchers.IO) {
          getApplication<Application>().contentResolver.openInputStream(uri)?.use { it.readBytes() }
            ?: throw IOException("Open $uri failed")
        }
        val image = parseFirmwareImage(bytes)
        val adapterTarget = mutableState.value.firmware?.target
        if (adapterTarget != null) {
          require(image.target == adapterTarget) { "Firmware target ${image.target}, adapter target $adapterTarget" }
        }
        UpdateState.Confirm(image)
      } catch (e: Exception) {
        UpdateState.Failed(R.string.update_file_invalid, e.message.orEmpty())
      }
    }
  }

  fun dismissUpdate() {
    mutableUpdateState.value = null
  }

  fun startUpdate(image: FirmwareImage) {
    val connectedAdapterLink = adapterLink
    val connectedBootloader = bootloaderDevice
    mutableUpdateState.value = UpdateState.Rebooting
    viewModelScope.launch {
      try {
        val dfuDevice = connectedBootloader
          ?: rebootToBootloader(checkNotNull(connectedAdapterLink) { "Adapter not connected" })
        mutableUpdateState.value = UpdateState.Writing(0f)
        writeFirmwareOverDfu(dfuDevice, image)
        mutableUpdateState.value = UpdateState.Rebooting
        val version = awaitRestartedAdapter(dfuDevice)
        check(version == image.version) { "Adapter reports version $version, expected ${image.version}" }
        mutableUpdateState.value = UpdateState.Done(version)
      } catch (e: Exception) {
        Log.e(TAG, "Update failed: ${e.message}")
        disconnect()
        mutableUpdateState.value = UpdateState.Failed(R.string.update_failed, e.message.orEmpty())
      }
    }
  }

  private suspend fun connectAdapter(device: UsbDevice): VersionResponse? {
    mutableState.value = AdapterState(ConnectionState.Connecting)
    if (!requestUsbPermission(getApplication(), usbManager, device)) {
      mutableState.value = AdapterState()
      return null
    }
    return try {
      withTimeout(CONNECT_TIMEOUT_MS) {
        val link = AdapterLink.open(usbManager, device)
        adapterLink = link
        val firmware = link.request(RpcRequest(version = VersionRequest())).version
          ?: error("Version request to ${device.deviceName}: response without version")
        mutableState.value = AdapterState(ConnectionState.Connected, firmware)
        firmware
      }
    } catch (e: Exception) {
      Log.e(TAG, "Connect ${device.deviceName} failed: ${e.message}")
      disconnect()
      null
    }
  }

  private suspend fun connectBootloader(device: UsbDevice) {
    mutableState.value = AdapterState(ConnectionState.Connecting)
    if (!requestUsbPermission(getApplication(), usbManager, device)) {
      mutableState.value = AdapterState()
      return
    }
    bootloaderDevice = device
    mutableState.value = AdapterState(ConnectionState.Bootloader)
  }

  private suspend fun rebootToBootloader(link: AdapterLink): UsbDevice {
    link.requestAndCheckOk("Reboot to bootloader", RpcRequest(reboot_to_bootloader = RebootToBootloaderRequest()))
    awaitDetached(link.device)
    disconnect()
    val device = awaitAttached("Bootloader") { findBootloaderDevice(usbManager) }
    if (!requestUsbPermission(getApplication(), usbManager, device)) {
      throw IOException("USB permission for ${device.deviceName} denied")
    }
    return device
  }

  private suspend fun writeFirmwareOverDfu(device: UsbDevice, image: FirmwareImage) = withContext(Dispatchers.IO) {
    DfuDevice.open(usbManager, device).use { dfu ->
      dfu.writeFirmware(image.bytes) { mutableUpdateState.value = UpdateState.Writing(it) }
      val writtenOptr = try {
        dfu.writeRequiredOptionBytes()
      } catch (e: IOException) {
        Log.e(TAG, "Option bytes on ${device.deviceName} failed: ${e.message}")
        null
      }
      if (writtenOptr == null) {
        dfu.startFirmware()
      } else {
        Log.i(TAG, "Option bytes OPTR 0x%08X written".format(writtenOptr))
      }
    }
  }

  private suspend fun awaitRestartedAdapter(dfuDevice: UsbDevice): String {
    awaitDetached(dfuDevice)
    disconnect()
    val device = awaitAttached("Adapter") { findAdapterDevice(usbManager) }
    val firmware = connectAdapter(device) ?: throw IOException("Connect ${device.deviceName} after reboot failed")
    return firmware.version
  }

  private suspend fun awaitDetached(device: UsbDevice) {
    try {
      withTimeout(REBOOT_TIMEOUT_MS) {
        while (usbManager.deviceList.values.contains(device)) delay(DEVICE_LIST_POLL_MS)
      }
    } catch (e: TimeoutCancellationException) {
      throw IOException("${device.deviceName} still attached after $REBOOT_TIMEOUT_MS ms")
    }
  }

  private suspend fun awaitAttached(name: String, find: () -> UsbDevice?): UsbDevice {
    try {
      return withTimeout(REBOOT_TIMEOUT_MS) {
        var device = find()
        while (device == null) {
          delay(DEVICE_LIST_POLL_MS)
          device = find()
        }
        device
      }
    } catch (e: TimeoutCancellationException) {
      throw IOException("$name not attached in $REBOOT_TIMEOUT_MS ms")
    }
  }

  private suspend fun AdapterLink.requestAndCheckOk(step: String, request: RpcRequest) {
    val response = try {
      withTimeout(REQUEST_TIMEOUT_MS) { request(request) }
    } catch (e: TimeoutCancellationException) {
      throw IOException("$step: no response in $REQUEST_TIMEOUT_MS ms")
    }
    val errType = response.generic?.err_type
    if (errType != ErrType.ERROR_TYPE_OK) throw IOException("$step: $errType")
  }

  override fun onCleared() {
    getApplication<Application>().unregisterReceiver(detachReceiver)
    adapterLink?.close()
  }
}
