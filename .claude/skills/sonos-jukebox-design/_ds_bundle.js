/* @ds-bundle: {"format":4,"namespace":"SonosJukeboxDesignSystem_e55a41","components":[{"name":"Dial","sourcePath":"components/controls/Dial.jsx"},{"name":"RoomChip","sourcePath":"components/controls/RoomChip.jsx"},{"name":"TransportButton","sourcePath":"components/controls/TransportButton.jsx"},{"name":"ListRow","sourcePath":"components/display/ListRow.jsx"},{"name":"Scrubber","sourcePath":"components/display/Scrubber.jsx"},{"name":"VolumeBar","sourcePath":"components/display/VolumeBar.jsx"},{"name":"Badge","sourcePath":"components/system/Badge.jsx"},{"name":"StatusBar","sourcePath":"components/system/StatusBar.jsx"}],"sourceHashes":{"components/controls/Dial.jsx":"0ee94573c599","components/controls/RoomChip.jsx":"09e28baacfba","components/controls/TransportButton.jsx":"6efa418ed037","components/display/ListRow.jsx":"15763d40ad62","components/display/Scrubber.jsx":"c16be3c84331","components/display/VolumeBar.jsx":"753af8b28b27","components/system/Badge.jsx":"c452e72a5adb","components/system/StatusBar.jsx":"ac31259c70f6","ui_kits/jukebox-screen/App.jsx":"ed64392d2e16","ui_kits/jukebox-screen/NowPlaying.jsx":"85817a5c6522","ui_kits/jukebox-screen/RadioBrowser.jsx":"0f2ccd31b33c","ui_kits/jukebox-screen/RoomPicker.jsx":"5531c7b999bb","ui_kits/jukebox-screen/data.js":"a1003d72f953"},"inlinedExternals":[],"unexposedExports":[]} */

