export interface ScrubberProps {
  /** Seconds elapsed. */
  elapsed?: number;
  /** Track length in seconds. */
  duration?: number;
  /** Live radio — fills the bar, hides the handle, shows LIVE. */
  live?: boolean;
  style?: React.CSSProperties;
}

/** Playback progress bar with mono elapsed / remaining timecodes. */
export function Scrubber(props: ScrubberProps): JSX.Element;
