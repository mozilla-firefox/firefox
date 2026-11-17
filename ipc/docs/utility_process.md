# Utility Process

:::{warning}
Please reach out to #ipc on <https://chat.mozilla.org/> if you intent to add a new utility.
:::

The utility process is used to provide a simple way to implement IPC actor with
some more specific sandboxing properties, in case where you don't need or want
to deal with the extra complexity of adding a whole new process type but you
just want to apply different sandboxing policies.
To implement such an actor, you will have to follow a few steps like for
implementing the trivial example visible in [EmptyUtil](https://phabricator.services.mozilla.com/D126402):

- Define a new IPC actor, e.g., `PEmptyUtil` that allows to get some string
  via `GetSomeString()` from the child to the parent

- In the `PUtilityProcess` definition, expose a new child-level method,
  e.g., `StartEmptyUtilService(Endpoint<PEmptyUtilChild>)`

- Implement `EmptyUtilChild` and `EmptyUtilParent` classes both deriving
  from their `PEmptyUtilXX`. If you want or need to run things from a
  different thread, you can have a look at `UtilityProcessGenericActor`

- Make sure both are refcounted

- Expose your new service on `UtilityProcessManager` with a method
  performing the heavy lifting of starting your process, you can take
  inspiration from `StartEmptyUtil()` in the sample.

- Ideally, this starting method should rely on {searchfox}`StartUtility() <mozilla-central/rev/f9f9b422f685244dcd3f6826b70d34a496ce5853:ipc/glue/UtilityProcessManager.cpp#238-318,347>`

- To use `StartUtility()` mentioned above, please ensure that you provide
  a `nsresult BindToUtilityProcess(RefPtr<UtilityProcessParent>
  aUtilityParent)`. Usually, it should be in charge of creating a set of
  endpoints and performing `Bind()` to setup the IPC. You can see some example for {searchfox}`UtilityMediaService <mozilla-central/rev/0fe32133f12ed0f20d5fde8fe970157f04817982:ipc/glue/UtilityMediaServiceChild.cpp#60-92>`.
  If the process fails to launch while your actor is still unbound,
  `BindToUtilityProcess()` is called with a null `aUtilityParent` instead, so
  it must handle that case and can use it to learn about the failure.

- For proper user-facing exposition in `about:processes` you will have to also provide an actor
  name via a method `UtilityActorName GetActorName() { return UtilityActorName::EmptyUtil; }`

  - Add member within {searchfox}`enum WebIDLUtilityActorName in <mozilla-central/rev/f9f9b422f685244dcd3f6826b70d34a496ce5853:dom/chrome-webidl/ChromeUtils.webidl#852-866>`

- Handle reception of `StartEmptyUtilService` on the child side of
  `UtilityProcess` within `RecvStartEmptyUtilService()`

- In `UtilityProcessChild::ActorDestroy`, release any resources that
  you stored a reference to in `RecvStartEmptyUtilService()`. This
  will probably include a reference to the `EmptyUtilChild`.

- The specific sandboxing requirements can be implemented by tracking
  `SandboxingKind`, and it starts within {searchfox}`UtilityProcessSandboxing header <ipc/glue/UtilityProcessSandboxing.h>`

- Try and make sure you at least add some `gtest` coverage of your new
  actor, for example like in {searchfox}`existing gtest <ipc/glue/test/gtest/TestUtilityProcess.cpp>`

- Also ensure actual sandbox testing within

  - `SandboxTest` to start your new process,
    {searchfox}`security/sandbox/common/test/SandboxTest.cpp`
  - `SandboxTestingChildTests` to define the test
    {searchfox}`security/sandbox/common/test/SandboxTestingChildTests.h`
  - `SandboxTestingChild` to run your test
    {searchfox}`security/sandbox/common/test/SandboxTestingChild.cpp`

- Please also consider having a look at {ref}`Process Bookkeeping <process-bookkeeping>` for anything you may want to ensure is supported by your new process, like e.g. profiler, crash reporting, etc.
