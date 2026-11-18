# Performance Infrastructure

```{contents}
:depth: 3
```

Performance tests run on bare metal, or real hardware devices instead of virtual machines (the memory tests and Talos xperf are the only exceptions). This gives us more realistic performance metrics, and helps with decreasing the variability of our data. See below for information on what hardware is used for each of the platforms we test on, try run wait times as well as guidelines for requesting new devices.

## Platforms and Hardware Used

The hardware below is maintained by the Relops (Release Operations) team. A platform label doesn't determine the machine on its own, as the same label can run different harnesses on different worker pools, so the platforms are listed under the worker pool they run on. The platforms found in the test documentation link back to this section.

This information is generated from `python/mozperftest/mozperftest/perfdocs/hardware.yml`, where each hardware is mapped to the worker pools it makes up. If a new worker pool starts running performance tests, it must be added there or the documentation generation will fail.

### Desktop

(hardware-hpe-moonshot)=

#### [HPE Moonshot](https://mana.mozilla.org/wiki/display/ROPS/HPE+Moonshot)

The HPE Moonshot System supports up to 45 servers in a single chassis, where each server resides on a cartridge. The `netperf` pools are the subset of them that is set up for the network bandwidth tests.

* **Location**: MDC1
* **Machines**: 45 cartridges per 4.3U chassis
* **Worker pools, and their platforms**:
  * `releng-hardware/gecko-t-linux-talos-2404`
    * `test-linux2404-64-clang-trunk/opt`
    * `test-linux2404-64-nightlyasrelease/opt`
    * `test-linux2404-64-shippable/opt`
    * `test-linux2404-64/opt`
  * `releng-hardware/gecko-t-linux-netperf-2404`
    * `test-linux2404-64-nightlyasrelease/opt`
    * `test-linux2404-64-shippable/opt`
    * `test-linux2404-64/opt`

```text
Model: m710x ProLiant cartridge
Processor: Intel E3-1585L v5 3.0GHz (4 cores)
Memory: 8GB DDR4 2400MHz
Disk: 256GB PCIe M.2 2280 SSD, and 64GB SATA M.2 2242 SSD
GPU: Intel Iris Pro Graphics P580
Power: 1500W Hot Plug redundant (1+1) power supply
```

(hardware-mac-mini-m4)=

#### Apple Silicon (M4)

Apple Silicon Mac Minis running macOS 15. They also host the Android arm64 emulator tests. A small number of the same machines run macOS 27 and are dedicated to Safari testing.

* **Worker pools, and their platforms**:
  * `releng-hardware/gecko-t-osx-1500-m4`
    * `test-android-em-14-arm64-shippable/opt`
    * `test-macosx1500-aarch64-nightlyasrelease/opt`
    * `test-macosx1500-aarch64-shippable/opt`
  * `releng-hardware/gecko-t-osx-2700-m4`
    * `test-macosx2700-aarch64-shippable/opt`

```text
Model Name: Mac mini
Model Identifier: Mac16,10
Model Number: MU9D3LL/A
Processor Name: Apple M4
Processor Speed: 4.46 GHz
Number of Processors: 1
Total Number of Cores: 10 (4 performance and 6 efficiency)
GPU: 10 Core
L2 Cache (per performance Core): 16 MB
L2 Cache (per efficiency Core): 4 MB
L3 Cache: None (Uses System Level Cache)
Memory: 16 GB
Disk: SSD 256 GB
```

(hardware-nuc12-windows11-reference)=

#### Windows 11 Reference

Low-end reference hardware used to catch regressions that are only visible on slower machines.

* **Location**: MDC1
* **Machines**: 15 machines
* **Worker pools, and their platforms**:
  * `releng-hardware/win11-64-24h2-hw-ref`
    * `test-windows11-64-24h2-hw-ref-shippable/opt`
    * `test-windows11-64-24h2-hw-ref/opt`

```text
Model Name: NUC12WSKi3
Processor Name: Intel Core i3-1220P
Processor Speed: 2.96 GHz
Number of Processors: 12
Total Number of Cores: 10
GPU: Intel UHD Graphics for 12th Gen Intel Processors
Memory: 8GB
Disk: 500GB SSD
Resolution: 1920 x 1080
```

(hardware-nuc13-windows11)=

#### Windows 11

* **Location**: MDC1
* **Machines**: 160 machines
* **Worker pools, and their platforms**:
  * `releng-hardware/win11-64-24h2-hw`
    * `test-windows11-64-24h2-nightlyasrelease/opt`
    * `test-windows11-64-24h2-shippable/opt`
    * `test-windows11-64-24h2/opt`

