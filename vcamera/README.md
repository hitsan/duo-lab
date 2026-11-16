# vcamera

マイクに向かって話すと、自分で描いた顔の口が動く Web カメラ。Milk-V Duo (64MB) を USB で PC につなぐと、UVC カメラとして映る。同じ USB で ssh もできる (NCM)。

```
MAX9814 ─GP26→ 小コア (FreeRTOS)                       Linux                         PC
               ADC 8kHz → 口の開き + 母音 ─共有メモリ→ vcamera-uvc → /dev/video0 ─USB→ UVC カメラ
                                          0x83FFF000                                     + NCM (ssh)
```

- 小コアがマイクを 8kHz で読み、口の開き (0〜255) と口の形 (あ / い・え / う・お) を共有メモリに書く。
- Linux の `vcamera-uvc` がそれを読み、顔の絵に口を重ねて 320x240 YUYV 30fps で UVC に流す。

## ディレクトリ

| パス | 中身 |
|---|---|
| `src/` | C のソース。ホスト・Linux・小コアで共有する |
| `rtos/` | 小コア専用の実装 (ADC 読み取り、共有メモリへの書き込み、FreeRTOS タスク) |
| `test/` | ホストで動くテスト |
| `art/` | 顔の絵と、それを C の配列に変換するスクリプト |
| `sdk/` | `duo-buildroot-sdk` に当てる変更 |
| `build.sh` | SDK をビルドして SD イメージを作る |
| `usb-uvc-ncm.sh` | Duo 上で USB を UVC + NCM にする |
| `build/` | ビルドの成果物 (git 管理外) |

## 準備

- **ハード**: 秋月 AE-MAX9814ETD (JP1 ショートで DC バイアス出力)。OUT を GP26 に直結し、GAIN を VDD につないで 40dB にする。GP26 は基板上で 10k/10k に分圧されて SoC の ADC1 (1.8V 系) に入るので、ピンでは約 3.6V まで扱える。マイクの 1.23V バイアスはそのまま入れてよい。
- **ホストのツール**: `.envrc` (direnv) が dev-shell の `milkv-duo-sdk` と `duo-examples/envsetup.sh` を読み込む。SDK のビルドと、Linux 用バイナリのクロスコンパイルの両方がこれでそろう。
- **顔の絵**: `art/face.png` に白黒の線画を置く。絵は git に入れない。

## 使い方

### 1. SD イメージを作って焼く

```sh
./build.sh image          # SD イメージ全体。duo-buildroot-sdk/out/ にできる
./build.sh kernel         # boot.sd だけ作る。SD の FAT 領域の boot.sd を置き換える
```

`build.sh` は submodule と host-tools を取得し、`sdk/` の変更を当ててからビルドする。当たっているかを確かめてから当てるので、何度実行しても結果は同じ。

### 2. 顔の絵を変換する

```sh
make face_art             # art/face.png → art/face_art.h。別の絵は ART=path。Pillow は nix から取る
```

`art/convert.py` が絵を 240px に縮めて 2 値化し、いちばん大きい線を輪郭、残りのうち一番下の線を口とみなして切り出す。結果は 1 bit/pixel の配列として `art/face_art.h` に書かれる (git 管理外)。

### 3. Duo で動かす

```sh
make uvc
scp -O build/vcamera-uvc usb-uvc-ncm.sh root@192.168.42.1:
ssh root@192.168.42.1 'nohup sh usb-uvc-ncm.sh > /tmp/usb-uvc-ncm.log 2>&1 &'
```

`usb-uvc-ncm.sh` が USB gadget を作り直し、隣に置いた `vcamera-uvc` を起動する。gadget を作り直すと ssh が一度切れるので、`nohup` で切り離して実行する。数秒後に PC から UVC カメラと NCM の両方が見える。

`vcamera-uvc` は止めずに動かし続ける。f_uvc は UVC の制御要求を userspace に渡すので、応答する者がいないと ep0 が止まり、NCM (ssh) の受信も詰まる。

動作中の `vcamera-uvc` を差し替えると NCM ごと落ちる (原因は未調査)。USB を挿し直せば戻る。差し替えは Duo を再起動した直後に行う。

## 母音のキャリブレーション

口の形は、5 母音のテンプレート (`src/vowel_templates.h`) のうち最も近いものから選ぶ。声やマイクを変えたら取り直す。

```sh
make calibrate
scp -O build/vcamera-calibrate root@192.168.42.1:
ssh -t root@192.168.42.1 './vcamera-calibrate > vowel_templates.h'
scp -O root@192.168.42.1:vowel_templates.h src/
./build.sh image          # テンプレートは小コアに埋め込まれるので、イメージを作り直す
```

あ・い・う・え・お の順に、Enter を押してから 3 秒ずつ伸ばして発音する。テンプレートを作るのに必要な有声フレームが足りなければ、その母音だけやり直しを求められる。

