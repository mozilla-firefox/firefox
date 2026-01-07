# Architecture

## Type Separation: FFI Types vs Business Logic Types

This component uses a clear separation between **FFI (Foreign Function Interface) types** and **business logic types**. This architectural decision provides several important benefits for maintainability, API stability, and development velocity.

### Overview

- **FFI Types** (`MozAds*` prefix, defined in `src/ffi.rs`): Types exposed through UniFFI to external consumers (e.g., Kotlin, Swift, Python bindings)
- **Business Logic Types** (no prefix, defined in `src/client/`): Internal types used for core functionality, serialization, and business logic

### Key Benefits

#### 1. Clear Public API Identification

All types prefixed with `MozAds` (e.g., `MozAdsClient`, `MozAd`, `MozAdsClientConfig`) represent the **public API contract**. This makes it immediately obvious:

- What types external consumers depend on
- What changes require coordination with consumers
- What documentation needs to be kept up-to-date

#### 2. Breaking Change Detection

When modifying types:

- Changes to `MozAds*` types in `ffi.rs` = **potential breaking change** for external consumers
- Changes to business logic types in `client/` = **internal refactoring** (non-breaking)

This clear boundary helps developers:

- Understand the impact of their changes before making them
- Follow proper deprecation processes for public API changes
- Make internal improvements without affecting external consumers

#### 3. Versioned Public API

The separation enables future API versioning strategies:

- Maintain multiple versions of `MozAds*` types (e.g., `MozAdV1`, `MozAdV2`)
- Evolve the public API independently from internal implementation
- Provide migration paths between API versions

### Implementation Pattern

The conversion between FFI and business logic types is handled through `From`/`Into` trait implementations in `ffi.rs`:

```rust
// FFI type (public API)
pub struct MozAd { ... }

// Business logic type (internal)
pub struct Ad { ... }

// Conversion implementations
impl From<Ad> for MozAd { ... }
impl From<MozAd> for Ad { ... }
```

The public API in `lib.rs` uses `MozAds*` types at the boundary and converts to/from business logic types internally:

```rust
pub fn request_ads(
    moz_ad_requests: Vec<MozAdsPlacementRequest>,  // FFI type
    options: Option<MozAdsRequestOptions>,
) -> AdsClientApiResult<HashMap<String, MozAd>> {
    // Convert to business logic types
    let requests: Vec<AdPlacementRequest> = moz_ad_requests.iter().map(|r| r.into()).collect();

    // Use business logic types internally
    let placements = inner.request_ads(requests, ...)?;

    // Convert back to FFI types
    placements.into_iter().map(|(k, v)| (k, v.into())).collect()
}
```

### File Organization

- `src/ffi.rs`: All UniFFI-exposed types (`MozAds*`), error types, and conversions
- `src/lib.rs`: Public API entry point, handles FFI ↔ business logic conversions
- `src/client/`: Business logic types and implementation
- `src/error.rs`: Internal error types (FFI errors are in `ffi.rs`)

### Guidelines for Developers

1. **Adding new public API**: Create `MozAds*` types in `ffi.rs` with corresponding business logic types
2. **Modifying public API**: Consider breaking change implications and deprecation strategy
3. **Internal refactoring**: Feel free to modify business logic types, ensuring conversions remain correct
4. **Removing unused code**: Check both FFI and business logic types for unused conversions
