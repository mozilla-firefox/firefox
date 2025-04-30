import { registerShutdownTask, runShutdownTasks } from '../../framework/on_shutdown.js';
import { LogMessageWithStack } from '../../internal/logging/log_message.js';
import { TransferredTestCaseResult, LiveTestCaseResult } from '../../internal/logging/result.js';
import { TestCaseRecorder } from '../../internal/logging/test_case_recorder.js';
import { TestQueryWithExpectation } from '../../internal/query/query.js';
import { timeout } from '../../util/timeout.js';
import { assert } from '../../util/util.js';

import { CTSOptions, WorkerMode, kDefaultCTSOptions } from './options.js';
import { WorkerTestRunRequest } from './utils_worker.js';

/** Query all currently-registered service workers, and unregister them. */
function unregisterAllServiceWorkers() {
  void navigator.serviceWorker.getRegistrations().then(registrations => {
    for (const registration of registrations) {
      void registration.unregister();
    }
  });
}

// In practice all Web-based runtimes should be including this file, so take the opportunity to set
// up runShutdownTasks. None of these events are guaranteed to happen, but cleaning up is very
// important, so we try our best (and don't worry about shutdown performance or disabling bfcache).
// (We could try 'visibilitychange', but since it can happen in the middle of the page lifetime,
// it is more likely to have unintended consequences and would need to do different stuff.)
// - 'unload' supposedly always disables the bfcache.
window.addEventListener('unload', runShutdownTasks);
// - 'beforeunload' may disable the bfcache but may be called more reliably than 'unload'.
window.addEventListener('beforeunload', runShutdownTasks);
// - 'pagehide' won't disable the bfcache but may be called more reliably than the others.
window.addEventListener('pagehide', runShutdownTasks);

abstract class TestBaseWorker {
  protected readonly ctsOptions: CTSOptions;
  protected readonly resolvers = new Map<string, (result: LiveTestCaseResult) => void>();

  constructor(worker: WorkerMode, ctsOptions?: CTSOptions) {
    this.ctsOptions = { ...(ctsOptions || kDefaultCTSOptions), ...{ worker } };
  }

  onmessage(ev: MessageEvent) {
    const query: string = ev.data.query;
    const transferredResult: TransferredTestCaseResult = ev.data.result;

    const result: LiveTestCaseResult = {
      status: transferredResult.status,
      timems: transferredResult.timems,
      logs: transferredResult.logs?.map(l => new LogMessageWithStack(l)),
    };

    this.resolvers.get(query)!(result);
    this.resolvers.delete(query);

    // MAINTENANCE_TODO(kainino0x): update the Logger with this result (or don't have a logger and
    // update the entire results JSON somehow at some point).
  }

  makeRequestAndRecordResult(
    target: MessagePort | Worker | ServiceWorker,
    query: string,
    expectations: TestQueryWithExpectation[]
  ): Promise<LiveTestCaseResult> {
    const request: WorkerTestRunRequest = {
      query,
      expectations,
      ctsOptions: this.ctsOptions,
    };
    target.postMessage(request);

    return new Promise<LiveTestCaseResult>(resolve => {
      assert(!this.resolvers.has(query), "can't request same query twice simultaneously");
      this.resolvers.set(query, resolve);
    });
  }

  async run(
    rec: TestCaseRecorder,
    query: string,
    expectations: TestQueryWithExpectation[] = []
  ): Promise<void> {
    try {
      rec.injectResult(await this.runImpl(query, expectations));
    } catch (ex) {
      rec.start();
      rec.threw(ex);
      rec.finish();
    }
  }

  protected abstract runImpl(
    query: string,
    expectations: TestQueryWithExpectation[]
  ): Promise<LiveTestCaseResult>;
}

export class TestDedicatedWorker extends TestBaseWorker {
  private readonly worker: Worker | Error;

  constructor(ctsOptions?: CTSOptions) {
    super('dedicated', ctsOptions);
    try {
      if (typeof Worker === 'undefined') {
        throw new Error('Dedicated Workers not available');
      }

      const selfPath = import.meta.url;
      const selfPathDir = selfPath.substring(0, selfPath.lastIndexOf('/'));
      const workerPath = selfPathDir + '/test_worker-worker.js';
      const worker = new Worker(workerPath, { type: 'module' });
      this.worker = worker;
      this.worker.onmessage = ev => this.onmessage(ev);

      // Try to send a shutdown signal to the worker on shutdown.
      registerShutdownTask(() => worker.postMessage('shutdown'));
    } catch (ex) {
      assert(ex instanceof Error);
      // Save the exception to re-throw in runImpl().
      this.worker = ex;
    }
  }

