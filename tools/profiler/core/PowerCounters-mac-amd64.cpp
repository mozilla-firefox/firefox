/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "PowerCounters.h"
#include "nsDebug.h"
#include "nsPrintfCString.h"
#include "nsXULAppAPI.h"  // for XRE_IsParentProcess

// Because of the pkg_energy_statistics_t::pkes_version check below, the
// earliest OS X version this code will work with is 10.9.0 (xnu-2422.1.72).

#include <sys/types.h>
#include <sys/sysctl.h>

// OS X has four kinds of system calls:
//
//  1. Mach traps;
//  2. UNIX system calls;
//  3. machine-dependent calls;
//  4. diagnostic calls.
//
// (See "Mac OS X and iOS Internals" by Jonathan Levin for more details.)
//
// The last category has a single call named diagCall() or diagCall64(). Its
// mode is controlled by its first argument, and one of the modes allows access
// to the Intel RAPL MSRs.
//
// The interface to diagCall64() is not exported, so we have to import some
// definitions from the XNU kernel. All imported definitions are annotated with
// the XNU source file they come from, and information about what XNU versions
// they were introduced in and (if relevant) modified.

// The diagCall64() mode.
// From osfmk/i386/Diagnostics.h
// - In 10.8.4 (xnu-2050.24.15) this value was introduced. (In 10.8.3 the value
//   17 was used for dgGzallocTest.)
#define dgPowerStat 17

// From osfmk/i386/cpu_data.h
// - In 10.8.5 these values were introduced, along with core_energy_stat_t.
#define CPU_RTIME_BINS (12)
#define CPU_ITIME_BINS (CPU_RTIME_BINS)

// core_energy_stat_t and pkg_energy_statistics_t are both from
// osfmk/i386/Diagnostics.c.
// - In 10.8.4 (xnu-2050.24.15) both structs were introduced, but with many
//   fewer fields.
// - In 10.8.5 (xnu-2050.48.11) both structs were substantially expanded, with
//   numerous new fields.
// - In 10.9.0 (xnu-2422.1.72) pkg_energy_statistics_t::pkes_version was added.
//   diagCall64(dgPowerStat) fills it with '1' in all versions since (up to
//   10.10.2 at time of writing).
// - in 10.10.2 (xnu-2782.10.72) core_energy_stat_t::gpmcs was conditionally
//   added, if DIAG_ALL_PMCS is true. (DIAG_ALL_PMCS is not even defined in the
//   source code, but it could be defined at compile-time via compiler flags.)
//   pkg_energy_statistics_t::pkes_version did not change, though.

typedef struct {
  uint64_t caperf;
  uint64_t cmperf;
  uint64_t ccres[6];
  uint64_t crtimes[CPU_RTIME_BINS];
  uint64_t citimes[CPU_ITIME_BINS];
  uint64_t crtime_total;
  uint64_t citime_total;
  uint64_t cpu_idle_exits;
  uint64_t cpu_insns;
  uint64_t cpu_ucc;
  uint64_t cpu_urc;
#if DIAG_ALL_PMCS           // Added in 10.10.2 (xnu-2782.10.72).
  uint64_t gpmcs[4];        // Added in 10.10.2 (xnu-2782.10.72).
#endif /* DIAG_ALL_PMCS */  // Added in 10.10.2 (xnu-2782.10.72).
} core_energy_stat_t;

