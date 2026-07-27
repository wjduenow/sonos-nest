export interface RoomChipProps {
  name: string;
  /** Room is currently playing — shows an accent status dot + glow. */
  playing?: boolean;
  /** Room is part of a group — shows a +N badge. */
  grouped?: boolean;
  /** Currently focused/selected in the room picker. */
  selected?: boolean;
  onClick?: () => void;
  style?: React.CSSProperties;
}

/** Selectable room pill for the speaker/room picker on the near-black UI. */
export function RoomChip(props: RoomChipProps): JSX.Element;
