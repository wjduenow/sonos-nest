// Rooms — grouping, per-room volume, per-room play/pause.
const DS = window.SonosJukeboxDesignSystem_e55a41;
const { StatusBar, VolumeBar, TransportButton, Badge } = DS;
const Icon = DS.Icon || window.JBIcon;

function RoomRow({ room, active, inGroup, groupSize, actions }) {
  return (
    <div style={{ display:'flex', alignItems:'center', gap:16, padding:'12px 16px', borderRadius:'var(--r-md)',
      background: inGroup ? 'var(--screen-elev)' : 'transparent',
      boxShadow: active ? 'inset 0 0 0 1px var(--accent)' : inGroup ? 'inset 0 0 0 1px var(--screen-line)' : 'none' }}>
      {/* group toggle */}
      <button onClick={() => actions.toggleGroup(room.id)} title={inGroup ? 'Remove from group' : 'Add to group'}
        style={{ width:26, height:26, flex:'none', borderRadius:8, cursor:'pointer', display:'grid', placeItems:'center',
          border: inGroup ? 'none' : '1.5px solid var(--screen-text-dim)',
          background: inGroup ? 'var(--accent)' : 'transparent', color:'var(--accent-ink)' }}>
        {inGroup && <Icon name="check" size={16} />}
      </button>
      {/* name + state */}
      <button onClick={() => actions.setActive(room.id)}
        style={{ width:180, flex:'none', textAlign:'left', border:'none', background:'transparent', cursor:'pointer', padding:0 }}>
        <div style={{ display:'flex', alignItems:'center', gap:8 }}>
          <span style={{ font:'600 17px/1.2 var(--font-ui)', color: inGroup ? 'var(--screen-text)' : 'var(--screen-text-mut)' }}>{room.name}</span>
          {active && <Badge tone="live">MAIN</Badge>}
        </div>
        <div style={{ font:'400 12px/1.3 var(--font-ui)', color:'var(--screen-text-dim)', marginTop:3 }}>
          {room.playing ? (inGroup && groupSize > 1 ? 'Playing · grouped' : 'Playing') : 'Idle'}
        </div>
      </button>
      {/* volume */}
      <div style={{ flex:1, minWidth:0, opacity: room.playing ? 1 : .45 }}>
        <VolumeBar value={room.vol} muted={room.vol===0} />
      </div>
      {/* stepper + play/pause */}
      <div style={{ display:'flex', alignItems:'center', gap:6, flex:'none' }}>
        <button onClick={() => actions.setRoomVol(room.id, -5)} style={stepStyle}><Icon name="minus" size={15} /></button>
        <button onClick={() => actions.setRoomVol(room.id, 5)} style={stepStyle}><Icon name="plus" size={15} /></button>
        <TransportButton icon={room.playing ? 'pause' : 'play'} size="sm"
          variant={room.playing ? 'solid' : 'elevated'} onClick={() => actions.toggleRoomPlay(room.id)} />
      </div>
    </div>
  );
}

const stepStyle = { width:32, height:32, borderRadius:10, border:'1px solid var(--screen-line)', background:'var(--screen-elev-2)',
  color:'var(--screen-text-mut)', cursor:'pointer', display:'grid', placeItems:'center' };

function RoomPicker({ state, actions }) {
  const rooms = state.rooms;
  const active = rooms.find(r => r.id === state.activeRoom) || rooms[0];
  const group = rooms.filter(r => state.grouped.includes(r.id));
  const groupVol = group.length ? Math.round(group.reduce((a,r) => a + r.vol, 0) / group.length) : 0;
  const anyPlaying = group.some(r => r.playing);

  return (
    <div style={{ height:'100%', boxSizing:'border-box', display:'flex', flexDirection:'column', padding:'22px 30px 16px' }}>
      <StatusBar room={active.name} grouped={state.groupCount} time="14:32" source="wifi" />

      {/* group summary bar */}
      <div style={{ display:'flex', alignItems:'center', gap:18, margin:'12px 0 14px', padding:'14px 18px',
        borderRadius:'var(--r-lg)', background:'var(--screen-elev)', border:'1px solid var(--screen-line)' }}>
        <div style={{ flex:'none' }}>
          <div style={{ font:'800 24px/1 var(--font-ui)', letterSpacing:'-.02em', color:'var(--screen-text)' }}>
            {group.length > 1 ? `${group.length} rooms` : active.name}
          </div>
          <div style={{ font:'400 13px/1.3 var(--font-ui)', color:'var(--screen-text-dim)', marginTop:4 }}>
            {group.length > 1 ? group.map(r => r.name).join(' · ') : 'Playing alone'}
          </div>
        </div>
        <div style={{ flex:1, minWidth:0 }}>
          <div style={{ font:'700 10px/1 var(--font-mono)', letterSpacing:'.14em', textTransform:'uppercase', color:'var(--screen-text-dim)', marginBottom:8 }}>Group volume</div>
          <VolumeBar value={groupVol} />
        </div>
        <div style={{ display:'flex', gap:10, flex:'none' }}>
          <button onClick={actions.ungroupAll} style={{ ...stepStyle, width:'auto', padding:'0 14px', height:40,
            font:'700 11px/1 var(--font-mono)', letterSpacing:'.1em', textTransform:'uppercase' }}>Ungroup</button>
          <TransportButton icon={anyPlaying ? 'pause' : 'play'} variant="solid" size="md" onClick={actions.toggleGroupPlay} />
        </div>
      </div>

      <div style={{ font:'700 10px/1 var(--font-mono)', letterSpacing:'.14em', textTransform:'uppercase', color:'var(--screen-text-dim)', marginBottom:8 }}>
        All rooms · tap ✓ to group
      </div>
      <div style={{ flex:1, overflow:'auto', display:'flex', flexDirection:'column', gap:4 }}>
        {rooms.map(r => (
          <RoomRow key={r.id} room={r} active={r.id === state.activeRoom}
            inGroup={state.grouped.includes(r.id)} groupSize={group.length} actions={actions} />
        ))}
      </div>
    </div>
  );
}
window.RoomPicker = RoomPicker;
