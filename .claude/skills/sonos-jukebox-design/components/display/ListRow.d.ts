import React from 'react';

export interface ListRowProps {
  title: string;
  subtitle?: string;
  /** Image URL for the leading tile (album/station art). */
  art?: string;
  /** Fallback solid color when no art. */
  artColor?: string;
  /** Now-playing — shows an animated audio glyph over the art. */
  playing?: boolean;
  selected?: boolean;
  /** Trailing node — a Badge, TransportButton, or chevron. */
  trailing?: React.ReactNode;
  onClick?: () => void;
  style?: React.CSSProperties;
}

/** Standard list row for the on-glass UI: radio stations, queue tracks, rooms, sources. */
export function ListRow(props: ListRowProps): JSX.Element;
