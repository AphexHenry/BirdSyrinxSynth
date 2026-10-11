"""Second-voice tracker: the strongest spectral peak that is neither the main voice nor its harmonics."""
import numpy as np
from scipy import signal
import track as T

def second_voice(x, sr, tr, fmin=1800.0, fmax=7000.0, rel_db=-18.0, min_ms=12):
    M, freqs = T.stft_mag(x, sr)
    L = 20 * np.log10(M + 1e-12)
    n = min(len(tr["f0"]), L.shape[0])
    f2 = np.full(n, np.nan); l2 = np.full(n, -200.0)
    band = (freqs >= fmin) & (freqs <= fmax)
    for i in range(n):
        if not tr["voiced"][i]:
            continue
        f1 = tr["f0"][i]; lv1 = tr["level_db"][i]
        row = L[i].copy()
        excl = np.abs(freqs - f1) < max(150.0, 0.06 * f1)
        for mult in (0.5, 2.0, 3.0, 1.5):
            excl |= np.abs(freqs - mult * f1) < 0.04 * mult * f1
        row[~band | excl] = -300
        pk, _ = signal.find_peaks(row)
        if len(pk) == 0:
            continue
        p = pk[np.argmax(row[pk])]
        if row[p] >= lv1 + rel_db:
            f2[i] = freqs[p]; l2[i] = row[p]
    # keep continuous runs only
    c = 1200 * np.log2(np.where(np.isnan(f2), 1.0, f2) / 440.0)
    ok = ~np.isnan(f2)
    ok[1:] &= ~(np.abs(np.diff(c)) > 60)
    runs = T.runs(ok)
    keep = np.zeros(n, bool)
    for a, b in runs:
        if b - a >= min_ms:
            keep[a:b] = True
    f2 = np.where(keep, f2, np.nan)
    amp2 = np.where(keep, 10 ** ((l2 - np.nanmax(tr["level_db"])) / 20.0), 0.0)
    return f2, amp2