  override runImpl(query: string, expectations: TestQueryWithExpectation[] = []) {
    if (this.worker instanceof Worker) {
      return this.makeRequestAndRecordResult(this.worker, query, expectations);
    } else {
      throw this.worker;
    }
  }
}

/** @deprecated Use TestDedicatedWorker instead. */
export class TestWorker extends TestDedicatedWorker {}

export class TestSharedWorker extends TestBaseWorker {
  /** MessagePort to the SharedWorker, or an Error if it couldn't be initialized. */
  private readonly port: MessagePort | Error;

  constructor(ctsOptions?: CTSOptions) {
    super('shared', ctsOptions);
    try {
      if (typeof SharedWorker === 'undefined') {
        throw new Error('Shared Workers not available');
      }

      const selfPath = import.meta.url;
      const selfPathDir = selfPath.substring(0, selfPath.lastIndexOf('/'));
      const workerPath = selfPathDir + '/test_worker-worker.js';
      const worker = new SharedWorker(workerPath, { type: 'module' });
      this.port = worker.port;
      this.port.start();
      this.port.onmessage = ev => this.onmessage(ev);

      // Try to send a shutdown signal to the worker on shutdown.
      registerShutdownTask(() => worker.port.postMessage('shutdown'));
    } catch (ex) {
      assert(ex instanceof Error);
      // Save the exception to re-throw in runImpl().
      this.port = ex;
    }
  }

  override runImpl(query: string, expectations: TestQueryWithExpectation[] = []) {
    if (this.port instanceof MessagePort) {
      return this.makeRequestAndRecordResult(this.port, query, expectations);
    } else {
      throw this.port;
    }
  }
}

export class TestServiceWorker extends TestBaseWorker {
  private lastServiceWorker: ServiceWorker | null = null;

  constructor(ctsOptions?: CTSOptions) {
    super('service', ctsOptions);

    // If the runtime is trying to use service workers, first clean up any service workers that may
    // have leaked from previous incarnations of the page. Service workers *shouldn't* affect
    // worker=none/dedicated/shared, so it should be OK if this constructor isn't called.
    unregisterAllServiceWorkers();

    // Try to send a shutdown signal to the worker on shutdown.
    registerShutdownTask(() => {
      this.runShutdownTasksOnLastServiceWorker();
      unregisterAllServiceWorkers();
    });
  }

  private runShutdownTasksOnLastServiceWorker() {
    this.lastServiceWorker?.postMessage('shutdown');
  }

  override async runImpl(query: string, expectations: TestQueryWithExpectation[] = []) {
    if (!('serviceWorker' in navigator)) {
      throw new Error('Service Workers not available');
    }
    const [suite, name] = query.split(':', 2);
    const fileName = name.split(',').join('/');

    const selfPath = import.meta.url;
    const selfPathDir = selfPath.substring(0, selfPath.lastIndexOf('/'));
    // Construct the path to the worker file, then use URL to resolve the `../` components.
    const serviceWorkerURL = new URL(
      `${selfPathDir}/../../../${suite}/webworker/${fileName}.as_worker.js`
    ).toString();

    // Ensure the correct service worker is registered.
    const registration = await navigator.serviceWorker.register(serviceWorkerURL, {
      type: 'module',
    });
    const registrationPending = () =>
      !registration.active || registration.active.scriptURL !== serviceWorkerURL;

    // Ensure the registration we just requested is active. (We don't worry about it being
    // outdated from a previous page load, because we wipe all service workers on shutdown/startup.)
    const isNewInstance = registrationPending();
    if (isNewInstance) {
      this.runShutdownTasksOnLastServiceWorker();
      do {
        await new Promise(resolve => timeout(resolve, 0));
      } while (registrationPending());
    }
    const serviceWorker = registration.active!;
    this.lastServiceWorker = serviceWorker;

    navigator.serviceWorker.onmessage = ev => this.onmessage(ev);
    return this.makeRequestAndRecordResult(serviceWorker, query, expectations);
  }
}
