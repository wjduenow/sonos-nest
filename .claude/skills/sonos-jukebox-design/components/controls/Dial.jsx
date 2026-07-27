import React from 'react';

/** On-glass rotary indicator that mirrors the physical dial — an accent arc + center value. */
export function Dial({ value=50, min=0, max=100, size=160, label='VOLUME', mode='volume', style, ...rest }) {
  const pct = Math.max(0, Math.min(1, (value - min) / (max - min)));
  const sweep = 270; // gap at bottom
  const start = 135;
  const r = size/2 - 10;
  const c = size/2;
  const rad = (a) => (a * Math.PI) / 180;
  const pt = (a) => [c + r * Math.cos(rad(a)), c + r * Math.sin(rad(a))];
  const [x0,y0] = pt(start);
  const [x1,y1] = pt(start + sweep);
  const [xv,yv] = pt(start + sweep * pct);
  const large = sweep * pct > 180 ? 1 : 0;
  const arc = (ex,ey,lg) => `M ${x0} ${y0} A ${r} ${r} 0 ${lg} 1 ${ex} ${ey}`;
  return (
    <div style={{ width:size, height:size, position:'relative', ...style }} {...rest}>
      <svg width={size} height={size} style={{ display:'block' }}>
        <path d={arc(x1,y1,1)} fill="none" stroke="var(--screen-elev-2)" strokeWidth={8} strokeLinecap="round" />
        <path d={arc(xv,yv,large)} fill="none" stroke="var(--accent)" strokeWidth={8} strokeLinecap="round" />
      </svg>
      <div style={{ position:'absolute', inset:0, display:'grid', placeItems:'center', textAlign:'center' }}>
        <div>
          <div style={{ font:`800 ${Math.round(size*0.26)}px/1 var(--font-ui)`, color:'var(--screen-text)' }}>
            {mode==='volume' ? value : value}
          </div>
          <div style={{ font:'600 11px/1 var(--font-mono)', letterSpacing:'0.14em', color:'var(--screen-text-dim)', marginTop:6 }}>{label}</div>
        </div>
      </div>
    </div>
  );
}
