# Engine Constraints

This document collects justifications for engine-imposed limits and other
constraints in SpiderMonkey that are not obvious from the specification
alone.

## Maximum string length

SpiderMonkey limits the length of a JS string to `2**30 - 2` characters
(`JS::MaxStringLength` in `js/public/String.h`), roughly 1 GiB for a
Latin1 string or 2 GiB for a TwoByte string. This is smaller than the
ECMAScript specification's limit of `2**53 - 1` elements.

For comparison, per [MDN][mdn-max-string-length]:

* In V8, the maximum length is `2**29 - 24` (~512MiB). On 32-bit systems, the
  maximum length is `2**28 - 16` (~256MiB).
* In Firefox, the maximum length is `2**30 - 2` (~1GiB).
* In Safari, the maximum length is `2**31 - 1` (~2GiB).

[mdn-max-string-length]: https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/String/length

The
[comment](https://searchfox.org/firefox-main/rev/28b5e86da948982342836c3cca50049659f0dc70/js/public/String.h#381-389)
at the definition of `JS::MaxStringLength` explains one motivation:

```cpp
/**
 * Maximum length of a JS string. This is chosen so that the number of bytes
 * allocated for a null-terminated TwoByte string still fits in int32_t.
 */
static constexpr uint32_t MaxStringLength = (1 << 30) - 2;
```

That alone may not be a sufficient reason to keep the limit this low; it would
need a scan of the code to see how the restriction is actually used. But there
are other reasons to end up in a similar place.

Reasons for not going above 32 bits:

1. We currently pack 32 bits of length with 32 bits of flags (and cached
   index values etc.) in the first 64-bit word of a string. Expanding
   beyond 32 bits would make all strings larger.
2. Gecko and other embedders store and manipulate string lengths with 32-bit
   types. Those would need to be modified, and all affected APIs would need to
   be updated. It would also use more space, and make it easy to accidentally
   truncate strings until everything is updated.

Reasons not related to bit width:

3. If a web page is generating >1GB strings, it's often unintentional, and
   expanding that size would expose us to larger stalls and pauses that we'd
   need to code around and prevent in advance. That would be fine if large
   strings were needed, but:
4. Strings really aren't a good place to store enormous globs of data. The
   engine is managing them in complex ways under the hood. The programmer
   doesn't have the control necessary for managing large data. `ArrayBuffer` and
   `Uint8Array` are the proper tools, and shortcomings in the platform for
   dealing with them should be addressed there, not by overloading strings with
   a job they weren't meant for.
5. As long as other JS engines don't support giant strings, supporting them in
   SpiderMonkey would mean more work for us (see above) with no corresponding
   gain. Nobody's going to write a general-purpose website that special-cases
   Firefox because strings can be enormous. At best, you'd have a site that says
   "if your data is too big, run it on Firefox". But our mission is to enhance
   the Web by raising the base level of functionality, not creating
   engine-specific bifurcation.

A modest increase from 30 to 31 bits would be more viable than bumping to more
than 32bits, but we would first have to identify a strong need in order to make
it worthwhile.
