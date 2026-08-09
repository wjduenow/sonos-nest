The only correct way to render an icon in this system — a Lucide glyph inside a React-owned wrapper.

```jsx
<Icon name="skip-forward" size={22} />
<Icon name="wifi" size={17} color="var(--screen-text-mut)" />
```

**Never call `lucide.createIcons()`.** That global scan replaces React-owned `<i>` nodes with raw SVG and crashes the tree the moment an icon is conditionally rendered or swapped. Just load the Lucide UMD script; `Icon` does the rest.
