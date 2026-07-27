A pill representing one room/speaker in the picker — shows playing state and group membership.

```jsx
<RoomChip name="Kitchen" playing grouped selected />
<RoomChip name="Bedroom" onClick={() => join('Bedroom')} />
```

The status dot glows accent when `playing`; `selected` tints the fill and outlines it in accent; `grouped` appends a `+N` badge.
