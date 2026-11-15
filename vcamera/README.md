WAV の音声に合わせて、ターミナルに描いた顔の口を 30fps で動かす。UVC カメラにする前のプロトタイプ。

```sh
make test
make run WAV=voice.wav                       # ホストで実行
make duo                                     # Duo 向けにビルド。scp して ./vcamera < voice.wav
```

入力は `mic.h`、出力は `display.h` で切り分け、実装はリンク時に選ぶ。
ホストでは `mic_wav.c` が stdin の WAV を 8kHz の実時間で返し、`display_terminal.c` がターミナルに描く。
WAV は 16bit PCM で、fs は 8kHz の整数倍にする。8kHz に間引き、ステレオなら先頭のチャンネルだけを使う。

パラメータは `lipsync.c` の `RISE_MS` `FALL_MS` と `lipsync.h` の `CLOSED_DB` `FULL_OPEN_DB` で決める。

## Duo の小コアで動かす

小コア (FreeRTOS) が GP26 のマイク (MAX9814, GAIN=VDD で 40dB) を SARADC で 8kHz で読んで口の開きを計算し、共有メモリ `0x83FFF000` (uint32、下位 8bit) に書く。
Linux 側の `vcamera-reader` がそれを 30fps で読んでターミナルに描く。

| ファイル | 役割 |
|---|---|
| `adc_sample.c` | ADC の生値 (約 1500 のバイアス付き 12bit) から、バイアスを追って引いた 16bit サンプルにする |
| `rtos/mic_adc.c` | `mic.h` の実装。SARADC ch1 (GP26) を 8kHz で読む。サンプル間は `rdtime` で待つ |
| `rtos/display_shared.c` | `display.h` の実装。共有メモリに書いて `clean_dcache_range` する |
| `rtos/vcamera_task.c` | `main.c` と同じループを回す FreeRTOS タスク |
| `sdk/rtos.patch` | duo-buildroot-sdk の FreeRTOS に上をビルドに入れ、タスクを作る。イメージが共有ページに重なればリンクで落とす |
| `reader.c` | Linux 側。`/dev/mem` を mmap して 30fps で `ShowMouth` を呼ぶ |

GP26 は Duo の基板上で 10k/10k に分圧されて SoC の ADC1 (1.8V 系) に入る。ピンで約 3.6V までが範囲で、マイクの 1.23V バイアス出力を直結してよい。

```sh
./build.sh image                             # SD イメージを作る。duo-buildroot-sdk/out/ にできる

make reader                                  # source duo-examples/envsetup.sh 済みで
scp vcamera-reader root@192.168.42.1:
ssh -t root@192.168.42.1 ./vcamera-reader
```

## SDK のビルド

SDK はリポジトリ直下の `duo-buildroot-sdk/` (submodule) を使う。SDK への変更は `sdk/` に置き、`build.sh` が当てる。何度実行しても同じ結果になる。

| ファイル | 役割 |
|---|---|
| `sdk/linux.config` | カーネルの defconfig に足す行。UVC ガジェットを有効にする |
| `sdk/rtos.patch` | FreeRTOS への変更 (上の表) |
| `sdk/uboot.patch` | カーネル引数に `clk_ignore_unused` を足す。Linux が未使用とみなして clk_saradc を止めると小コアの ADC が固まるため |
| `build.sh` | submodule と host-tools を取得し、`sdk/` を当ててビルドする |

ビルドに要るホストのツールは dev-shell の `milkv-duo-sdk` にそろっていて、`.envrc` (direnv) が読み込む。

```sh
./build.sh kernel                            # boot.sd だけ作る。SD の FAT 領域の boot.sd を置き換える
./build.sh image                             # SD イメージ全体
```

## 顔の絵

UVC に映す顔は `art/face.png` (白黒の線画) から作る。絵と、それから作る `face_art.h` は git に入れない。テストと `vcamera-uvc` のビルドには先に `make face_art.h` が要る。`art/convert.py` が 240px に縮小して 2 値化し、いちばん大きい線を輪郭、残りでいちばん下にある線を口とみなして切り出し、`face_art.h` (1 bit/pixel) に書く。
`uvc_control.c` は口を消した顔をそのまま描き、口だけを開き具合で縦に、母音の形 (あ / い・え / う・お) で縦横に伸縮して重ねる。閉じると口の輪郭を 3 行に潰して線にする。

```sh
make face_art.h                              # art/face.png から。別の絵は ART=path。Pillow を nix から取る
```
