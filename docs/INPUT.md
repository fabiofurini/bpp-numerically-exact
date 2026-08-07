# Input format

[← Back to README](../README.md) · [← Usage](USAGE.md)

The instance file is plain text: one line `<item_count> <capacity>`,
followed by one integer weight per item, one per line.

```
5 10
6
5
4
3
2
```

This describes 5 items (weights 6, 5, 4, 3, 2) to be packed into bins of
capacity 10.

- `item_count`: number of items (positive integer).
- `capacity`: bin capacity (positive integer).
- Each of the following `item_count` lines: one item's weight (positive
  integer, must not exceed `capacity`).

There is no header beyond the first line, no comments, and no trailing
metadata — the reader is strict about the format.

---

See also: [Output format](OUTPUT.md) · [Usage](USAGE.md)
