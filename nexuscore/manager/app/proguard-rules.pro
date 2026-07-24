# Protobuf 生成类
-keep class com.nexus.manager.ipc.proto.** { *; }
-keepclassmembers class com.nexus.manager.ipc.proto.** { *; }

# Coroutines
-keepclassmembers class kotlinx.coroutines.** { *; }
-dontwarn kotlinx.coroutines.**

# Compose
-dontwarn androidx.compose.**

# keep Application class
-keep class com.nexus.manager.NexusApp { *; }
