# Sharing rust libraries across the Firefox (for Android) stack

`Agi Sferro <agi@sferro.dev>`

March 20th, 2021

## The problem

We don’t have a good story for integrating a rust library so that it’s
available to use in Gecko, GeckoView, AC and Fenix and also in a way that rust
can call rust directly avoiding a C FFI layer.

## Goals

- Being able to integrate a rust library that can be called from Gecko,
  GeckoView, AC, Fenix, including having singleton-like instances that are
  shared across the stack, per-process.
- The rust library should be able to call and be called by other rust libraries
  or rust code in Gecko directly (i.e. without a C FFI layer)
- A build-time assurance that all components in the stack compile against the
  same version of the rust library
- Painless, quick and automated updates. Should be able to produce chemspill
  updates for the rust library in under 24h with little manual intervention
  (besides security checks / code review / QA).
- Support for non-Gecko consumers of the rust library is essential. I.e.
  providing a version of Gecko that does not include any of the libraries
- (optional?) Provide an easy way to create bundles of rust libraries depending
  on consumers needs.

## Proposal

1. Rename libmegazord.so to librustcomponents.so to clarify what the purpose of
   this artifact is.
2. Every rust library that wants to be called or wants to call rust code
   directly will be included in libxul.so (which contains most of Gecko native
   code), and vendored in the Firefox repository. This includes, among others, Glean and
   Nimbus.
3. libxul.so will expose the necessary FFI symbols for the Kotlin wrappers
   needed by the libraries vendored in the Firefox repository in step (2).
4. At every nightly / beta / release build of Gecko, we will generate an (or
   possibly many) additional librustcomponents.so artifacts that will be published
   as an AAR in maven.mozilla.org. This will also publish all the vendored
   libraries in the Firefox repository to maven, which will have a dependency on the
   librustcomponents.so produced as part of this step. Doing this will ensure that
   both libxul.so and librustcomponents.so contain the exact same code and can be
   swapped freely in the dependency graph.
5. Provide a new GeckoView build with artifactId geckoview-omni which will
   depend on all the rust libraries. The existing geckoview will not have such
   dependency and will be kept for third-party users of GeckoView.
6. GeckoView will depend on the Kotlin wrappers of all the libraries that
   depend on librustcomponents.so built in step (4) in the .pom file. For example

```xml
<dependency>
  <groupId>org.mozilla.telemetry</groupId>
  <artifactId>glean</artifactId>
  <version>33.1.2</version>
  <scope>compile</scope>
</dependency>
```

It will also exclude the org.mozilla.telemetry.glean dependency to
librustcomponents.so, as the native code is now included in libxul.so as part
of step (2). Presumably Glean will discover where its native code lives by
either trying librustcomponents.so or libxul.so (or some other better methods,
suggestions welcome).

7. Android Components and Fenix will remove their explicit dependency on Glean,
   Nimbus and all other libraries provided by GeckoView, and instead consume the
   one provided by GeckoView (this step is optional, note that any version
   conflict would cause a build error).

## The good

- We get automated integration with AC for free. When an update for a library
  is pushed to the `main` branch, a new nightly build for GeckoView will be
  produced which is already consumed by AC automatically (and indirectly into
  Fenix).
- Publishing infrastructure to maven is already figured out, and we can reuse
  the existing process for GeckoView to publish all the dependencies.
- If a consumer (say AC) uses a mismatched version for a dependency, a
  compile-time error will be thrown.
- All consumers of the rust libraries packaged this way are on the same version
  (provided they stay up to date with releases)
- Non-Mozilla consumers get first-class visibility into what is packaged into
  GeckoView, and can independently discover Glean, Nimbus, etc, since we define
  our dependencies in the pom file.
- Gecko Desktop and Gecko Mobile consumer Glean and other libraries in the same
  way, removing unnecessary deviation.

## Worth Noting

- Uplifts to beta / release versions of Fenix will involve more checks as they
  impact Gecko too.

## The Bad

- Libraries need to be vendored in the Firefox repository. Dependencies will follow
  the Gecko train which might not be right for them, as some dependencies don’t
  really have a nightly vs stable version. - This could change in the future, as
  the integration gets deeper and updates to the library become more frequent /
  at every commit.
- Locally testing a change in a rust library involves rebuilding all of Gecko.
  This is a side effect of statically linking rust libraries to Gecko.
- All rust libraries that are both used by Android and Gecko will need to be
  updated together, and we cannot have separate versions on Desktop/Mobile.
  Although this can be mitigated by providing flexible dependency on the library
  side (e.g. nimbus doesn’t need to depend on a specific version of - Glean and
  can accept whatever is in Gecko)
- Code that doesn’t natively live in the Firefox repository has double the work to get
  into a product - first a release process is needed from the native repo, then
  a phabricator process for the vendoring.

## Alternatives Considered

### Telemetry delegate

GeckoView provides a Java Telemetry delegate interface that Glean can implement
on the AC layer to provide Glean functionality to consumers. Glean would offer
a rust wrapper to the Java delegate API to transparently call either the
delegate (when built for mobile) or the Glean instance directly (when built for
Desktop).

#### Drawbacks

- This involves a lot of work on the Glean side to build and maintain the
  delegate
- A large section of the Glean API is embedded in the GeckoView API without a
  direct dependency
