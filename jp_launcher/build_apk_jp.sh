#!/bin/sh
# JP100 APK: android/ (GameActivity, controls, jni) + jp_launcher/ (MainActivity, manifest) + build/android/libme1game.so
set -e
R=${JP_ROOT:-$(cd "$(dirname "$0")/.." && pwd -P)}; A=$R/android_jp; J=$R/jp_launcher
SDK=${ANDROID_SDK:-/data/tools/android-sdk}; BT=${ANDROID_BT:-$SDK/build-tools/30.0.3}; AJAR=${ANDROID_JAR:-$SDK/platforms/android-29/android.jar}
NDK=${NDK:-/data/tools/android-ndk-r27c}; TC=$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin; CC=$TC/aarch64-linux-android29-clang
JAVA_HOME=${JAVA_HOME:-/data/sdk/jdk17}; export JAVA_HOME; PATH=$JAVA_HOME/bin:$PATH; export PATH
rm -rf $A; mkdir -p $A
cp -r $R/android/src $R/android/res $R/android/assets $R/android/jni $R/android/keystore $A/
cp $J/MainActivity.java $A/src/com/me1port/launcher/MainActivity.java
cp $J/AndroidManifest.xml $A/AndroidManifest.xml
if command -v magick >/dev/null 2>&1; then
  for d in mdpi:48 hdpi:72 xhdpi:96 xxhdpi:144 xxxhdpi:192; do
    n=${d%%:*}; s=${d##*:}; mkdir -p $A/res/mipmap-$n
    magick -size 192x192 xc:none -fill '#1f3d1a' -draw 'circle 96,96 96,6' -fill '#f2c230' -draw 'circle 96,96 96,20' \
      -fill '#1f3d1a' -font Liberation-Sans-Bold -pointsize 80 -gravity center -annotate +0+3 'JP' -resize ${s}x${s} \
      $A/res/mipmap-$n/ic_launcher.png || true
  done
  if [ -f $A/assets/loading.jpg ]; then
    magick -size 1280x720 xc:'#0b0f0a' -fill '#f2c230' -font Liberation-Sans-Bold -pointsize 64 -gravity center \
      -annotate +0-30 'Jurassic Park: The Game' -fill '#9fb39a' -pointsize 32 -annotate +0+50 'Episode 1' \
      -quality 88 $A/assets/loading.jpg || true
  fi
fi
cd $A
B=build; rm -rf $B; mkdir -p $B/classes $B/lib/arm64-v8a $B/assets/jpimg/fmodex $B/assets/jpimg/bink out
echo "[apk] native libs"
$CC -O2 -shared -fPIC -fvisibility=hidden -Wl,-soname,libme1vkshim.so -Wl,-z,max-page-size=16384 -s -o $B/lib/arm64-v8a/libme1vkshim.so jni/vkshim.c
$TC/llvm-strip -o $B/lib/arm64-v8a/libme1game.so $R/build/android/libme1game.so
$TC/llvm-strip -o $B/lib/arm64-v8a/libme1run.so $R/build/android/libme1run.so
# v15 (из me1port android/build_apk.sh): libme1spv.so — GLSL -> SPIR-V для своего рендерера D3D9->Vulkan (hle_d3d9_vk.h, dlopen)
if [ -z "${NO_SPV:-}" ] && [ -f $R/build/android/libme1spv.so ]; then cp $R/build/android/libme1spv.so $B/lib/arm64-v8a/; echo "[apk] + libme1spv.so (Vulkan-рендерер)"
else echo "[apk] без libme1spv.so: Vulkan-рендерер не запустится, игра уйдёт на OpenGL ES"; fi
# v15: необязательные GPU-библиотеки (android/gpu/build_gpu.sh -> build/android/gpu): Mesa Zink + libadrenotools и hook-библиотеки
GPU=$R/build/android/gpu
if [ -z "${NO_GPU:-}" ] && ls $GPU/*.so >/dev/null 2>&1; then echo "[apk] + GPU: $(cd $GPU && ls *.so | tr '\n' ' ')"; cp $GPU/*.so $B/lib/arm64-v8a/; fi
echo "[apk] javac + d8"
javac -encoding UTF-8 -source 8 -target 8 -bootclasspath $AJAR:$BT/core-lambda-stubs.jar -Xlint:-options -nowarn -d $B/classes $(find src -name '*.java')
$BT/d8 --release --min-api 29 --lib $AJAR --output $B $(find $B/classes -name '*.class')
echo "[apk] resources"
$BT/aapt2 compile --dir res -o $B/res.zip
cp -r assets/. $B/assets/
cp $R/out/fmodex/image.bin $B/assets/jpimg/fmodex/image.bin
cp $R/out/bink/image.bin $B/assets/jpimg/bink/image.bin
$BT/aapt2 link -o $B/base.apk -I $AJAR --manifest AndroidManifest.xml --rename-manifest-package com.jp100port.launcher -A $B/assets $B/res.zip
(cd $B && zip -q -X base.apk classes.dex && zip -q -X -r base.apk lib)
echo "[apk] align + sign"
$BT/zipalign -f -p 4 $B/base.apk $B/aligned.apk
KS=${KEYSTORE:-$A/keystore/me1port-debug.keystore}
[ -f "$KS" ] || keytool -genkeypair -keystore "$KS" -storepass android -keypass android -alias me1port -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=JP100 Port Debug"
$BT/apksigner sign --v1-signing-enabled true --v2-signing-enabled true --v3-signing-enabled true --ks "$KS" --ks-pass pass:android --key-pass pass:android --out out/JP100-port.apk $B/aligned.apk
$BT/apksigner verify --print-certs out/JP100-port.apk | head -1
ls -la out/JP100-port.apk
echo APK_DONE
