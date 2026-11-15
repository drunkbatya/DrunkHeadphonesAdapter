package com.drunkbatya.drunkheadphonesadapter

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties

@Composable
fun UpdateDialogs(
  update: UpdateState?,
  currentVersion: String?,
  onConfirm: (FirmwareImage) -> Unit,
  onDismiss: () -> Unit,
) {
  when (update) {
    null -> Unit
    is UpdateState.Confirm -> AlertDialog(
      onDismissRequest = onDismiss,
      title = { Text(stringResource(R.string.update_confirm_title)) },
      text = {
        val unknown = stringResource(R.string.value_unknown)
        Text(stringResource(R.string.update_confirm_text, currentVersion ?: unknown, update.image.version))
      },
      confirmButton = { TextButton(onClick = { onConfirm(update.image) }) { Text(stringResource(R.string.yes)) } },
      dismissButton = { TextButton(onClick = onDismiss) { Text(stringResource(R.string.no)) } },
    )
    is UpdateState.Writing -> ProgressDialog(update.progress)
    UpdateState.Rebooting -> ProgressDialog(null)
    is UpdateState.Done -> MessageDialog(
      title = stringResource(R.string.update_done),
      text = stringResource(R.string.update_done_text, update.version),
      onDismiss = onDismiss,
    )
    is UpdateState.Failed -> MessageDialog(stringResource(update.title), update.reason, onDismiss)
  }
}

@Composable
private fun ProgressDialog(progress: Float?) {
  val view = LocalView.current
  DisposableEffect(view) {
    view.keepScreenOn = true
    onDispose { view.keepScreenOn = false }
  }
  Dialog(
    onDismissRequest = {},
    properties = DialogProperties(dismissOnBackPress = false, dismissOnClickOutside = false),
  ) {
    Surface(shape = RoundedCornerShape(28.dp), color = MaterialTheme.colorScheme.surfaceContainer) {
      Column(
        modifier = Modifier.padding(24.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(16.dp),
      ) {
        if (progress == null) {
          CircularProgressIndicator()
          Text(stringResource(R.string.update_rebooting), textAlign = TextAlign.Center)
        } else {
          LinearProgressIndicator(progress = { progress }, modifier = Modifier.fillMaxWidth())
          Text("${(progress * 100).toInt()} %")
        }
        Text(
          stringResource(R.string.update_writing),
          color = MaterialTheme.colorScheme.onSurfaceVariant,
          textAlign = TextAlign.Center,
        )
      }
    }
  }
}

@Composable
private fun MessageDialog(title: String, text: String, onDismiss: () -> Unit) {
  AlertDialog(
    onDismissRequest = onDismiss,
    title = { Text(title) },
    text = { Text(text) },
    confirmButton = { TextButton(onClick = onDismiss) { Text(stringResource(R.string.ok)) } },
  )
}