typedef struct {
  uint64_t pkes_version;  // Added in 10.9.0 (xnu-2422.1.72).
  uint64_t pkg_cres[2][7];

  // This is read from MSR 0x606, which Intel calls MSR_RAPL_POWER_UNIT
  // and XNU calls MSR_IA32_PKG_POWER_SKU_UNIT.
  uint64_t pkg_power_unit;

  // These are the four fields for the four RAPL domains. For each field
  // we list:
  //
  // - the corresponding MSR number;
  // - Intel's name for that MSR;
  // - XNU's name for that MSR;
  // - which Intel processors the MSR is supported on.
  //
  // The last of these is determined from chapter 35 of Volume 3 of the
  // "Intel 64 and IA-32 Architecture's Software Developer's Manual",
  // Order Number 325384. (Note that chapter 35 contradicts section 14.9
  // to some degree.)

  // 0x611 == MSR_PKG_ENERGY_STATUS == MSR_IA32_PKG_ENERGY_STATUS
  // Atom (various), Sandy Bridge, Next Gen Xeon Phi (model 0x57).
  uint64_t pkg_energy;

  // 0x639 == MSR_PP0_ENERGY_STATUS == MSR_IA32_PP0_ENERGY_STATUS
  // Atom (various), Sandy Bridge, Next Gen Xeon Phi (model 0x57).
  uint64_t pp0_energy;

  // 0x641 == MSR_PP1_ENERGY_STATUS == MSR_PP1_ENERGY_STATUS
  // Sandy Bridge, Haswell.
  uint64_t pp1_energy;

  // 0x619 == MSR_DRAM_ENERGY_STATUS == MSR_IA32_DDR_ENERGY_STATUS
  // Xeon E5, Xeon E5 v2, Haswell/Haswell-E, Next Gen Xeon Phi (model
  // 0x57)
  uint64_t ddr_energy;

  uint64_t llc_flushed_cycles;
  uint64_t ring_ratio_instantaneous;
  uint64_t IA_frequency_clipping_cause;
  uint64_t GT_frequency_clipping_cause;
  uint64_t pkg_idle_exits;
  uint64_t pkg_rtimes[CPU_RTIME_BINS];
  uint64_t pkg_itimes[CPU_ITIME_BINS];
  uint64_t mbus_delay_time;
  uint64_t mint_delay_time;
  uint32_t ncpus;
  core_energy_stat_t cest[];
} pkg_energy_statistics_t;

static int diagCall64(uint64_t aMode, void* aBuf) {
  // We cannot use syscall() here because it doesn't work with diagnostic
  // system calls -- it raises SIGSYS if you try. So we have to use asm.

#ifdef __x86_64__
  // The 0x40000 prefix indicates it's a diagnostic system call. The 0x01
  // suffix indicates the syscall number is 1, which also happens to be the
  // only diagnostic system call. See osfmk/mach/i386/syscall_sw.h for more
  // details.
  static const uint64_t diagCallNum = 0x4000001;
  uint64_t rv;

  __asm__ __volatile__(
      "syscall"

      // Return value goes in "a" (%rax).
      : /* outputs */ "=a"(rv)

      // The syscall number goes in "0", a synonym (from outputs) for "a"
      // (%rax). The syscall arguments go in "D" (%rdi) and "S" (%rsi).
      : /* inputs */ "0"(diagCallNum), "D"(aMode), "S"(aBuf)

      // The |syscall| instruction clobbers %rcx, %r11, and %rflags ("cc"). And
      // this particular syscall also writes memory (aBuf).
      : /* clobbers */ "rcx", "r11", "cc", "memory");
  return rv;
#else
#  error Sorry, only x86-64 is supported
#endif
}

// This is a counter to collect power utilization during profiling.
// It cannot be a raw `ProfilerCounter` because we need to manually add/remove
// it while the profiler lock is already held.
class RaplDomain final : public BaseProfilerCount {
 public:
  explicit RaplDomain(const char* aLabel, const char* aDescription)
      : BaseProfilerCount(aLabel, nullptr, nullptr, "power", aDescription),
        mSample(0),
        mEnergyStatusUnits(0),
        mWrapAroundCount(0),
        mIsSampleNew(false) {}

  CountSample Sample() override {
    CountSample result;

    // To be consistent with the Windows EMI API,
    // return values in picowatt-hour.
    constexpr double NANOJOULES_PER_JOULE = 1'000'000'000;
    constexpr double NANOJOULES_TO_PICOWATTHOUR = 3.6;

    uint64_t ticks = (uint64_t(mWrapAroundCount) << 32) + mSample;
    double joulesPerTick = (double)1 / (1 << mEnergyStatusUnits);
    result.count = static_cast<double>(ticks) * joulesPerTick *
                   NANOJOULES_PER_JOULE / NANOJOULES_TO_PICOWATTHOUR;

    result.number = 0;
    result.isSampleNew = mIsSampleNew;
    mIsSampleNew = false;
    return result;
  }

