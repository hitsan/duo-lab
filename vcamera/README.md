WAV の音声に合わせて、ターミナルに描いた顔の口を 30fps で動かす。UVC カメラにする前のプロトタイプ。

```sh
make test
make run WAV=voice.wav                       # ホストで実行
make duo                                     # Duo 向けにビルド。scp して ./vcamera < voice.wav
```

入力は `mic.h`、出力は `display.h` で切り分け、実装はリンク時に選ぶ。
ホストでは `mic_wav.c` が stdin の WAV を 8kHz の実時間で返し、`display_terminal.c` がターミナルに描く。
WAV は 16bit PCM で、fs は 8kHz の整数倍にする。8kHz に間引き、ステレオなら先頭のチャンネルだけを使う。

パラメータは `main.c` の定数 `RISE_MS` `FALL_MS` `CLOSED_DB` `FULL_OPEN_DB` で決める。
