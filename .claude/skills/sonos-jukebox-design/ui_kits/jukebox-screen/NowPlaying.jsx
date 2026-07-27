// Now Playing screen — album art, meta, scrubber, transport.
const { StatusBar, TransportButton, Scrubber, Badge, VolumeBar } = window.SonosJukeboxDesignSystem_e55a41;

function NowPlaying({ state, actions }) {
  const t = state.track;
  const room = window.JB.rooms.find(r => r.id === state.activeRoom) || window.JB.rooms[0];
  return (
    <div style={{ height:'100%', display:'flex', flexDirection:'column', padding:'22px 30px 18px' }}>
      <StatusBar room={room.name} grouped={state.groupCount} time="14:32" source="wifi" />
      <div style={{ flex:1, display:'flex', gap:34, alignItems:'center', minHeight:0 }}>
        <div style={{ width:280, height:280, borderRadius:20, flex:'none', background:t.art,
          boxShadow:'0 24px 60px rgba(0,0,0,.5)', position:'relative', overflow:'hidden' }}>
          <div style={{ position:'absolute', inset:0, boxShadow:'inset 0 0 0 1px rgba(255,255,255,.08)', borderRadius:20 }}></div>
        </div>
        <div style={{ flex:1, minWidth:0 }}>
          <div style={{ display:'flex', gap:8, marginBottom:16 }}>
            <Badge tone="hi">{state.source}</Badge>
            <Badge>{t.quality}</Badge>
          </div>
          <div style={{ font:'800 52px/1.02 var(--font-ui)', letterSpacing:'-.02em', color:'var(--screen-text)' }}>{t.title}</div>
          <div style={{ font:'500 22px/1.3 var(--font-ui)', color:'var(--screen-text-mut)', marginTop:8 }}>{t.artist} · {t.album}</div>
          <div style={{ marginTop:28 }}><Scrubber elapsed={state.playing ? t.elapsed : t.elapsed} duration={t.duration} /></div>
        </div>
      </div>
      <div style={{ display:'flex', alignItems:'center', gap:22 }}>
        <div style={{ flex:1 }}><VolumeBar value={room.vol} /></div>
        <div style={{ display:'flex', alignItems:'center', gap:14 }}>
          <TransportButton icon="prev" variant="ghost" />
          <TransportButton icon={state.playing ? 'pause' : 'play'} variant="solid" size="lg" onClick={actions.togglePlay} />
          <TransportButton icon="next" variant="ghost" />
          <TransportButton icon="shuffle" variant="ghost" active={state.shuffle} onClick={actions.toggleShuffle} />
        </div>
      </div>
    </div>
  );
}
window.NowPlaying = NowPlaying;
