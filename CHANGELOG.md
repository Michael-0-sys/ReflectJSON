# Changelog

## 1.0.0

First public release.

- `JSONwriter`: buffered streaming writer with structural validation (depth, key limits, duplicate keys),
  struct reflection via `REFLECT` / `MEMBER`, `JsonFixed` / `JsonNumber` precision control,
  `std::optional`, struct flattening (`write(status)` without a key writes the members into the current object).
- `JSONreader`: single-pass tokenizer with strict JSON validation (escapes, UTF-8, numbers, duplicate keys,
  missing colons), reflection mapping into structs/arrays, `SearchMode::MatchByKey` / `MatchByType`.
- `JsonConfig` shared by reader and writer: `ignoreUnknownKeys`, `coerceInputValues`, `explicitNulls`
  (Kotlin `Json { ... }` semantics); `JsonConfig::strict()` for the old strict behaviour.
- Pretty printing: `writer.getJSON(true)` / `getJSONSize(true)`, created lazily in a separate buffer.
- Error locations: `getErrorPath()` on reader and writer, e.g. `root.devices[2].limits.max`.
- Optimized for 32-bit MCUs (no per-character heap use, word-wise checksum, integer fast paths).
