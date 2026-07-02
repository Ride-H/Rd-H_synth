# Rd-H Synth

FM 合成・ノイズ・AI マッチを備えたソフトウェアシンセサイザー（オーディオプラグイン）。
A polyphonic software synthesizer plugin with FM synthesis, colored noise, and an AI-match workflow.

- **形式 / Formats:** VST3 / AU / Standalone
- **ポリフォニー / Polyphony:** 最大 16 ボイス
- **対応 OS:** macOS（Apple Silicon / Intel）

## 主な機能 / Features

- **OSC セクション** — 3 オシレーター（複数波形）+ アンプエンベロープ + ユニゾン + ピッチエンベロープ
- **FM エンジン** — 4-OP・4 アルゴリズム・OP フィードバック・キースケーリング・オペレーター毎の独立 ADSR
- **ノイズ** — White / Pink / Brown（独立エンベロープ・Filter Send / Direct Out 経路）
- **フィルター** — IIR ローパス
- **AI マッチ** — 目標音に寄せるワークフロー

詳しい使い方は [`説明/`](説明/) を参照（日本語ドキュメント・機能概要 / パラメータ / ビルド方法 / FM アルゴリズム解説）。

## リンク / Links

- YouTube: [@ride_on_the_2b](https://www.youtube.com/@ride_on_the_2b)

## ビルド / Build

[JUCE 8](https://juce.com) と CMake が必要です（JUCE は `/Applications/JUCE` を想定）。

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

生成物（VST3 / AU / Standalone）は `build/RdhSynth_artefacts/Release/` 以下に出力されます。

## ライセンス / License

本ソフトウェアは **GNU Affero General Public License v3.0（AGPLv3）** で提供されます（[`LICENSE`](LICENSE)）。

本プロジェクトは [JUCE](https://juce.com) フレームワークを使用しています。JUCE は AGPLv3 と商用ライセンスのデュアルライセンスです。本リポジトリのソースを AGPLv3 で利用・再配布する場合は、JUCE の AGPLv3 条件も併せて遵守してください。商用利用には別途 JUCE 商用ライセンスが必要になる場合があります。

Copyright (C) 2026 RdH (Rd-H Synth)
