import React from 'react';

const fmt = (s) => { s = Math.max(0, Math.floor(s)); const m = Math.floor(s/60); return `${m}:${String(s%60).padStart(2,'0')}`; };

/** Playback progress bar with elapsed / remaining times. */
export function Scrubber({ elapsed=0, duration=210, live=false, style, ...rest }) {
  const pct = live ? 1 : Math.max(0, Math.min(1, elapsed / (duration||1)));
  return (
    <div style={{ width:'100%', ...style }} {...rest}>
      <div style={{ height:6, borderRadius:'999px', background:'var(--screen-elev-2)', position:'relative' }}>
        <div style={{ position:'absolute', left:0, top:0, height:'100%', width:`${pct*100}%`, borderRadius:'999px', background:'var(--accent)' }}></div>
        {!live && <div style={{ position:'absolute', top:'50%', left:`${pct*100}%`, width:14, height:14, borderRadius:'999px', background:'var(--screen-text)', transform:'translate(-50%,-50%)', boxShadow:'0 2px 6px rgba(0,0,0,.5)' }}></div>}
      </div>
      <div style={{ display:'flex', justifyContent:'space-between', marginTop:8, font:'500 12px/1 var(--font-mono)', color:'var(--screen-text-dim)' }}>
        <span>{live ? 'LIVE' : fmt(elapsed)}</span>
        <span>{live ? '· on air' : '-' + fmt(duration - elapsed)}</span>
      </div>
    </div>
  );
}
