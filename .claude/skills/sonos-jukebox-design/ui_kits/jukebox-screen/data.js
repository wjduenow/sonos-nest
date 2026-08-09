// Sonos Jukebox — sample content for the UI kit (fake data).
window.JB = {
  rooms: [
    { id:'kitchen', name:'Kitchen', vol:64, playing:true },
    { id:'living',  name:'Living Room', vol:40, playing:false },
    { id:'bedroom', name:'Bedroom', vol:22, playing:false },
    { id:'office',  name:'Office', vol:55, playing:false },
    { id:'bath',    name:'Bathroom', vol:30, playing:false },
  ],
  track: { title:'Teardrop', artist:'Massive Attack', album:'Mezzanine', elapsed:84, duration:330, art:'linear-gradient(135deg,#7a3b2e,#2b1b14)', quality:'FLAC · 44.1' },
  stations: [
    { id:'r6',   name:'BBC Radio 6 Music', sub:'Now: Lauren Laverne', art:'linear-gradient(135deg,#e8892b,#7a3b12)', live:true },
    { id:'kexp', name:'KEXP 90.3', sub:'Seattle · Variety', art:'linear-gradient(135deg,#2f6f45,#14301f)', live:true },
    { id:'nts',  name:'NTS Radio 1', sub:'London · Eclectic', art:'linear-gradient(135deg,#2b3a52,#12161f)', live:true },
    { id:'jazz', name:'Jazz24', sub:'Straight-ahead jazz', art:'linear-gradient(135deg,#5a3d6b,#241628)', live:true },
    { id:'fip',  name:'FIP', sub:'Paris · No genre', art:'linear-gradient(135deg,#b23b4e,#3a1218)', live:true },
    { id:'classic', name:'Classic FM', sub:'Now: Einaudi', art:'linear-gradient(135deg,#3a5240,#161f18)', live:true },
  ],
};

// Fallback Icon — used only if the compiled bundle predates the Icon component.
window.JBIcon = function JBIcon({ name, size = 20, strokeWidth = 2, color = 'currentColor', style }) {
  const ref = React.useRef(null);
  React.useEffect(() => {
    const el = ref.current; if (!el) return;
    el.replaceChildren();
    const lib = window.lucide;
    const pascal = String(name || '').replace(/(^\w|-\w)/g, s => s.replace('-', '').toUpperCase());
    const node = lib && lib.icons && (lib.icons[pascal] || lib.icons[name]);
    if (!node || !lib.createElement) return;
    const svg = lib.createElement(node);
    svg.setAttribute('width', size); svg.setAttribute('height', size);
    svg.setAttribute('stroke-width', strokeWidth); svg.setAttribute('stroke', color);
    svg.style.display = 'block';
    el.appendChild(svg);
  }, [name, size, strokeWidth, color]);
  return React.createElement('span', { ref, 'aria-hidden':'true',
    style: Object.assign({ display:'inline-flex', width:size, height:size, flex:'none', color }, style) });
};
