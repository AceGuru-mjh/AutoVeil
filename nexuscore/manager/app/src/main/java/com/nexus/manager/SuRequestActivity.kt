package com.nexus.manager

import android.app.Activity
import android.content.Intent
import android.os.Bundle
import android.util.Log
import android.view.Gravity
import android.view.View
import android.view.WindowManager
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView
import androidx.lifecycle.lifecycleScope
import com.nexus.manager.data.model.SuPolicy
import kotlinx.coroutines.launch

/**
 * SuRequest 独立 Activity（Phase 6 新增）
 *
 * daemon 收到 su 请求时，通过 am start 唤起本 Activity：
 *   am start --user 0 -n com.nexus.manager/.SuRequestActivity
 *     --es package_name <pkg> --ei uid <uid> --ei pid <pid> --es command <cmd>
 *
 * 用户响应后调用 setSuPolicy 并 finish。
 *
 * 使用传统 View 而非 Compose，避免 Compose 依赖问题。
 */
class SuRequestActivity : Activity() {

    companion object {
        private const val TAG = "SuRequestActivity"
        const val EXTRA_PACKAGE_NAME = "package_name"
        const val EXTRA_UID = "uid"
        const val EXTRA_PID = "pid"
        const val EXTRA_COMMAND = "command"
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

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

        // 构建简单 UI（不依赖 Compose）
        val layout = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER
            setPadding(48, 48, 48, 48)
        }

        val title = TextView(this).apply {
            text = "Root 授权请求"
            textSize = 20f
            setPadding(0, 0, 0, 16)
            gravity = Gravity.CENTER
        }

        val message = TextView(this).apply {
            text = "应用 $packageName 请求超级用户权限。\nuid=$uid · pid=$pid"
            textSize = 14f
            setPadding(0, 0, 0, 8)
        }

        val commandText = TextView(this).apply {
            text = if (command.isNotEmpty()) "命令：$command" else ""
            textSize = 12f
            setPadding(0, 0, 0, 24)
        }

        val buttonLayout = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER
        }

        val allowButton = Button(this).apply { text = "永久允许" }
        val onceButton = Button(this).apply { text = "仅一次" }
        val denyButton = Button(this).apply { text = "拒绝" }
        val laterButton = Button(this).apply { text = "稍后" }

        allowButton.setOnClickListener { respond(policy = SuPolicy.ALLOW, timeoutSec = 0) }
        onceButton.setOnClickListener { respond(policy = SuPolicy.ALLOW_ONCE, timeoutSec = 300) }
        denyButton.setOnClickListener { respond(policy = SuPolicy.DENY, timeoutSec = 0) }
        laterButton.setOnClickListener { respond(policy = SuPolicy.DENY, timeoutSec = 60) }

        buttonLayout.apply {
            addView(allowButton)
            addView(onceButton)
            addView(denyButton)
            addView(laterButton)
        }

        layout.apply {
            addView(title)
            addView(message)
            addView(commandText)
            addView(buttonLayout)
        }

        setContentView(layout)

        // 确保弹窗在锁屏之上
        window?.apply {
            addFlags(
                WindowManager.LayoutParams.FLAG_SHOW_WHEN_LOCKED or
                    WindowManager.LayoutParams.FLAG_TURN_SCREEN_ON or
                    WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON
            )
        }
    }

    private fun respond(policy: SuPolicy, timeoutSec: Int) {
        val packageName = intent?.getStringExtra(EXTRA_PACKAGE_NAME) ?: return
        val uid = intent?.getIntExtra(EXTRA_UID, -1) ?: return

        lifecycleScope.launch {
            val app = application as? NexusApp
            app?.repository?.setSuPolicy(packageName, uid, policy, timeoutSec)
            finish()
        }
    }
}
