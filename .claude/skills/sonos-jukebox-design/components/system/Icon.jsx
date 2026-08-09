import React from 'react';

const pascal = (n) => String(n || '').replace(/(^\w|-\w)/g, s => s.replace('-', '').toUpperCase());

/**
 * Renders a Lucide glyph inside a React-owned wrapper. React never reconciles
 * the SVG itself, so conditionally rendering or swapping icons is safe.
 */
export function Icon({ name, size = 20, strokeWidth = 2, color = 'currentColor', style, ...rest }) {
  const ref = React.useRef(null);
  React.useEffect(() => {
    const el = ref.current;
    if (!el) return;
    el.replaceChildren();
    const lib = (typeof window !== 'undefined' && window.lucide) || null;
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
  return <span ref={ref} aria-hidden="true" style={{ display:'inline-flex', width:size, height:size, flex:'none', color, ...style }} {...rest} />;
}
