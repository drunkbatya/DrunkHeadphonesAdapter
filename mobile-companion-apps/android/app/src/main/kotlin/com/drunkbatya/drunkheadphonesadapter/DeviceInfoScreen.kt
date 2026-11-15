package com.drunkbatya.drunkheadphonesadapter

import adapter.VersionResponse
import androidx.activity.compose.BackHandler
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun DeviceInfoScreen(firmware: VersionResponse?, onBack: () -> Unit) {
  BackHandler(onBack = onBack)
  Scaffold(
    topBar = {
      TopAppBar(
        title = { Text(stringResource(R.string.device_info)) },
        navigationIcon = {
          IconButton(onClick = onBack) {
            Icon(painterResource(R.drawable.ic_arrow_back), contentDescription = stringResource(R.string.back))
          }
        },
      )
    },
  ) { padding ->
    Column(
      modifier = Modifier.padding(padding).padding(16.dp),
      verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
      InfoRow(stringResource(R.string.firmware_version), firmware?.version)
      InfoRow(stringResource(R.string.build_date), firmware?.build_date)
      InfoRow(stringResource(R.string.git_commit), firmware?.git_commit)
      InfoRow(stringResource(R.string.git_branch), firmware?.git_branch)
      InfoRow(
        stringResource(R.string.dirty_build),
        firmware?.let { stringResource(if (it.dirty) R.string.yes else R.string.no) },
      )
      InfoRow(stringResource(R.string.target), firmware?.target?.toString())
    }
  }
}
