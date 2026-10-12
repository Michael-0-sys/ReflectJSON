# Contributing

Thanks for helping! A few ground rules:

1. **Licensing / contributor agreement.** ReflectJSON is dual-licensed (noncommercial + commercial, see
   [LICENSE](LICENSE)). By opening a pull request you confirm that you wrote the contribution (or have the
   right to submit it) and that you grant the maintainer the right to distribute it under the
   PolyForm Noncommercial License 1.0.0 **and** under commercial licenses. If you cannot agree to this,
   please open an issue instead of a pull request.
2. **Tests.** Run `make -C test` and `make -C test examples` before submitting. Both must pass
   (they run with AddressSanitizer and UndefinedBehaviorSanitizer).
3. **Speed and size matter.** The library targets small MCUs (RP2040/RP2350, ESP32, Teensy, Arduino UNO R4). Avoid
   heap allocations, `std::string`, exceptions and RTTI. Measure on hardware for performance changes - x86
   benchmarks do not predict Cortex-M behaviour.
4. **Style.** English comments, no new global state, keep the public API source compatible.
5. **Bugs.** Please include the JSON input / the calls that trigger it, the board, and the exact error message
   and `getErrorPath()` output.
