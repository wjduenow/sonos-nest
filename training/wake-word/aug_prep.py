# Augmentation-data prep (MIT RIR + AudioSet + FMA -> 16 kHz mono wav).
# Bypasses `datasets`/torchcodec (broken against this torch build): downloads raw files and
# converts with ffmpeg directly, which is present and reliable.
import os, glob, subprocess, concurrent.futures
os.chdir('/home/wesd/mww-train/work')

def ff16k(src, dst):
    subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", src,
                    "-ar", "16000", "-ac", "1", "-sample_fmt", "s16", dst],
                   check=False)

def convert_all(files, out_dir, new_ext=".wav"):
    os.makedirs(out_dir, exist_ok=True)
    def one(f):
        name = os.path.splitext(os.path.basename(f))[0] + new_ext
        ff16k(f, os.path.join(out_dir, name))
    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as ex:
        list(ex.map(one, files))

# --- MIT RIR: 270 raw 16 kHz wavs, download directly ---
if not os.path.exists("mit_rirs") or len(os.listdir("mit_rirs")) < 200:
    from huggingface_hub import snapshot_download
    import shutil
    p = snapshot_download("davidscripka/MIT_environmental_impulse_responses",
                          repo_type="dataset", allow_patterns="16khz/*.wav")
    os.makedirs("mit_rirs", exist_ok=True)
    for f in glob.glob(os.path.join(p, "16khz", "*.wav")):
        shutil.copy(f, os.path.join("mit_rirs", os.path.basename(f)))
    print(f"MIT RIR: {len(os.listdir('mit_rirs'))} files")

# --- AudioSet: SKIPPED (agkphysics/AudioSet dropped its .tar shards; parquet path needs the
#     broken datasets/torchcodec stack). FMA below + the rich pre-computed negative feature sets
#     cover background noise for a first model. Add AudioSet in iteration if needed. ---

# --- FMA (extra-small music set) -> 16k ---
if not os.path.exists("fma_16k") or len(os.listdir("fma_16k")) < 10:
    os.makedirs("fma", exist_ok=True)
    if not glob.glob("fma/**/*.mp3", recursive=True):
        subprocess.run("curl -fL --retry 3 -o fma/fma_xs.zip "
                       "https://huggingface.co/datasets/mchl914/fma_xsmall/resolve/main/fma_xs.zip",
                       shell=True, check=True)
        subprocess.run("cd fma && unzip -q fma_xs.zip", shell=True, check=True)
    mp3s = glob.glob("fma/**/*.mp3", recursive=True)
    print(f"FMA: converting {len(mp3s)} mp3 -> 16k")
    convert_all(mp3s, "fma_16k")
    print(f"FMA: {len(os.listdir('fma_16k'))} wavs")

print("AUGMENTATION PREP COMPLETE")
