#!/bin/sh
# Build the LocalStage Android app (APK) from the C sources.
# Needs: Android SDK (build-tools, platforms;android-34) and NDK. Run from the repo root:
#   ANDROID_HOME=~/Android/Sdk android/build.sh
set -eu
SDK=${ANDROID_HOME:-${ANDROID_SDK_ROOT:-$HOME/Android/Sdk}}
NDK=${ANDROID_NDK_HOME:-$(ls -d "$SDK"/ndk/* | sort -V | tail -1)}
BT=$(ls -d "$SDK"/build-tools/* | sort -V | tail -1)
PLATFORM=$(ls -d "$SDK"/platforms/android-* | sort -V | tail -1)
TC=$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin
MIN=24
OUT=build/android
rm -rf "$OUT" && mkdir -p "$OUT/apk" "$OUT/res"
make -s build/web.c build/pdfjs.c build/pdfworker.c

SRC="src/main.c src/http.c src/ws.c src/signaling.c src/decks.c src/net.c src/json.c src/qr.c src/util.c src/plat.c
     build/web.c build/pdfjs.c build/pdfworker.c android/app.c"
for abi in arm64-v8a armeabi-v7a x86_64; do
  case $abi in
    arm64-v8a) cc=$TC/aarch64-linux-android$MIN-clang ;;
    armeabi-v7a) cc=$TC/armv7a-linux-androideabi$MIN-clang ;;
    x86_64) cc=$TC/x86_64-linux-android$MIN-clang ;;
  esac
  mkdir -p "$OUT/apk/lib/$abi"
  # shellcheck disable=SC2086
  $cc -O2 -std=gnu11 -fPIC -shared -Wall -DLOCALSTAGE_NO_MAIN -Wl,-z,max-page-size=16384 \
      -o "$OUT/apk/lib/$abi/liblocalstage.so" $SRC -landroid -llog -lm
  "$TC/llvm-strip" "$OUT/apk/lib/$abi/liblocalstage.so"
done

"$BT/aapt2" compile --dir android/res -o "$OUT/res.zip"
"$BT/aapt2" link -o "$OUT/unsigned.apk" -I "$PLATFORM/android.jar" --manifest android/AndroidManifest.xml \
  --min-sdk-version $MIN --target-sdk-version 34 "$OUT/res.zip"
(cd "$OUT/apk" && zip -q -0 -r ../unsigned.apk lib 2>/dev/null || python3 -I -c "
import zipfile,os,sys
z=zipfile.ZipFile('../unsigned.apk','a')
for d,_,fs in os.walk('lib'):
    for f in fs: z.write(os.path.join(d,f), compress_type=zipfile.ZIP_STORED)
z.close()")
"$BT/zipalign" -f -p 4 "$OUT/unsigned.apk" "$OUT/aligned.apk"

KS=${KEYSTORE:-$HOME/.android/debug.keystore}
if [ ! -f "$KS" ]; then
  mkdir -p "$(dirname "$KS")"
  keytool -genkeypair -keystore "$KS" -storepass android -keypass android -alias androiddebugkey \
    -dname "CN=Android Debug,O=Android,C=US" -keyalg RSA -keysize 2048 -validity 10000 >/dev/null
fi
"$BT/apksigner" sign --ks "$KS" --ks-pass "pass:${KEYSTORE_PASS:-android}" --out LocalStage.apk "$OUT/aligned.apk"
echo "Built LocalStage.apk"
