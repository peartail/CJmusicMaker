from pathlib import Path
import wave, json, hashlib
import numpy as np

folder = Path(__file__).resolve().parent.parent / 'build' / 'synth-demos'
files = sorted(folder.glob('[1-5]-*.wav'))
assert len(files) == 5, 'Render all five synth demos before comparing.'
signals = []
report = {'method': 'Unchanged application FourOsc presets, direct offline plugin processing; uniform 0.25 output gain, no per-file normalization. Same MIDI, velocity 100, 120 BPM, 12 seconds including tail.', 'files': [], 'pairs': []}
for path in files:
    with wave.open(str(path), 'rb') as w:
        rate, channels, width = w.getframerate(), w.getnchannels(), w.getsampwidth()
        raw = w.readframes(w.getnframes())
    packed = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3).astype(np.int32)
    pcm = packed[:, 0] | (packed[:, 1] << 8) | (packed[:, 2] << 16)
    pcm = (pcm ^ 0x800000) - 0x800000
    audio = pcm.reshape(-1, channels) / 8388608.0
    assert np.isfinite(audio).all() and 0.001 < np.max(np.abs(audio)) < 0.9999
    signals.append(audio)
    report['files'].append({'file': path.name, 'seconds': len(audio)/rate, 'peak': float(np.max(np.abs(audio))), 'rms': float(np.sqrt(np.mean(audio**2))), 'clipped_fraction': float(np.mean(np.abs(audio) >= 0.9999)), 'pcm_sha256': hashlib.sha256(raw).hexdigest()})
for i in range(len(signals)):
    for j in range(i + 1, len(signals)):
        report['pairs'].append({'a': files[i].stem, 'b': files[j].stem, 'identical': bool(np.array_equal(signals[i], signals[j])), 'mean_absolute_difference': float(np.mean(np.abs(signals[i]-signals[j])))})
assert all(not pair['identical'] for pair in report['pairs'])
peak = max(float(np.max(np.abs(a))) for a in signals)
gain = min(1.0, 0.8/peak)
report['combined_uniform_gain'] = gain
combined = []
for a in signals:
    combined += [a * gain, np.zeros((rate, 2))]
pcm16 = (np.clip(np.concatenate(combined), -1, 1) * 32767).astype('<i2')
with wave.open(str(folder/'all-synths-comparison.wav'), 'wb') as w:
    w.setnchannels(2); w.setsampwidth(2); w.setframerate(rate); w.writeframes(pcm16.tobytes())
(folder/'comparison.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
print(json.dumps(report, indent=2))