(() => {

const __ds_ns = (window.SonosJukeboxDesignSystem_e55a41 = window.SonosJukeboxDesignSystem_e55a41 || {});

const __ds_scope = {};

(__ds_ns.__errors = __ds_ns.__errors || []);

// components/controls/Dial.jsx
try { (() => {
function _extends() { return _extends = Object.assign ? Object.assign.bind() : function (n) { for (var e = 1; e < arguments.length; e++) { var t = arguments[e]; for (var r in t) ({}).hasOwnProperty.call(t, r) && (n[r] = t[r]); } return n; }, _extends.apply(null, arguments); }
/** On-glass rotary indicator that mirrors the physical dial — an accent arc + center value. */
function Dial({
  value = 50,
  min = 0,
  max = 100,
  size = 160,
  label = 'VOLUME',
  mode = 'volume',
  style,
  ...rest
}) {
  const pct = Math.max(0, Math.min(1, (value - min) / (max - min)));
  const sweep = 270; // gap at bottom
  const start = 135;
  const r = size / 2 - 10;
  const c = size / 2;
  const rad = a => a * Math.PI / 180;
  const pt = a => [c + r * Math.cos(rad(a)), c + r * Math.sin(rad(a))];
  const [x0, y0] = pt(start);
  const [x1, y1] = pt(start + sweep);
  const [xv, yv] = pt(start + sweep * pct);
  const large = sweep * pct > 180 ? 1 : 0;
  const arc = (ex, ey, lg) => `M ${x0} ${y0} A ${r} ${r} 0 ${lg} 1 ${ex} ${ey}`;
  return /*#__PURE__*/React.createElement("div", _extends({
    style: {
      width: size,
      height: size,
      position: 'relative',
      ...style
    }
  }, rest), /*#__PURE__*/React.createElement("svg", {
    width: size,
    height: size,
    style: {
      display: 'block'
    }
  }, /*#__PURE__*/React.createElement("path", {
    d: arc(x1, y1, 1),
    fill: "none",
    stroke: "var(--screen-elev-2)",
    strokeWidth: 8,
    strokeLinecap: "round"
  }), /*#__PURE__*/React.createElement("path", {
    d: arc(xv, yv, large),
    fill: "none",
    stroke: "var(--accent)",
    strokeWidth: 8,
    strokeLinecap: "round"
  })), /*#__PURE__*/React.createElement("div", {
    style: {
      position: 'absolute',
      inset: 0,
      display: 'grid',
      placeItems: 'center',
      textAlign: 'center'
    }
  }, /*#__PURE__*/React.createElement("div", null, /*#__PURE__*/React.createElement("div", {
    style: {
      font: `800 ${Math.round(size * 0.26)}px/1 var(--font-ui)`,
      color: 'var(--screen-text)'
    }
  }, mode === 'volume' ? value : value), /*#__PURE__*/React.createElement("div", {
    style: {
      font: '600 11px/1 var(--font-mono)',
      letterSpacing: '0.14em',
      color: 'var(--screen-text-dim)',
      marginTop: 6
    }
  }, label))));
}
Object.assign(__ds_scope, { Dial });
})(); } catch (e) { __ds_ns.__errors.push({ path: "components/controls/Dial.jsx", error: String((e && e.message) || e) }); }

// components/controls/RoomChip.jsx
try { (() => {
function _extends() { return _extends = Object.assign ? Object.assign.bind() : function (n) { for (var e = 1; e < arguments.length; e++) { var t = arguments[e]; for (var r in t) ({}).hasOwnProperty.call(t, r) && (n[r] = t[r]); } return n; }, _extends.apply(null, arguments); }
/** Selectable room pill for grouping speakers on-glass. */
function RoomChip({
  name,
  playing = false,
  grouped = false,
  selected = false,
  onClick,
  style,
  ...rest
}) {
  const base = {
    display: 'inline-flex',
    alignItems: 'center',
    gap: 8,
    height: 40,
    padding: '0 16px',
    borderRadius: '999px',
    font: '600 15px/1 var(--font-ui)',
    cursor: 'pointer',
    border: '1px solid var(--screen-line)',
    background: 'var(--screen-elev)',
    color: 'var(--screen-text-mut)',
    transition: 'all .15s ease',
    WebkitTapHighlightColor: 'transparent'
  };
  const sel = selected ? {
    background: 'color-mix(in oklch, var(--accent) 18%, var(--screen-elev))',
    borderColor: 'var(--accent)',
    color: 'var(--screen-text)'
  } : {};
  return /*#__PURE__*/React.createElement("button", _extends({
    type: "button",
    onClick: onClick,
    "aria-pressed": selected,
    style: {
      ...base,
      ...sel,
      ...style
    }
  }, rest), /*#__PURE__*/React.createElement("span", {
    style: {
      width: 8,
      height: 8,
      borderRadius: '999px',
      flex: 'none',
      background: playing ? 'var(--accent)' : 'var(--screen-text-dim)',
      boxShadow: playing ? '0 0 8px var(--accent)' : 'none'
    }
  }), name, grouped && /*#__PURE__*/React.createElement("span", {
    style: {
      font: '500 11px/1 var(--font-mono)',
      color: 'var(--screen-text-dim)'
    }
  }, "+1"));
}
Object.assign(__ds_scope, { RoomChip });
})(); } catch (e) { __ds_ns.__errors.push({ path: "components/controls/RoomChip.jsx", error: String((e && e.message) || e) }); }

// components/controls/TransportButton.jsx
try { (() => {
function _extends() { return _extends = Object.assign ? Object.assign.bind() : function (n) { for (var e = 1; e < arguments.length; e++) { var t = arguments[e]; for (var r in t) ({}).hasOwnProperty.call(t, r) && (n[r] = t[r]); } return n; }, _extends.apply(null, arguments); }
const ICONS = {
  play: 'play',
  pause: 'pause',
  back: 'chevron-left',
  next: 'skip-forward',
  prev: 'skip-back',
  room: 'arrow-left-right',
  menu: 'menu',
  shuffle: 'shuffle',
  repeat: 'repeat',
  add: 'plus',
  more: 'more-horizontal'
};
const SIZES = {
  sm: {
    d: 44,
    i: 20
  },
  md: {
    d: 56,
    i: 24
  },
  lg: {
    d: 72,
    i: 30
  }
};

/** Round transport control for the on-glass UI — mirrors the physical caps. */
function TransportButton({
  icon = 'play',
  size = 'md',
  variant = 'ghost',
  active = false,
  label,
  onClick,
  style,
  ...rest
}) {
  const s = SIZES[size] || SIZES.md;
  const base = {
    width: s.d,
    height: s.d,
    display: 'inline-grid',
    placeItems: 'center',
    borderRadius: '999px',
    cursor: 'pointer',
    border: 'none',
    padding: 0,
    transition: 'transform .12s ease, background .15s ease, opacity .15s ease',
    color: 'var(--screen-text)',
    WebkitTapHighlightColor: 'transparent'
  };
  const variants = {
    ghost: {
      background: 'transparent',
      color: active ? 'var(--accent)' : 'var(--screen-text-mut)'
    },
    elevated: {
      background: 'var(--screen-elev-2)',
      boxShadow: 'inset 0 0 0 1px var(--screen-line)',
      color: 'var(--screen-text)'
    },
    solid: {
      background: 'var(--accent)',
      color: 'var(--accent-ink)',
      boxShadow: '0 6px 18px color-mix(in oklch, var(--accent) 45%, transparent)'
    }
  };
  return /*#__PURE__*/React.createElement("button", _extends({
    type: "button",
    "aria-label": label || icon,
    "aria-pressed": active,
    onClick: onClick,
    style: {
      ...base,
      ...(variants[variant] || variants.ghost),
      ...style
    }
  }, rest), /*#__PURE__*/React.createElement("i", {
    "data-lucide": ICONS[icon] || icon,
    width: s.i,
    height: s.i,
    "stroke-width": 2.2
  }));
}
Object.assign(__ds_scope, { TransportButton });
})(); } catch (e) { __ds_ns.__errors.push({ path: "components/controls/TransportButton.jsx", error: String((e && e.message) || e) }); }

// components/display/ListRow.jsx
try { (() => {
function _extends() { return _extends = Object.assign ? Object.assign.bind() : function (n) { for (var e = 1; e < arguments.length; e++) { var t = arguments[e]; for (var r in t) ({}).hasOwnProperty.call(t, r) && (n[r] = t[r]); } return n; }, _extends.apply(null, arguments); }
/** A tappable row for lists — radio stations, queue tracks, rooms, sources. */
function ListRow({
  title,
  subtitle,
  art,
  artColor = 'var(--screen-elev-2)',
  playing = false,
  selected = false,
  trailing,
  onClick,
  style,
  ...rest
}) {
  const base = {
    display: 'flex',
    alignItems: 'center',
    gap: 14,
    width: '100%',
    textAlign: 'left',
    padding: '10px 14px',
    borderRadius: 'var(--r-md)',
    cursor: 'pointer',
    border: 'none',
    background: selected ? 'var(--screen-elev)' : 'transparent',
    transition: 'background .15s ease',
    WebkitTapHighlightColor: 'transparent'
  };
  return /*#__PURE__*/React.createElement("button", _extends({
    type: "button",
    onClick: onClick,
    style: {
      ...base,
      ...style
    }
  }, rest), /*#__PURE__*/React.createElement("span", {
    style: {
      width: 52,
      height: 52,
      borderRadius: 10,
      flex: 'none',
      overflow: 'hidden',
      background: art ? `center/cover url(${art})` : artColor,
      display: 'grid',
      placeItems: 'center',
      boxShadow: 'inset 0 0 0 1px var(--screen-line)'
    }
  }, playing && /*#__PURE__*/React.createElement("i", {
    "data-lucide": "audio-lines",
    width: 22,
    height: 22,
    style: {
      color: 'var(--accent)'
    }
  })), /*#__PURE__*/React.createElement("span", {
    style: {
      flex: 1,
      minWidth: 0
    }
  }, /*#__PURE__*/React.createElement("span", {
    style: {
      display: 'block',
      font: '600 17px/1.2 var(--font-ui)',
      color: 'var(--screen-text)',
      whiteSpace: 'nowrap',
      overflow: 'hidden',
      textOverflow: 'ellipsis'
    }
  }, title), subtitle && /*#__PURE__*/React.createElement("span", {
    style: {
      display: 'block',
      font: '400 13px/1.3 var(--font-ui)',
      color: 'var(--screen-text-mut)',
      marginTop: 3,
      whiteSpace: 'nowrap',
      overflow: 'hidden',
      textOverflow: 'ellipsis'
    }
  }, subtitle)), trailing);
}
Object.assign(__ds_scope, { ListRow });
})(); } catch (e) { __ds_ns.__errors.push({ path: "components/display/ListRow.jsx", error: String((e && e.message) || e) }); }

// components/display/Scrubber.jsx
try { (() => {
function _extends() { return _extends = Object.assign ? Object.assign.bind() : function (n) { for (var e = 1; e < arguments.length; e++) { var t = arguments[e]; for (var r in t) ({}).hasOwnProperty.call(t, r) && (n[r] = t[r]); } return n; }, _extends.apply(null, arguments); }
const fmt = s => {
  s = Math.max(0, Math.floor(s));
  const m = Math.floor(s / 60);
  return `${m}:${String(s % 60).padStart(2, '0')}`;
};

/** Playback progress bar with elapsed / remaining times. */
function Scrubber({
  elapsed = 0,
  duration = 210,
  live = false,
  style,
  ...rest
}) {
  const pct = live ? 1 : Math.max(0, Math.min(1, elapsed / (duration || 1)));
  return /*#__PURE__*/React.createElement("div", _extends({
    style: {
      width: '100%',
      ...style
    }
  }, rest), /*#__PURE__*/React.createElement("div", {
    style: {
      height: 6,
      borderRadius: '999px',
      background: 'var(--screen-elev-2)',
      position: 'relative'
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      position: 'absolute',
      left: 0,
      top: 0,
      height: '100%',
      width: `${pct * 100}%`,
      borderRadius: '999px',
      background: 'var(--accent)'
    }
  }), !live && /*#__PURE__*/React.createElement("div", {
    style: {
      position: 'absolute',
      top: '50%',
      left: `${pct * 100}%`,
      width: 14,
      height: 14,
      borderRadius: '999px',
      background: 'var(--screen-text)',
      transform: 'translate(-50%,-50%)',
      boxShadow: '0 2px 6px rgba(0,0,0,.5)'
    }
  })), /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      justifyContent: 'space-between',
      marginTop: 8,
      font: '500 12px/1 var(--font-mono)',
      color: 'var(--screen-text-dim)'
    }
  }, /*#__PURE__*/React.createElement("span", null, live ? 'LIVE' : fmt(elapsed)), /*#__PURE__*/React.createElement("span", null, live ? '· on air' : '-' + fmt(duration - elapsed))));
}
Object.assign(__ds_scope, { Scrubber });
})(); } catch (e) { __ds_ns.__errors.push({ path: "components/display/Scrubber.jsx", error: String((e && e.message) || e) }); }

