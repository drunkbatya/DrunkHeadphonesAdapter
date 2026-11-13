package com.drunkbatya.drunkheadphonesadapter

import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp

@Composable
fun FlippableBoard(modifier: Modifier = Modifier) {
  var isFlipped by rememberSaveable { mutableStateOf(false) }
  val rotation by animateFloatAsState(
    targetValue = if (isFlipped) 180f else 0f,
    animationSpec = tween(durationMillis = 500),
    label = "boardRotation",
  )
  val isFrontVisible = rotation <= 90f

  Surface(
    onClick = { isFlipped = !isFlipped },
    shape = RoundedCornerShape(16.dp),
    color = MaterialTheme.colorScheme.surfaceContainer,
    border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant),
    modifier = modifier.graphicsLayer {
      rotationY = if (isFrontVisible) rotation else rotation - 180f
      cameraDistance = 12f * density
    },
  ) {
    Image(
      painter = painterResource(if (isFrontVisible) R.drawable.board_front else R.drawable.board_back),
      contentDescription = stringResource(R.string.board),
      contentScale = ContentScale.Fit,
      modifier = Modifier.fillMaxSize().padding(12.dp),
    )
  }
}
