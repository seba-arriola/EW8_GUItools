#!/usr/bin/env python3
"""export_onnx.py — exporta EQTransformer (SeisBench/PyTorch) a ONNX para pickml.

Este script es la ÚNICA pieza con Python del proyecto y se ejecuta A MANO, una
vez, fuera del runtime: el binario `pickml` sólo consume el `.onnx` con ONNX
Runtime (API C). No se instala nada en el arranque del sistema.

Qué garantiza el grafo exportado (contrato con pickml_engine_onnx.c):

    entrada  : float32[B, 3, 6000]   canales en orden Z, N, E, 100 Hz
    salida   : float32[B, 6000, 3]   (prob P, prob S, prob detección) por muestra

La normalización (norm="std" de EQTransformer) viaja DENTRO del grafo: pickml
pasa las muestras CRUDAS del tank/store y no filtra ni normaliza. El blinding,
el solape y los umbrales NO están en el grafo: los hace el C.

Uso típico (entorno aparte, con torch + seisbench + onnx):

    python3 -m venv ~/.venvs/pickml
    . ~/.venvs/pickml/bin/activate
    pip install "torch==2.4.*" --index-url https://download.pytorch.org/whl/cpu
    pip install "seisbench==0.10.*" onnx onnxruntime
    python3 tools/export_onnx.py --out /ruta/eqt.onnx

Y para dejar el fixture dorado del gate (una ventana real de 60 s):

    python3 tools/export_onnx.py --out /ruta/eqt.onnx \
        --fixture-tank /ruta/dia.tank --fixture-t0 1700000000 \
        --fixture-dir test/fixtures

`test/fixtures/win1.tank` + `win1.ref.p.f32` + `win1.ref.s.f32` permiten que
`make check-engine` compare el C contra esta referencia.
"""

import argparse
import hashlib
import json
import struct
import sys

DEFAULT_WINDOW = {"eqt": 6000, "phasenet": 3001}
WINDOW = DEFAULT_WINDOW["eqt"]   # main() lo fija segun --arch/--window
RATE = 100.0
HEADER = struct.Struct("<iiddd7s9s4s3s2s3s2s2s")


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def model_forward(model, x, arch, order=(1, 2, 0)):
    """Normaliza la salida del modelo a [B, T, 3] = (P, S, detección).

    EQTransformer (seisbench 0.10): entrada [B,3,T] channel-major y salida
    TUPLA (Detection, P, S). PhaseNet: entrada [B,3,T] y salida [B,3,T], pero el
    ORDEN DE LAS CLASES no es estable entre pesos (ver `detect_order`). El C
    espera (P, S, detección) en el último eje.
    """
    import torch

    y = model(x)

    if arch == "phasenet":
        if isinstance(y, (tuple, list)) or y.ndim != 3 or y.shape[1] != 3:
            raise RuntimeError(f"PhaseNet: salida inesperada {type(y).__name__}")
        p, s = y[:, order[0]], y[:, order[1]]
        det = torch.maximum(p, s)   # PhaseNet no tiene curva de detección
        return torch.stack([p, s, det], dim=-1)

    if isinstance(y, (tuple, list)):
        if len(y) != 3:
            raise RuntimeError(
                f"salida con {len(y)} elementos (esperaba Detection,P,S)")
        det, p, s = y
        return torch.stack([p, s, det], dim=-1)
    if y.shape[-1] == 3:
        return y
    if y.shape[1] == 3:
        return y.permute(0, 2, 1)
    raise RuntimeError(f"salida de forma inesperada {tuple(y.shape)}")