// components/display/VolumeBar.jsx
try { (() => {
function _extends() { return _extends = Object.assign ? Object.assign.bind() : function (n) { for (var e = 1; e < arguments.length; e++) { var t = arguments[e]; for (var r in t) ({}).hasOwnProperty.call(t, r) && (n[r] = t[r]); } return n; }, _extends.apply(null, arguments); }
/** Horizontal volume readout with a mute/level icon. */
function VolumeBar({
  value = 40,
  muted = false,
  style,
  ...rest
}) {
  const pct = muted ? 0 : Math.max(0, Math.min(100, value));
  const icon = muted || pct === 0 ? 'volume-x' : pct < 50 ? 'volume-1' : 'volume-2';
  return /*#__PURE__*/React.createElement("div", _extends({
    style: {
      display: 'flex',
      alignItems: 'center',
      gap: 14,
      width: '100%',
      ...style
    }
  }, rest), /*#__PURE__*/React.createElement("i", {
    "data-lucide": icon,
    width: 22,
    height: 22,
    style: {
      color: 'var(--screen-text-mut)',
      flex: 'none'
    }
  }), /*#__PURE__*/React.createElement("div", {
    style: {
      flex: 1,
      height: 6,
      borderRadius: '999px',
      background: 'var(--screen-elev-2)',
      position: 'relative'
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      position: 'absolute',
      left: 0,
      top: 0,
      height: '100%',
      width: `${pct}%`,
      borderRadius: '999px',
      background: muted ? 'var(--screen-text-dim)' : 'var(--accent)'
    }
  })), /*#__PURE__*/React.createElement("span", {
    style: {
      font: '600 13px/1 var(--font-mono)',
      color: 'var(--screen-text-mut)',
      width: 28,
      textAlign: 'right'
    }
  }, pct));
}
Object.assign(__ds_scope, { VolumeBar });
})(); } catch (e) { __ds_ns.__errors.push({ path: "components/display/VolumeBar.jsx", error: String((e && e.message) || e) }); }