```text
Model Name: NUC13ANHi5
Processor Name: 13th Gen Intel Core i5-1340P
Processor Speed: 1.9/4.6 GHz
Number of Processors: 16
Total Number of Cores: 12
GPU: Intel Iris Xe Adapter
Memory: 8GB
Disk: 500GB SSD
Resolution: 1920 x 1080
```

### Mobile

(hardware-samsung-a55)=

#### Samsung A55

The devices are split across two device farms: a subset of the performance tests, as well as the unit tests, run on the LambdaTest devices, and the rest run on the Bitbar ones.

* **Location**: Bitbar, and LambdaTest
* **Machines**: 67 devices total
* **Worker pools, and their platforms**:
  * `proj-autophone/gecko-t-bitbar-gw-perf-a55`
  * `proj-autophone/gecko-t-lambda-perf-a55`
    * `test-android-hw-a55-14-0-aarch64-nightlyasrelease/opt`
    * `test-android-hw-a55-14-0-aarch64-shippable/opt`
    * `test-android-hw-a55-14-0-aarch64/opt`

```text
Model Name: Samsung A55
Model Identifier: SM_A556E (International)
Processor Name: Exynos 1480
Processor Speed: Octa-core (4x2.75GHz ARM Cortex-A78 and 4x2GHz ARM Cortex-A55)
Number of Processors: 2
Total Number of Cores: 8
GPU: Samsung Xclipse 530
Memory: 8GB
Disk: 128 | 256 GB
More Info: https://www.phonemore.com/specs/samsung/galaxy-a55/
```

(hardware-samsung-s24)=

#### Samsung Galaxy S24

* **Location**: Bitbar
* **Machines**: 4 devices total
* **Worker pools, and their platforms**:
  * `proj-autophone/gecko-t-bitbar-gw-perf-s24`
    * `test-android-hw-s24-14-0-aarch64-nightlyasrelease/opt`
    * `test-android-hw-s24-14-0-aarch64-shippable/opt`
  * `proj-autophone/gecko-t-bitbar-gw-unit-s24`

```text
Model Name: Samsung Galaxy S24
Model Identifier: SM_S921B (International)
Processor Name: Exynos 2400
Processor Speed: Deca-core (1x3.21GHz ARM Cortex-X4, 2x2.9GHz ARM Cortex-A720, 3x2.59GHz ARM Cortex-A720 and 4x1.96GHz ARM Cortex-A520)
Number of Processors: 4
Total Number of Cores: 10
GPU: Samsung Xclipse 940
Memory: 8 GB
Disk: 128 | 256 GB
More Info: https://www.phonemore.com/specs/samsung/galaxy-s24/sm-s921bds-128gb/
```

(hardware-google-pixel-6)=

#### Google Pixel 6

* **Location**: Bitbar
* **Machines**: 4 devices total
* **Worker pools, and their platforms**:
  * `proj-autophone/gecko-t-bitbar-gw-perf-p6`
    * `test-android-hw-p6-13-0-aarch64-nightlyasrelease/opt`
    * `test-android-hw-p6-13-0-aarch64-shippable/opt`
  * `proj-autophone/gecko-t-bitbar-gw-unit-p6`

```text
Model Name: Google Pixel 6
Model Identifier: Pixel 6
Processor Name: Google Tensor
Processor Speed: Octa-core (2x2.80 GHz Cortex-X1, 2x2.25 GHz Cortex-A76 and 4x1.80 GHz Cortex-A55)
Number of Processors: 1
Total Number of Cores: 8
GPU: Mali-G78 MP20
Memory: 8 GB
Disk: 128 | 256 GB
More Info: https://www.phonemore.com/specs/google/pixel-6/
```

### Virtual machines

(hardware-gcp-c3d-standard-8)=

#### GCP c3d-standard-8

Google Cloud virtual machines running the tests in a docker container. They are only used by the memory tests, which are not sensitive enough to the noise introduced by the hypervisor to require bare metal.

* **Worker pools, and their platforms**:
  * `gecko-t/t-linux-docker-noscratch-amd`
    * `test-linux2404-64-clang-trunk/opt`
    * `test-linux2404-64-shippable/opt`
    * `test-linux2404-64/opt`

```text
Instance Type: c3d-standard-8
Processor Name: AMD EPYC (Genoa)
Total Number of Cores: 8 vCPUs
Memory: 32 GB
Image: ubuntu-2404-headless
```

