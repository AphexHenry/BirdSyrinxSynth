"""Side-by-side spectrograms: python spec.py out.png file[:t0:t1[:label]] ..."""
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import soundfile as sf
from scipy import signal


def load(spec):
    parts = spec.split("::")
    path = parts[0]
    t0 = float(parts[1]) if len(parts) > 1 else 0.0
    t1 = float(parts[2]) if len(parts) > 2 else None
    label = parts[3] if len(parts) > 3 else path.split("/")[-1]
    x, sr = sf.read(path, always_2d=True)
    x = x.mean(axis=1)
    i0 = int(t0 * sr)
    i1 = int(t1 * sr) if t1 else len(x)
    return x[i0:i1], sr, label


out = sys.argv[1]
specs = sys.argv[2:]
fig, axes = plt.subplots(len(specs), 1, figsize=(14, 3.2 * len(specs)), squeeze=False)
for ax, s in zip(axes[:, 0], specs):
    x, sr, label = load(s)
    nper = int(0.006 * sr) // 2 * 2
    f, t, S = signal.spectrogram(x, sr, window="hann", nperseg=nper * 2, noverlap=nper * 2 - nper // 4, nfft=4096)
    SdB = 10 * np.log10(S + 1e-20)
    SdB -= SdB.max()
    ax.pcolormesh(t, f / 1000, SdB, vmin=-70, vmax=0, cmap="magma", shading="auto")
    ax.set_ylim(0, 12)
    ax.set_ylabel("kHz")
    ax.set_title(label, fontsize=10, loc="left")
axes[-1, 0].set_xlabel("time (s)")
fig.tight_layout()
fig.savefig(out, dpi=90)