// components/system/Badge.jsx
try { (() => {
function _extends() { return _extends = Object.assign ? Object.assign.bind() : function (n) { for (var e = 1; e < arguments.length; e++) { var t = arguments[e]; for (var r in t) ({}).hasOwnProperty.call(t, r) && (n[r] = t[r]); } return n; }, _extends.apply(null, arguments); }
const TONES = {
  live: {
    bg: 'color-mix(in oklch, var(--accent) 22%, var(--screen-elev))',
    fg: 'var(--accent)'
  },
  neutral: {
    bg: 'var(--screen-elev-2)',
    fg: 'var(--screen-text-mut)'
  },
  hi: {
    bg: 'var(--accent)',
    fg: 'var(--accent-ink)'
  }
};

/** Small uppercase tag for source / quality / state labels. */
function Badge({
  children,
  tone = 'neutral',
  style,
  ...rest
}) {
  const t = TONES[tone] || TONES.neutral;
  return /*#__PURE__*/React.createElement("span", _extends({
    style: {
      display: 'inline-flex',
      alignItems: 'center',
      gap: 5,
      height: 22,
      padding: '0 9px',
      borderRadius: '999px',
      font: '700 10px/1 var(--font-mono)',
      letterSpacing: '0.1em',
      textTransform: 'uppercase',
      background: t.bg,
      color: t.fg,
      ...style
    }
  }, rest), tone === 'live' && /*#__PURE__*/React.createElement("span", {
    style: {
      width: 6,
      height: 6,
      borderRadius: '999px',
      background: 'var(--accent)'
    }
  }), children);
}
Object.assign(__ds_scope, { Badge });
})(); } catch (e) { __ds_ns.__errors.push({ path: "components/system/Badge.jsx", error: String((e && e.message) || e) }); }

// components/system/StatusBar.jsx
try { (() => {
function _extends() { return _extends = Object.assign ? Object.assign.bind() : function (n) { for (var e = 1; e < arguments.length; e++) { var t = arguments[e]; for (var r in t) ({}).hasOwnProperty.call(t, r) && (n[r] = t[r]); } return n; }, _extends.apply(null, arguments); }
/** Top status strip for the on-glass UI — room, clock, connectivity. */
function StatusBar({
  room = 'Kitchen',
  grouped = 0,
  time = '14:32',
  source = 'wifi',
  style,
  ...rest
}) {
  return /*#__PURE__*/React.createElement("div", _extends({
    style: {
      display: 'flex',
      alignItems: 'center',
      justifyContent: 'space-between',
      width: '100%',
      padding: '0 4px',
      color: 'var(--screen-text-mut)',
      ...style
    }
  }, rest), /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      alignItems: 'center',
      gap: 9
    }
  }, /*#__PURE__*/React.createElement("span", {
    style: {
      width: 9,
      height: 9,
      borderRadius: '999px',
      background: 'var(--accent)',
      boxShadow: '0 0 8px var(--accent)'
    }
  }), /*#__PURE__*/React.createElement("span", {
    style: {
      font: '600 15px/1 var(--font-ui)',
      color: 'var(--screen-text)'
    }
  }, room), grouped > 0 && /*#__PURE__*/React.createElement("span", {
    style: {
      font: '500 12px/1 var(--font-mono)',
      color: 'var(--screen-text-dim)'
    }
  }, "+", grouped)), /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      alignItems: 'center',
      gap: 12
    }
  }, /*#__PURE__*/React.createElement("i", {
    "data-lucide": source,
    width: 17,
    height: 17
  }), /*#__PURE__*/React.createElement("span", {
    style: {
      font: '600 14px/1 var(--font-mono)',
      color: 'var(--screen-text-mut)'
    }
  }, time)));
}
Object.assign(__ds_scope, { StatusBar });
})(); } catch (e) { __ds_ns.__errors.push({ path: "components/system/StatusBar.jsx", error: String((e && e.message) || e) }); }

