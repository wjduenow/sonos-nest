export interface VolumeBarProps {
  /** 0–100. */
  value?: number;
  muted?: boolean;
  style?: React.CSSProperties;
}

/** Inline horizontal volume level with a state-aware speaker icon. Requires Lucide. */
export function VolumeBar(props: VolumeBarProps): JSX.Element;
