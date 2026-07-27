import React from 'react';

/** Horizontal volume readout with a mute/level icon. */
export function VolumeBar({ value=40, muted=false, style, ...rest }) {
  const pct = muted ? 0 : Math.max(0, Math.min(100, value));
  const icon = muted || pct===0 ? 'volume-x' : pct < 50 ? 'volume-1' : 'volume-2';
  return (
    <div style={{ display:'flex', alignItems:'center', gap:14, width:'100%', ...style }} {...rest}>
      <i data-lucide={icon} width={22} height={22} style={{ color:'var(--screen-text-mut)', flex:'none' }}></i>
      <div style={{ flex:1, height:6, borderRadius:'999px', background:'var(--screen-elev-2)', position:'relative' }}>
        <div style={{ position:'absolute', left:0, top:0, height:'100%', width:`${pct}%`, borderRadius:'999px', background: muted ? 'var(--screen-text-dim)' : 'var(--accent)' }}></div>
      </div>
      <span style={{ font:'600 13px/1 var(--font-mono)', color:'var(--screen-text-mut)', width:28, textAlign:'right' }}>{pct}</span>
    </div>
  );
}
