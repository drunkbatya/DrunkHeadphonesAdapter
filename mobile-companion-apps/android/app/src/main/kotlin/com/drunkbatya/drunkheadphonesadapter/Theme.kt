package com.drunkbatya.drunkheadphonesadapter

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

private val background = Color(0xFF101214)

private val colorScheme = darkColorScheme(
  primary = Color(0xFF87CFF9),
  background = background,
  surface = background,
  surfaceContainer = Color(0xFF1A1D20),
)

@Composable
fun AppTheme(content: @Composable () -> Unit) {
  MaterialTheme(colorScheme = colorScheme, content = content)
}
