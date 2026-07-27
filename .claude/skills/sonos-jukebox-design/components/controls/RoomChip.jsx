import React from 'react';

/** Selectable room pill for grouping speakers on-glass. */
export function RoomChip({ name, playing=false, grouped=false, selected=false, onClick, style, ...rest }) {
  const base = {
    display:'inline-flex', alignItems:'center', gap:8, height:40, padding:'0 16px',
    borderRadius:'999px', font:'600 15px/1 var(--font-ui)', cursor:'pointer',
    border:'1px solid var(--screen-line)', background:'var(--screen-elev)',
    color:'var(--screen-text-mut)', transition:'all .15s ease', WebkitTapHighlightColor:'transparent',
  };
  const sel = selected ? {
    background:'color-mix(in oklch, var(--accent) 18%, var(--screen-elev))',
    borderColor:'var(--accent)', color:'var(--screen-text)',
  } : {};
  return (
    <button type="button" onClick={onClick} aria-pressed={selected} style={{ ...base, ...sel, ...style }} {...rest}>
      <span style={{ width:8, height:8, borderRadius:'999px', flex:'none',
        background: playing ? 'var(--accent)' : 'var(--screen-text-dim)',
        boxShadow: playing ? '0 0 8px var(--accent)' : 'none' }}></span>
      {name}
      {grouped && <span style={{ font:'500 11px/1 var(--font-mono)', color:'var(--screen-text-dim)' }}>+1</span>}
    </button>
  );
}
