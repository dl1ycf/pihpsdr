# Protocol-2 diversity: ADC1 dither/random bit was never set

## Symptom

With DIVERSITY enabled on a Protocol-2 radio, ADC0 and ADC1 ran with
*different* LTC2208 dither/random settings whenever dither or random was
switched on. The two receive chains are then no longer identical, which
defeats the point of diversity: the phase/gain relationship the user dials
in on the Diversity menu is only valid if both ADCs are configured the same
way.

## Cause

`new_protocol_receive_specific()` in `src/new_protocol.c` packs the dither
and random flags for both ADCs into bytes 5 and 6 of the "receive specific"
packet — bit 0 for ADC0, bit 1 for ADC1.

The normal (non-diversity) path builds them correctly:

```c
receive_specific_buffer[5] = adc[0].dither | (adc[1].dither << 1);
receive_specific_buffer[6] = adc[0].random | (adc[1].random << 1);
```

The diversity path then overrides them, so that *both* ADCs inherit the
ADC0 setting — but it shifted the wrong way:

```c
receive_specific_buffer[5] = adc[0].dither | (adc[0].dither >> 1);   /* BUG */
receive_specific_buffer[6] = adc[0].random | (adc[0].random >> 1);   /* BUG */
```

`adc[0].dither` is 0 or 1, so `>> 1` is always 0. Bit 1 (ADC1) was
therefore always cleared, and the code silently did the opposite of what
its own comment ("Boths ADCs take the dither/random setting from ADC0")
promises: with dither on, ADC0 got dither and ADC1 did not.

## Fix

Shift left, matching the non-diversity path:

```c
receive_specific_buffer[5] = adc[0].dither | (adc[0].dither << 1);
receive_specific_buffer[6] = adc[0].random | (adc[0].random << 1);
```

## Scope

* Protocol 2 only. Protocol 1 (`src/old_protocol.c`) has a single shared
  dither/random bit pair in `buffer[C3]` for all Mercury cards and already
  ORs the two ADC settings together, so it cannot get out of step.
* Only visible when diversity is enabled *and* dither or random is enabled.
  With both off (the default) the buggy and fixed expressions both yield 0,
  which is why this went unnoticed.
