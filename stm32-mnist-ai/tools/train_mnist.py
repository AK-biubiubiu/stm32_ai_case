#!/usr/bin/env python3
"""
MNIST -> INT8 MLP (784-64-10) trainer / quantizer for STM32F103.

Outputs
-------
Model/model_data.h          int8 weights + per-tensor scales + float biases
tools/data/test_images.bin  N x 784 uint8, raw MNIST test images
tools/data/test_labels.bin  N uint8 ground truth
tools/data/test_pred.bin    N uint8 labels predicted by the int8 simulation
                            (used by host_test.c to cross-check the C engine)

Quantisation scheme (must stay in sync with App/ai_mlp.c)
--------------------------------------------------------
    x_q   = round_half_away(x * 127),  x in [0,1]   -> 0..127, x_scale = 1/127
    w_q   = round_half_away(w / w_scale)           -> -127..127, symmetric
    acc   = sum(w_q * x_q)                         int32, exact
    real  = acc * (w_scale * x_scale)
    h_q   = clamp(round_half_away(relu(real) / h_scale), 0, 127)
    logit = acc2 * (w2_scale * h_scale) + b2       float32
"""

import os
import sys
import urllib.request

import numpy as np

# ----------------------------------------------------------------------------
# config
# ----------------------------------------------------------------------------
HIDDEN = 96
EPOCHS = 16
BATCH = 128
LR = 1e-3
SEED = 20261107

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
DATA_DIR = os.path.join(HERE, "data")
MODEL_DIR = os.path.join(ROOT, "Model")
MNIST_NPZ = os.path.join(DATA_DIR, "mnist.npz")
MNIST_URL = "https://storage.googleapis.com/tensorflow/tf-keras-datasets/mnist.npz"

N_TEST_EXPORT = 1000  # samples exported for the C host test


# ----------------------------------------------------------------------------
# helpers
# ----------------------------------------------------------------------------
def round_half_away(v):
    """Same rounding as round_f2i() in ai_mlp.c."""
    v = np.asarray(v, dtype=np.float32)
    return np.where(v >= 0, np.floor(v + 0.5), np.ceil(v - 0.5)).astype(np.int32)


def load_mnist():
    os.makedirs(DATA_DIR, exist_ok=True)
    if not os.path.exists(MNIST_NPZ):
        print("[data] downloading mnist.npz ...")
        urllib.request.urlretrieve(MNIST_URL, MNIST_NPZ)
    d = np.load(MNIST_NPZ)
    xtr = d["x_train"].astype(np.float32).reshape(-1, 784) / 255.0
    ytr = d["y_train"].astype(np.int64)
    xte = d["x_test"].astype(np.float32).reshape(-1, 784) / 255.0
    yte = d["y_test"].astype(np.int64)
    return xtr, ytr, xte, yte


def affine_batch(imgs, rng, max_rot=10.0, scale_lo=0.85, scale_hi=1.15, shift=2.0):
    """Per-image rotate / scale / translate, nearest neighbour, zero fill."""
    b = imgs.shape[0]
    ang = np.deg2rad(rng.uniform(-max_rot, max_rot, b)).astype(np.float32)
    sc = rng.uniform(scale_lo, scale_hi, b).astype(np.float32)
    tx = rng.uniform(-shift, shift, b).astype(np.float32)
    ty = rng.uniform(-shift, shift, b).astype(np.float32)

    yy, xx = np.mgrid[0:28, 0:28].astype(np.float32)
    xx -= 13.5
    yy -= 13.5
    out = np.zeros_like(imgs)
    c = np.cos(ang)
    s = np.sin(ang)
    for i in range(b):
        dx = xx - tx[i]
        dy = yy - ty[i]
        sx = (c[i] * dx + s[i] * dy) / sc[i] + 13.5
        sy = (-s[i] * dx + c[i] * dy) / sc[i] + 13.5
        xi = np.clip(round_half_away(sx), 0, 27)
        yi = np.clip(round_half_away(sy), 0, 27)
        inside = (sx >= -0.5) & (sx <= 27.5) & (sy >= -0.5) & (sy <= 27.5)
        out[i] = imgs[i][yi, xi] * inside
    return out


