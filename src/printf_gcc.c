/* CFW toolchain probe: SDK 6.0.22 arch_main.c already provides _write/_read
 * stubs, so the RTT retarget here would clash under LTO. Featureless CFW has no
 * printf anyway — left empty on purpose. */