(hardware-azure-standard-f8alds-v7)=

#### Azure Standard_F8alds_v7

Azure virtual machines. As with the Linux virtual machines above, they are only used by the tests that do not need bare metal: the memory tests and the Talos xperf test, which needs the privileged pool to record ETW traces.

* **Worker pools, and their platforms**:
  * `gecko-t/win11-64-25h2`
    * `test-windows11-64-25h2-shippable/opt`
    * `test-windows11-64-25h2/opt`
  * `gecko-t/win11-64-25h2-privileged`
    * `test-windows11-64-25h2-shippable/opt`
    * `test-windows11-64-25h2/opt`

```text
Instance Type: Standard_F8alds_v7
Processor Name: AMD EPYC
Total Number of Cores: 8 vCPUs
Memory: 16 GB
```

(hardware-tart-mac-mini-m4)=

#### Apple Silicon (M4) tart VMs

macOS virtual machines run with tart on the Apple Silicon Mac Minis, two per host. Of the performance tests, only the AWSY pageload tests run on them.

* **Location**: MDC1
* **Worker pools, and their platforms**:
  * `releng-hardware/gecko-t-osx-1500-m-vms`
    * `test-macosx1500-aarch64-vms-shippable/opt`

```text
Model Identifier: VirtualMac2,1
Host: Mac mini (Mac16,10), Apple M4, 16 GB
Total Number of Cores: 4 vCPUs
Memory: 8 GB
```


## Try Runs and Wait Times

Given that our tests run on hardware, there's a limited amount of devices that can be used to run them. This means that it's very likely that a try run (e.g. tests scheduled by {doc}`mach-try-perf`) will be delayed waiting for capacity to free up. This limited capacity is also why there is a limit of 600 tasks that can be scheduled with `mach try perf`.

Something to keep in mind is that try runs have a low priority, and our production branches (e.g. autoland/mozilla-central) have a higher priority. On days when there are more pushes to those branches, try runs will hit more delays. The platforms also have different capacities available to them which will change how long you have to wait for tests to start on them. **To find out how many tasks are currently pending, or running across all platforms** [see this redash query](https://sql.telemetry.mozilla.org/queries/98004#241985) **or consult the graph below.**

```{raw} html
<iframe src="https://sql.telemetry.mozilla.org/embed/query/98004/visualization/241985?api_key=MNTnV4fwt3oblbx1uXDK9njIvDUa6rp1sla9RENT&" width="720" height="550"></iframe>
```

[See this dashboard for average wait times across all the platforms](https://sql.telemetry.mozilla.org/dashboard/average-performance-test-wait-times).

In some cases, the tasks may hit a "task timeout" where they expire before they can run. This can be expected if there are a lot of other higher priority tasks being scheduled, so it's good to check the query mentioned above to see what the load looks like on the platform being targeted. However, there are situations where the test pools go offline or other issues occur. In general, for these task timeouts, reach out in [#perftest on Matrix](https://matrix.to/#/#perftest:mozilla.org) to notify us about the issue as we might not be aware of it.

## Requesting New Devices for Testing

At times, it can be useful to test changes on a different device due to a lack of configuration coverage with our existing set of devices. It's simple to request/expense a device for local testing if you need one. However, for testing in continuous integration (CI) and/or in try runs, it can be quite expensive, and time-consuming to get a device ready for it. In general it takes a **few months** to get the device(s) running, and given contractual limitations, it may also reduce the availability of other devices. The work spans multiple teams as well so the time it takes depends on the current/future tasks that those teams have at the moment.

If a device is only required for a single issue, then it's recommended to order the device for local testing. If it's a long term project with multiple developers/teams that needs continuous monitoring, then it could be useful to have the devices in CI so they can be run on mozilla-central/autoland. This can also help ensure that your changes don't regress over time. Note that this should be planned ahead of time so that there's time to set up the devices before work on the project starts.

For long term projects, it's also good to determine how long these devices will be needed for, and if the device setup in CI needs to be adjusted to encompass these additional configurations. If the devices are no longer needed after the long term project completes, it might not be useful to have these devices running in CI, and using local devices would be quicker and cheaper to get.

If you need to get a device for local testing, reach out to your manager to ask about expensing it. If you believe that you need to start testing on a new device in CI, please reach out to us in [#perftest on Matrix](https://matrix.to/#/#perftest:mozilla.org). Alternatively, you can [file a bug here](https://bugzilla.mozilla.org/enter_bug.cgi?product=Testing&component=Performance&status_whiteboard=[fxp]) for support on this.