def _shift_max(a):
    m = a
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            m = np.maximum(m, np.roll(np.roll(a, dy, axis=1), dx, axis=2))
    return m


def _shift_min(a):
    m = a
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            m = np.minimum(m, np.roll(np.roll(a, dy, axis=1), dx, axis=2))
    return m


def augment(imgs, rng):
    """Geometric jitter + stroke thickness variation (closes the domain gap
    between MNIST and a fat resistive-touch stroke)."""
    out = affine_batch(imgs, rng)
    n = out.shape[0]
    pick = rng.random(n)
    thick = pick < 0.30
    thin = (pick >= 0.30) & (pick < 0.50)
    if thick.any():
        out[thick] = _shift_max(out[thick])
    if thin.any():
        out[thin] = _shift_min(out[thin])
    return np.clip(out, 0.0, 1.0)


# ----------------------------------------------------------------------------
# model
# ----------------------------------------------------------------------------
def init_params(rng):
    w1 = (rng.standard_normal((784, HIDDEN)) * np.sqrt(2.0 / 784)).astype(np.float32)
    b1 = np.zeros(HIDDEN, dtype=np.float32)
    w2 = (rng.standard_normal((HIDDEN, 10)) * np.sqrt(2.0 / HIDDEN)).astype(np.float32)
    b2 = np.zeros(10, dtype=np.float32)
    return [w1, b1, w2, b2]


def forward(params, x):
    w1, b1, w2, b2 = params
    h = x @ w1 + b1
    h = np.maximum(h, 0.0)
    logits = h @ w2 + b2
    return h, logits


def softmax(z):
    z = z - z.max(axis=1, keepdims=True)
    e = np.exp(z)
    return e / e.sum(axis=1, keepdims=True)


def train(x, y, rng):
    params = init_params(rng)
    m = [np.zeros_like(p) for p in params]
    v = [np.zeros_like(p) for p in params]
    beta1, beta2, eps = 0.9, 0.999, 1e-8
    step = 0
    n = x.shape[0]

    for ep in range(EPOCHS):
        idx = rng.permutation(n)
        total_loss = 0.0
        for start in range(0, n, BATCH):
            bi = idx[start:start + BATCH]
            xb = augment(x[bi].reshape(-1, 28, 28), rng).reshape(len(bi), 784)
            yb = y[bi]

            h, logits = forward(params, xb)
            p = softmax(logits)
            total_loss += -np.log(p[np.arange(len(bi)), yb] + 1e-9).mean()

            dlog = p.copy()
            dlog[np.arange(len(bi)), yb] -= 1.0
            dlog /= len(bi)

            dh = (dlog @ params[2].T) * (h > 0)
            grad = [
                xb.T @ dh,
                dh.sum(axis=0),
                h.T @ dlog,
                dlog.sum(axis=0),
            ]

            step += 1
            lr_t = LR * np.sqrt(1 - beta2 ** step) / (1 - beta1 ** step)
            for i in range(4):
                m[i] = beta1 * m[i] + (1 - beta1) * grad[i]
                v[i] = beta2 * v[i] + (1 - beta2) * (grad[i] ** 2)
                params[i] -= lr_t * m[i] / (np.sqrt(v[i]) + eps)

        acc = evaluate(params, x[:5000], y[:5000])
        print(f"[train] epoch {ep + 1:2d}/{EPOCHS}  loss={total_loss / (n / BATCH):.4f}  "
              f"train-acc={acc * 100:.2f}%", flush=True)
    return params


def evaluate(params, x, y):
    _, logits = forward(params, x)
    return float((logits.argmax(axis=1) == y).mean())


