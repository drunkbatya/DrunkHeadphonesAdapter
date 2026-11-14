package com.drunkbatya.drunkheadphonesadapter

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
import android.util.Log
import androidx.annotation.StringRes
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withTimeout

private const val TAG = "Adapter"
private const val CONNECT_TIMEOUT_MS = 1000L

enum class ConnectionState(@StringRes val label: Int) {
  Disconnected(R.string.connection_disconnected),
  Connecting(R.string.connection_connecting),
  Connected(R.string.connection_connected),
}

data class AdapterState(
  val connection: ConnectionState = ConnectionState.Disconnected,
  val version: VersionResponse? = null,
)

class AdapterViewModel(application: Application) : AndroidViewModel(application) {
  private val usbManager = application.getSystemService(UsbManager::class.java)
  private val mutableState = MutableStateFlow(AdapterState())
  val state: StateFlow<AdapterState> = mutableState
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
    mutableState.value = AdapterState(ConnectionState.Connecting)
    viewModelScope.launch {
      val device = findAdapterDevice(usbManager)
      if (device == null || !requestUsbPermission(getApplication(), usbManager, device)) {
        mutableState.value = AdapterState()
        return@launch
      }
      try {
        withTimeout(CONNECT_TIMEOUT_MS) {
          val openedLink = AdapterLink.open(usbManager, device)
          link = openedLink
          val version = openedLink.request(RpcRequest(version = VersionRequest())).version
            ?: error("Version request to ${device.deviceName}: response without version")
          mutableState.value = AdapterState(ConnectionState.Connected, version)
        }
      } catch (e: Exception) {
        Log.e(TAG, "Connect ${device.deviceName} failed: ${e.message}")
        disconnect()
      }
    }
  }

  fun disconnect() {
    link?.close()
    link = null
    mutableState.value = AdapterState()
  }

  override fun onCleared() {
    getApplication<Application>().unregisterReceiver(detachReceiver)
    link?.close()
  }
}
