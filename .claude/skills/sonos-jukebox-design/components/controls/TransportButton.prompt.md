One round transport control for the on-glass UI — use for play/pause, back/menu, room-switch, and skip actions; the `solid` variant is reserved for the primary play/pause.

```jsx
<TransportButton icon="play" variant="solid" size="lg" label="Play" onClick={toggle} />
<TransportButton icon="back" variant="ghost" />
<TransportButton icon="shuffle" variant="ghost" active />
```

Variants: `ghost` (bare, secondary/list), `elevated` (raised chip on screen-elev), `solid` (accent fill, primary only). Sizes `sm` 44 · `md` 56 · `lg` 72. Requires Lucide loaded on the page.
