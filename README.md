# duo-lab

Milk-V Duo（CV1800B、64MB）で遊ぶための実験置き場。

## プロジェクト

| ディレクトリ | 中身 |
|---|---|
| [`vcamera/`](vcamera/) | マイクに向かって話すと、イラストの口が動くWebカメラ。USBでPCにつなぐとUVCカメラとして映る |
| [`sobel/`](sobel/) | Duoの2つのコア（Linux側とRTOS側）で、画像のエッジ検出を分担して処理する |

使い方はそれぞれのREADMEにある。

## 準備

- Duo向けのビルドには[duo-examples](https://github.com/milkv-duo/duo-examples)のツールチェーンを使う。先に`source /PATH/TO/duo-examples/envsetup.sh`を実行しておく。
- SDカードのイメージやカーネルを作るときは、submoduleの[duo-buildroot-sdk](https://github.com/milkv-duo/duo-buildroot-sdk)を使う。

```sh
git submodule update --init
```
