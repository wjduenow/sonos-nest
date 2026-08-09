// Device shell: matte face + screen + physical dial/buttons, with nav + volume overlay.
const DS = window.SonosJukeboxDesignSystem_e55a41;
const { TransportButton, Dial } = DS;
const Icon = DS.Icon || window.JBIcon;
const { useState, useEffect, useRef } = React;

const RAIL = [
  { id:'now',   icon:'disc-3', label:'Now' },
  { id:'radio', icon:'radio',  label:'Radio' },
  { id:'rooms', icon:'speaker',label:'Rooms' },
];

function App() {
  const [state, setState] = useState({
    view:'now', playing:true, shuffle:false,
    activeRoom:'kitchen', grouped:['kitchen','bedroom'], source:'RADIO',
    stationId:'r6',
    track:(() => { const st = window.JB.stations.find(s=>s.id==='r6');
      return { title:st.name, artist:'Lauren Laverne', album:'Live radio', elapsed:0, duration:0, art:st.art, quality:'AAC · 128' }; })(),
    vol:64, volShow:false, artLayout:'split',
    rooms: window.JB.rooms.map(r => ({ ...r, playing: ['kitchen','bedroom'].includes(r.id) })),
  });
  const volTimer = useRef(null);
  const groupCount = Math.max(0, state.grouped.length - 1);

  const showVol = () => {
    setState(s => ({ ...s, volShow:true }));
    clearTimeout(volTimer.current);
    volTimer.current = setTimeout(() => setState(s => ({ ...s, volShow:false })), 1400);
  };

  const actions = {
    togglePlay: () => setState(s => ({ ...s, playing:!s.playing })),
    toggleShuffle: () => setState(s => ({ ...s, shuffle:!s.shuffle })),
    setView: (view) => setState(s => ({ ...s, view })),
    setRoom: (id) => setState(s => ({ ...s, activeRoom:id, grouped: s.grouped.includes(id) ? s.grouped : [...s.grouped, id] })),
    playStation: (st) => setState(s => ({ ...s, stationId:st.id, source:'RADIO', playing:true, view:'now',
      track:{ title:st.name, artist:st.sub.replace(/^Now:\s*/,''), album:'Live radio', elapsed:0, duration:0, art:st.art, quality:'AAC · 128' } })),
    bumpVol: (d) => { setState(s => ({ ...s, vol: Math.max(0, Math.min(100, s.vol + d)) })); showVol(); },
    cycleArt: () => setState(s => ({ ...s, view:'now', artLayout: s.artLayout==='split' ? 'hero' : s.artLayout==='hero' ? 'bleed' : 'split' })),
    setActive: (id) => setState(s => ({ ...s, activeRoom:id, grouped: s.grouped.includes(id) ? s.grouped : [...s.grouped, id] })),
    toggleGroup: (id) => setState(s => {
      const inG = s.grouped.includes(id);
      if (inG && id === s.activeRoom) return s;              // main room stays in its own group
      const grouped = inG ? s.grouped.filter(g => g !== id) : [...s.grouped, id];
      const anchor = s.rooms.find(r => r.id === s.activeRoom);
      return { ...s, grouped, rooms: s.rooms.map(r => r.id===id ? { ...r, playing: inG ? false : anchor.playing } : r) };
    }),
    setRoomVol: (id, d) => setState(s => ({ ...s,
      rooms: s.rooms.map(r => r.id===id ? { ...r, vol: Math.max(0, Math.min(100, r.vol + d)) } : r) })),
    toggleRoomPlay: (id) => setState(s => ({ ...s,
      rooms: s.rooms.map(r => r.id===id ? { ...r, playing:!r.playing } : r) })),
    toggleGroupPlay: () => setState(s => {
      const on = !s.rooms.filter(r => s.grouped.includes(r.id)).some(r => r.playing);
      return { ...s, playing:on, rooms: s.rooms.map(r => s.grouped.includes(r.id) ? { ...r, playing:on } : r) };
    }),
    ungroupAll: () => setState(s => ({ ...s, grouped:[s.activeRoom],
      rooms: s.rooms.map(r => r.id===s.activeRoom ? r : { ...r, playing:false }) })),
  };

  const isLive = !state.track.duration;
  const nowTrack = { ...state.track, elapsed: isLive ? 0 : 84, duration: state.track.duration || 0, live: isLive };
  const viewState = { ...state, track: state.view==='now' ? nowTrack : state.track, groupCount };
  const View = { now: window.NowPlaying, radio: window.RadioBrowser, rooms: window.RoomPicker }[state.view];

  return (
    <div style={{ display:'flex', gap:0, alignItems:'stretch', padding:22, background:'linear-gradient(160deg,#fbfbf9,#f0f0ec)', borderRadius:26, boxShadow:'0 40px 90px rgba(22,24,28,.22)' }}>
      {/* screen */}
      <div style={{ width:1024, height:600, background:'var(--screen-bg)', borderRadius:16, overflow:'hidden', display:'flex', boxShadow:'inset 0 2px 8px rgba(0,0,0,.5)', position:'relative' }}>
        <nav style={{ width:66, flex:'none', borderRight:'1px solid var(--screen-line)', display:'flex', flexDirection:'column', alignItems:'center', paddingTop:22, gap:10 }}>
          {RAIL.map(item => {
            const on = state.view === item.id;
            return (
              <button key={item.id} onClick={() => actions.setView(item.id)} title={item.label}
                style={{ width:48, height:48, borderRadius:14, border:'none', cursor:'pointer', display:'grid', placeItems:'center',
                  background: on ? 'color-mix(in oklch, var(--accent) 18%, transparent)' : 'transparent',
                  color: on ? 'var(--accent)' : 'var(--screen-text-dim)' }}>
                <Icon name={item.icon} size={22} />
              </button>
            );
          })}
          <button onClick={actions.cycleArt} title={`Album art: ${state.artLayout} — tap to change`}
            style={{ width:48, height:48, marginTop:'auto', marginBottom:18, borderRadius:14, border:'1px solid var(--screen-line)',
              cursor:'pointer', display:'grid', placeItems:'center', background:'transparent', color:'var(--screen-text-dim)' }}>
            <Icon name={state.artLayout==='bleed' ? 'maximize' : state.artLayout==='hero' ? 'square' : 'columns-2'} size={20} />
          </button>
        </nav>
        <div style={{ flex:1, minWidth:0 }}><View state={viewState} actions={actions} /></div>
        {state.volShow && (
          <div style={{ position:'absolute', inset:0, display:'grid', placeItems:'center', background:'rgba(8,9,11,.55)', backdropFilter:'blur(3px)' }}>
            <Dial value={state.vol} size={220} label="VOLUME" />
          </div>
        )}
      </div>
      {/* physical control column */}
      <div style={{ width:196, display:'flex', flexDirection:'column', alignItems:'center', justifyContent:'space-between', padding:'20px 0 26px' }}>
        <button onClick={() => actions.bumpVol(6)} onWheel={(e)=>actions.bumpVol(e.deltaY<0?4:-4)} title="Rotary dial — turn for volume, press to select"
          style={{ width:130, height:130, borderRadius:'999px', border:'none', cursor:'pointer', position:'relative',
            background:'radial-gradient(circle at 38% 32%,#4a4d55,#2a2c31 62%)', boxShadow:'0 10px 22px rgba(0,0,0,.3), inset 0 1px 2px rgba(255,255,255,.18)' }}>
          <span style={{ position:'absolute', inset:12, borderRadius:'999px', background:'repeating-conic-gradient(from 0deg, rgba(255,255,255,.055) 0 1.6deg, rgba(0,0,0,.05) 1.6deg 3.2deg, transparent 3.2deg 9deg)' }}></span>
          <span style={{ position:'absolute', top:12, left:'50%', transform:'translateX(-50%)', width:4, height:18, borderRadius:3, background:'var(--accent)', boxShadow:'0 0 8px var(--accent)' }}></span>
        </button>
        <div style={{ display:'grid', gridTemplateColumns:'repeat(2,1fr)', gap:16 }}>
          <TransportButton icon={state.playing ? 'pause' : 'play'} variant="solid" size="md" onClick={actions.togglePlay} />
          <TransportButton icon="room" variant="elevated" size="md" style={{ color:'var(--ink-700)', background:'#fff', boxShadow:'0 3px 8px rgba(0,0,0,.12)' }} onClick={() => actions.setView('rooms')} />
          <TransportButton icon="prev" variant="elevated" size="md" style={{ color:'var(--ink-700)', background:'#fff', boxShadow:'0 3px 8px rgba(0,0,0,.12)' }} />
          <TransportButton icon="next" variant="elevated" size="md" style={{ color:'var(--ink-700)', background:'#fff', boxShadow:'0 3px 8px rgba(0,0,0,.12)' }} />
        </div>
      </div>
    </div>
  );
}
window.App = App;
