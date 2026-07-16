#!/usr/bin/env bash
source ~/miniconda3/etc/profile.d/conda.sh && conda activate mww
cd /home/wesd/mww-train/piper-sample-generator
M=models/en_US-libritts_r-medium.onnx
gen () {  # label, text
  out=/home/wesd/mww-train/work/samples_$1
  if [ -d "$out" ] && [ "$(ls $out/*.wav 2>/dev/null | wc -l)" -gt 1500 ]; then echo "$1 already generated"; return; fi
  echo "=== generating $1 : '$2' ==="
  python -m piper_sample_generator "$2" --model $M --max-samples 2000 --batch-size 50 \
    --max-speakers 100 --output-dir "$out" 2>&1 | grep -viE "DEBUG|phonemes=" | tail -3
  echo "=== $1 : $(ls $out/*.wav 2>/dev/null | wc -l) samples ==="
}
gen kinder_bedtime        "Kinnder Bedtime."
gen kinder_wakeup         "Kinnder Wake Up."
gen kinder_riseandshine   "Kinnder Rise and Shine."
echo "POSITIVES COMPLETE"
