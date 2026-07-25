package com.nexus.manager.ui.components

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.height
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.outlined.Security
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import com.nexus.manager.data.model.SuPolicy
import com.nexus.manager.viewmodel.NexusViewModelFactory
import com.nexus.manager.viewmodel.SuRequestViewModel

/**
 * 全局 Su 请求对话框
 *
 * 在 NexusRoot 顶层挂载一次即可：当 Daemon 推送 [com.nexus.manager.ipc.proto.SuRequestEvent]
 * 时，弹出授权对话框供用户允许 / 仅一次 / 拒绝。
 *
 * 一次只展示一个请求；队列中的后续请求在用户处理后立即弹出下一个。
 */
@Composable
fun SuRequestDialog(
    viewModel: SuRequestViewModel = viewModel(factory = NexusViewModelFactory()),
) {
    val pending by viewModel.pending.collectAsStateWithLifecycle()
    val current = pending.firstOrNull()

    CollectMessages(viewModel.messages)

    if (current != null) {
        AlertDialog(
            onDismissRequest = viewModel::dismiss,
            icon = {
                Icon(
                    Icons.Outlined.Security,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.primary,
                )
            },
            title = {
                Text(
                    "Root 授权请求",
                    fontWeight = FontWeight.SemiBold,
                )
            },
            text = {
                Column {
                    Text(
                        "应用 ${current.packageName} 请求超级用户权限。",
                        style = MaterialTheme.typography.bodyMedium,
                    )
                    Spacer(Modifier.height(4.dp))
                    Text(
                        "uid=${current.uid} · pid=${current.pid}",
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                    if (current.command.isNotEmpty()) {
                        Spacer(Modifier.height(8.dp))
                        Text(
                            "命令：${current.command}",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                    }
                }
            },
            confirmButton = {
                TextButton(onClick = { viewModel.respond(SuPolicy.ALLOW_ONCE) }) {
                    Text("仅一次", color = MaterialTheme.colorScheme.primary)
                }
            },
            dismissButton = {
                Column {
                    TextButton(onClick = { viewModel.respond(SuPolicy.ALLOW) }) {
                        Text("永久允许", color = MaterialTheme.colorScheme.tertiary)
                    }
                    androidx.compose.foundation.layout.Row {
                        TextButton(onClick = { viewModel.respond(SuPolicy.DENY) }) {
                            Text("拒绝", color = MaterialTheme.colorScheme.error)
                        }
                        TextButton(onClick = viewModel::dismiss) {
                            Text("稍后")
                        }
                    }
                }
            },
        )
    }
}
