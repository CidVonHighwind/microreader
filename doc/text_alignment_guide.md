# Text Alignment Guide

The TextLayout system now supports text alignment (left, center, and right).

## Text Alignment Options

The `LayoutStrategy::TextAlignment` enum provides three alignment options:

- `ALIGN_LEFT` - Text aligned to the left margin (default)
- `ALIGN_CENTER` - Text centered within the available width
- `ALIGN_RIGHT` - Text aligned to the right margin

## Usage

To set text alignment, configure the `alignment` field in your `LayoutConfig`:

```cpp
TextLayout::LayoutConfig config;
config.marginLeft = 10;
config.marginRight = 10;
config.marginTop = 40;
config.marginBottom = 40;
config.lineHeight = 30;
config.minSpaceWidth = 10;
config.pageWidth = 480;
config.pageHeight = 800;

// Set alignment (default is ALIGN_LEFT)
config.alignment = LayoutStrategy::ALIGN_CENTER;

// Use the config for layout
textLayout.layoutText(text, renderer, config);
```

## Examples

### Left Alignment (Default)
```cpp
config.alignment = LayoutStrategy::ALIGN_LEFT;
```
Result:
```
This is left aligned text
that starts at the left
margin of the page.
```

### Center Alignment
```cpp
config.alignment = LayoutStrategy::ALIGN_CENTER;
```
Result:
```
    This is centered text
  that appears in the middle
      of the page.
```

### Right Alignment
```cpp
config.alignment = LayoutStrategy::ALIGN_RIGHT;
```
Result:
```
    This is right aligned text
          that ends at the right
              margin of the page.
```

## Implementation Details

- Alignment is applied per line, not per paragraph
- The alignment calculation takes into account:
  - Word widths
  - Space widths
  - Available text width (page width minus margins)
- All lines in a document use the same alignment setting
- The alignment affects all text rendered through the layout engine

## Notes

- The alignment feature works with both the Greedy and any future layout strategies
- Alignment is calculated after line breaks are determined
- For best results, ensure adequate margins are set in the config
