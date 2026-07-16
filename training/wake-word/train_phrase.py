# Train one microWakeWord phrase end-to-end (feature-gen -> config -> train -> export).
# Faithfully follows notebooks/basic_training_notebook.ipynb, parameterized per phrase.
# Usage:  python train_phrase.py <label>   (expects work/samples_<label>/ of positive wavs)
import os, sys, yaml, subprocess
# TF otherwise grabs ~all 24 GB VRAM on startup, starving cuDNN's handle
# (CUDNN_STATUS_NOT_INITIALIZED). Grow allocation incrementally instead — this env var
# propagates to the model_train_eval subprocess too.
os.environ["TF_FORCE_GPU_ALLOW_GROWTH"] = "true"
os.chdir('/home/wesd/mww-train/work')

label = sys.argv[1]
samples_dir  = f'samples_{label}'
features_dir = f'features_{label}'
train_dir    = f'trained_models/{label}'
config_path  = f'train_params_{label}.yaml'

assert os.path.isdir(samples_dir), f"missing {samples_dir}"

# ---- 1. Feature generation (augment TTS positives -> spectrogram mmaps) ----
if not os.path.exists(features_dir):
    from microwakeword.audio.augmentation import Augmentation
    from microwakeword.audio.clips import Clips
    from microwakeword.audio.spectrograms import SpectrogramGeneration
    from mmap_ninja.ragged import RaggedMmap

    clips = Clips(input_directory=samples_dir, file_pattern='*.wav',
                  max_clip_duration_s=None, remove_silence=False,
                  random_split_seed=10, split_count=0.1)
    augmenter = Augmentation(
        augmentation_duration_s=3.2,
        augmentation_probabilities={
            "SevenBandParametricEQ": 0.1, "TanhDistortion": 0.1, "PitchShift": 0.1,
            "BandStopFilter": 0.1, "AddColorNoise": 0.1, "AddBackgroundNoise": 0.75,
            "Gain": 1.0, "RIR": 0.5},
        impulse_paths=['mit_rirs'], background_paths=['fma_16k'],
        background_min_snr_db=-5, background_max_snr_db=10,
        min_jitter_s=0.195, max_jitter_s=0.205)

    os.mkdir(features_dir)
    for split in ["training", "validation", "testing"]:
        out_dir = os.path.join(features_dir, split); os.makedirs(out_dir, exist_ok=True)
        if split == "training":     split_name, rep, slide = "train", 2, 10
        elif split == "validation": split_name, rep, slide = "validation", 1, 10
        else:                       split_name, rep, slide = "test", 1, 1
        sg = SpectrogramGeneration(clips=clips, augmenter=augmenter, slide_frames=slide, step_ms=10)
        RaggedMmap.from_generator(
            out_dir=os.path.join(out_dir, 'wakeword_mmap'),
            sample_generator=sg.spectrogram_generator(split=split_name, repeat=rep),
            batch_size=100, verbose=True)
    print(f"[{label}] features generated")
else:
    print(f"[{label}] features exist, skipping gen")

# ---- 2. Training config YAML ----
config = {
    "window_step_ms": 10,
    "train_dir": train_dir,
    "features": [
        {"features_dir": features_dir, "sampling_weight": 2.0, "penalty_weight": 1.0,
         "truth": True, "truncation_strategy": "truncate_start", "type": "mmap"},
        # Cross-phrase HARD NEGATIVES: the other two "Kinder ..." phrases, so the model discriminates
        # the suffix. Two flavors per sibling: the heavily-augmented positives (features_*) AND clean
        # renditions (features_xneg_*) — the augmented-only version left CLEAN siblings slipping
        # through (train/test augmentation mismatch), so the clean set is weighted higher.
        *[{"features_dir": f"features_{p}", "sampling_weight": 6.0, "penalty_weight": 1.0,
           "truth": False, "truncation_strategy": "truncate_start", "type": "mmap"}
          for p in ["kinder_bedtime", "kinder_wakeup", "kinder_riseandshine"] if p != label],
        *[{"features_dir": f"features_xneg_{p}", "sampling_weight": 12.0, "penalty_weight": 1.0,
           "truth": False, "truncation_strategy": "truncate_start", "type": "mmap"}
          for p in ["kinder_bedtime", "kinder_wakeup", "kinder_riseandshine"] if p != label],
        {"features_dir": "negative_datasets/speech", "sampling_weight": 10.0, "penalty_weight": 1.0,
         "truth": False, "truncation_strategy": "random", "type": "mmap"},
        {"features_dir": "negative_datasets/dinner_party", "sampling_weight": 10.0, "penalty_weight": 1.0,
         "truth": False, "truncation_strategy": "random", "type": "mmap"},
        {"features_dir": "negative_datasets/no_speech", "sampling_weight": 5.0, "penalty_weight": 1.0,
         "truth": False, "truncation_strategy": "random", "type": "mmap"},
        {"features_dir": "negative_datasets/dinner_party_eval", "sampling_weight": 0.0, "penalty_weight": 1.0,
         "truth": False, "truncation_strategy": "split", "type": "mmap"},
    ],
    "training_steps": [10000],
    "positive_class_weight": [1], "negative_class_weight": [20],
    "learning_rates": [0.001], "batch_size": 128,
    "time_mask_max_size": [0], "time_mask_count": [0],
    "freq_mask_max_size": [0], "freq_mask_count": [0],
    "eval_step_interval": 500, "clip_duration_ms": 1500,
    "target_minimization": 0.9, "minimization_metric": None,
    "maximization_metric": "average_viable_recall",
}
yaml.dump(config, open(config_path, "w"))
print(f"[{label}] wrote {config_path}")

# ---- 3. Train + quantize + export streaming tflite (robust to WSL2-GPU segfaults) ----
MIX = ["mixednet", "--pointwise_filters", "64,64,64,64", "--repeat_in_block", "1, 1, 1, 1",
       "--mixconv_kernel_sizes", "[5], [7,11], [9,15], [23]", "--residual_connection", "0,0,0,0",
       "--first_conv_filters", "32", "--first_conv_kernel_size", "5", "--stride", "3"]
tflite = os.path.join(train_dir, "tflite_stream_state_internal_quant", "stream_state_internal_quant.tflite")
best   = os.path.join(train_dir, "best_weights.weights.h5")

def run(train_flag):
    cmd = ["python", "-m", "microwakeword.model_train_eval", "--training_config", config_path,
           "--train", train_flag, "--restore_checkpoint", "1",
           "--test_tflite_streaming_quantized", "1", "--use_weights", "best_weights"] + MIX
    subprocess.run(cmd)   # not check=True: segfaults handled below by retrying / exporting

def have_tflite():       # a 0-byte file (crashed conversion) does NOT count as done
    return os.path.exists(tflite) and os.path.getsize(tflite) > 1000

for attempt in range(1, 6):                    # up to 5 full runs; each resumes from checkpoint
    if have_tflite(): break
    print(f"[{label}] train attempt {attempt} (targeting full 10k)", flush=True)
    run("1")                                    # trains 10k then exports; only produces tflite at 10k
# re-export ONLY as a last resort, after every --train 1 attempt has been tried (a premature
# export here would defeat the goal of reaching 10k):
if not have_tflite() and os.path.exists(best):
    print(f"[{label}] all --train 1 attempts fell short — exporting best_weights (--train 0)", flush=True)
    run("0")
print(f"[{label}] DONE -> {tflite}  exists={os.path.exists(tflite)}")