// ui_kits/jukebox-screen/App.jsx
try { (() => {
// Device shell: matte face + screen + physical dial/buttons, with nav + volume overlay.
const {
  TransportButton,
  Dial
} = window.SonosJukeboxDesignSystem_e55a41;
const {
  useState,
  useEffect,
  useRef
} = React;
const RAIL = [{
  id: 'now',
  icon: 'disc-3',
  label: 'Now'
}, {
  id: 'radio',
  icon: 'radio',
  label: 'Radio'
}, {
  id: 'rooms',
  icon: 'speaker',
  label: 'Rooms'
}];
function App() {
  const [state, setState] = useState({
    view: 'now',
    playing: true,
    shuffle: false,
    activeRoom: 'kitchen',
    grouped: ['kitchen', 'bedroom'],
    source: 'RADIO',
    stationId: 'r6',
    track: window.JB.stations.find(s => s.id === 'r6'),
    vol: 64,
    volShow: false
  });
  const volTimer = useRef(null);
  const groupCount = Math.max(0, state.grouped.length - 1);
  const showVol = () => {
    setState(s => ({
      ...s,
      volShow: true
    }));
    clearTimeout(volTimer.current);
    volTimer.current = setTimeout(() => setState(s => ({
      ...s,
      volShow: false
    })), 1400);
  };
  const actions = {
    togglePlay: () => setState(s => ({
      ...s,
      playing: !s.playing
    })),
    toggleShuffle: () => setState(s => ({
      ...s,
      shuffle: !s.shuffle
    })),
    setView: view => setState(s => ({
      ...s,
      view
    })),
    setRoom: id => setState(s => ({
      ...s,
      activeRoom: id,
      grouped: s.grouped.includes(id) ? s.grouped : [...s.grouped, id]
    })),
    playStation: st => setState(s => ({
      ...s,
      stationId: st.id,
      source: 'RADIO',
      playing: true,
      view: 'now',
      track: {
        title: st.name,
        artist: st.sub.replace(/^Now:\s*/, ''),
        album: 'Live radio',
        elapsed: 0,
        duration: 0,
        art: st.art,
        quality: 'AAC · 128'
      }
    })),
    bumpVol: d => {
      setState(s => ({
        ...s,
        vol: Math.max(0, Math.min(100, s.vol + d))
      }));
      showVol();
    }
  };
  useEffect(() => {
    window.lucide && lucide.createIcons();
  });
  const nowTrack = {
    ...state.track,
    elapsed: state.track.duration ? 84 : 0,
    duration: state.track.duration || 330
  };
  const viewState = {
    ...state,
    track: state.view === 'now' ? nowTrack : state.track,
    groupCount
  };
  const View = {
    now: window.NowPlaying,
    radio: window.RadioBrowser,
    rooms: window.RoomPicker
  }[state.view];
  return /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      gap: 0,
      alignItems: 'stretch',
      padding: 22,
      background: 'linear-gradient(160deg,#fbfbf9,#f0f0ec)',
      borderRadius: 26,
      boxShadow: '0 40px 90px rgba(22,24,28,.22)'
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      width: 1024,
      height: 600,
      background: 'var(--screen-bg)',
      borderRadius: 16,
      overflow: 'hidden',
      display: 'flex',
      boxShadow: 'inset 0 2px 8px rgba(0,0,0,.5)',
      position: 'relative'
    }
  }, /*#__PURE__*/React.createElement("nav", {
    style: {
      width: 66,
      flex: 'none',
      borderRight: '1px solid var(--screen-line)',
      display: 'flex',
      flexDirection: 'column',
      alignItems: 'center',
      paddingTop: 22,
      gap: 10
    }
  }, RAIL.map(item => {
    const on = state.view === item.id;
    return /*#__PURE__*/React.createElement("button", {
      key: item.id,
      onClick: () => actions.setView(item.id),
      title: item.label,
      style: {
        width: 48,
        height: 48,
        borderRadius: 14,
        border: 'none',
        cursor: 'pointer',
        display: 'grid',
        placeItems: 'center',
        background: on ? 'color-mix(in oklch, var(--accent) 18%, transparent)' : 'transparent',
        color: on ? 'var(--accent)' : 'var(--screen-text-dim)'
      }
    }, /*#__PURE__*/React.createElement("i", {
      "data-lucide": item.icon,
      width: 22,
      height: 22
    }));
  })), /*#__PURE__*/React.createElement("div", {
    style: {
      flex: 1,
      minWidth: 0
    }
  }, /*#__PURE__*/React.createElement(View, {
    state: viewState,
    actions: actions
  })), state.volShow && /*#__PURE__*/React.createElement("div", {
    style: {
      position: 'absolute',
      inset: 0,
      display: 'grid',
      placeItems: 'center',
      background: 'rgba(8,9,11,.55)',
      backdropFilter: 'blur(3px)'
    }
  }, /*#__PURE__*/React.createElement(Dial, {
    value: state.vol,
    size: 220,
    label: "VOLUME"
  }))), /*#__PURE__*/React.createElement("div", {
    style: {
      width: 196,
      display: 'flex',
      flexDirection: 'column',
      alignItems: 'center',
      justifyContent: 'space-between',
      padding: '20px 0 26px'
    }
  }, /*#__PURE__*/React.createElement("button", {
    onClick: () => actions.bumpVol(6),
    onWheel: e => actions.bumpVol(e.deltaY < 0 ? 4 : -4),
    title: "Rotary dial — turn for volume, press to select",
    style: {
      width: 130,
      height: 130,
      borderRadius: '999px',
      border: 'none',
      cursor: 'pointer',
      position: 'relative',
      background: 'radial-gradient(circle at 38% 32%,#4a4d55,#2a2c31 62%)',
      boxShadow: '0 10px 22px rgba(0,0,0,.3), inset 0 1px 2px rgba(255,255,255,.18)'
    }
  }, /*#__PURE__*/React.createElement("span", {
    style: {
      position: 'absolute',
      inset: 12,
      borderRadius: '999px',
      background: 'repeating-conic-gradient(from 0deg, rgba(255,255,255,.10) 0 3deg, transparent 3deg 9deg)'
    }
  }), /*#__PURE__*/React.createElement("span", {
    style: {
      position: 'absolute',
      top: 12,
      left: '50%',
      transform: 'translateX(-50%)',
      width: 4,
      height: 18,
      borderRadius: 3,
      background: 'var(--accent)',
      boxShadow: '0 0 8px var(--accent)'
    }
  })), /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'grid',
      gridTemplateColumns: 'repeat(2,1fr)',
      gap: 16
    }
  }, /*#__PURE__*/React.createElement(TransportButton, {
    icon: state.playing ? 'pause' : 'play',
    variant: "solid",
    size: "md",
    onClick: actions.togglePlay
  }), /*#__PURE__*/React.createElement(TransportButton, {
    icon: "room",
    variant: "elevated",
    size: "md",
    style: {
      color: 'var(--ink-700)',
      background: '#fff',
      boxShadow: '0 3px 8px rgba(0,0,0,.12)'
    },
    onClick: () => actions.setView('rooms')
  }), /*#__PURE__*/React.createElement(TransportButton, {
    icon: "prev",
    variant: "elevated",
    size: "md",
    style: {
      color: 'var(--ink-700)',
      background: '#fff',
      boxShadow: '0 3px 8px rgba(0,0,0,.12)'
    }
  }), /*#__PURE__*/React.createElement(TransportButton, {
    icon: "next",
    variant: "elevated",
    size: "md",
    style: {
      color: 'var(--ink-700)',
      background: '#fff',
      boxShadow: '0 3px 8px rgba(0,0,0,.12)'
    }
  }))));
}
window.App = App;
})(); } catch (e) { __ds_ns.__errors.push({ path: "ui_kits/jukebox-screen/App.jsx", error: String((e && e.message) || e) }); }

