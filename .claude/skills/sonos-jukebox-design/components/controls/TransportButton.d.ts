import React from 'react';

/**
 * @param props transport button props
 */
export interface TransportButtonProps {
  /** Which transport glyph to render. */
  icon?: 'play'|'pause'|'back'|'next'|'prev'|'room'|'menu'|'shuffle'|'repeat'|'add'|'more';
  size?: 'sm'|'md'|'lg';
  /** ghost = bare (list/secondary), elevated = raised chip, solid = accent primary (play). */
  variant?: 'ghost'|'elevated'|'solid';
  /** Toggled/engaged state (e.g. shuffle on). */
  active?: boolean;
  label?: string;
  onClick?: () => void;
  style?: React.CSSProperties;
}

/**
 * Round transport control for the near-black on-glass UI. Uses Lucide glyphs,
 * so the host page must load Lucide and call `lucide.createIcons()` after mount.
 */
export function TransportButton(props: TransportButtonProps): JSX.Element;
