// Radio browser — station list.
const { StatusBar, ListRow, Badge } = window.SonosJukeboxDesignSystem_e55a41;

function RadioBrowser({ state, actions }) {
  const room = window.JB.rooms.find(r => r.id === state.activeRoom) || window.JB.rooms[0];
  const genres = ['Featured','Music','Talk','Local','Podcasts'];
  return (
    <div style={{ height:'100%', display:'flex', flexDirection:'column', padding:'22px 30px 12px' }}>
      <StatusBar room={room.name} grouped={state.groupCount} time="14:32" source="wifi" />
      <div style={{ display:'flex', alignItems:'baseline', justifyContent:'space-between', margin:'8px 0 14px' }}>
        <div style={{ font:'800 30px/1 var(--font-ui)', letterSpacing:'-.02em', color:'var(--screen-text)' }}>Radio</div>
        <div style={{ display:'flex', gap:8 }}>
          {genres.map((g,i) => (
            <span key={g} style={{ font:'600 13px/1 var(--font-ui)', padding:'8px 14px', borderRadius:'999px',
              color: i===0 ? 'var(--accent)' : 'var(--screen-text-dim)',
              background: i===0 ? 'color-mix(in oklch, var(--accent) 16%, transparent)' : 'transparent' }}>{g}</span>
          ))}
        </div>
      </div>
      <div style={{ flex:1, overflow:'auto', display:'flex', flexDirection:'column', gap:2 }}>
        {window.JB.stations.map(s => (
          <ListRow key={s.id} title={s.name} subtitle={s.sub} artColor={s.art}
            playing={state.stationId === s.id} selected={state.stationId === s.id}
            trailing={<Badge tone="live">LIVE</Badge>} onClick={() => actions.playStation(s)} />
        ))}
      </div>
    </div>
  );
}
window.RadioBrowser = RadioBrowser;