// ui_kits/jukebox-screen/NowPlaying.jsx
try { (() => {
// Now Playing screen — album art, meta, scrubber, transport.
const {
  StatusBar,
  TransportButton,
  Scrubber,
  Badge,
  VolumeBar
} = window.SonosJukeboxDesignSystem_e55a41;
function NowPlaying({
  state,
  actions
}) {
  const t = state.track;
  const room = window.JB.rooms.find(r => r.id === state.activeRoom) || window.JB.rooms[0];
  return /*#__PURE__*/React.createElement("div", {
    style: {
      height: '100%',
      display: 'flex',
      flexDirection: 'column',
      padding: '22px 30px 18px'
    }
  }, /*#__PURE__*/React.createElement(StatusBar, {
    room: room.name,
    grouped: state.groupCount,
    time: "14:32",
    source: "wifi"
  }), /*#__PURE__*/React.createElement("div", {
    style: {
      flex: 1,
      display: 'flex',
      gap: 34,
      alignItems: 'center',
      minHeight: 0
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      width: 280,
      height: 280,
      borderRadius: 20,
      flex: 'none',
      background: t.art,
      boxShadow: '0 24px 60px rgba(0,0,0,.5)',
      position: 'relative',
      overflow: 'hidden'
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      position: 'absolute',
      inset: 0,
      boxShadow: 'inset 0 0 0 1px rgba(255,255,255,.08)',
      borderRadius: 20
    }
  })), /*#__PURE__*/React.createElement("div", {
    style: {
      flex: 1,
      minWidth: 0
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      gap: 8,
      marginBottom: 16
    }
  }, /*#__PURE__*/React.createElement(Badge, {
    tone: "hi"
  }, state.source), /*#__PURE__*/React.createElement(Badge, null, t.quality)), /*#__PURE__*/React.createElement("div", {
    style: {
      font: '800 52px/1.02 var(--font-ui)',
      letterSpacing: '-.02em',
      color: 'var(--screen-text)'
    }
  }, t.title), /*#__PURE__*/React.createElement("div", {
    style: {
      font: '500 22px/1.3 var(--font-ui)',
      color: 'var(--screen-text-mut)',
      marginTop: 8
    }
  }, t.artist, " \xB7 ", t.album), /*#__PURE__*/React.createElement("div", {
    style: {
      marginTop: 28
    }
  }, /*#__PURE__*/React.createElement(Scrubber, {
    elapsed: state.playing ? t.elapsed : t.elapsed,
    duration: t.duration
  })))), /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      alignItems: 'center',
      gap: 22
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      flex: 1
    }
  }, /*#__PURE__*/React.createElement(VolumeBar, {
    value: room.vol
  })), /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      alignItems: 'center',
      gap: 14
    }
  }, /*#__PURE__*/React.createElement(TransportButton, {
    icon: "prev",
    variant: "ghost"
  }), /*#__PURE__*/React.createElement(TransportButton, {
    icon: state.playing ? 'pause' : 'play',
    variant: "solid",
    size: "lg",
    onClick: actions.togglePlay
  }), /*#__PURE__*/React.createElement(TransportButton, {
    icon: "next",
    variant: "ghost"
  }), /*#__PURE__*/React.createElement(TransportButton, {
    icon: "shuffle",
    variant: "ghost",
    active: state.shuffle,
    onClick: actions.toggleShuffle
  }))));
}
window.NowPlaying = NowPlaying;
})(); } catch (e) { __ds_ns.__errors.push({ path: "ui_kits/jukebox-screen/NowPlaying.jsx", error: String((e && e.message) || e) }); }

