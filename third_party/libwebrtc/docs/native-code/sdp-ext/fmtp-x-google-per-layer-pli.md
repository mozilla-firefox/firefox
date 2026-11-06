# x-google-per-layer-pli FMTP parameter

The x-google-per-layer-pli FMTP parameter is a format specific parameter
that can be added to a remote description in the `a=fmtp:` line:
  a=fmtp:96 x-google-per-layer-pli=1

When using simulcast with more than a single SSRC, it will change how the
simulcast encoder reacts to Picture Loss Indication (PLI) and Full Intra
Request (FIR) RTCP feedback.

When the parameter value is 1, a PLI requests the generation of a key frame
for the spatial layer associated with the SSRC of the media source and a
FIR does the same for the SSRC value of the media sender.

When the value is 0 or the parameter is missing, a keyframe is generated
on all spatial layers for backward compability.

## Experimentation

This parameter does allow for large-scale A/B testing and opt-in to the
new behavior. For multiparty calls enabling it should reduce the number of
keyframes sent by the client and number of key frames received by the
receivers which results in a better bandwidth utilization.

This parameter is experimental and may be removed again in the future.

## IANA considerations

Since the current behavior of reacting to a PLI for a specific SSRC with
key frames on all spatial layers can be considered an implementation bug
this parameter is not registered with the IANA.

If experimentation shows that the current behavior is better for some
codecs like VP8 which can share encoding parameters with synchronous
keyframes a standardized variant of this parameter shall be registered
with the IANA.