  void AddSample(uint32_t aSample, uint32_t aEnergyStatusUnits) {
    if (aSample == mSample) {
      return;
    }

    mEnergyStatusUnits = aEnergyStatusUnits;

    if (aSample > mSample) {
      mIsSampleNew = true;
      mSample = aSample;
      return;
    }

    // Despite being returned in uint64_t fields, the power counter values
    // only use the lowest 32 bits of their fields, and we need to handle
    // wraparounds to avoid our power tracks stopping after a few hours.
    constexpr uint32_t highestBit = 1 << 31;
    if ((mSample & highestBit) && !(aSample & highestBit)) {
      mIsSampleNew = true;
      ++mWrapAroundCount;
      mSample = aSample;
    } else {
      NS_WARNING("unexpected sample with smaller value");
    }
  }

 private:
  uint32_t mSample;
  uint32_t mEnergyStatusUnits;
  uint32_t mWrapAroundCount;
  bool mIsSampleNew;
};

class RAPL {
  // Is the GPU domain supported by the processor?
  bool mIsGpuSupported = false;
  // Is the RAM domain supported by the processor?
  bool mIsRamSupported = false;

  // The DRAM domain on Haswell servers has a fixed energy unit (1/65536 J ==
  // 15.3 microJoules) which is different to the power unit MSR. (See the
  // "Intel Xeon Processor E5-1600 and E5-2600 v3 Product Families, Volume 2 of
  // 2, Registers" datasheet, September 2014, Reference Number: 330784-001.)
  // This field records whether the quirk is present.
  bool mHasRamUnitsQuirk;

  // The abovementioned 15.3 microJoules value. (2^16 = 65536)
  static constexpr double kQuirkyRamEnergyStatusUnits = 16;

  // The struct passed to diagCall64().
  pkg_energy_statistics_t* mPkes = nullptr;

  RaplDomain* mPkg = nullptr;
  RaplDomain* mCores = nullptr;
  RaplDomain* mGpu = nullptr;
  RaplDomain* mRam = nullptr;

 public:
  explicit RAPL(PowerCounters::CountVector& aCounters)
      : mHasRamUnitsQuirk(false) {
    // Work out which RAPL MSRs this CPU model supports.
    int cpuModel;
    size_t size = sizeof(cpuModel);
    if (sysctlbyname("machdep.cpu.model", &cpuModel, &size, NULL, 0) != 0) {
      NS_WARNING("sysctlbyname(\"machdep.cpu.model\") failed");
      return;
    }

    // This is similar to arch/x86/kernel/cpu/perf_event_intel_rapl.c in
    // linux-4.1.5/.
    //
    // By linux-5.6.14/, this stuff had moved into
    // arch/x86/events/intel/rapl.c, which references processor families in
    // arch/x86/include/asm/intel-family.h.
    switch (cpuModel) {
      case 0x2a:  // Sandy Bridge
      case 0x3a:  // Ivy Bridge
        // Supports package, cores, GPU.
        mIsGpuSupported = true;
        mIsRamSupported = false;
        break;

      case 0x3f:  // Haswell X
      case 0x4f:  // Broadwell X
      case 0x55:  // Skylake X
      case 0x56:  // Broadwell D
        // Supports package, cores, RAM. Has the units quirk.
        mIsGpuSupported = false;
        mIsRamSupported = true;
        mHasRamUnitsQuirk = true;
        break;

      case 0x2d:  // Sandy Bridge X
      case 0x3e:  // Ivy Bridge X
        // Supports package, cores, RAM.
        mIsGpuSupported = false;
        mIsRamSupported = true;
        break;

      case 0x3c:  // Haswell
      case 0x3d:  // Broadwell
      case 0x45:  // Haswell L
      case 0x46:  // Haswell G
      case 0x47:  // Broadwell G
        // Supports package, cores, GPU, RAM.
        mIsGpuSupported = true;
        mIsRamSupported = true;
        break;

      case 0x4e:  // Skylake L
      case 0x5e:  // Skylake
      case 0x8e:  // Kaby Lake L
      case 0x9e:  // Kaby Lake
      case 0x66:  // Cannon Lake L
      case 0x7d:  // Ice Lake
      case 0x7e:  // Ice Lake L
      case 0xa5:  // Comet Lake
      case 0xa6:  // Comet Lake L
        // Supports package, cores, GPU, RAM, PSYS.
        // XXX: this tool currently doesn't measure PSYS.
        mIsGpuSupported = true;
        mIsRamSupported = true;
        break;

      default:
        NS_WARNING(nsPrintfCString("unknown CPU model: %d", cpuModel).get());
        return;
    }

    // Get the maximum number of logical CPUs so that we know how big to make
    // |mPkes|.
    int logicalcpu_max;
    size = sizeof(logicalcpu_max);
    if (sysctlbyname("hw.logicalcpu_max", &logicalcpu_max, &size, NULL, 0) !=
        0) {
      NS_WARNING("sysctlbyname(\"hw.logicalcpu_max\") failed");
      return;
    }

    // Over-allocate by 1024 bytes per CPU to allow for the uncertainty around
    // core_energy_stat_t::gpmcs and for any other future extensions to that
    // struct. (The fields we read all come before the core_energy_stat_t
    // array, so it won't matter to us whether gpmcs is present or not.)
    size_t pkesSize = sizeof(pkg_energy_statistics_t) +
                      logicalcpu_max * sizeof(core_energy_stat_t) +
                      logicalcpu_max * 1024;
    mPkes = (pkg_energy_statistics_t*)malloc(pkesSize);
    if (mPkes && aCounters.reserve(4)) {
      mPkg = new RaplDomain("Power: CPU package", "RAPL PKG");
      aCounters.infallibleAppend(mPkg);

      mCores = new RaplDomain("Power: CPU cores", "RAPL PP0");
      aCounters.infallibleAppend(mCores);

      if (mIsGpuSupported) {
        mGpu = new RaplDomain("Power: iGPU", "RAPL PP1");
        aCounters.infallibleAppend(mGpu);
      }

      if (mIsRamSupported) {
        mRam = new RaplDomain("Power: DRAM", "RAPL DRAM");
        aCounters.infallibleAppend(mRam);
      }
    }
  }

