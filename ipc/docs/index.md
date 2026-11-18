# Processes, Threads and IPC

These pages contain the documentation for Gecko's architecture for platform
process and thread creation, communication and synchronization. They live
in the 'ipc/docs' directory of the Firefox repository.

```{toctree}
:maxdepth: 3

ipdl
processes
proc_shutdown
utility_process
```

For inter-process communication involving Javascript, see {doc}`JSActors </dom/ipc/jsactors>`.
They are a very limited case, used for communication between elements in the DOM,
which may exist in separate processes. They only involve the main process and
content processes -- no other processes run Javascript.
