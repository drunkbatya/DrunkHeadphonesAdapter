package com.drunkbatya.drunkheadphonesadapter

import adapter.VersionResponse
import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Button
import androidx.compose.material3.FilledTonalIconButton
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp

@Composable
fun AppScreen(
  state: AdapterState,
  update: UpdateState?,
  onConnect: () -> Unit,
  onDisconnect: () -> Unit,
  onFirmwareFile: (Uri) -> Unit,
  onConfirmUpdate: (FirmwareImage) -> Unit,
  onDismissUpdate: () -> Unit,
) {
  UpdateDialogs(update, state.firmware?.version, onConfirmUpdate, onDismissUpdate)
  var isDeviceInfoOpen by rememberSaveable { mutableStateOf(false) }
  if (isDeviceInfoOpen) {
    DeviceInfoScreen(state.firmware, onBack = { isDeviceInfoOpen = false })
    return
  }
  Scaffold(
    bottomBar = {
      NavigationBar {
        NavigationBarItem(
          selected = true,
          onClick = {},
          icon = { Icon(painterResource(R.drawable.ic_chip), contentDescription = null) },
          label = { Text(stringResource(state.connection.label)) },
        )
      }
    },
  ) { padding ->
    BoardTab(
      state = state,
      onConnect = onConnect,
      onDisconnect = onDisconnect,
      onDeviceInfo = { isDeviceInfoOpen = true },
      onFirmwareFile = onFirmwareFile,
      modifier = Modifier.padding(padding),
    )
  }
}

@Composable
private fun BoardTab(
  state: AdapterState,
  onConnect: () -> Unit,
  onDisconnect: () -> Unit,
  onDeviceInfo: () -> Unit,
  onFirmwareFile: (Uri) -> Unit,
  modifier: Modifier = Modifier,
) {
  Column(
    modifier = modifier.fillMaxSize().padding(horizontal = 16.dp),
    verticalArrangement = Arrangement.spacedBy(12.dp),
  ) {
    Box(
      modifier = Modifier.fillMaxWidth().fillMaxHeight(0.5f).padding(top = 16.dp),
      contentAlignment = Alignment.Center,
    ) {
      FlippableBoard(Modifier.fillMaxHeight().aspectRatio(1f, matchHeightConstraintsFirst = true))
    }
    ConnectionButton(state.connection, onConnect, onDisconnect)
    DeviceInfoCard(
      firmware = state.firmware,
      isUpdateAvailable = state.connection == ConnectionState.Connected ||
        state.connection == ConnectionState.Bootloader,
      onClick = onDeviceInfo,
      onFirmwareFile = onFirmwareFile,
    )
  }
}

@Composable
private fun ConnectionButton(connection: ConnectionState, onConnect: () -> Unit, onDisconnect: () -> Unit) {
  val modifier = Modifier.fillMaxWidth()
  when (connection) {
    ConnectionState.Connected, ConnectionState.Bootloader -> OutlinedButton(onClick = onDisconnect, modifier = modifier) {
      Text(stringResource(R.string.disconnect))
    }
    ConnectionState.Connecting -> Button(onClick = {}, enabled = false, modifier = modifier) {
      Text(stringResource(R.string.connection_connecting))
    }
    ConnectionState.Disconnected -> Button(onClick = onConnect, modifier = modifier) {
      Text(stringResource(R.string.connect))
    }
  }
}

@Composable
private fun DeviceInfoCard(
  firmware: VersionResponse?,
  isUpdateAvailable: Boolean,
  onClick: () -> Unit,
  onFirmwareFile: (Uri) -> Unit,
) {
  val firmwareFilePicker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
    if (uri != null) onFirmwareFile(uri)
  }
  Surface(
    onClick = onClick,
    shape = RoundedCornerShape(16.dp),
    color = MaterialTheme.colorScheme.surfaceContainer,
    modifier = Modifier.fillMaxWidth(),
  ) {
    Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
      Row(verticalAlignment = Alignment.CenterVertically) {
        Text(
          stringResource(R.string.device_info),
          style = MaterialTheme.typography.titleMedium,
          modifier = Modifier.weight(1f),
        )
        Icon(painterResource(R.drawable.ic_chevron_right), contentDescription = null)
      }
      InfoRow(stringResource(R.string.firmware_version), firmware?.version)
      InfoRow(stringResource(R.string.build_date), firmware?.build_date)
      Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        Button(onClick = {}, enabled = false, modifier = Modifier.weight(1f)) {
          Text(stringResource(R.string.update))
        }
        FilledTonalIconButton(onClick = { firmwareFilePicker.launch(arrayOf("*/*")) }, enabled = isUpdateAvailable) {
          Icon(painterResource(R.drawable.ic_file), contentDescription = stringResource(R.string.update_select_file))
        }
      }
    }
  }
}

@Composable
fun InfoRow(label: String, value: String?) {
  Row(Modifier.fillMaxWidth()) {
    Text(
      label,
      color = MaterialTheme.colorScheme.onSurfaceVariant,
      modifier = Modifier.weight(1f),
    )
    Text(value?.takeUnless { it.isBlank() } ?: stringResource(R.string.value_unknown))
  }
}
