/* @ds-bundle: {"format":4,"namespace":"SonosJukeboxDesignSystem_e55a41","components":[{"name":"Dial","sourcePath":"components/controls/Dial.jsx"},{"name":"RoomChip","sourcePath":"components/controls/RoomChip.jsx"},{"name":"TransportButton","sourcePath":"components/controls/TransportButton.jsx"},{"name":"ListRow","sourcePath":"components/display/ListRow.jsx"},{"name":"Scrubber","sourcePath":"components/display/Scrubber.jsx"},{"name":"VolumeBar","sourcePath":"components/display/VolumeBar.jsx"},{"name":"Badge","sourcePath":"components/system/Badge.jsx"},{"name":"Icon","sourcePath":"components/system/Icon.jsx"},{"name":"StatusBar","sourcePath":"components/system/StatusBar.jsx"}],"sourceHashes":{"components/controls/Dial.jsx":"0ee94573c599","components/controls/RoomChip.jsx":"09e28baacfba","components/controls/TransportButton.jsx":"9709e795a46b","components/display/ListRow.jsx":"60d1427c1b3b","components/display/Scrubber.jsx":"c16be3c84331","components/display/VolumeBar.jsx":"d61102254179","components/system/Badge.jsx":"c452e72a5adb","components/system/Icon.jsx":"c28be0222acd","components/system/StatusBar.jsx":"378be7f6bdad","ui_kits/jukebox-screen/App.jsx":"8a9769b128b2","ui_kits/jukebox-screen/NowPlaying.jsx":"da51f99cb686","ui_kits/jukebox-screen/RadioBrowser.jsx":"5319ca1ecf08","ui_kits/jukebox-screen/RoomPicker.jsx":"b204dfe7ff3f","ui_kits/jukebox-screen/data.js":"9b7db066cee0"},"inlinedExternals":[],"unexposedExports":[]} */

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

