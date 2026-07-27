import React from 'react';

/** A tappable row for lists — radio stations, queue tracks, rooms, sources. */
export function ListRow({ title, subtitle, art, artColor='var(--screen-elev-2)', playing=false, selected=false, trailing, onClick, style, ...rest }) {
  const base = {
    display:'flex', alignItems:'center', gap:14, width:'100%', textAlign:'left',
    padding:'10px 14px', borderRadius:'var(--r-md)', cursor:'pointer', border:'none',
    background: selected ? 'var(--screen-elev)' : 'transparent', transition:'background .15s ease',
    WebkitTapHighlightColor:'transparent',
  };
  return (
    <button type="button" onClick={onClick} style={{ ...base, ...style }} {...rest}>
      <span style={{ width:52, height:52, borderRadius:10, flex:'none', overflow:'hidden',
        background: art ? `center/cover url(${art})` : artColor,
        display:'grid', placeItems:'center', boxShadow:'inset 0 0 0 1px var(--screen-line)' }}>
        {playing && <i data-lucide="audio-lines" width={22} height={22} style={{ color:'var(--accent)' }}></i>}
      </span>
      <span style={{ flex:1, minWidth:0 }}>
        <span style={{ display:'block', font:'600 17px/1.2 var(--font-ui)', color:'var(--screen-text)', whiteSpace:'nowrap', overflow:'hidden', textOverflow:'ellipsis' }}>{title}</span>
        {subtitle && <span style={{ display:'block', font:'400 13px/1.3 var(--font-ui)', color:'var(--screen-text-mut)', marginTop:3, whiteSpace:'nowrap', overflow:'hidden', textOverflow:'ellipsis' }}>{subtitle}</span>}
      </span>
      {trailing}
    </button>
  );
}
