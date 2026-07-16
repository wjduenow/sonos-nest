import os, sys
os.environ["TF_FORCE_GPU_ALLOW_GROWTH"]="true"
os.chdir('/home/wesd/mww-train/work')
label=sys.argv[1]
from microwakeword.audio.augmentation import Augmentation
from microwakeword.audio.clips import Clips
from microwakeword.audio.spectrograms import SpectrogramGeneration
from mmap_ninja.ragged import RaggedMmap
fd=f'features_{label}'
if os.path.exists(fd): print(f'{label} features exist'); sys.exit()
clips=Clips(input_directory=f'samples_{label}',file_pattern='*.wav',max_clip_duration_s=None,remove_silence=False,random_split_seed=10,split_count=0.1)
aug=Augmentation(augmentation_duration_s=3.2,augmentation_probabilities={"SevenBandParametricEQ":0.1,"TanhDistortion":0.1,"PitchShift":0.1,"BandStopFilter":0.1,"AddColorNoise":0.1,"AddBackgroundNoise":0.75,"Gain":1.0,"RIR":0.5},impulse_paths=['mit_rirs'],background_paths=['fma_16k'],background_min_snr_db=-5,background_max_snr_db=10,min_jitter_s=0.195,max_jitter_s=0.205)
os.mkdir(fd)
for split in ["training","validation","testing"]:
    od=os.path.join(fd,split); os.makedirs(od,exist_ok=True)
    if split=="training": sn,rep,sl="train",2,10
    elif split=="validation": sn,rep,sl="validation",1,10
    else: sn,rep,sl="test",1,1
    sg=SpectrogramGeneration(clips=clips,augmenter=aug,slide_frames=sl,step_ms=10)
    RaggedMmap.from_generator(out_dir=os.path.join(od,'wakeword_mmap'),sample_generator=sg.spectrogram_generator(split=sn,repeat=rep),batch_size=100,verbose=False)
print(f'{label} features generated')
