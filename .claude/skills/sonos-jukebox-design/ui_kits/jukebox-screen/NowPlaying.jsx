// Now Playing screen — three art-prominence layouts: split / hero / full-bleed.
const { StatusBar, TransportButton, Scrubber, Badge, VolumeBar } = window.SonosJukeboxDesignSystem_e55a41;

function NowPlaying({ state, actions }) {
  const t = state.track;
  const room = state.rooms.find(r => r.id === state.activeRoom) || state.rooms[0];
  const layout = state.artLayout || 'split';

  const meta = (
    <div style={{ display:'flex', gap:8 }}>
      <Badge tone="hi">{state.source}</Badge>
      <Badge>{t.quality}</Badge>
    </div>
  );
  const transport = (
    <div style={{ display:'flex', alignItems:'center', gap:14 }}>
      <TransportButton icon="prev" variant="ghost" />
      <TransportButton icon={state.playing ? 'pause' : 'play'} variant="solid" size="lg" onClick={actions.togglePlay} />
      <TransportButton icon="next" variant="ghost" />
      <TransportButton icon="shuffle" variant="ghost" active={state.shuffle} onClick={actions.toggleShuffle} />
    </div>
  );
  const artTile = (size) => (
    <div style={{ width:size, height:size, borderRadius:20, flex:'none', background:t.art,
      boxShadow:'0 24px 60px rgba(0,0,0,.5)', position:'relative', overflow:'hidden' }}>
      <div style={{ position:'absolute', inset:0, boxShadow:'inset 0 0 0 1px rgba(255,255,255,.08)', borderRadius:20 }}></div>
    </div>
  );

  // ---- FULL-BLEED: art fills the screen, text over a protection gradient ----
  if (layout === 'bleed') {
    return (
      <div style={{ height:'100%', boxSizing:'border-box', position:'relative', overflow:'hidden' }}>
        <div style={{ position:'absolute', inset:0, background:t.art }}></div>
        <div style={{ position:'absolute', inset:0, background:'linear-gradient(180deg, rgba(8,9,11,.72) 0%, rgba(8,9,11,.18) 32%, rgba(8,9,11,.86) 78%, rgba(8,9,11,.96) 100%)' }}></div>
        <div style={{ position:'relative', height:'100%', boxSizing:'border-box', display:'flex', flexDirection:'column', padding:'22px 34px 20px' }}>
          <StatusBar room={room.name} grouped={state.groupCount} time="14:32" source="wifi" />
          <div style={{ flex:1, minHeight:0 }}></div>
          <div style={{ marginBottom:18 }}>
            {meta}
            <div style={{ font:'800 56px/1 var(--font-ui)', letterSpacing:'-.025em', color:'var(--screen-text)', marginTop:14, textShadow:'0 2px 24px rgba(0,0,0,.5)' }}>{t.title}</div>
            <div style={{ font:'500 24px/1.3 var(--font-ui)', color:'var(--screen-text-mut)', marginTop:8 }}>{t.artist} · {t.album}</div>
          </div>
          <Scrubber elapsed={t.elapsed} duration={t.duration} live={!t.duration} />
          <div style={{ display:'flex', alignItems:'center', gap:22, marginTop:16 }}>
            <div style={{ flex:1 }}><VolumeBar value={room.vol} /></div>
            {transport}
          </div>
        </div>
      </div>
    );
  }

  // ---- HERO: oversized centred art, compact text beneath ----
  if (layout === 'hero') {
    return (
      <div style={{ height:'100%', boxSizing:'border-box', display:'flex', flexDirection:'column', padding:'20px 30px 18px', alignItems:'stretch' }}>
        <StatusBar room={room.name} grouped={state.groupCount} time="14:32" source="wifi" />
        <div style={{ flex:1, minHeight:0, display:'flex', gap:30, alignItems:'center', justifyContent:'center', overflow:'hidden', padding:'10px 0' }}>
          {artTile(300)}
          <div style={{ maxWidth:360 }}>
            {meta}
            <div style={{ font:'800 44px/1.02 var(--font-ui)', letterSpacing:'-.02em', color:'var(--screen-text)', marginTop:14 }}>{t.title}</div>
            <div style={{ font:'500 20px/1.3 var(--font-ui)', color:'var(--screen-text-mut)', marginTop:8 }}>{t.artist}</div>
            <div style={{ font:'400 16px/1.3 var(--font-ui)', color:'var(--screen-text-dim)', marginTop:4 }}>{t.album}</div>
            <div style={{ marginTop:22 }}><Scrubber elapsed={t.elapsed} duration={t.duration} live={!t.duration} /></div>
          </div>
        </div>
        <div style={{ display:'flex', alignItems:'center', gap:22 }}>
          <div style={{ flex:1 }}><VolumeBar value={room.vol} /></div>
          {transport}
        </div>
      </div>
    );
  }

  // ---- SPLIT (default) ----
  return (
    <div style={{ height:'100%', boxSizing:'border-box', display:'flex', flexDirection:'column', padding:'22px 30px 18px' }}>
      <StatusBar room={room.name} grouped={state.groupCount} time="14:32" source="wifi" />
      <div style={{ flex:1, minHeight:0, display:'flex', gap:34, alignItems:'center', overflow:'hidden' }}>
        {artTile(268)}
        <div style={{ flex:1, minWidth:0 }}>
          <div style={{ marginBottom:16 }}>{meta}</div>
          <div style={{ font:'800 52px/1.02 var(--font-ui)', letterSpacing:'-.02em', color:'var(--screen-text)' }}>{t.title}</div>
          <div style={{ font:'500 22px/1.3 var(--font-ui)', color:'var(--screen-text-mut)', marginTop:8 }}>{t.artist} · {t.album}</div>
          <div style={{ marginTop:28 }}><Scrubber elapsed={t.elapsed} duration={t.duration} live={!t.duration} /></div>
        </div>
      </div>
      <div style={{ display:'flex', alignItems:'center', gap:22 }}>
        <div style={{ flex:1 }}><VolumeBar value={room.vol} /></div>
        {transport}
      </div>
    </div>
  );
}
window.NowPlaying = NowPlaying;
