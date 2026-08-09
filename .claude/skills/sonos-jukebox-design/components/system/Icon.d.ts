import React from 'react';

export interface IconProps {
  /** Lucide icon name, kebab-case (e.g. "skip-forward") or PascalCase. */
  name: string;
  /** Pixel box for the glyph. */
  size?: number;
  strokeWidth?: number;
  /** Defaults to currentColor. */
  color?: string;
  style?: React.CSSProperties;
}

/**
 * Lucide glyph in a React-safe wrapper. Requires the Lucide UMD script on the page,
 * but never call `lucide.createIcons()` — this component owns its own DOM.
 */
export function Icon(props: IconProps): JSX.Element;
