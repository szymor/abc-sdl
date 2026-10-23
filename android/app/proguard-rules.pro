# The native code uses JNI entry points in pl.szymor.abc.PlatformBridge.
-keep class pl.szymor.abc.PlatformBridge { *; }
-keep class org.libsdl.app.** { *; }
