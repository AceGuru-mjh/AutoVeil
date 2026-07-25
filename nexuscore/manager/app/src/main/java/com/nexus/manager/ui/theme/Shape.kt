package com.nexus.manager.ui.theme

import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Shapes
import androidx.compose.ui.unit.dp

// M3 形状令牌：使用较大圆角，呼应毛玻璃柔和质感
val NexusShapes = Shapes(
    extraSmall = RoundedCornerShape(4.dp),
    small = RoundedCornerShape(8.dp),
    medium = RoundedCornerShape(16.dp),
    large = RoundedCornerShape(24.dp),
    extraLarge = RoundedCornerShape(32.dp),
)

// 玻璃卡片专用：稍大圆角，搭配 blur 视觉更柔和
val GlassCardShape = RoundedCornerShape(24.dp)
val GlassSheetShape = RoundedCornerShape(topStart = 28.dp, topEnd = 28.dp)
val GlassChipShape = RoundedCornerShape(12.dp)
