import React from 'react';
import { Icon } from '../system/Icon.jsx';

const ICONS = { play:'play', pause:'pause', back:'chevron-left', next:'skip-forward', prev:'skip-back', room:'arrow-left-right', menu:'menu', shuffle:'shuffle', repeat:'repeat', add:'plus', more:'more-horizontal' };
const SIZES = { sm:{d:44,i:20}, md:{d:56,i:24}, lg:{d:72,i:30} };

/** Round transport control for the on-glass UI — mirrors the physical caps. */
export function TransportButton({ icon='play', size='md', variant='ghost', active=false, label, onClick, style, ...rest }) {
  const s = SIZES[size] || SIZES.md;
  const base = {
    width:s.d, height:s.d, display:'inline-grid', placeItems:'center',
    borderRadius:'999px', cursor:'pointer', border:'none', padding:0,
    transition:'transform .12s ease, background .15s ease, opacity .15s ease',
    color:'var(--screen-text)', WebkitTapHighlightColor:'transparent',
  };
  const variants = {
    ghost:{ background:'transparent', color: active ? 'var(--accent)' : 'var(--screen-text-mut)' },
    elevated:{ background:'var(--screen-elev-2)', boxShadow:'inset 0 0 0 1px var(--screen-line)', color:'var(--screen-text)' },
    solid:{ background:'var(--accent)', color:'var(--accent-ink)', boxShadow:'0 6px 18px color-mix(in oklch, var(--accent) 45%, transparent)' },
  };
  return (
    <button type="button" aria-label={label || icon} aria-pressed={active}
      onClick={onClick} style={{ ...base, ...(variants[variant]||variants.ghost), ...style }} {...rest}>
      <Icon name={ICONS[icon] || icon} size={s.i} strokeWidth={2.2} />
    </button>
  );
}
