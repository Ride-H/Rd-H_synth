# ビルドガイド / Building

## 必要なもの

- macOS（Apple Silicon / Intel）
- CMake 3.22+
- Xcode Command Line Tools（`xcode-select --install`）
- JUCE 8 — **任意**。`/Applications/JUCE` にあればそれを使用、無ければ CMake が
  自動でフェッチします（初回のみダウンロード時間がかかります）。

## プラグイン（VST3 / AU / Standalone）

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target RdhSynth_All --parallel
```

生成物:

```
build/RdhSynth_artefacts/Release/
├── VST3/Rd-H Synth.vst3
├── AU/Rd-H Synth.component
└── Standalone/Rd-H Synth.app
```

手元で使う場合のインストール先（ユーザー単位）:

- VST3 → `~/Library/Audio/Plug-Ins/VST3/`
- AU → `~/Library/Audio/Plug-Ins/Components/`

> Release ページの zip は**未署名**です。Gatekeeper に止められた場合は
> 右クリック → 開く、または `xattr -d com.apple.quarantine <ファイル>`。

## ヘッドレスレンダラー（RdhRender）

GUI・DAW なしでプリセットを WAV に描画する検証ツール。CI のスモークテストにも使用。

```sh
cmake --build build --target RdhRender --parallel
./build/RdhRender_artefacts/Release/RdhRender <preset.rdhs> <out.wav> \
    [--note N] [--secs S] [--sr SR] [--vel V] [--poly P] [--bench] \
    [--preload <preset>] [--dump-params]
```

例:

```sh
# 4-OP FM プリセットを 2 秒・4 和音で描画
./build/RdhRender_artefacts/Release/RdhRender \
    test_presets/test-4op-algo0-chain.rdhs out.wav --secs 2 --poly 4

# CPU ベンチ（リアルタイム比を表示・WAV は書かない）
./build/RdhRender_artefacts/Release/RdhRender \
    test_presets/test-4op-algo0-chain.rdhs /dev/null --secs 5 --poly 8 --bench
```

`test_presets/` に検証用プリセット（FM 4 アルゴリズム・フィードバック・White/Pink/Brown）があります。

## CI と同じ検証をローカルで走らせる

```sh
cmake -S . -B build-ci -DCMAKE_BUILD_TYPE=Release
cmake --build build-ci --target RdhRender --parallel
./build-ci/RdhRender_artefacts/Release/RdhRender \
    test_presets/test-4op-algo0-chain.rdhs smoke.wav --secs 1 --poly 2
# smoke.wav が生成され、無音でなければ OK（CI は自動判定）
```
