export interface StatusBarProps {
  /** Active room name. */
  room?: string;
  /** Number of additional grouped rooms (shows +N). */
  grouped?: number;
  /** Clock string. */
  time?: string;
  /** Lucide icon name for connectivity/source, e.g. wifi / bluetooth / cast. */
  source?: string;
  style?: React.CSSProperties;
}

/** Persistent top strip: active room + group, connectivity, clock. Requires Lucide. */
export function StatusBar(props: StatusBarProps): JSX.Element;