// ui_kits/jukebox-screen/RadioBrowser.jsx
try { (() => {
// Radio browser — station list.
const {
  StatusBar,
  ListRow,
  Badge
} = window.SonosJukeboxDesignSystem_e55a41;
function RadioBrowser({
  state,
  actions
}) {
  const room = window.JB.rooms.find(r => r.id === state.activeRoom) || window.JB.rooms[0];
  const genres = ['Featured', 'Music', 'Talk', 'Local', 'Podcasts'];
  return /*#__PURE__*/React.createElement("div", {
    style: {
      height: '100%',
      display: 'flex',
      flexDirection: 'column',
      padding: '22px 30px 12px'
    }
  }, /*#__PURE__*/React.createElement(StatusBar, {
    room: room.name,
    grouped: state.groupCount,
    time: "14:32",
    source: "wifi"
  }), /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      alignItems: 'baseline',
      justifyContent: 'space-between',
      margin: '8px 0 14px'
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      font: '800 30px/1 var(--font-ui)',
      letterSpacing: '-.02em',
      color: 'var(--screen-text)'
    }
  }, "Radio"), /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      gap: 8
    }
  }, genres.map((g, i) => /*#__PURE__*/React.createElement("span", {
    key: g,
    style: {
      font: '600 13px/1 var(--font-ui)',
      padding: '8px 14px',
      borderRadius: '999px',
      color: i === 0 ? 'var(--accent)' : 'var(--screen-text-dim)',
      background: i === 0 ? 'color-mix(in oklch, var(--accent) 16%, transparent)' : 'transparent'
    }
  }, g)))), /*#__PURE__*/React.createElement("div", {
    style: {
      flex: 1,
      overflow: 'auto',
      display: 'flex',
      flexDirection: 'column',
      gap: 2
    }
  }, window.JB.stations.map(s => /*#__PURE__*/React.createElement(ListRow, {
    key: s.id,
    title: s.name,
    subtitle: s.sub,
    artColor: s.art,
    playing: state.stationId === s.id,
    selected: state.stationId === s.id,
    trailing: /*#__PURE__*/React.createElement(Badge, {
      tone: "live"
    }, "LIVE"),
    onClick: () => actions.playStation(s)
  }))));
}
window.RadioBrowser = RadioBrowser;
})(); } catch (e) { __ds_ns.__errors.push({ path: "ui_kits/jukebox-screen/RadioBrowser.jsx", error: String((e && e.message) || e) }); }

