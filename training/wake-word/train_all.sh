#!/usr/bin/env bash
source ~/miniconda3/etc/profile.d/conda.sh && conda activate mww
cd /home/wesd/mww-train/work
export TF_FORCE_GPU_ALLOW_GROWTH=true
echo "waiting for clean cross-neg (xneg) features..."
while [ ! -d features_xneg_kinder_riseandshine ]; do sleep 15; done
sleep 10   # let last write settle
echo "xneg features ready — retraining all 3 (clean cross-negs, weight 6+12)"
for label in kinder_bedtime kinder_wakeup kinder_riseandshine; do
  rm -rf trained_models/$label
  echo "===== TRAINING $label ====="
  python train_phrase.py $label > /home/wesd/mww-train/train_$label.log 2>&1
  f=trained_models/$label/tflite_stream_state_internal_quant/stream_state_internal_quant.tflite
  sz=$(stat -c%s "$f" 2>/dev/null || echo 0)
  if grep -q "Step #10000" /home/wesd/mww-train/train_$label.log && [ "${sz:-0}" -gt 1000 ]; then
    touch trained_models/$label/.full10k; echo "$label FULL ($sz bytes)"
  else echo "$label INCOMPLETE ($sz bytes)"; fi
done
echo "ALL_MODELS_DONE_V2"