# ----------------------------------------------------------------------------
# quantisation
# ----------------------------------------------------------------------------
def quantize(params, calib_x):
    w1, b1, w2, b2 = params

    s1 = float(np.abs(w1).max() / 127.0)
    s2 = float(np.abs(w2).max() / 127.0)
    w1q = np.clip(round_half_away(w1 / s1), -127, 127).astype(np.int8)
    w2q = np.clip(round_half_away(w2 / s2), -127, 127).astype(np.int8)

    # calibrate the hidden activation scale on the 99.9th percentile
    xq = np.clip(round_half_away(calib_x * 127.0), 0, 127).astype(np.int32)
    acc1 = xq @ w1q.astype(np.int32)  # (N, HIDDEN)
    h = np.maximum(acc1.astype(np.float32) * (s1 / 127.0) + b1, 0.0)
    h_scale = float(np.percentile(h, 99.9)) / 127.0
    if h_scale <= 0:
        h_scale = 1e-6

    return w1q, b1, w2q, b2, s1, s2, h_scale


def infer_int8(x_uint8, w1q, b1, w2q, b2, s1, s2, h_scale):
    """Bit-exact mirror of ai_mlp.c."""
    x = x_uint8.astype(np.float32) / 255.0
    xq = np.clip(round_half_away(x * 127.0), 0, 127).astype(np.int32)
    acc1 = xq @ w1q.astype(np.int32)
    h = np.maximum(acc1.astype(np.float32) * (s1 * (1.0 / 127.0)) + b1, 0.0)
    hq = np.clip(round_half_away(h / h_scale), 0, 127).astype(np.int32)
    acc2 = hq @ w2q.astype(np.int32)
    logits = acc2.astype(np.float32) * (s2 * h_scale) + b2
    return logits


def predict_batch(x_uint8, *q):
    """Vectorised version of infer_int8 for the whole test set."""
    w1q, b1, w2q, b2, s1, s2, h_scale = q
    x = x_uint8.astype(np.float32) / 255.0
    xq = np.clip(round_half_away(x * 127.0), 0, 127).astype(np.int32)
    acc1 = xq @ w1q.astype(np.int32)
    h = np.maximum(acc1.astype(np.float32) * (s1 * (1.0 / 127.0)) + b1, 0.0)
    hq = np.clip(round_half_away(h / h_scale), 0, 127).astype(np.int32)
    acc2 = hq @ w2q.astype(np.int32)
    return acc2.astype(np.float32) * (s2 * h_scale) + b2


# ----------------------------------------------------------------------------
# export
# ----------------------------------------------------------------------------
def _fmt_f(v):
    return f"{np.float32(v):.10e}f"


