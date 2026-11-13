package com.drunkbatya.drunkheadphonesadapter

import androidx.annotation.StringRes
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Icon
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp

enum class ConnectionState(@StringRes val label: Int) {
  Disconnected(R.string.connection_disconnected),
  Connecting(R.string.connection_connecting),
  Connected(R.string.connection_connected),
}

@Composable
fun AppScreen(connectionState: ConnectionState) {
  Scaffold(
    bottomBar = {
      NavigationBar {
        NavigationBarItem(
          selected = true,
          onClick = {},
          icon = { Icon(painterResource(R.drawable.ic_chip), contentDescription = null) },
          label = { Text(stringResource(connectionState.label)) },
        )
      }
    },
  ) { padding ->
    BoardTab(Modifier.padding(padding))
  }
}

@Composable
private fun BoardTab(modifier: Modifier = Modifier) {
  Column(modifier.fillMaxSize(), horizontalAlignment = Alignment.CenterHorizontally) {
    FlippableBoard(
      Modifier
        .fillMaxHeight(0.3f)
        .padding(top = 24.dp)
        .aspectRatio(1f, matchHeightConstraintsFirst = true)
    )
  }
}
