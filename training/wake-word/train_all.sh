#!/usr/bin/env bash
source ~/miniconda3/etc/profile.d/conda.sh && conda activate mww
cd /home/wesd/mww-train/work
export TF_FORCE_GPU_ALLOW_GROWTH=true

# Every phrase trains against the others as hard negatives, so ALL xneg features must exist before
# ANY training starts. Keep this list in sync with gen_positives.sh and train_phrase.py's sibling
# lists (see README "Adding a new phrase" — there is no single source of truth).
LABELS=(kinder_bedtime kinder_wakeup kinder_riseandshine)
for label in "${LABELS[@]}"; do
  if [ ! -d "features_xneg_$label" ]; then
    echo "missing features_xneg_$label — run: python gen_clean_xneg.py $label"; exit 1
  fi
done
echo "xneg features present for ${#LABELS[@]} phrases — training (clean cross-negs, weight 6+12)"
for label in "${LABELS[@]}"; do
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