// components/system/Icon.jsx
try { (() => {
function _extends() { return _extends = Object.assign ? Object.assign.bind() : function (n) { for (var e = 1; e < arguments.length; e++) { var t = arguments[e]; for (var r in t) ({}).hasOwnProperty.call(t, r) && (n[r] = t[r]); } return n; }, _extends.apply(null, arguments); }
const pascal = n => String(n || '').replace(/(^\w|-\w)/g, s => s.replace('-', '').toUpperCase());

/**
 * Renders a Lucide glyph inside a React-owned wrapper. React never reconciles
 * the SVG itself, so conditionally rendering or swapping icons is safe.
 */
function Icon({
  name,
  size = 20,
  strokeWidth = 2,
  color = 'currentColor',
  style,
  ...rest
}) {
  const ref = React.useRef(null);
  React.useEffect(() => {
    const el = ref.current;
    if (!el) return;
    el.replaceChildren();
    const lib = typeof window !== 'undefined' && window.lucide || null;
    const node = lib && lib.icons && (lib.icons[pascal(name)] || lib.icons[name]);
    if (!node || !lib.createElement) return;
    const svg = lib.createElement(node);
    svg.setAttribute('width', size);
    svg.setAttribute('height', size);
    svg.setAttribute('stroke-width', strokeWidth);
    svg.setAttribute('stroke', color);
    svg.style.display = 'block';
    el.appendChild(svg);
  }, [name, size, strokeWidth, color]);
  return /*#__PURE__*/React.createElement("span", _extends({
    ref: ref,
    "aria-hidden": "true",
    style: {
      display: 'inline-flex',
      width: size,
      height: size,
      flex: 'none',
      color,
      ...style
    }
  }, rest));
}
Object.assign(__ds_scope, { Icon });
})(); } catch (e) { __ds_ns.__errors.push({ path: "components/system/Icon.jsx", error: String((e && e.message) || e) }); }

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
  }, rest), /*#__PURE__*/React.createElement(__ds_scope.Icon, {
    name: ICONS[icon] || icon,
    size: s.i,
    strokeWidth: 2.2
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
  }, playing && /*#__PURE__*/React.createElement(__ds_scope.Icon, {
    name: "audio-lines",
    size: 22,
    color: "var(--accent)"
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
  }, rest), /*#__PURE__*/React.createElement(__ds_scope.Icon, {
    name: icon,
    size: 22,
    color: "var(--screen-text-mut)"
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
  }, /*#__PURE__*/React.createElement(__ds_scope.Icon, {
    name: source,
    size: 17
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
const DS = window.SonosJukeboxDesignSystem_e55a41;
const {
  TransportButton,
  Dial
} = DS;
const Icon = DS.Icon || window.JBIcon;
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
    track: (() => {
      const st = window.JB.stations.find(s => s.id === 'r6');
      return {
        title: st.name,
        artist: 'Lauren Laverne',
        album: 'Live radio',
        elapsed: 0,
        duration: 0,
        art: st.art,
        quality: 'AAC · 128'
      };
    })(),
    vol: 64,
    volShow: false,
    artLayout: 'split',
    rooms: window.JB.rooms.map(r => ({
      ...r,
      playing: ['kitchen', 'bedroom'].includes(r.id)
    }))
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
    },
    cycleArt: () => setState(s => ({
      ...s,
      view: 'now',
      artLayout: s.artLayout === 'split' ? 'hero' : s.artLayout === 'hero' ? 'bleed' : 'split'
    })),
    setActive: id => setState(s => ({
      ...s,
      activeRoom: id,
      grouped: s.grouped.includes(id) ? s.grouped : [...s.grouped, id]
    })),
    toggleGroup: id => setState(s => {
      const inG = s.grouped.includes(id);
      if (inG && id === s.activeRoom) return s; // main room stays in its own group
      const grouped = inG ? s.grouped.filter(g => g !== id) : [...s.grouped, id];
      const anchor = s.rooms.find(r => r.id === s.activeRoom);
      return {
        ...s,
        grouped,
        rooms: s.rooms.map(r => r.id === id ? {
          ...r,
          playing: inG ? false : anchor.playing
        } : r)
      };
    }),
    setRoomVol: (id, d) => setState(s => ({
      ...s,
      rooms: s.rooms.map(r => r.id === id ? {
        ...r,
        vol: Math.max(0, Math.min(100, r.vol + d))
      } : r)
    })),
    toggleRoomPlay: id => setState(s => ({
      ...s,
      rooms: s.rooms.map(r => r.id === id ? {
        ...r,
        playing: !r.playing
      } : r)
    })),
    toggleGroupPlay: () => setState(s => {
      const on = !s.rooms.filter(r => s.grouped.includes(r.id)).some(r => r.playing);
      return {
        ...s,
        playing: on,
        rooms: s.rooms.map(r => s.grouped.includes(r.id) ? {
          ...r,
          playing: on
        } : r)
      };
    }),
    ungroupAll: () => setState(s => ({
      ...s,
      grouped: [s.activeRoom],
      rooms: s.rooms.map(r => r.id === s.activeRoom ? r : {
        ...r,
        playing: false
      })
    }))
  };
  const isLive = !state.track.duration;
  const nowTrack = {
    ...state.track,
    elapsed: isLive ? 0 : 84,
    duration: state.track.duration || 0,
    live: isLive
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
    }, /*#__PURE__*/React.createElement(Icon, {
      name: item.icon,
      size: 22
    }));
  }), /*#__PURE__*/React.createElement("button", {
    onClick: actions.cycleArt,
    title: `Album art: ${state.artLayout} — tap to change`,
    style: {
      width: 48,
      height: 48,
      marginTop: 'auto',
      marginBottom: 18,
      borderRadius: 14,
      border: '1px solid var(--screen-line)',
      cursor: 'pointer',
      display: 'grid',
      placeItems: 'center',
      background: 'transparent',
      color: 'var(--screen-text-dim)'
    }
  }, /*#__PURE__*/React.createElement(Icon, {
    name: state.artLayout === 'bleed' ? 'maximize' : state.artLayout === 'hero' ? 'square' : 'columns-2',
    size: 20
  }))), /*#__PURE__*/React.createElement("div", {
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
    title: "Rotary dial \u2014 turn for volume, press to select",
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
      background: 'repeating-conic-gradient(from 0deg, rgba(255,255,255,.055) 0 1.6deg, rgba(0,0,0,.05) 1.6deg 3.2deg, transparent 3.2deg 9deg)'
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
// Now Playing screen — three art-prominence layouts: split / hero / full-bleed.
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
  const room = state.rooms.find(r => r.id === state.activeRoom) || state.rooms[0];
  const layout = state.artLayout || 'split';
  const meta = /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      gap: 8
    }
  }, /*#__PURE__*/React.createElement(Badge, {
    tone: "hi"
  }, state.source), /*#__PURE__*/React.createElement(Badge, null, t.quality));
  const transport = /*#__PURE__*/React.createElement("div", {
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
  }));
  const artTile = size => /*#__PURE__*/React.createElement("div", {
    style: {
      width: size,
      height: size,
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
  }));

  // ---- FULL-BLEED: art fills the screen, text over a protection gradient ----
  if (layout === 'bleed') {
    return /*#__PURE__*/React.createElement("div", {
      style: {
        height: '100%',
        boxSizing: 'border-box',
        position: 'relative',
        overflow: 'hidden'
      }
    }, /*#__PURE__*/React.createElement("div", {
      style: {
        position: 'absolute',
        inset: 0,
        background: t.art
      }
    }), /*#__PURE__*/React.createElement("div", {
      style: {
        position: 'absolute',
        inset: 0,
        background: 'linear-gradient(180deg, rgba(8,9,11,.72) 0%, rgba(8,9,11,.18) 32%, rgba(8,9,11,.86) 78%, rgba(8,9,11,.96) 100%)'
      }
    }), /*#__PURE__*/React.createElement("div", {
      style: {
        position: 'relative',
        height: '100%',
        boxSizing: 'border-box',
        display: 'flex',
        flexDirection: 'column',
        padding: '22px 34px 20px'
      }
    }, /*#__PURE__*/React.createElement(StatusBar, {
      room: room.name,
      grouped: state.groupCount,
      time: "14:32",
      source: "wifi"
    }), /*#__PURE__*/React.createElement("div", {
      style: {
        flex: 1,
        minHeight: 0
      }
    }), /*#__PURE__*/React.createElement("div", {
      style: {
        marginBottom: 18
      }
    }, meta, /*#__PURE__*/React.createElement("div", {
      style: {
        font: '800 56px/1 var(--font-ui)',
        letterSpacing: '-.025em',
        color: 'var(--screen-text)',
        marginTop: 14,
        textShadow: '0 2px 24px rgba(0,0,0,.5)'
      }
    }, t.title), /*#__PURE__*/React.createElement("div", {
      style: {
        font: '500 24px/1.3 var(--font-ui)',
        color: 'var(--screen-text-mut)',
        marginTop: 8
      }
    }, t.artist, " \xB7 ", t.album)), /*#__PURE__*/React.createElement(Scrubber, {
      elapsed: t.elapsed,
      duration: t.duration,
      live: !t.duration
    }), /*#__PURE__*/React.createElement("div", {
      style: {
        display: 'flex',
        alignItems: 'center',
        gap: 22,
        marginTop: 16
      }
    }, /*#__PURE__*/React.createElement("div", {
      style: {
        flex: 1
      }
    }, /*#__PURE__*/React.createElement(VolumeBar, {
      value: room.vol
    })), transport)));
  }

  // ---- HERO: oversized centred art, compact text beneath ----
  if (layout === 'hero') {
    return /*#__PURE__*/React.createElement("div", {
      style: {
        height: '100%',
        boxSizing: 'border-box',
        display: 'flex',
        flexDirection: 'column',
        padding: '20px 30px 18px',
        alignItems: 'stretch'
      }
    }, /*#__PURE__*/React.createElement(StatusBar, {
      room: room.name,
      grouped: state.groupCount,
      time: "14:32",
      source: "wifi"
    }), /*#__PURE__*/React.createElement("div", {
      style: {
        flex: 1,
        minHeight: 0,
        display: 'flex',
        gap: 30,
        alignItems: 'center',
        justifyContent: 'center',
        overflow: 'hidden',
        padding: '10px 0'
      }
    }, artTile(300), /*#__PURE__*/React.createElement("div", {
      style: {
        maxWidth: 360
      }
    }, meta, /*#__PURE__*/React.createElement("div", {
      style: {
        font: '800 44px/1.02 var(--font-ui)',
        letterSpacing: '-.02em',
        color: 'var(--screen-text)',
        marginTop: 14
      }
    }, t.title), /*#__PURE__*/React.createElement("div", {
      style: {
        font: '500 20px/1.3 var(--font-ui)',
        color: 'var(--screen-text-mut)',
        marginTop: 8
      }
    }, t.artist), /*#__PURE__*/React.createElement("div", {
      style: {
        font: '400 16px/1.3 var(--font-ui)',
        color: 'var(--screen-text-dim)',
        marginTop: 4
      }
    }, t.album), /*#__PURE__*/React.createElement("div", {
      style: {
        marginTop: 22
      }
    }, /*#__PURE__*/React.createElement(Scrubber, {
      elapsed: t.elapsed,
      duration: t.duration,
      live: !t.duration
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
    })), transport));
  }

  // ---- SPLIT (default) ----
  return /*#__PURE__*/React.createElement("div", {
    style: {
      height: '100%',
      boxSizing: 'border-box',
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
      minHeight: 0,
      display: 'flex',
      gap: 34,
      alignItems: 'center',
      overflow: 'hidden'
    }
  }, artTile(268), /*#__PURE__*/React.createElement("div", {
    style: {
      flex: 1,
      minWidth: 0
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      marginBottom: 16
    }
  }, meta), /*#__PURE__*/React.createElement("div", {
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
    elapsed: t.elapsed,
    duration: t.duration,
    live: !t.duration
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
  })), transport));
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
  const room = state.rooms.find(r => r.id === state.activeRoom) || state.rooms[0];
  const genres = ['Featured', 'Music', 'Talk', 'Local', 'Podcasts'];
  return /*#__PURE__*/React.createElement("div", {
    style: {
      height: '100%',
      boxSizing: 'border-box',
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
// Rooms — grouping, per-room volume, per-room play/pause.
const DS = window.SonosJukeboxDesignSystem_e55a41;
const {
  StatusBar,
  VolumeBar,
  TransportButton,
  Badge
} = DS;
const Icon = DS.Icon || window.JBIcon;
function RoomRow({
  room,
  active,
  inGroup,
  groupSize,
  actions
}) {
  return /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      alignItems: 'center',
      gap: 16,
      padding: '12px 16px',
      borderRadius: 'var(--r-md)',
      background: inGroup ? 'var(--screen-elev)' : 'transparent',
      boxShadow: active ? 'inset 0 0 0 1px var(--accent)' : inGroup ? 'inset 0 0 0 1px var(--screen-line)' : 'none'
    }
  }, /*#__PURE__*/React.createElement("button", {
    onClick: () => actions.toggleGroup(room.id),
    title: inGroup ? 'Remove from group' : 'Add to group',
    style: {
      width: 26,
      height: 26,
      flex: 'none',
      borderRadius: 8,
      cursor: 'pointer',
      display: 'grid',
      placeItems: 'center',
      border: inGroup ? 'none' : '1.5px solid var(--screen-text-dim)',
      background: inGroup ? 'var(--accent)' : 'transparent',
      color: 'var(--accent-ink)'
    }
  }, inGroup && /*#__PURE__*/React.createElement(Icon, {
    name: "check",
    size: 16
  })), /*#__PURE__*/React.createElement("button", {
    onClick: () => actions.setActive(room.id),
    style: {
      width: 180,
      flex: 'none',
      textAlign: 'left',
      border: 'none',
      background: 'transparent',
      cursor: 'pointer',
      padding: 0
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      alignItems: 'center',
      gap: 8
    }
  }, /*#__PURE__*/React.createElement("span", {
    style: {
      font: '600 17px/1.2 var(--font-ui)',
      color: inGroup ? 'var(--screen-text)' : 'var(--screen-text-mut)'
    }
  }, room.name), active && /*#__PURE__*/React.createElement(Badge, {
    tone: "live"
  }, "MAIN")), /*#__PURE__*/React.createElement("div", {
    style: {
      font: '400 12px/1.3 var(--font-ui)',
      color: 'var(--screen-text-dim)',
      marginTop: 3
    }
  }, room.playing ? inGroup && groupSize > 1 ? 'Playing · grouped' : 'Playing' : 'Idle')), /*#__PURE__*/React.createElement("div", {
    style: {
      flex: 1,
      minWidth: 0,
      opacity: room.playing ? 1 : .45
    }
  }, /*#__PURE__*/React.createElement(VolumeBar, {
    value: room.vol,
    muted: room.vol === 0
  })), /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      alignItems: 'center',
      gap: 6,
      flex: 'none'
    }
  }, /*#__PURE__*/React.createElement("button", {
    onClick: () => actions.setRoomVol(room.id, -5),
    style: stepStyle
  }, /*#__PURE__*/React.createElement(Icon, {
    name: "minus",
    size: 15
  })), /*#__PURE__*/React.createElement("button", {
    onClick: () => actions.setRoomVol(room.id, 5),
    style: stepStyle
  }, /*#__PURE__*/React.createElement(Icon, {
    name: "plus",
    size: 15
  })), /*#__PURE__*/React.createElement(TransportButton, {
    icon: room.playing ? 'pause' : 'play',
    size: "sm",
    variant: room.playing ? 'solid' : 'elevated',
    onClick: () => actions.toggleRoomPlay(room.id)
  })));
}
const stepStyle = {
  width: 32,
  height: 32,
  borderRadius: 10,
  border: '1px solid var(--screen-line)',
  background: 'var(--screen-elev-2)',
  color: 'var(--screen-text-mut)',
  cursor: 'pointer',
  display: 'grid',
  placeItems: 'center'
};
function RoomPicker({
  state,
  actions
}) {
  const rooms = state.rooms;
  const active = rooms.find(r => r.id === state.activeRoom) || rooms[0];
  const group = rooms.filter(r => state.grouped.includes(r.id));
  const groupVol = group.length ? Math.round(group.reduce((a, r) => a + r.vol, 0) / group.length) : 0;
  const anyPlaying = group.some(r => r.playing);
  return /*#__PURE__*/React.createElement("div", {
    style: {
      height: '100%',
      boxSizing: 'border-box',
      display: 'flex',
      flexDirection: 'column',
      padding: '22px 30px 16px'
    }
  }, /*#__PURE__*/React.createElement(StatusBar, {
    room: active.name,
    grouped: state.groupCount,
    time: "14:32",
    source: "wifi"
  }), /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      alignItems: 'center',
      gap: 18,
      margin: '12px 0 14px',
      padding: '14px 18px',
      borderRadius: 'var(--r-lg)',
      background: 'var(--screen-elev)',
      border: '1px solid var(--screen-line)'
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      flex: 'none'
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      font: '800 24px/1 var(--font-ui)',
      letterSpacing: '-.02em',
      color: 'var(--screen-text)'
    }
  }, group.length > 1 ? `${group.length} rooms` : active.name), /*#__PURE__*/React.createElement("div", {
    style: {
      font: '400 13px/1.3 var(--font-ui)',
      color: 'var(--screen-text-dim)',
      marginTop: 4
    }
  }, group.length > 1 ? group.map(r => r.name).join(' · ') : 'Playing alone')), /*#__PURE__*/React.createElement("div", {
    style: {
      flex: 1,
      minWidth: 0
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      font: '700 10px/1 var(--font-mono)',
      letterSpacing: '.14em',
      textTransform: 'uppercase',
      color: 'var(--screen-text-dim)',
      marginBottom: 8
    }
  }, "Group volume"), /*#__PURE__*/React.createElement(VolumeBar, {
    value: groupVol
  })), /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      gap: 10,
      flex: 'none'
    }
  }, /*#__PURE__*/React.createElement("button", {
    onClick: actions.ungroupAll,
    style: {
      ...stepStyle,
      width: 'auto',
      padding: '0 14px',
      height: 40,
      font: '700 11px/1 var(--font-mono)',
      letterSpacing: '.1em',
      textTransform: 'uppercase'
    }
  }, "Ungroup"), /*#__PURE__*/React.createElement(TransportButton, {
    icon: anyPlaying ? 'pause' : 'play',
    variant: "solid",
    size: "md",
    onClick: actions.toggleGroupPlay
  }))), /*#__PURE__*/React.createElement("div", {
    style: {
      font: '700 10px/1 var(--font-mono)',
      letterSpacing: '.14em',
      textTransform: 'uppercase',
      color: 'var(--screen-text-dim)',
      marginBottom: 8
    }
  }, "All rooms \xB7 tap \u2713 to group"), /*#__PURE__*/React.createElement("div", {
    style: {
      flex: 1,
      overflow: 'auto',
      display: 'flex',
      flexDirection: 'column',
      gap: 4
    }
  }, rooms.map(r => /*#__PURE__*/React.createElement(RoomRow, {
    key: r.id,
    room: r,
    active: r.id === state.activeRoom,
    inGroup: state.grouped.includes(r.id),
    groupSize: group.length,
    actions: actions
  }))));
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

