package com.nexus.manager

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.SystemBarStyle
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.fragment.app.FragmentActivity
import com.nexus.manager.ui.NexusRoot

/**
 * 整改 B3：原为 ComponentActivity，无法挂 BiometricPrompt 的 Fragment。
 * 改为 FragmentActivity 后，BiometricGate 可正常弹出指纹/面部认证对话框。
 *
 * 注意：FragmentActivity 是 ComponentActivity 的子类，原有 enableEdgeToEdge +
 * setContent 行为完全兼容。
 */
class MainActivity : FragmentActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        // edge-to-edge：状态栏/导航栏透明，让毛玻璃背景延伸到系统栏下方
        enableEdgeToEdge(
            statusBarStyle = SystemBarStyle.auto(android.graphics.Color.TRANSPARENT, android.graphics.Color.TRANSPARENT),
            navigationBarStyle = SystemBarStyle.auto(android.graphics.Color.TRANSPARENT, android.graphics.Color.TRANSPARENT),
        )
        super.onCreate(savedInstanceState)
        setContent {
            NexusRoot()
        }
    }
}
