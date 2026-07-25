package com.nexus.manager

import android.app.Activity
import android.content.Intent
import android.os.Bundle
import android.util.Log
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.outlined.Security
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.lifecycle.lifecycleScope
import com.nexus.manager.data.model.SuPolicy
import com.nexus.manager.ui.components.GlassSurface
import com.nexus.manager.ui.theme.NexusTheme
import kotlinx.coroutines.launch

/**
 * SuRequest 独立 Activity（Phase 6 新增）
 *
 * 解决原 SuRequestDialog 只能在 App 前台时弹出的限制：
 * - 原方案：SuRequestDialog 挂载在 NexusRoot Composable，App 后台/被杀时请求被静默吞
 * - 新方案：daemon 收到 su 请求时，通过 `am start -n com.nexus.manager/.SuRequestActivity
 *           --es package_name <pkg> --ei uid <uid> --ei pid <pid> --es command <cmd>` 唤起本 Activity
 * - 本 Activity 在 onCreate 解析 Intent extras，构造 SuRequestViewModel.Request
 * - 用户响应后调用 setSuPolicy 并 finish
 *
 * 注意：本 Activity 是 exported=true，但通过权限保护：
 * - 在 AndroidManifest 中声明 android:permission="nexus.permission.SU_REQUEST"
 * - daemon 在 am start 时通过 `--user 0` + 自定义 permission 保护
 * - 第三方 app 无法直接启动本 Activity
 *
 * 启动模式：singleTask，避免重复创建实例
 */
class SuRequestActivity : Activity() {

    companion object {
        private const val TAG = "SuRequestActivity"

        const val EXTRA_PACKAGE_NAME = "package_name"
        const val EXTRA_UID = "uid"
        const val EXTRA_PID = "pid"
        const val EXTRA_COMMAND = "command"

        // 自定义权限：保护 SuRequestActivity 只能被 daemon (root) 启动
        const val PERMISSION_SU_REQUEST = "nexus.permission.SU_REQUEST"
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()

        // 解析 Intent extras
        val packageName = intent?.getStringExtra(EXTRA_PACKAGE_NAME) ?: run {
            Log.e(TAG, "missing $EXTRA_PACKAGE_NAME extra")
            finish()
            return
        }
        val uid = intent?.getIntExtra(EXTRA_UID, -1) ?: -1
        val pid = intent?.getIntExtra(EXTRA_PID, -1) ?: -1
        val command = intent?.getStringExtra(EXTRA_COMMAND) ?: ""

        if (uid < 0 || pid < 0) {
            Log.e(TAG, "invalid uid=$uid or pid=$pid")
            finish()
            return
        }

        Log.i(TAG, "SuRequest: pkg=$packageName uid=$uid pid=$pid cmd=$command")

        setContent {
            NexusTheme(mode = com.nexus.manager.ui.theme.ThemeMode.SYSTEM) {
                SuRequestContent(
                    packageName = packageName,
                    uid = uid,
                    pid = pid,
                    command = command,
                    onRespond = { policy ->
                        // 调用 daemon 设置策略
                        lifecycleScope.launch {
                            val app = application as? NexusApp
                            app?.repository?.setSuPolicy(packageName, uid, policy, timeoutSecFor(policy))
                            finish()
                        }
                    },
                    onDismiss = {
                        // "稍后" 等价于 DENY + 60s 短超时
                        lifecycleScope.launch {
                            val app = application as? NexusApp
                            app?.repository?.setSuPolicy(packageName, uid, SuPolicy.DENY, 60)
                            finish()
                        }
                    },
                )
            }
        }
    }

    private fun timeoutSecFor(policy: SuPolicy): Int =
        if (policy == SuPolicy.ALLOW_ONCE) 300 else 0
}

@androidx.compose.runtime.Composable
private fun SuRequestContent(
    packageName: String,
    uid: Int,
    pid: Int,
    command: String,
    onRespond: (SuPolicy) -> Unit,
    onDismiss: () -> Unit,
) {
    Box(
        modifier = Modifier
            .fillMaxSize()
            .padding(24.dp),
        contentAlignment = Alignment.Center,
    ) {
        GlassSurface(
            modifier = Modifier.padding(16.dp),
            contentPadding = 24.dp,
        ) {
            Column(
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                Icon(
                    Icons.Outlined.Security,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.primary,
                    modifier = Modifier.padding(8.dp),
                )
                Text(
                    "Root 授权请求",
                    style = MaterialTheme.typography.headlineSmall,
                    fontWeight = FontWeight.SemiBold,
                )
                Spacer(Modifier.height(8.dp))
                Text(
                    "应用 $packageName 请求超级用户权限。",
                    style = MaterialTheme.typography.bodyMedium,
                )
                Text(
                    "uid=$uid · pid=$pid",
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                if (command.isNotEmpty()) {
                    Spacer(Modifier.height(8.dp))
                    Text(
                        "命令：$command",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
                Spacer(Modifier.height(24.dp))
                androidx.compose.foundation.layout.Row(
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    TextButton(onClick = { onRespond(SuPolicy.ALLOW) }) {
                        Text("永久允许", color = MaterialTheme.colorScheme.tertiary)
                    }
                    TextButton(onClick = { onRespond(SuPolicy.ALLOW_ONCE) }) {
                        Text("仅一次", color = MaterialTheme.colorScheme.primary)
                    }
                    TextButton(onClick = { onRespond(SuPolicy.DENY) }) {
                        Text("拒绝", color = MaterialTheme.colorScheme.error)
                    }
                    TextButton(onClick = onDismiss) {
                        Text("稍后", color = MaterialTheme.colorScheme.onSurfaceVariant)
                    }
                }
            }
        }
    }
}
