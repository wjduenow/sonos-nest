// Room picker — select active room, group, per-room volume.
const { StatusBar, RoomChip, VolumeBar } = window.SonosJukeboxDesignSystem_e55a41;

function RoomPicker({ state, actions }) {
  const active = state.activeRoom;
  const grouped = state.grouped;
  return (
    <div style={{ height:'100%', display:'flex', flexDirection:'column', padding:'22px 30px 16px' }}>
      <StatusBar room={window.JB.rooms.find(r=>r.id===active).name} grouped={state.groupCount} time="14:32" source="wifi" />
      <div style={{ font:'800 30px/1 var(--font-ui)', letterSpacing:'-.02em', color:'var(--screen-text)', margin:'8px 0 16px' }}>Rooms</div>
      <div style={{ display:'flex', flexWrap:'wrap', gap:10, marginBottom:22 }}>
        {window.JB.rooms.map(r => (
          <RoomChip key={r.id} name={r.name} playing={r.playing} selected={active===r.id}
            grouped={grouped.includes(r.id) && r.id!==active} onClick={() => actions.setRoom(r.id)} />
        ))}
      </div>
      <div style={{ font:'700 10px/1 var(--font-mono)', letterSpacing:'.14em', textTransform:'uppercase', color:'var(--screen-text-dim)', marginBottom:14 }}>Volume · grouped rooms</div>
      <div style={{ flex:1, display:'flex', flexDirection:'column', gap:20, overflow:'auto' }}>
        {window.JB.rooms.filter(r => r.id===active || grouped.includes(r.id)).map(r => (
          <div key={r.id} style={{ display:'flex', alignItems:'center', gap:18 }}>
            <span style={{ width:130, font:'600 16px/1 var(--font-ui)', color:'var(--screen-text)' }}>{r.name}</span>
            <div style={{ flex:1 }}><VolumeBar value={r.vol} /></div>
          </div>
        ))}
      </div>
    </div>
  );
}
window.RoomPicker = RoomPicker;
