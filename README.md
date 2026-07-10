# Rd-H Synth

[![CI & Deploy](https://github.com/Ride-H/Rd-H_synth/actions/workflows/ci.yml/badge.svg)](https://github.com/Ride-H/Rd-H_synth/actions/workflows/ci.yml)

**▶ UI モックをブラウザで試す（インストール不要）: https://ride-h.github.io/Rd-H_synth/**

FM 合成・カラードノイズ・AI マッチを備えたソフトウェアシンセサイザー（オーディオプラグイン）。
A polyphonic software synthesizer plugin with FM synthesis, colored noise, and an AI-match workflow.

- **形式 / Formats:** VST3 / AU / Standalone（macOS, Apple Silicon / Intel）
- **ポリフォニー / Polyphony:** 最大 16 ボイス

## 主な機能 / Features

- **OSC セクション** — 3 オシレーター（Sine/Saw/Square/Tri）+ アンプ ADSR + ユニゾン（4声デチューン）+ ピッチエンベロープ
- **FM エンジン** — 4 オペレーター・4 アルゴリズム・OP フィードバック・キースケーリング・OP 毎の独立 ADSR・オーバーサンプリング
- **ノイズ** — White / Pink / Brown（独立エンベロープ・Filter Send / Direct Out の 2 経路）
- **フィルター** — IIR ローパス（Cutoff / Resonance）
- **AI マッチ** — オーディオファイル（WAV/AIFF/FLAC）を解析して音色パラメーターに反映するワークフロー
- **4 モード UI** — AI / SIMPLE / ADVANCED（+ 将来 PERFORMANCE）・リアルタイムスペクトラム・常設 MIDI 鍵盤

## クイックビルド / Quick build

CMake 3.22+ があればビルドできます。JUCE 8 は `/Applications/JUCE` にあればそれを使い、
無ければ **自動でフェッチ**されます（CI と同じ挙動）。

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target RdhSynth_All --parallel
```

生成物は `build/RdhSynth_artefacts/Release/`（VST3 / AU / Standalone）。
詳細は [`guide/building.md`](guide/building.md)。

### ヘッドレスレンダラー（検証用）

GUI なしでプリセットを WAV に描画するツールを同梱しています（CI のスモークテストにも使用）:

```sh
cmake --build build --target RdhRender --parallel
./build/RdhRender_artefacts/Release/RdhRender test_presets/test-4op-algo0-chain.rdhs out.wav --secs 2
```

## 開発 / Development

**Issue 駆動**で開発しています — バグ報告・提案は [Issues](https://github.com/Ride-H/Rd-H_synth/issues) へ。

- ブランチ: `feature/*`（Issue 毎）→ `dev`（統合）→ `main`（リリース・PR のみ）
- CI: 全ブランチ push / PR でビルド + レンダースモークテスト。`main` への push で
  [UI モック](https://ride-h.github.io/Rd-H_synth/)を GitHub Pages へ自動デプロイ
- リリース: タグ `v*` で VST3 / AU / Standalone の zip を自動添付（未署名ビルド）

詳細は [`guide/development.md`](guide/development.md)、設計の概要は [`guide/architecture.md`](guide/architecture.md)。

## リンク / Links

- YouTube: [@ride_on_the_2b](https://www.youtube.com/@ride_on_the_2b)

## ライセンス / License

本ソフトウェアは **GNU Affero General Public License v3.0（AGPLv3）** で提供されます（[`LICENSE`](LICENSE)）。

本プロジェクトは [JUCE](https://juce.com) フレームワークを使用しています。JUCE は AGPLv3 と商用ライセンスのデュアルライセンスです。本リポジトリのソースを AGPLv3 で利用・再配布する場合は、JUCE の AGPLv3 条件も併せて遵守してください。商用利用には別途 JUCE 商用ライセンスが必要になる場合があります。
