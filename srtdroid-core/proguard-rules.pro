# Add project specific ProGuard rules here.
# You can control the set of applied configuration files using the
# proguardFiles setting in build.gradle.
#
# For more details, see
#   http://developer.android.com/guide/developing/tools/proguard.html

# If your project uses WebView with JS, uncomment the following
# and specify the fully qualified class name to the JavaScript interface
# class:
#-keepclassmembers class fqcn.of.javascript.interface.for.webview {
#   public *;
#}

# Uncomment this to preserve the line number information for
# debugging stack traces.
#-keepattributes SourceFile,LineNumberTable

# If you keep the line number information, uncomment this to
# hide the original source file name.
#-renamesourcefileattribute SourceFile

# srtdroid のモデルクラスとそのフィールド名をリネーム（難読化）から保護する
# srtdroid のC++層から文字列リフレクションをかけるモデルを、難読化・剥離から完全保護する
-keep class io.github.thibaultbee.srtdroid.models.Pair { *; }
-keep class io.github.thibaultbee.srtdroid.models.MsgCtrl { *; }
-keepclassmembers class io.github.thibaultbee.srtdroid.models.MsgCtrl {
    int flags;
    int ttl;
    boolean inorder;
    int pktSeq;
    int msgNumber;
}

# キャッシュのフォールバック動作を安定化させるための基本プリミティブの保持
-keep class java.lang.Integer { public static java.lang.Integer valueOf(int); }
-keep class java.lang.Long { public static java.lang.Long valueOf(long); }
-keep class java.lang.Boolean { public static java.lang.Boolean valueOf(boolean); }
-keep class java.util.ArrayList { public <init>(int); public boolean add(java.lang.Object); }
-keep class java.net.InetSocketAddress { *; }
