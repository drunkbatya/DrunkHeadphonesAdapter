package com.drunkbatya.drunkheadphonesadapter

import adapter.ErrType
import adapter.RpcRequest
import adapter.UpdateBeginRequest
import adapter.UpdateFinishRequest
import adapter.UpdateWriteRequest
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
import okio.ByteString.Companion.toByteString

private const val TAG = "Adapter"
private const val CONNECT_TIMEOUT_MS = 1000L
private const val REQUEST_TIMEOUT_MS = 1000L
private const val REBOOT_TIMEOUT_MS = 10000L
private const val REBOOT_POLL_MS = 100L
private const val UPDATE_CHUNK_SIZE = 256

enum class ConnectionState(@StringRes val label: Int) {
  Disconnected(R.string.connection_disconnected),
  Connecting(R.string.connection_connecting),
  Connected(R.string.connection_connected),
}

data class AdapterState(
  val connection: ConnectionState = ConnectionState.Disconnected,
  val version: VersionResponse? = null,
)

sealed interface UpdateState {
  data class Confirm(val image: FirmwareImage) : UpdateState
  data class Writing(val progress: Float) : UpdateState
  data object Rebooting : UpdateState
  data class Done(val version: String) : UpdateState
  data class Failed(@StringRes val title: Int, val reason: String) : UpdateState
}

class AdapterViewModel(application: Application) : AndroidViewModel(application) {
  private val usbManager = application.getSystemService(UsbManager::class.java)
  private val mutableState = MutableStateFlow(AdapterState())
  val state: StateFlow<AdapterState> = mutableState
  private val mutableUpdateState = MutableStateFlow<UpdateState?>(null)
  val updateState: StateFlow<UpdateState?> = mutableUpdateState
  private var link: AdapterLink? = null

  private val detachReceiver = object : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
      val device = intent.getParcelableExtra(UsbManager.EXTRA_DEVICE, UsbDevice::class.java)
      if (device == link?.device) disconnect()
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
    viewModelScope.launch { connectAdapter() }
  }

  fun disconnect() {
    link?.close()
    link = null
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
        val adapterTarget = mutableState.value.version?.target
        require(image.target == adapterTarget) { "Firmware target ${image.target}, adapter target $adapterTarget" }
        UpdateState.Confirm(image)
      } catch (e: Exception) {
        UpdateState.Failed(R.string.update_file_invalid, e.message.orEmpty())
      }
    }
  }

  fun dismissUpdate() {
    val update = mutableUpdateState.value
    if (update is UpdateState.Writing || update is UpdateState.Rebooting) return
    mutableUpdateState.value = null
  }

  fun startUpdate(image: FirmwareImage) {
    val updateLink = link ?: return
    mutableUpdateState.value = UpdateState.Writing(0f)
    viewModelScope.launch {
      try {
        writeFirmware(updateLink, image)
        mutableUpdateState.value = UpdateState.Rebooting
        val version = awaitRebootedAdapter(updateLink.device)
        check(version == image.version) { "Adapter reports version $version, expected ${image.version}" }
        mutableUpdateState.value = UpdateState.Done(version)
      } catch (e: Exception) {
        Log.e(TAG, "Update ${updateLink.device.deviceName} failed: ${e.message}")
        disconnect()
        mutableUpdateState.value = UpdateState.Failed(R.string.update_failed, e.message.orEmpty())
      }
    }
  }

  private suspend fun connectAdapter(): VersionResponse? {
    mutableState.value = AdapterState(ConnectionState.Connecting)
    val device = findAdapterDevice(usbManager)
    if (device == null || !requestUsbPermission(getApplication(), usbManager, device)) {
      mutableState.value = AdapterState()
      return null
    }
    return try {
      withTimeout(CONNECT_TIMEOUT_MS) {
        val openedLink = AdapterLink.open(usbManager, device)
        link = openedLink
        val version = openedLink.request(RpcRequest(version = VersionRequest())).version
          ?: error("Version request to ${device.deviceName}: response without version")
        mutableState.value = AdapterState(ConnectionState.Connected, version)
        version
      }
    } catch (e: Exception) {
      Log.e(TAG, "Connect ${device.deviceName} failed: ${e.message}")
      disconnect()
      null
    }
  }

  private suspend fun writeFirmware(updateLink: AdapterLink, image: FirmwareImage) {
    val size = image.bytes.size
    updateLink.requestStatus("Begin", RpcRequest(update_begin = UpdateBeginRequest(size, image.crc32)))
    for (offset in 0 until size step UPDATE_CHUNK_SIZE) {
      val data = image.bytes.toByteString(offset, minOf(UPDATE_CHUNK_SIZE, size - offset))
      updateLink.requestStatus("Write at $offset", RpcRequest(update_write = UpdateWriteRequest(offset, data)))
      mutableUpdateState.value = UpdateState.Writing((offset + data.size).toFloat() / size)
    }
    updateLink.requestStatus("Finish", RpcRequest(update_finish = UpdateFinishRequest()))
  }

  private suspend fun awaitRebootedAdapter(oldDevice: UsbDevice): String {
    try {
      withTimeout(REBOOT_TIMEOUT_MS) {
        while (usbManager.deviceList.values.contains(oldDevice)) delay(REBOOT_POLL_MS)
        disconnect()
        while (findAdapterDevice(usbManager) == null) delay(REBOOT_POLL_MS)
      }
    } catch (e: TimeoutCancellationException) {
      throw IOException("Adapter did not reconnect in $REBOOT_TIMEOUT_MS ms")
    }
    val version = connectAdapter() ?: throw IOException("Connect after reboot failed")
    return version.version
  }

  private suspend fun AdapterLink.requestStatus(step: String, request: RpcRequest) {
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
    link?.close()
  }
}
