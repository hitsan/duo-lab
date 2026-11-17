# vcamera

マイクに向かって話すと、自分で描いた顔の口が動く Web カメラ。Milk-V Duo (64MB) を USB で PC につなぐと、UVC カメラとして映る。同じ USB で ssh もできる (NCM)。

```
MAX9814 ─GP26→ 小コア (FreeRTOS)                       Linux                         PC
               ADC 8kHz → 口の開き + 母音 ─共有メモリ→ vcamera-uvc → /dev/video0 ─USB→ UVC カメラ
                                          0x83FFF000                                     + NCM (ssh)
```

- 小コアがマイクを 8kHz で読み、口の開き (0〜255) と口の形 (あ / い・え / う・お) を共有メモリに書く。
- Linux の `vcamera-uvc` がそれを読み、キャラクターの絵に口の絵を重ねて 320x240 YUYV 30fps で UVC に流す。

## ディレクトリ

| パス | 中身 |
|---|---|
| `src/shared/` | 小コアと Linux の取り決め (共有メモリの配置、口の形などの型、`ShowMouth`) |
| `src/rtos/` | 小コアの実装 (ADC 読み取り、口の開き、母音判定、共有メモリへの書き込み、FreeRTOS タスク) |
| `src/linux/` | Linux の実装 (共有メモリの読み取り、UVC、設定ファイル、キャリブレーション) |
| `test/` | ホストで動くテスト |
| `sdk/` | `duo-buildroot-sdk` に当てる変更 |
| `build.sh` | SDK をビルドして SD イメージを作る (`make image` / `make kernel` から呼ぶ) |
| `usb-uvc-ncm.sh` | Duo 上で USB を UVC + NCM にする |
| `build/` | ビルドの成果物 (git 管理外) |

## 準備

- **ハード**: 秋月 AE-MAX9814ETD (JP1 ショートで DC バイアス出力)。OUT を GP26 に直結し、GAIN を VDD につないで 40dB にする。GP26 は基板上で 10k/10k に分圧されて SoC の ADC1 (1.8V 系) に入るので、ピンでは約 3.6V まで扱える。マイクの 1.23V バイアスはそのまま入れてよい。
- **ホストのツール**: `.envrc` (direnv) が dev-shell の `milkv-duo-sdk` と `duo-examples/envsetup.sh` を読み込む。SDK のビルドと、Linux 用バイナリのクロスコンパイルの両方がこれでそろう。
- **キャラクターの絵**: `face.png` と `mouth.png` の 2 枚 (下の「2. 絵を用意する」)。絵は git に入れない (`vcamera/*.png` は無視される)。

## 使い方

### 1. SD イメージを作って焼く

```sh
make image                # SD イメージ全体。duo-buildroot-sdk/out/ にできる
make kernel               # boot.sd だけ作る。SD の FAT 領域の boot.sd を置き換える
```

`make image` / `make kernel` は `build.sh` を呼ぶ。`build.sh` は submodule と host-tools を取得し、`sdk/` の変更を当ててからビルドする。当たっているかを確かめてから当てるので、何度実行しても結果は同じ。

### 2. 絵を用意する

同じキャンバスサイズの透過 PNG を 2 枚用意する (AI ツールなどで作る)。

| ファイル | 中身 |
|---|---|
| `face.png` | キャラクターをカラーで。口だけを消し、それ以外は透明。体が入っていてもよい。サイズは自由 |
| `mouth.png` | `face.png` と同じサイズ。口だけを描き、それ以外は透明 |

口の位置は、`mouth.png` の透明でない画素を囲む矩形で決まる。ほぼ透明 (alpha 16 未満) の画素は無視する。キャンバスは縦横比を保って 320x240 に収まるよう拡大縮小して中央に置き、余白には背景が出る。背景は任意の画像 (PNG / JPEG) で、320x240 を覆うよう拡大縮小してはみ出た分を切り落とす。指定しなければ steelblue の単色になる。

絵は Duo の上で `vcamera-uvc` の起動時に読み込む。読み込みにはメモリを幅 x 高さ x 4 バイト使うので、1000px 程度までにしておく。

### 3. Duo で動かす

