package com.drunkbatya.drunkheadphonesadapter

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.viewModels
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue

class MainActivity : ComponentActivity() {
  private val adapter: AdapterViewModel by viewModels()

  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)
    enableEdgeToEdge()
    setContent {
      val state by adapter.state.collectAsState()
      AppTheme { AppScreen(state, onConnect = adapter::connect, onDisconnect = adapter::disconnect) }
    }
  }
}
