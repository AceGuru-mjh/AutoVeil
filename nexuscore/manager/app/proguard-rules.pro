# Protobuf 生成类（整改 B20：补充 protobuf-javalite 内部类保留）
-keep class com.nexus.manager.ipc.proto.** { *; }
-keepclassmembers class com.nexus.manager.ipc.proto.** { *; }
-keep class com.google.protobuf.** { *; }
-keepclassmembers class com.google.protobuf.** { *; }
-dontwarn com.google.protobuf.**

# Coroutines
-keepclassmembers class kotlinx.coroutines.** { *; }
-dontwarn kotlinx.coroutines.**

# Compose
-dontwarn androidx.compose.**

# keep Application class
-keep class com.nexus.manager.NexusApp { *; }

# 整改 B20: BiometricPrompt 内部 callback 不能被混淆（reflection）
-keep class androidx.biometric.** { *; }
-keepclassmembers class androidx.biometric.** { *; }

# DataStore
-keep class androidx.datastore.** { *; }
-keepclassmembers class androidx.datastore.** { *; }

# ViewModel factory（反射创建）
-keep class com.nexus.manager.viewmodel.** { *; }
-keepclassmembers class com.nexus.manager.viewmodel.** { <init>(...); }