def detect_order(model, arch):
    """Ubica P y S en la salida cruda de PhaseNet: (p_idx, s_idx, n_idx).

    SeisBench NO garantiza el orden de las clases y los pesos no lo declaran.
    Medido en 0.10.2: `original` sale (N,P,S) y `jma_wc` (PhaseNetWC) sale
    (P,S,N). Sobre ruido la clase N domina siempre, asi que se localiza por
    media maxima y P/S quedan en orden de indice (el orden relativo P->S es el
    mismo en ambas convenciones).
    """
    import torch

    if arch != "phasenet":
        return (0, 1, 2), []

    g = torch.Generator().manual_seed(0)
    x = torch.randn(1, 3, WINDOW, generator=g)
    with torch.no_grad():
        y = model(model.annotate_batch_pre(x, {}))
    if isinstance(y, (tuple, list)) or y.ndim != 3 or y.shape[1] != 3:
        raise RuntimeError(f"PhaseNet: salida inesperada "
                           f"{tuple(getattr(y, 'shape', ()))}")
    means = [float(y[:, k].mean()) for k in range(3)]
    n = max(range(3), key=lambda k: means[k])
    rest = [k for k in range(3) if k != n]
    return (rest[0], rest[1], n), means


def detect_layout(model, arch):
    """Comprueba empíricamente si el modelo quiere [B,3,T] o [B,T,3]."""
    import torch

    last = None
    for name, shape in (("chan_major", (1, 3, WINDOW)),
                        ("time_major", (1, WINDOW, 3))):
        try:
            with torch.no_grad():
                out = model_forward(model, torch.zeros(*shape), arch)
            return name, tuple(out.shape)
        except Exception as e:          # noqa: BLE001 - se prueban las dos
            last = e
    raise RuntimeError(f"no se pudo determinar el layout de entrada: {last}")


def export(model, out, opset, layout, arch, order=(1, 2, 0)):
    import torch
    import torch.nn as nn

    class Wrap(nn.Module):
        """Fija el contrato: [B,3,T] -> [B,T,3] = (P,S,detección).

        Aplica la normalización estándar de SeisBench (`annotate_batch_pre`).
        Sin ella el modelo ve CUENTAS CRUDAS del tank/store y las curvas salen
        mal: medido en el sismo de CX.PB15, P=0.446 en vez de 0.924 y el arribo
        S etiquetado como P (P=0.339 sobre S=0.207).
        """

        def __init__(self, m, time_major, arch, order):
            super().__init__()
            self.m = m
            self.time_major = time_major
            self.arch = arch
            self.order = order

        def forward(self, x):
            if self.time_major:
                x = x.permute(0, 2, 1)
            pre = self.m.annotate_batch_pre(x, {})
            if isinstance(pre, tuple):      # (batch, piggyback)
                pre = pre[0]
            return model_forward(self.m, pre, self.arch, self.order)

    w = Wrap(model, layout == "time_major", arch, order).eval()
    dummy = torch.zeros(1, 3, WINDOW)
    with torch.no_grad():
        y = w(dummy)
    if tuple(y.shape) != (1, WINDOW, 3):
        raise RuntimeError(f"salida inesperada del wrapper: {tuple(y.shape)}")

    torch.onnx.export(
        w, dummy, out,
        input_names=["waveform"], output_names=["psd"],
        dynamic_axes={"waveform": {0: "batch"}, "psd": {0: "batch"}},
        opset_version=opset, do_constant_folding=True,
    )
    return w


def verify(onnx_path, wrapper):
    """Compara onnxruntime contra PyTorch; devuelve max|dif| (o None si no hay ORT)."""
    try:
        import numpy as np
        import onnxruntime as ort
        import torch
    except ImportError:
        return None

    rng = np.random.default_rng(0)
    x = rng.standard_normal((2, 3, WINDOW)).astype(np.float32) * 1000.0

    sess = ort.InferenceSession(onnx_path, providers=["CPUExecutionProvider"])
    got = sess.run(None, {sess.get_inputs()[0].name: x})[0]
    with torch.no_grad():
        ref = wrapper(torch.from_numpy(x)).numpy()
    return float(np.max(np.abs(got - ref)))