```sh
make uvc
scp -O build/vcamera-uvc usb-uvc-ncm.sh vcamera.conf face.png mouth.png root@192.168.42.1:   # 背景を使うならその画像も
ssh root@192.168.42.1 'nohup sh usb-uvc-ncm.sh > /tmp/usb-uvc-ncm.log 2>&1 &'
```

`usb-uvc-ncm.sh` が USB gadget を作り直し、隣に置いた `vcamera-uvc` を起動する。gadget を作り直すと ssh が一度切れるので、`nohup` で切り離して実行する。数秒後に PC から UVC カメラと NCM の両方が見える。

`vcamera-uvc` は起動時に、実行ファイルと同じディレクトリの `vcamera.conf` を読む。1 行に 1 つ `key=value` で書き、`#` から始まる行はコメントになる。書いていない項目と、ファイルがない場合は既定値を使う。相対パスは `vcamera.conf` のあるディレクトリが基準。間違った行は `/tmp/vcamera-uvc.log` に書いて無視する。

```
background=ajito.jpg
face_scale=80
ease_percent=40
```

| キー | 中身 | 既定値 |
|---|---|---|
| `face` | 口を消したキャラクターの PNG | `face.png` |
| `mouth` | 口だけの PNG | `mouth.png` |
| `background` | 背景の画像 (PNG / JPEG) | なし (steelblue の単色) |
| `face_scale` | 絵の大きさ。画面に収まる最大の大きさに対する百分率 (1〜100) | 85 |
| `ease_percent` | 口が 1 フレームで目標の大きさへ近づく割合 (1〜100)。小さいほどゆっくり動く。100 で補間なし | 50 |
| `ascii` | 1 で ASCII アートにする (後述) | 0 |
| `mouth_a` `mouth_i` `mouth_u` | 口を開ききったときの大きさ。元の口に対する 幅x高さ の百分率 (1〜300)。`mouth_i` は い・え、`mouth_u` は う・お | `100x100` `140x55` `70x90` |

設定を変えたら Duo を再起動して `usb-uvc-ncm.sh` からやり直す (動作中の `vcamera-uvc` を差し替えると NCM ごと落ちるため)。

絵が読めない、`face.png` と `mouth.png` のサイズが違う、`mouth.png` が全部透明、のどれかなら、`vcamera-uvc` は `/tmp/vcamera-uvc.log` に理由を書き、既定の単色だけを映して動き続ける。終了すると UVC の要求に答える者がいなくなり NCM (ssh) まで止まるため。絵を直したら Duo を再起動する。

`vcamera-uvc` は止めずに動かし続ける。f_uvc は UVC の制御要求を userspace に渡すので、応答する者がいないと ep0 が止まり、NCM (ssh) の受信も詰まる。

`vcamera.conf` に `ascii=1` と書くと、映像を ASCII アートにする。フレームを 8x8 のセルに分け、セルの明るさで ` .:-=+*%#@` のどれかを選び、セルの色で黒地に描く。顔と背景の ASCII は最初に 1 回だけ作り、毎フレーム描き直すのは口の周りのセル行だけ。

動作中の `vcamera-uvc` を差し替えると NCM ごと落ちる (原因は未調査)。USB を挿し直せば戻る。差し替えは Duo を再起動した直後に行う。

## 母音のキャリブレーション

口の形は、5 母音のテンプレート (`src/rtos/vowel_templates.h`) のうち最も近いものから選ぶ。声やマイクを変えたら取り直す。

```sh
make calibrate
scp -O build/vcamera-calibrate root@192.168.42.1:
ssh -t root@192.168.42.1 './vcamera-calibrate > vowel_templates.h'
scp -O root@192.168.42.1:vowel_templates.h src/rtos/
make image                # テンプレートは小コアに埋め込まれるので、イメージを作り直す
```

あ・い・う・え・お の順に、Enter を押してから 3 秒ずつ伸ばして発音する。テンプレートを作るのに必要な有声フレームが足りなければ、その母音だけやり直しを求められる。

## 仕組み

### 口の開き

`src/rtos/lipsync.c` が声の包絡を dB にし、閉じる dB と全開の dB の間を 0〜255 に割り当てる。包絡の追従速さは `RISE_MS` / `FALL_MS` で決める。