## 仕組み

### 口の開き

`src/lipsync.c` が声の包絡を dB にし、閉じる dB と全開の dB の間を 0〜255 に割り当てる。包絡の追従速さは `RISE_MS` / `FALL_MS` で決める。

| 定数 | 場所 | 用途 |
|---|---|---|
| `RISE_MS` `FALL_MS` | `src/lipsync.c` | 包絡が立ち上がる・下がる速さ |
| `MIC_CLOSED_DB` `MIC_FULL_OPEN_DB` | `rtos/vcamera_task.c` | 小コア (実マイク) のしきい値 |
| `CLOSED_DB` `FULL_OPEN_DB` | `src/lipsync.h` | ホストの WAV 再生用のしきい値 |

### 口の形

`src/vowel.c` が 32ms ごとに FFT をかけ、150〜2500Hz を 10 帯域に分ける。無音中に学習したノイズを差し引いてから log をとり、平均を除いて音量の影響を消す。この特徴量にいちばん近い母音テンプレートを選び、3 つの口の形に対応させる (え は い と同じ横長、お は う と同じ丸)。ただし お の音は う より あ に近いので、判定そのものは 5 母音で行う。

### 共有メモリ

FreeRTOS 領域の最後のページ `0x83FFF000` を使う (`src/shared_openness.h`)。

| オフセット | 中身 |
|---|---|
| `+0` | uint32。bit 0-7 が口の開き、bit 8-15 が口の形 |
| `+64` | 直近フレームの母音特徴量 (`vcamera-calibrate` が読む) |

小コアは書き込んだあとに `clean_dcache_range` する。Linux 側は `/dev/mem` を `O_SYNC` で mmap して読む。小コアのイメージがこのページに重なった場合は、`sdk/rtos.patch` の ASSERT によってリンク時にエラーになる。

### 入出力の切り替え

入力は `src/mic.h`、出力は `src/display.h` で抽象化していて、どの実装を使うかはリンク時に決まる。

| ターゲット | 入力 (`mic.h`) | 出力 (`display.h`) |
|---|---|---|
| 小コア | `rtos/mic_adc.c` (SARADC ch1 を `rdtime` で 8kHz ポーリング) | `rtos/display_shared.c` (共有メモリ) |
| `vcamera-uvc` | 共有メモリ (`src/reader.c`) | `src/display_uvc.c` (`/dev/video0`) |
| `vcamera-reader` | 共有メモリ (`src/reader.c`) | `src/display_terminal.c` |
| `vcamera-host` | `src/mic_wav.c` (stdin の WAV) | `src/display_terminal.c` |

`src/adc_sample.c` は、ADC の生値 (約 1500 のバイアス付き 12bit) からバイアスを追従して引き、16bit のサンプルにする。`src/uvc_control.c` は UVC の制御要求への応答とフレームの描画を担当する。口を消した顔をそのまま描き、口だけを開き具合に応じて縦に、口の形に応じて縦横に伸縮して重ねる。閉じているときは、口の輪郭を 3 行に潰して線にする。

### 口の動きをなめらかにする

小コアから届く値は段階的に変わる。口の形は 32ms ごとの母音判定で あ / い・え / う・お が一気に切り替わり、開き具合も声の立ち上がり (`RISE_MS` 25ms) で 1 フレームのうちに全開まで跳ぶ。そこで `vcamera-uvc` は、口の幅と高さをそのまま使わず、30fps の毎フレーム目標の大きさへ残りの半分 (`EASE_PERCENT`, `src/uvc_control.c`) ずつ近づける。形が変わるときも幅と高さが数フレームかけて移るので、口がモーフィングして見える。小コアには手を入れないので、変えても fip を作り直す必要はない。

### SDK への変更 (`sdk/`)

| ファイル | 内容 |
|---|---|
| `linux.config` | カーネルの defconfig に足す行。UVC gadget を有効にする |
| `rtos.patch` | FreeRTOS に `src/` と `rtos/` を組み込み、vcamera タスクを起動する |
| `uboot.patch` | カーネル引数に `clk_ignore_unused` を足す。Linux の `clk_disable_unused` が小コアしか使わない `clk_saradc` を止め、ADC が固まるのを防ぐ |

## デバッグ用

```sh
make test                                     # ホストでテスト (先に make face_art)
make run WAV=voice.wav                        # ホストで WAV に合わせてターミナルの顔を動かす

make reader                                   # 小コアの出力をターミナルで見る
scp -O build/vcamera-reader root@192.168.42.1:
ssh -t root@192.168.42.1 ./vcamera-reader
```

`make run` に渡す WAV は 16bit PCM で、サンプリング周波数を 8kHz の整数倍にする。8kHz に間引いて使い、ステレオなら先頭のチャンネルだけを読む。

