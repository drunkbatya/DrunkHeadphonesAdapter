package com.example.drunkheadphonesadapter

import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Image
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.unit.dp

@Composable
fun FlippableBoard(
    modifier: Modifier = Modifier
) {
    var isFlipped by remember { mutableStateOf(false) }

    val rotation by animateFloatAsState(
        targetValue = if (isFlipped) 180f else 0f,
        animationSpec = tween(durationMillis = 600),
        label = "boardRotation"
    )

    val isFrontVisible = rotation <= 90f

    Image(
        painter = painterResource(
            id = if (isFrontVisible) {
                R.drawable.board_front
            } else {
                R.drawable.board_back
            }
        ),
        contentDescription = "PCB board",
        contentScale = ContentScale.Fit,
        modifier = modifier
            .fillMaxWidth()
            .height(250.dp)
            .graphicsLayer {
                rotationY = if (isFrontVisible) {
                    rotation
                } else {
                    rotation - 180f
                }
                cameraDistance = 12f * density
            }
            .clickable {
                isFlipped = !isFlipped
            }
    )
}