  ~RAPL() {
    free(mPkes);
    delete mPkg;
    delete mCores;
    delete mGpu;
    delete mRam;
  }

  void Sample() {
    constexpr uint64_t kSupportedVersion = 1;

    // If we failed to allocate the memory for package energy statistics, we
    // have nothing to sample.
    if (MOZ_UNLIKELY(!mPkes)) {
      return;
    }

    // Write an unsupported version number into pkes_version so that the check
    // below cannot succeed by dumb luck.
    mPkes->pkes_version = kSupportedVersion - 1;

    // diagCall64() returns 1 on success, and 0 on failure (which can only
    // happen if the mode is unrecognized, e.g. in 10.7.x or earlier versions).
    if (diagCall64(dgPowerStat, mPkes) != 1) {
      NS_WARNING("diagCall64() failed");
      return;
    }

    if (mPkes->pkes_version != kSupportedVersion) {
      NS_WARNING(
          nsPrintfCString("unexpected pkes_version: %llu", mPkes->pkes_version)
              .get());
      return;
    }

    // Bits 12:8 are the ESU.
    // Energy measurements come in multiples of 1/(2^ESU).
    uint32_t energyStatusUnits = (mPkes->pkg_power_unit >> 8) & 0x1f;
    mPkg->AddSample(mPkes->pkg_energy, energyStatusUnits);
    mCores->AddSample(mPkes->pp0_energy, energyStatusUnits);
    if (mIsGpuSupported) {
      mGpu->AddSample(mPkes->pp1_energy, energyStatusUnits);
    }
    if (mIsRamSupported) {
      mRam->AddSample(mPkes->ddr_energy, mHasRamUnitsQuirk
                                             ? kQuirkyRamEnergyStatusUnits
                                             : energyStatusUnits);
    }
  }
};

PowerCounters::PowerCounters() {
  // RAPL values are global, so only sample them on the parent.
  mRapl = XRE_IsParentProcess() ? new RAPL(mCounters) : nullptr;
}

PowerCounters::~PowerCounters() {
  mCounters.clear();
  delete mRapl;
  mRapl = nullptr;
}

void PowerCounters::Sample() {
  if (mRapl) {
    mRapl->Sample();
  }
}