def export_header(q, path):
    w1q, b1, w2q, b2, s1, s2, h_scale = q
    lines = []
    lines.append("/* Auto-generated by tools/train_mnist.py -- do not edit. */")
    lines.append("#ifndef MODEL_DATA_H")
    lines.append("#define MODEL_DATA_H")
    lines.append("")
    lines.append("#include <stdint.h>")
    lines.append("")
    lines.append(f"#define AI_INPUT_SIZE   {w1q.shape[0]}")
    lines.append(f"#define AI_HIDDEN_SIZE  {w1q.shape[1]}")
    lines.append(f"#define AI_CLASSES      {w2q.shape[1]}")
    lines.append("")
    lines.append(f"#define AI_X_SCALE      {_fmt_f(1.0 / 127.0)}")
    lines.append(f"#define AI_W1_SCALE     {_fmt_f(s1)}")
    lines.append(f"#define AI_W2_SCALE     {_fmt_f(s2)}")
    lines.append(f"#define AI_H_SCALE      {_fmt_f(h_scale)}")
    lines.append("")
    lines.append("/* row-major [AI_HIDDEN_SIZE][AI_INPUT_SIZE]: W1[j][i] */")
    lines.append("static const int8_t AI_W1[] = {")
    flat = np.ascontiguousarray(w1q.T).reshape(-1)
    for i in range(0, flat.size, 32):
        lines.append("  " + ",".join(str(int(v)) for v in flat[i:i + 32]) + ",")
    lines.append("};")
    lines.append("")
    lines.append("static const float AI_B1[AI_HIDDEN_SIZE] = {")
    for i in range(0, b1.size, 8):
        lines.append("  " + ",".join(_fmt_f(v) for v in b1[i:i + 8]) + ",")
    lines.append("};")
    lines.append("")
    lines.append("/* row-major [AI_CLASSES][AI_HIDDEN_SIZE]: W2[j][i] */")
    lines.append("static const int8_t AI_W2[] = {")
    flat = np.ascontiguousarray(w2q.T).reshape(-1)
    for i in range(0, flat.size, 32):
        lines.append("  " + ",".join(str(int(v)) for v in flat[i:i + 32]) + ",")
    lines.append("};")
    lines.append("")
    lines.append("static const float AI_B2[AI_CLASSES] = {")
    lines.append("  " + ",".join(_fmt_f(v) for v in b2) + ",")
    lines.append("};")
    lines.append("")
    lines.append("#endif /* MODEL_DATA_H */")
    lines.append("")
    with open(path, "w") as f:
        f.write("\n".join(lines))


def export_test_vectors(x_te_uint8, y_te, pred, n):
    with open(os.path.join(DATA_DIR, "test_images.bin"), "wb") as f:
        f.write(x_te_uint8[:n].astype(np.uint8).tobytes())
    with open(os.path.join(DATA_DIR, "test_labels.bin"), "wb") as f:
        f.write(y_te[:n].astype(np.uint8).tobytes())
    with open(os.path.join(DATA_DIR, "test_pred.bin"), "wb") as f:
        f.write(pred[:n].astype(np.uint8).tobytes())
    print(f"[export] {n} test vectors -> tools/data/*.bin")


# ----------------------------------------------------------------------------
# main
# ----------------------------------------------------------------------------
def main():
    np.random.seed(SEED)
    rng = np.random.default_rng(SEED)

    xtr, ytr, xte, yte = load_mnist()
    print(f"[data] train={xtr.shape} test={xte.shape}")

    params = train(xtr, ytr, rng)

    float_acc = evaluate(params, xte, yte)
    print(f"[float] test accuracy = {float_acc * 100:.2f}%")

    xte_u8 = (xte * 255.0).round().astype(np.uint8)
    calib = xtr[:5000]
    q = quantize(params, calib)

    logits_q = predict_batch(xte_u8, *q)
    pred_q = logits_q.argmax(axis=1)
    int8_acc = float((pred_q == yte).mean())

    _, logits_f = forward(params, xte)
    pred_f = logits_f.argmax(axis=1)
    agree = float((pred_f == pred_q).mean())

    print(f"[int8 ] test accuracy = {int8_acc * 100:.2f}%  (drop {(float_acc - int8_acc) * 100:+.2f} pp)")
    print(f"[int8 ] argmax agreement with float = {agree * 100:.2f}%")

    os.makedirs(MODEL_DIR, exist_ok=True)
    export_header(q, os.path.join(MODEL_DIR, "model_data.h"))
    print(f"[export] {os.path.join(MODEL_DIR, 'model_data.h')}")

    export_test_vectors(xte_u8, yte, pred_q, N_TEST_EXPORT)

    if int8_acc < 0.975:
        print(f"[WARN] int8 accuracy below 97.5% ({int8_acc * 100:.2f}%)", file=sys.stderr)
    if float_acc - int8_acc > 0.01:
        print("[WARN] quantisation drop > 1 pp", file=sys.stderr)

    return 0


if __name__ == "__main__":
    sys.exit(main())
