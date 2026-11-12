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

## Duo の小コアで動かす (マイクなし)

小コア (FreeRTOS) がファームに埋め込んだ WAV を 8kHz の実時間で読んで口の開きを計算し、共有メモリ `0x83FFF000` (uint32、下位 8bit) に書く。
Linux 側の `vcamera-reader` がそれを 30fps で読んでターミナルに描く。
マイクをつないだときに、おかしいのが ADC 側かそれ以降かを切り分けるためのモック。

| ファイル | 役割 |
|---|---|
| `rtos/mic_embedded_wav.c` | `mic.h` の実装。埋め込み WAV を tick ごとに 40 サンプルずつ返し、末尾で先頭に戻る |
| `rtos/embedded_wav.S` | `.incbin` で WAV を埋め込む |
| `rtos/display_shared.c` | `display.h` の実装。共有メモリに書いて `clean_dcache_range` する |
| `rtos/vcamera_task.c` | `main.c` と同じループを回す FreeRTOS タスク |
| `rtos/sdk.patch` | duo-buildroot-sdk の FreeRTOS に上をビルドに入れ、タスクを作る。イメージが共有ページに重なればリンクで落とす |
| `reader.c` | Linux 側。`/dev/mem` を mmap して 30fps で `ShowMouth` を呼ぶ |

WAV は 16bit PCM。ファームの空きは約 70KB なので、8kHz mono で 4 秒程度まで。

```sh
# duo-buildroot-sdk で
patch -p1 < /PATH/TO/duo-lab/vcamera/rtos/sdk.patch
export VCAMERA_DIR=/PATH/TO/duo-lab/vcamera
export VCAMERA_WAV=$(realpath voice.wav)     # 絶対パス
rm -rf freertos/cvitek/build                 # WAV の中身だけ変えたときは再ビルドされないので消す
./build.sh milkv-duo-sd                      # fip.bin に FreeRTOS が入る。SD に書く

# duo-lab/vcamera で
make reader                                  # source duo-examples/envsetup.sh 済みで
scp vcamera-reader root@192.168.42.1:
ssh -t root@192.168.42.1 ./vcamera-reader
```