# --------------------------------------------------------------------- fixture

def read_tank(path):
    """Itera (header, muestras) de un tank TRACE2_HEADER + int32."""
    with open(path, "rb") as f:
        while True:
            raw = f.read(HEADER.size)
            if len(raw) < HEADER.size:
                return
            pin, nsamp, t0, _t1, rate, sta, net, chan, loc, _v, dtype, _q, _p = \
                HEADER.unpack(raw)
            if nsamp <= 0 or nsamp > 200000:
                return
            data = f.read(nsamp * 4)
            if len(data) < nsamp * 4:
                return
            yield {
                "nsamp": nsamp, "t0": t0, "rate": rate,
                "sta": sta.rstrip(b"\0").decode(), "net": net.rstrip(b"\0").decode(),
                "chan": chan.rstrip(b"\0").decode(), "loc": loc.rstrip(b"\0").decode(),
                "x": struct.unpack(f"<{nsamp}i", data),
            }


def write_tank(path, recs):
    with open(path, "wb") as f:
        for r in recs:
            n = len(r["x"])
            hdr = HEADER.pack(
                0, n, r["t0"], r["t0"] + (n - 1) / RATE, RATE,
                r["sta"].encode(), r["net"].encode(), r["chan"].encode(),
                r["loc"].encode(), b"20", b"i4", b"", b"",
            )
            f.write(hdr)
            f.write(struct.pack(f"<{n}i", *r["x"]))


def build_fixture(tank, t0, wrapper, outdir, prefix="win1"):
    """Extrae una ventana 3C completa y su referencia P/S de PyTorch.

    Alinea por TIEMPO, no por orden de lectura: en un stream real cada
    componente puede empezar en un instante distinto (se han visto 6 s de
    diferencia) y concatenar por componente desplazaría unas respecto de otras.
    """
    import numpy as np
    import torch

    grid = {c: [None] * WINDOW for c in "ZNE"}
    got = {c: 0 for c in "ZNE"}

    for r in read_tank(tank):
        key = r["chan"][-1]
        if key not in grid:
            continue
        idx0 = int(round((r["t0"] - t0) * RATE))
        for i, v in enumerate(r["x"]):
            k = idx0 + i
            if 0 <= k < WINDOW and grid[key][k] is None:
                grid[key][k] = v
                got[key] += 1

    missing = [c for c in "ZNE" if got[c] < WINDOW]
    if missing:
        raise RuntimeError(
            f"la ventana no cubre {WINDOW} muestras de {missing} "
            f"(cobertura {got}) en {tank}")

    zne = np.stack([np.asarray(grid[c], dtype=np.float32) for c in "ZNE"])
    with torch.no_grad():
        ref = wrapper(torch.from_numpy(zne[None])).numpy()[0]   # [6000,3]

    import os
    os.makedirs(outdir, exist_ok=True)
    sta = "TEST"
    recs = [{"t0": t0, "sta": sta, "net": "XX", "loc": "--",
             "chan": "HH" + c, "x": grid[c]} for c in "ZNE"]
    write_tank(os.path.join(outdir, prefix + ".tank"), recs)

    # float32 por curva, sin cabecera: lo lee test/test_engine.sh
    ref[:, 0].astype("<f4").tofile(os.path.join(outdir, prefix + ".ref.p.f32"))
    ref[:, 1].astype("<f4").tofile(os.path.join(outdir, prefix + ".ref.s.f32"))
    print(f"fixture: {outdir}/{prefix}.tank + {prefix}.ref.{{p,s}}.f32 "
          f"(P max {ref[:,0].max():.3f} en {int(ref[:,0].argmax())}, "
          f"S max {ref[:,1].max():.3f} en {int(ref[:,1].argmax())})")


# ------------------------------------------------------------------------ main

