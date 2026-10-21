#!/usr/bin/env python3
"""slim_onnx.py — reduce los nodos `Constant` de un grafo ONNX.

El grafo de EQTransformer exportado por torch tiene ~8 300 nodos `Constant` (los
pesos y los parámetros de Slice/Clip van como nodos, no como initializers). ORT
recorre el grafo al crear la sesión y eso explica el minuto de arranque.

Este script:
  1. convierte cada `Constant` en un initializer;
  2. deduplica constantes con el mismo dtype/forma/bytes (hay muchos repetidos);
  3. reescribe los inputs de los nodos;
  4. guarda el grafo y reporta el cambio.

No cambia la semántica: sólo mueve datos del cuerpo del grafo a initializers.

    python3 tools/slim_onnx.py eqt.onnx eqt_slim.onnx
"""

import sys

import numpy as np
import onnx
from onnx import TensorProto, helper, numpy_helper


def const_tensor(node):
    """Devuelve el TensorProto de un nodo Constant, o None."""
    for a in node.attribute:
        if a.name == "value":
            return a.t
        if a.name == "value_ints":
            return helper.make_tensor(node.output[0], TensorProto.INT64,
                                      [len(a.ints)], list(a.ints))
        if a.name == "value_floats":
            return helper.make_tensor(node.output[0], TensorProto.FLOAT,
                                      [len(a.floats)], list(a.floats))
        if a.name == "value_strings":
            return helper.make_tensor(node.output[0], TensorProto.STRING,
                                      [len(a.strings)], list(a.strings))
    return None


def tensor_key(t):
    """Clave de deduplicación: dtype + forma + bytes (sin el nombre)."""
    arr = numpy_helper.to_array(t)
    return (arr.dtype.str, arr.shape, arr.tobytes())


def main():
    if len(sys.argv) != 3:
        sys.exit("uso: slim_onnx.py <in.onnx> <out.onnx>")

    src, dst = sys.argv[1], sys.argv[2]
    m = onnx.load(src)
    g = m.graph

    n_const = sum(1 for n in g.node if n.op_type == "Constant")
    n_before = len(g.node)
    n_init_before = len(g.initializer)

    # 1) constantes a tensores, con clave de deduplicación
    canon = {}          # clave -> nombre del initializer
    repl = {}           # nombre original -> nombre canónico
    new_inits = []

    for n in g.node:
        if n.op_type != "Constant" or len(n.output) != 1:
            continue
        t = const_tensor(n)
        if t is None:
            continue
        key = tensor_key(t)
        if key not in canon:
            name = f"c_{len(canon)}"
            canon[key] = name
            new_inits.append(numpy_helper.from_array(
                numpy_helper.to_array(t), name=name))
        repl[n.output[0]] = canon[key]

    # 2) quitar los Constant convertidos y reescribir inputs
    keep = []
    for n in g.node:
        if n.op_type == "Constant" and len(n.output) == 1 and n.output[0] in repl:
            continue
        for i, name in enumerate(n.input):
            if name in repl:
                n.input[i] = repl[name]
        keep.append(n)

    # 3) conservar los initializers originales (pesos) y añadir los únicos
    orig = list(g.initializer)
    del g.initializer[:]
    g.initializer.extend(orig)
    g.initializer.extend(new_inits)

    del g.node[:]
    g.node.extend(keep)

    del g.value_info[:]          # se recalcula al cargar; abarata el archivo
    m = onnx.shape_inference.infer_shapes(m)
    onnx.checker.check_model(m)
    onnx.save(m, dst)

    print(f"nodos:        {n_before} -> {len(g.node)} "
          f"(Constant {n_const} -> {sum(1 for n in g.node if n.op_type == 'Constant')})")
    print(f"initializers: {n_init_before} -> {len(g.initializer)} "
          f"(constantes únicas: {len(new_inits)})")
    print(f"escrito {dst}")


if __name__ == "__main__":
    main()