// ui_kits/jukebox-screen/RoomPicker.jsx
try { (() => {
// Room picker — select active room, group, per-room volume.
const {
  StatusBar,
  RoomChip,
  VolumeBar
} = window.SonosJukeboxDesignSystem_e55a41;
function RoomPicker({
  state,
  actions
}) {
  const active = state.activeRoom;
  const grouped = state.grouped;
  return /*#__PURE__*/React.createElement("div", {
    style: {
      height: '100%',
      display: 'flex',
      flexDirection: 'column',
      padding: '22px 30px 16px'
    }
  }, /*#__PURE__*/React.createElement(StatusBar, {
    room: window.JB.rooms.find(r => r.id === active).name,
    grouped: state.groupCount,
    time: "14:32",
    source: "wifi"
  }), /*#__PURE__*/React.createElement("div", {
    style: {
      font: '800 30px/1 var(--font-ui)',
      letterSpacing: '-.02em',
      color: 'var(--screen-text)',
      margin: '8px 0 16px'
    }
  }, "Rooms"), /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      flexWrap: 'wrap',
      gap: 10,
      marginBottom: 22
    }
  }, window.JB.rooms.map(r => /*#__PURE__*/React.createElement(RoomChip, {
    key: r.id,
    name: r.name,
    playing: r.playing,
    selected: active === r.id,
    grouped: grouped.includes(r.id) && r.id !== active,
    onClick: () => actions.setRoom(r.id)
  }))), /*#__PURE__*/React.createElement("div", {
    style: {
      font: '700 10px/1 var(--font-mono)',
      letterSpacing: '.14em',
      textTransform: 'uppercase',
      color: 'var(--screen-text-dim)',
      marginBottom: 14
    }
  }, "Volume \xB7 grouped rooms"), /*#__PURE__*/React.createElement("div", {
    style: {
      flex: 1,
      display: 'flex',
      flexDirection: 'column',
      gap: 20,
      overflow: 'auto'
    }
  }, window.JB.rooms.filter(r => r.id === active || grouped.includes(r.id)).map(r => /*#__PURE__*/React.createElement("div", {
    key: r.id,
    style: {
      display: 'flex',
      alignItems: 'center',
      gap: 18
    }
  }, /*#__PURE__*/React.createElement("span", {
    style: {
      width: 130,
      font: '600 16px/1 var(--font-ui)',
      color: 'var(--screen-text)'
    }
  }, r.name), /*#__PURE__*/React.createElement("div", {
    style: {
      flex: 1
    }
  }, /*#__PURE__*/React.createElement(VolumeBar, {
    value: r.vol
  }))))));
}
window.RoomPicker = RoomPicker;
})(); } catch (e) { __ds_ns.__errors.push({ path: "ui_kits/jukebox-screen/RoomPicker.jsx", error: String((e && e.message) || e) }); }

// ui_kits/jukebox-screen/data.js
try { (() => {
// Sonos Jukebox — sample content for the UI kit (fake data).
window.JB = {
  rooms: [{
    id: 'kitchen',
    name: 'Kitchen',
    vol: 64,
    playing: true
  }, {
    id: 'living',
    name: 'Living Room',
    vol: 40,
    playing: false
  }, {
    id: 'bedroom',
    name: 'Bedroom',
    vol: 22,
    playing: false
  }, {
    id: 'office',
    name: 'Office',
    vol: 55,
    playing: false
  }, {
    id: 'bath',
    name: 'Bathroom',
    vol: 30,
    playing: false
  }],
  track: {
    title: 'Teardrop',
    artist: 'Massive Attack',
    album: 'Mezzanine',
    elapsed: 84,
    duration: 330,
    art: 'linear-gradient(135deg,#7a3b2e,#2b1b14)',
    quality: 'FLAC · 44.1'
  },
  stations: [{
    id: 'r6',
    name: 'BBC Radio 6 Music',
    sub: 'Now: Lauren Laverne',
    art: 'linear-gradient(135deg,#e8892b,#7a3b12)',
    live: true
  }, {
    id: 'kexp',
    name: 'KEXP 90.3',
    sub: 'Seattle · Variety',
    art: 'linear-gradient(135deg,#2f6f45,#14301f)',
    live: true
  }, {
    id: 'nts',
    name: 'NTS Radio 1',
    sub: 'London · Eclectic',
    art: 'linear-gradient(135deg,#2b3a52,#12161f)',
    live: true
  }, {
    id: 'jazz',
    name: 'Jazz24',
    sub: 'Straight-ahead jazz',
    art: 'linear-gradient(135deg,#5a3d6b,#241628)',
    live: true
  }, {
    id: 'fip',
    name: 'FIP',
    sub: 'Paris · No genre',
    art: 'linear-gradient(135deg,#b23b4e,#3a1218)',
    live: true
  }, {
    id: 'classic',
    name: 'Classic FM',
    sub: 'Now: Einaudi',
    art: 'linear-gradient(135deg,#3a5240,#161f18)',
    live: true
  }]
};
})(); } catch (e) { __ds_ns.__errors.push({ path: "ui_kits/jukebox-screen/data.js", error: String((e && e.message) || e) }); }

__ds_ns.Dial = __ds_scope.Dial;

__ds_ns.RoomChip = __ds_scope.RoomChip;

__ds_ns.TransportButton = __ds_scope.TransportButton;

__ds_ns.ListRow = __ds_scope.ListRow;

__ds_ns.Scrubber = __ds_scope.Scrubber;

__ds_ns.VolumeBar = __ds_scope.VolumeBar;

__ds_ns.Badge = __ds_scope.Badge;

__ds_ns.StatusBar = __ds_scope.StatusBar;

})();
