The workhorse list item — one row for stations, queue tracks, rooms, or sources. Composes with Badge / TransportButton in the trailing slot.

```jsx
<ListRow title="BBC Radio 6 Music" subtitle="Now: Lauren Laverne" playing
  trailing={<Badge tone="live">LIVE</Badge>} onClick={open} />
```

`playing` overlays an audio glyph on the art tile; `selected` fills the row. Requires Lucide when `playing`.
