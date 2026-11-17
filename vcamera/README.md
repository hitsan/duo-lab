# vcamera

マイクに向かって話すと、イラストの口が動くWebカメラ。Milk-V DuoをUSBでPCにつなぐとUVCカメラとして映る。

## 用意するもの

- マイク: AE-MAX9814ETD
- 絵: 同じキャンバスサイズの透過PNGを2枚。

| ファイル | 中身 |
|---|---|
| `face.png` | イラストから口だけを消したもの。サイズは1000px程度まで |
| `mouth.png` | `face.png`と同じサイズで、口だけを描いたもの |

## 動かし方

### 1. SDカードを作る

```sh
make image
```

`duo-buildroot-sdk/out/`にできたイメージを、Milk-Vの公式の手順でmicroSDカードに書き込む。Duoに挿してUSBでPCにつなぐ。

### 2. カメラを起動する

```sh
make uvc
scp -O build/vcamera-uvc usb-uvc-ncm.sh vcamera.conf face.png mouth.png root@192.168.42.1:
ssh root@192.168.42.1 'nohup sh usb-uvc-ncm.sh > /tmp/usb-uvc-ncm.log 2>&1 &'
```

数秒後にPCからUVCカメラとして見える。うまく映らないときはDuoの`/tmp/vcamera-uvc.log`を見る。

### 3. 見た目を変える

`vcamera.conf`に`key=value`で書く。書き換えたらDuoを再起動して、手順2の`scp`からやり直す。

| キー | 中身 | 既定値 |
|---|---|---|
| `face` | 口を消した絵 | `face.png` |
| `mouth` | 口だけの絵 | `mouth.png` |
| `background` | 背景の画像。PNGかJPEG | steelblueの単色 |
| `face_scale` | 絵の大きさ。1〜100 | 85 |
| `ease_percent` | 口の動きのなめらかさ。小さいほどゆっくり動く。1〜100 | 50 |
| `ascii` | 1でASCIIアートにする | 0 |
| `mouth_a` `mouth_i` `mouth_u` | あ、い・え、う・お の口を開ききったときの幅x高さ。元の口に対する百分率 | `100x100` `140x55` `70x90` |

### 4. 自分の声に合わせる

口の形がうまく切り替わらないときは、自分の声で母音を録り直す。

```sh
make calibrate
scp -O build/vcamera-calibrate root@192.168.42.1:
ssh -t root@192.168.42.1 './vcamera-calibrate > vowel_templates.h'
scp -O root@192.168.42.1:vowel_templates.h src/rtos/
make image
```

画面の指示に従って、あ・い・う・え・お をそれぞれ3秒ずつ伸ばして発音する。最後に`make image`で作り直したイメージをSDカードに書き込む。

## Linux側を変更する

`src/linux/`だけを変えたときは、`vcamera-uvc`を作り直してDuoに置けばよい。SDイメージは作り直さない。動作中の`vcamera-uvc`を差し替えるとNCMが落ちるので、Duoを再起動してから置く。

```sh
make test
make uvc
ssh root@192.168.42.1 reboot
scp -O build/vcamera-uvc root@192.168.42.1:
ssh root@192.168.42.1 'nohup sh usb-uvc-ncm.sh > /tmp/usb-uvc-ncm.log 2>&1 &'
```

`src/shared/`を変えたときは小コアにも影響するので、`make image`でSDイメージも作り直す。

## SDKへの変更

`make image`は`sdk/`の変更を`duo-buildroot-sdk`に当ててからビルドする。

| ファイル | 内容 |
|---|---|
| `linux.config` | カーネルのdefconfigに足す行。UVC gadgetを有効にする |
| `rtos.patch` | FreeRTOSに`src/shared/`と`src/rtos/`を組み込み、vcameraタスクを起動する |
| `uboot.patch` | カーネル引数に`clk_ignore_unused`を足す。Linuxの`clk_disable_unused`が小コアしか使わない`clk_saradc`を止め、ADCが固まるのを防ぐ |