// Fallback Icon — used only if the compiled bundle predates the Icon component.
window.JBIcon = function JBIcon({
  name,
  size = 20,
  strokeWidth = 2,
  color = 'currentColor',
  style
}) {
  const ref = React.useRef(null);
  React.useEffect(() => {
    const el = ref.current;
    if (!el) return;
    el.replaceChildren();
    const lib = window.lucide;
    const pascal = String(name || '').replace(/(^\w|-\w)/g, s => s.replace('-', '').toUpperCase());
    const node = lib && lib.icons && (lib.icons[pascal] || lib.icons[name]);
    if (!node || !lib.createElement) return;
    const svg = lib.createElement(node);
    svg.setAttribute('width', size);
    svg.setAttribute('height', size);
    svg.setAttribute('stroke-width', strokeWidth);
    svg.setAttribute('stroke', color);
    svg.style.display = 'block';
    el.appendChild(svg);
  }, [name, size, strokeWidth, color]);
  return React.createElement('span', {
    ref,
    'aria-hidden': 'true',
    style: Object.assign({
      display: 'inline-flex',
      width: size,
      height: size,
      flex: 'none',
      color
    }, style)
  });
};
})(); } catch (e) { __ds_ns.__errors.push({ path: "ui_kits/jukebox-screen/data.js", error: String((e && e.message) || e) }); }

__ds_ns.Dial = __ds_scope.Dial;

__ds_ns.RoomChip = __ds_scope.RoomChip;

__ds_ns.TransportButton = __ds_scope.TransportButton;

__ds_ns.ListRow = __ds_scope.ListRow;

__ds_ns.Scrubber = __ds_scope.Scrubber;

__ds_ns.VolumeBar = __ds_scope.VolumeBar;

__ds_ns.Badge = __ds_scope.Badge;

__ds_ns.Icon = __ds_scope.Icon;

__ds_ns.StatusBar = __ds_scope.StatusBar;

})();
