export interface DialProps {
  value?: number;
  min?: number;
  max?: number;
  /** Overall diameter in px. */
  size?: number;
  /** Caps label under the value, e.g. VOLUME / BASS. */
  label?: string;
  mode?: 'volume'|'value';
  style?: React.CSSProperties;
}

/**
 * On-glass rotary indicator (270° accent arc + center value) that visually
 * echoes the physical dial. Used for the volume overlay and list-scroll feedback.
 */
export function Dial(props: DialProps): JSX.Element;
