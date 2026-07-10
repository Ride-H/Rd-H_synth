#!/usr/bin/env python3
"""
音出し確認用テストプリセット (.rdhs) 生成。
形式: APVTS ValueTree XML, root=<Parameters rdh_version="2.0">, 子=<PARAM id value/>。
新パラメーターは GUI 未接続のため、ここで値を仕込んで Standalone でロードして鳴らす。

分離のコツ: amp(OSC) を sustain=0/decay 短で減衰させ、FM/Noise の独立 EG を
sustain させることで、保持中は新機能だけが鳴る。
"""
import os

# --- 全パラメーター既定値（createParameterLayout と一致）---
DEFAULTS = {
    "waveType": 1, "waveType2": 1, "osc2Enable": 0, "waveType3": 2, "osc3Enable": 0,
    "attack": 0.005, "decay": 0.2, "sustain": 0.6, "release": 0.3,
    "filterCutoff": 8000.0, "filterResonance": 1.0,
    "unisonVoices": 1, "unisonDetune": 0.0,
    "pitchEnvAmount": 0.0, "pitchEnvAttack": 0.01,
    "volume": 0.7,
    "fm_enable": 0, "fm_output_level": 0.5, "fm_algorithm": 0, "fm_feedback": 0.0,
    "fm_op1_ratio": 1.0, "fm_op1_detune": 0.0, "fm_op1_level": 0.7, "fm_op1_attack": 0.001,
    "fm_op1_decay": 0.2, "fm_op1_sustain": 0.7, "fm_op1_release": 0.3, "fm_op1_vel_sens": 0.5, "fm_op1_key_scale": 0.0,
    "fm_op2_ratio": 2.0, "fm_op2_detune": 0.0, "fm_op2_level": 0.5, "fm_op2_attack": 0.001,
    "fm_op2_decay": 0.3, "fm_op2_sustain": 0.5, "fm_op2_release": 0.4, "fm_op2_vel_sens": 0.5, "fm_op2_key_scale": 0.0,
    "fm_op3_ratio": 1.0, "fm_op3_detune": 0.0, "fm_op3_level": 0.0, "fm_op3_attack": 0.001,
    "fm_op3_decay": 0.2, "fm_op3_sustain": 0.7, "fm_op3_release": 0.3, "fm_op3_vel_sens": 0.5, "fm_op3_key_scale": 0.0,
    "fm_op4_ratio": 1.0, "fm_op4_detune": 0.0, "fm_op4_level": 0.0, "fm_op4_attack": 0.001,
    "fm_op4_decay": 0.3, "fm_op4_sustain": 0.5, "fm_op4_release": 0.4, "fm_op4_vel_sens": 0.5, "fm_op4_key_scale": 0.0,
    "noise_enable": 0, "noise_type": 0, "noise_level": 0.5, "noise_filter_send": 1.0, "noise_direct_out": 0.0,
    "noise_eg_attack": 0.001, "noise_eg_decay": 0.1, "noise_eg_sustain": 0.0, "noise_eg_release": 0.2,
}

# 全テスト共通: OSC を減衰させ、明るいフィルター
TEST_BASE = {
    "waveType": 0, "attack": 0.005, "decay": 0.08, "sustain": 0.0, "release": 0.25,
    "filterCutoff": 18000.0, "filterResonance": 1.0, "volume": 0.8,
}

def fm_ops(levels, ratios, sustain=1.0):
    """4 オペレーターの level/ratio/sustain を一括設定。"""
    d = {}
    for i, (lv, rt) in enumerate(zip(levels, ratios), start=1):
        d[f"fm_op{i}_level"] = lv
        d[f"fm_op{i}_ratio"] = rt
        d[f"fm_op{i}_sustain"] = sustain
        d[f"fm_op{i}_decay"] = 0.5
        d[f"fm_op{i}_release"] = 0.3
    return d

NOISE_BASE = {
    "noise_enable": 1, "noise_level": 0.8, "noise_filter_send": 0.0, "noise_direct_out": 1.0,
    "noise_eg_attack": 0.005, "noise_eg_decay": 0.1, "noise_eg_sustain": 1.0, "noise_eg_release": 0.3,
}
FM_BASE = {"fm_enable": 1, "fm_output_level": 0.8}

PRESETS = {
    "test-white":        {**NOISE_BASE, "noise_type": 0},
    "test-pink":         {**NOISE_BASE, "noise_type": 1},
    "test-brown":        {**NOISE_BASE, "noise_type": 2},
    "test-4op-algo0-chain":    {**FM_BASE, "fm_algorithm": 0, **fm_ops([0.8, 0.6, 0.6, 0.6], [1, 2, 3, 4])},
    "test-4op-algo1-parallel": {**FM_BASE, "fm_algorithm": 1, **fm_ops([0.8, 0.7, 0.6, 0.6], [1, 1.01, 2, 3])},
    "test-4op-algo2-2to1":     {**FM_BASE, "fm_algorithm": 2, **fm_ops([0.8, 0.6, 0.6, 0.6], [1, 2, 3, 5])},
    "test-4op-algo3-additive": {**FM_BASE, "fm_algorithm": 3, **fm_ops([0.7, 0.4, 0.3, 0.2], [1, 2, 3, 4])},
    "test-fm-feedback":        {**FM_BASE, "fm_algorithm": 0, "fm_feedback": 0.85, **fm_ops([0.8, 0.4, 0.4, 0.7], [1, 2, 3, 1])},
}

def fmt(v):
    if isinstance(v, bool):
        return "1.0" if v else "0.0"
    if isinstance(v, int):
        return f"{float(v)}"
    return repr(float(v))

def build_xml(overrides):
    params = dict(DEFAULTS)
    params.update(TEST_BASE)
    params.update(overrides)
    lines = ['<?xml version="1.0" encoding="UTF-8"?>',
             '', '<Parameters rdh_version="2.0">']
    for pid, val in params.items():
        lines.append(f'  <PARAM id="{pid}" value="{fmt(val)}"/>')
    lines.append('</Parameters>')
    return "\n".join(lines) + "\n"

def main():
    here = os.path.dirname(os.path.abspath(__file__))
    for name, ov in PRESETS.items():
        path = os.path.join(here, name + ".rdhs")
        with open(path, "w", encoding="utf-8") as f:
            f.write(build_xml(ov))
        print("wrote", os.path.basename(path))


# import 時は副作用なし（Tools/analyze_sprint1.py が DEFAULTS/build_xml を再利用するため）
if __name__ == "__main__":
    main()
