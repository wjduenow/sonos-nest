#!/usr/bin/env bash
# microWakeWord training environment bootstrap (local, RTX 4090 / WSL2).
set -e
cd /home/wesd/mww-train
source ~/miniconda3/etc/profile.d/conda.sh

echo "=== [1/6] create conda env (python 3.10) ==="
conda create -y -n mww python=3.10 >/dev/null 2>&1 || echo "(env may already exist)"
conda activate mww
python --version

echo "=== [2/6] pip upgrade ==="
pip install --upgrade pip -q

echo "=== [3/6] TensorFlow with CUDA (GPU) — the finicky one ==="
pip install -q "tensorflow[and-cuda]>=2.18"

echo "=== [4/6] verify TF sees the 4090 ==="
python - <<'PY'
import tensorflow as tf
gpus = tf.config.list_physical_devices('GPU')
print("TF", tf.__version__, "GPUs:", gpus)
assert gpus, "NO GPU visible to TensorFlow"
print("GPU OK")
PY

echo "=== [5/6] microWakeWord + training deps ==="
pip install -q -e ./micro-wake-word
pip install -q torch torchaudio
pip install -q piper-phonemize-cross==1.2.1
pip install -q "git+https://github.com/puddly/pymicro-features@puddly/minimum-cpp-version"
pip install -q "git+https://github.com/whatsnowplaying/audio-metadata@d4ebb238e6a401bb1a5aaaac60c9e2b3cb30929f"
pip install -q audiomentations

echo "=== [6/6] piper-sample-generator (positive-sample synth) ==="
[ -d piper-sample-generator ] || git clone --depth 1 https://github.com/rhasspy/piper-sample-generator >/dev/null 2>&1
# its default voice model (multi-speaker en_US) for generating varied pronunciations
mkdir -p piper-sample-generator/models
if [ ! -f piper-sample-generator/models/en_US-libritts_r-medium.pt ]; then
  curl -fsSL -o piper-sample-generator/models/en_US-libritts_r-medium.pt \
    "https://github.com/rhasspy/piper-sample-generator/releases/download/v2.0.0/en_US-libritts_r-medium.pt" \
    && echo "got piper voice model" || echo "WARN: piper voice model download failed (will retry later)"
fi

echo "=== torch GPU check ==="
python -c "import torch; print('torch', torch.__version__, 'cuda', torch.cuda.is_available())"

echo "=== SETUP COMPLETE ==="