- We don’t expect the telemetry delegate to have other implementations other
  than Glean itself, despite the apparent generic nature of the telemetry
  delegate
- Glean and GeckoView engineers need to coordinate for every API update, as an
  update to the Glean API likely triggers an update to the GV API.
- Gecko Desktop and Gecko Mobile use Glean a meaningfully different way
- Doesn’t solve the dependency problem: even though in theory this would allow
  Gecko to work with multiple Glean versions, in practice the GV Telemetry
  delegate is going to track Glean so closely that it will inevitably require
  pretty specific Glean versions to work.

#### Advantages

- Explicit code dependency, an uninformed observer can understand how telemetry
  is extracted from GeckoView by just looking at the API
- No hard Glean version requirement, AC can be (in theory) built with a
  different Glean version than Gecko and things would still work

#### Why we decided against

The amount of ongoing maintenance work involved on the Glean side far outweighs
the small advantages, namely to not tie AC to a specific Glean version.
Significantly complicates the stack.

### Dynamic Discovery

Gecko discovers when it’s being loaded as part of Fenix (or some other
Gecko-powered browser) by calling dlsym on the Glean library. When the
discovery is successful, and the Glean version matches, Gecko will directly use
the Glean provided by Fenix.

#### Drawbacks

- Non standard, non-Mozilla apps will not expect this to work the way it does
- “Magic”: there’s no way to know that the dyscovery is happening (or what
  version of Glean is provided with Gecko) unless you know it’s there.
- The standard failure mode is at runtime, as there’s no built-in way to check
  that the version provided by Gecko is the same as the one provided by Fenix
  at build time.
- Doesn’t solve the synchronization problem: Gecko and Fenix will have to be on
  the same Glean version for this to work.
- Gecko Mobile deviates meaningfully from Desktop in the way it uses Glean for
  no intrinsic reason

#### Advantages

- This system is transparent to Consuming apps, e.g. Nimbus can use Glean as
  is, with no significant modifications needed.

#### Why we decided against

- This alternative does not provide substantial benefits over the proposal
  outlined in this doc and has significant drawbacks like the runtime failure
  case and the non-standard linking process.

### Hybrid Dynamic Discovery

This is a variation of the Dynamic Discovery where Gecko and GeckoView include
Glean directly and consumers get Glean from Gecko dynamically (i.e. they dlsym
libxul.so).

#### Drawbacks

- Glean still needs to build a wrapper for libraries not included in Gecko
  (like Nimbus) that want to call Glean directly.

#### Advantages

- The dependency to Glean is explicit and clear from an uninformed observer
  point of view.
- Smaller scope, only Glean would need to be moved to the Firefox repository

#### Why we decided against

Not enough advantages over the proposal, significant ongoing maintenance work
required from the Glean side.

## Open Questions

- How does iOS consume megazord today? Do they have a maven-like dependency
  system we can use to publish the iOS megazord?
- How do we deal with licenses in <about:license>? Application-services has a
  build step that extracts rust dependencies and puts them in the pom file
- What would be the process for coordinating a-c breaking changes?
- Would the desire to vendor apply even if this were not Rust code?

## Common Questions

- **How do we make sure GV/AC/Gecko consume the same version of the native
  libraries?** The pom dependency in GeckoView ensures that any GeckoView
  consumers depend on the same version of a given library, this includes AC and
  Fenix.

- **What happens to non-Gecko consumers of megazord?** This plan is transparent
  to a non-Gecko consumer of megazord, as they will still consume the native
  libraries through the megazord dependency in Glean/Nimbus/etc. With the added
  benefit that, if the consumer stays up to date with the megazord dependency,
  they will use the same version that Gecko uses.

- **What’s the process to publish an update to the megazord?** When a team
  wants to publish an update to the megazord it will need to commit the update
  to the `main` branch. A new build will be generated in the next nightly cycle,
  producing an updated version of the megazord. My understanding is that current
  megazord releases are stable (and don’t have beta/nightly cycles) so for
  external consumers, consuming the nightly build could be adequate, and provide
  the fastest turnaround on updates. For Gecko consumers the turnaround will be
  the same to Firefox Desktop (i.e. roughly 6-8 weeks from commit to release
  build).

- **How do we handle security uplifts?** If you have a security release one
  rust library you would need to request uplift to beta/release branches
  (depending on impact) like all other Gecko changes. The process in itself can
  be expedited and have a fast turnaround when needed (below 24h). We have been
  using this process for all Gecko changes so I would not expect particular
  problems with it.

- **What about OOP cases? E.g. GeckoView as a service?** We briefly discussed
  this in the email chain, there are ways we could make that work (e.g.
  providing a IPC shim). The details are fuzzy but since we don’t have any
  immediate need for such support knowing that it’s doable with a reasonable
  amount of work is enough for now.

- **Vendoring in the Firefox repository seems excessive.** I agree. This is an
  unfortunate requirement stemming from a few assumptions (which could be
  challenged! We are choosing not to):

  > - Gecko wants to vendor whatever it consumes for rust
  > - We want rust to call rust directly (without a C FFI layer)
  > - We want adding new libraries to be a painless experience

  Because of the above, vendoring in the Firefox repository seems to be the best if not
  the only way to achieve our goals.
