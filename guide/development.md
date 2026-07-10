# 開発フロー / Development Workflow

このリポジトリは **Issue 駆動**で開発しています。

## ブランチモデル

```
feature/issue-<N>-<slug>   … Issue 毎の作業ブランチ（dev から分岐）
        │  PR（CI 合格）
        ▼
       dev                 … 統合ブランチ（直接 push 可・force-push 禁止）
        │  PR（CI 合格）
        ▼
       main                … リリースブランチ（PR のみ・保護）
                              └→ push で UI モックを GitHub Pages へ自動デプロイ
```

- **main は直接 push できません**（ブランチ保護）。必ず PR + CI「Test & build」合格で結合します。
- 1 Issue = 1 ブランチ = 1 PR を基本にします。PR 本文に `Closes #<N>` を書いて Issue と紐付けます。

## Issue

- バグ報告・機能提案・性能改善はすべて [GitHub Issues](https://github.com/Ride-H/Rd-H_synth/issues) へ。
- ラベル: `bug` / `enhancement` / `performance` / `investigation` / `infrastructure` / `docs`、
  優先度 `priority:high|medium|low`、領域 `area:dsp|ui|midi|build`。
- 着手時はコメントで方針・受入条件を残してからブランチを切ります。

## CI（GitHub Actions）

| トリガ | 内容 |
|---|---|
| push（main / dev / feature/**）・PR | **Test & build**: CMake ビルド（JUCE 自動フェッチ）+ ヘッドレスレンダラーで実プリセットを描画し無音でないことを検証 |
| main への push | 上記合格後、`site/`（UI モック）を GitHub Pages へ自動デプロイ |
| タグ `v*` の push | VST3 / AU / Standalone を zip 化して GitHub Release に添付 |

## 変更時のチェックリスト

- [ ] ビルドが通る（`guide/building.md` の「CI と同じ検証」）
- [ ] 挙動が変わる変更は、ヘッドレスレンダラーでの前後比較（PCM / スペクトラム）を PR に記載
- [ ] デフォルト状態の音を変えない変更は、決定論プリセットの **PCM ビット一致**を確認
- [ ] README / guide の更新要否を確認（機能・API・挙動が変わったら更新）
- [ ] リアルタイム安全（オーディオスレッドでの確保・ロック・I/O なし）を維持

## コードスタイル

- 既存コードの流儀に合わせます（JUCE スタイル・4 スペース・メンバー初期化はヘッダー内）。
- コメントは「コードから読み取れない制約」のみ。課題・経緯の議論は Issue / PR に残します。

## ライセンス

貢献されたコードは AGPLv3（[`LICENSE`](../LICENSE)）の下で公開されます。
JUCE のデュアルライセンス条件（README 参照）にも留意してください。
