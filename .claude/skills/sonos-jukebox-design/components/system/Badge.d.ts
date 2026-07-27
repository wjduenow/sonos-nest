import React from 'react';

export interface BadgeProps {
  children: React.ReactNode;
  /** live = accent tint + dot, neutral = grey, hi = solid accent. */
  tone?: 'live'|'neutral'|'hi';
  style?: React.CSSProperties;
}

/** Uppercase mono tag for source/quality/state (LIVE, FLAC, SPOTIFY). */
export function Badge(props: BadgeProps): JSX.Element;