def main():
    global WINDOW

    ap = argparse.ArgumentParser(
        description="Exporta un picker de SeisBench (EQTransformer/PhaseNet) a ONNX")
    ap.add_argument("--arch", choices=("eqt", "phasenet"), default="eqt")
    ap.add_argument("--out", default=None,
                    help="ruta del .onnx (default: <arch>.onnx)")
    ap.add_argument("--model", default="original", help="variante de pesos de SeisBench")
    ap.add_argument("--opset", type=int, default=17)
    ap.add_argument("--channel-order", choices=("auto", "nps", "psn"), default="auto",
                    help="orden de clases de PhaseNet; auto lo detecta del modelo")
    ap.add_argument("--window", type=int, default=None,
                    help="muestras por ventana (default: la nativa del modelo)")
    ap.add_argument("--fixture-tank", help="tank del que extraer la ventana dorada")
    ap.add_argument("--fixture-t0", type=float, help="epoch de inicio de la ventana")
    ap.add_argument("--fixture-dir", default="test/fixtures")
    ap.add_argument("--fixture-prefix", default=None)
    args = ap.parse_args()

    out = args.out or ("eqt.onnx" if args.arch == "eqt" else "phasenet.onnx")
    WINDOW = args.window or DEFAULT_WINDOW[args.arch]

    try:
        import seisbench
        import torch
        from seisbench.models import EQTransformer, PhaseNet
    except ImportError as e:
        sys.exit(f"export_onnx: faltan dependencias ({e}). Ver el docstring.")

    print(f"seisbench {seisbench.__version__} / torch {torch.__version__}")
    cls = EQTransformer if args.arch == "eqt" else PhaseNet
    model = cls.from_pretrained(args.model)
    model.eval()
    print(f"arch {args.arch} / ventana {WINDOW} muestras ({WINDOW / RATE:.2f} s) "
          f"norm={getattr(model, 'norm', None)}")

    layout, shape = detect_layout(model, args.arch)
    print(f"layout de entrada detectado: {layout} (salida modelo {shape})")

    if args.arch == "phasenet":
        if args.channel_order == "nps":
            order = (1, 2, 0)
        elif args.channel_order == "psn":
            order = (0, 1, 2)
        else:
            order, order_means = detect_order(model, args.arch)
            print("orden de clases detectado: P=%d S=%d N=%d (medias %s)"
                  % (order[0], order[1], order[2],
                     " ".join(f"{m:.3f}" for m in order_means)))
    else:
        order = None

    wrapper = export(model, out, args.opset, layout, args.arch, order)
    diff = verify(out, wrapper)

    meta = {
        "artifact": out,
        "sha256": sha256(out),
        "arch": args.arch,
        "opset": args.opset,
        "seisbench": seisbench.__version__,
        "torch": torch.__version__,
        "weights": args.model,
        "channel_order": list(order) if order else None,
        "input": {"name": "waveform", "shape": ["batch", 3, WINDOW],
                  "layout": "ZNE", "dtype": "float32"},
        "output": {"name": "psd", "shape": ["batch", WINDOW, 3],
                   "classes": ["P", "S", "detection"]},
        "sampling_rate": RATE,
        "window_samples": WINDOW,
        "normalization": "incluida en el grafo (annotate_batch_pre)",
        "max_abs_diff_vs_torch": diff,
    }
    with open(out + ".json", "w") as f:
        json.dump(meta, f, indent=2, sort_keys=True)
        f.write("\n")

    print(f"exportado {out} ({meta['sha256'][:16]}…)")
    if diff is not None:
        print(f"max|onnx - torch| = {diff:.3e}")

    if args.fixture_tank:
        if args.fixture_t0 is None:
            sys.exit("--fixture-tank requiere --fixture-t0")
        prefix = args.fixture_prefix or ("win1" if args.arch == "eqt" else "pn1")
        build_fixture(args.fixture_tank, args.fixture_t0, wrapper,
                      args.fixture_dir, prefix)


if __name__ == "__main__":
    main()
