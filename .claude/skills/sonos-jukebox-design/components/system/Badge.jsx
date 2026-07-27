import React from 'react';

const TONES = {
  live:{ bg:'color-mix(in oklch, var(--accent) 22%, var(--screen-elev))', fg:'var(--accent)' },
  neutral:{ bg:'var(--screen-elev-2)', fg:'var(--screen-text-mut)' },
  hi:{ bg:'var(--accent)', fg:'var(--accent-ink)' },
};

/** Small uppercase tag for source / quality / state labels. */
export function Badge({ children, tone='neutral', style, ...rest }) {
  const t = TONES[tone] || TONES.neutral;
  return (
    <span style={{ display:'inline-flex', alignItems:'center', gap:5, height:22, padding:'0 9px',
      borderRadius:'999px', font:'700 10px/1 var(--font-mono)', letterSpacing:'0.1em',
      textTransform:'uppercase', background:t.bg, color:t.fg, ...style }} {...rest}>
      {tone==='live' && <span style={{ width:6, height:6, borderRadius:'999px', background:'var(--accent)' }}></span>}
      {children}
    </span>
  );
}
