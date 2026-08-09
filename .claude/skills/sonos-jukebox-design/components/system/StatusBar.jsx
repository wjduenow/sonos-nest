import React from 'react';
import { Icon } from './Icon.jsx';

/** Top status strip for the on-glass UI — room, clock, connectivity. */
export function StatusBar({ room='Kitchen', grouped=0, time='14:32', source='wifi', style, ...rest }) {
  return (
    <div style={{ display:'flex', alignItems:'center', justifyContent:'space-between',
      width:'100%', padding:'0 4px', color:'var(--screen-text-mut)', ...style }} {...rest}>
      <div style={{ display:'flex', alignItems:'center', gap:9 }}>
        <span style={{ width:9, height:9, borderRadius:'999px', background:'var(--accent)', boxShadow:'0 0 8px var(--accent)' }}></span>
        <span style={{ font:'600 15px/1 var(--font-ui)', color:'var(--screen-text)' }}>{room}</span>
        {grouped>0 && <span style={{ font:'500 12px/1 var(--font-mono)', color:'var(--screen-text-dim)' }}>+{grouped}</span>}
      </div>
      <div style={{ display:'flex', alignItems:'center', gap:12 }}>
        <Icon name={source} size={17} />
        <span style={{ font:'600 14px/1 var(--font-mono)', color:'var(--screen-text-mut)' }}>{time}</span>
      </div>
    </div>
  );
}