| 定数 | 場所 | 用途 |
|---|---|---|
| `RISE_MS` `FALL_MS` | `src/rtos/lipsync.c` | 包絡が立ち上がる・下がる速さ |
| `MIC_CLOSED_DB` `MIC_FULL_OPEN_DB` | `src/rtos/vcamera_task.c` | 小コア (実マイク) のしきい値 |

### 口の形

`src/rtos/vowel.c` が 32ms ごとに FFT をかけ、150〜2500Hz を 10 帯域に分ける。無音中に学習したノイズを差し引いてから log をとり、平均を除いて音量の影響を消す。この特徴量にいちばん近い母音テンプレートを選び、3 つの口の形に対応させる (え は い と同じ横長、お は う と同じ丸)。ただし お の音は う より あ に近いので、判定そのものは 5 母音で行う。

### 共有メモリ

FreeRTOS 領域の最後のページ `0x83FFF000` を使う (`src/shared/shared_openness.h`)。

| オフセット | 中身 |
|---|---|
| `+0` | uint32。bit 0-7 が口の開き、bit 8-15 が口の形 |
| `+64` | 直近フレームの母音特徴量 (`vcamera-calibrate` が読む) |

小コアは書き込んだあとに `clean_dcache_range` する。Linux 側は `/dev/mem` を `O_SYNC` で mmap して読む。小コアのイメージがこのページに重なった場合は、`sdk/rtos.patch` の ASSERT によってリンク時にエラーになる。

### 入出力の切り替え

入力は `src/rtos/mic.h`、出力は `src/shared/display.h` で抽象化していて、どの実装を使うかはリンク時に決まる。

| ターゲット | 入力 (`mic.h`) | 出力 (`display.h`) |
|---|---|---|
| 小コア | `src/rtos/mic_adc.c` (SARADC ch1 を `rdtime` で 8kHz ポーリング) | `src/rtos/display_shared.c` (共有メモリ) |
| `vcamera-uvc` | 共有メモリ (`src/linux/reader.c`) | `src/linux/display_uvc.c` (`/dev/video0`) |

`src/rtos/adc_sample.c` は、ADC の生値 (約 1500 のバイアス付き 12bit) からバイアスを追従して引き、16bit のサンプルにする。`src/linux/uvc_control.c` は UVC の制御要求への応答とフレームの描画を担当する。起動時に 1 回だけ、`face.png` をフレームに合わせて縮小し (縮小先の 1 画素に入る元画素を平均する)、背景にアルファブレンドして YUYV (BT.601, limited range) にしておく。`mouth.png` も同じ変換で縮小し、口の矩形の分だけ Y・U・V・alpha で持っておく。毎フレームはそのフレームをコピーし、口を元の中心のまわりで伸縮させて (最近傍) アルファブレンドするだけ。口は開き具合に応じて縦に、口の形に応じて縦横に伸縮する (`mouth_a` `mouth_i` `mouth_u`)。閉じているときは 3 行 (`MOUTH_CLOSED_HEIGHT`) に潰し、潰した行のうち最も不透明な画素を残すので、唇の線が消えずに線として残る。

### 口の動きをなめらかにする

小コアから届く値は段階的に変わる。口の形は 32ms ごとの母音判定で あ / い・え / う・お が一気に切り替わり、開き具合も声の立ち上がり (`RISE_MS` 25ms) で 1 フレームのうちに全開まで跳ぶ。そこで `vcamera-uvc` は、口の幅と高さをそのまま使わず、30fps の毎フレーム目標の大きさへ残りの半分 (`ease_percent`) ずつ近づける。形が変わるときも幅と高さが数フレームかけて移るので、口がモーフィングして見える。小コアには手を入れないので、変えても fip を作り直す必要はない。

### SDK への変更 (`sdk/`)

| ファイル | 内容 |
|---|---|
| `linux.config` | カーネルの defconfig に足す行。UVC gadget を有効にする |
| `rtos.patch` | FreeRTOS に `src/shared/` と `src/rtos/` を組み込み、vcamera タスクを起動する |
| `uboot.patch` | カーネル引数に `clk_ignore_unused` を足す。Linux の `clk_disable_unused` が小コアしか使わない `clk_saradc` を止め、ADC が固まるのを防ぐ |

## デバッグ用

```sh
make test                                     # ホストでテスト
```
