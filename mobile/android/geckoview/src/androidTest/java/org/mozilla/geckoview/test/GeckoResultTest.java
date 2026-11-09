/* Any copyright is dedicated to the Public Domain.
http://creativecommons.org/publicdomain/zero/1.0/ */

package org.mozilla.geckoview.test;

import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.*;

import android.os.Handler;
import android.os.Looper;
import androidx.test.annotation.UiThreadTest;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.filters.MediumTest;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.util.Arrays;
import java.util.List;
import java.util.concurrent.CancellationException;
import java.util.concurrent.SynchronousQueue;
import java.util.concurrent.TimeoutException;
import java.util.concurrent.atomic.AtomicBoolean;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mozilla.gecko.util.ThreadUtils;
import org.mozilla.geckoview.GeckoResult;
import org.mozilla.geckoview.test.util.Environment;
import org.mozilla.geckoview.test.util.UiThreadUtils;

@RunWith(AndroidJUnit4.class)
@MediumTest
public class GeckoResultTest {
  private static class MockException extends RuntimeException {
    private static final long serialVersionUID = 1L;
  }

  private boolean mDone;

  private final Environment mEnv = new Environment();

  private void waitUntilDone() {
    assertThat("We should not be done", mDone, equalTo(false));
    UiThreadUtils.waitForCondition(() -> mDone, mEnv.getDefaultTimeoutMillis());
  }

  private void done() {
    UiThreadUtils.HANDLER.post(() -> mDone = true);
  }

  @Before
  public void setup() {
    mDone = false;
  }

  @Test
  @UiThreadTest
  public void thenWithResult() {
    GeckoResult.fromValue(42)
        .accept(
            value -> {
              assertThat("Value should match", value, equalTo(42));
              done();
            });

    waitUntilDone();
  }

  @Test
  @UiThreadTest
  public void thenWithException() {
    final Throwable boom = new Exception("boom");
    GeckoResult.fromException(boom)
        .accept(
            null,
            error -> {
              assertThat("Exception should match", error, equalTo(boom));
              done();
            });

    waitUntilDone();
  }

  @Test(expected = IllegalArgumentException.class)
  @UiThreadTest
  public void thenNoListeners() {
    GeckoResult.fromValue(42).then(null, null);
  }

  @Test
  @UiThreadTest
  public void testCopy() {
    final GeckoResult<Integer> result = new GeckoResult<>(GeckoResult.fromValue(42));
    result.accept(
        value -> {
          assertThat("Value should match", value, equalTo(42));
          done();
        });

    waitUntilDone();
  }

  @Test
  @UiThreadTest
  public void allOfError() throws Throwable {
    final GeckoResult<List<Integer>> result =
        GeckoResult.allOf(
            new GeckoResult<>(GeckoResult.fromValue(12)),
            new GeckoResult<>(GeckoResult.fromValue(35)),
            new GeckoResult<>(GeckoResult.fromException(new RuntimeException("Sorry not sorry"))),
            new GeckoResult<>(GeckoResult.fromValue(0)));

    UiThreadUtils.waitForResult(
        result.accept(
            value -> {
              throw new AssertionError("result should fail");
            },
            error -> {
              assertThat("Error should match", error instanceof RuntimeException, is(true));
              assertThat("Error should match", error.getMessage(), equalTo("Sorry not sorry"));
            }),
        mEnv.getDefaultTimeoutMillis());
  }

  @Test
  @UiThreadTest
  public void allOfEmpty() {
    final GeckoResult<List<Integer>> result = GeckoResult.allOf();

    result.accept(
        value -> {
          assertThat("Value should match", value.isEmpty(), is(true));
          done();
        });

    waitUntilDone();
  }

  @Test
  @UiThreadTest
  public void allOfNull() {
    final GeckoResult<List<Integer>> result = GeckoResult.allOf((List<GeckoResult<Integer>>) null);

    result.accept(
        value -> {
          assertThat("Value should match", value, equalTo(null));
          done();
        });

    waitUntilDone();
  }

  @Test
  @UiThreadTest
  public void allOfMany() {
    final GeckoResult<Integer> pending1 = new GeckoResult<>();
    final GeckoResult<Integer> pending2 = new GeckoResult<>();

    final GeckoResult<List<Integer>> result =
        GeckoResult.allOf(
            pending1,
            new GeckoResult<>(GeckoResult.fromValue(12)),
            pending2,
            new GeckoResult<>(GeckoResult.fromValue(35)),
            new GeckoResult<>(GeckoResult.fromValue(9)),
            new GeckoResult<>(GeckoResult.fromValue(0)));

    result.accept(
        value -> {
          assertThat("Value should match", value, equalTo(Arrays.asList(123, 12, 321, 35, 9, 0)));
          done();
        });

    try {
      Thread.sleep(50);
    } catch (final InterruptedException ex) {
    }

    // Complete the results out of order so that we can verify the input order is preserved
    pending2.complete(321);
    pending1.complete(123);
    waitUntilDone();
  }

  @Test(expected = IllegalStateException.class)
  @UiThreadTest
  public void completeMultiple() {
    final GeckoResult<Integer> deferred = new GeckoResult<>();
    deferred.complete(42);
    deferred.complete(43);
  }

  @Test(expected = IllegalStateException.class)
  @UiThreadTest
  public void completeMultipleExceptions() {
    final GeckoResult<Integer> deferred = new GeckoResult<>();
    deferred.completeExceptionally(new Exception("boom"));
    deferred.completeExceptionally(new Exception("boom again"));
  }

  @Test(expected = IllegalStateException.class)
  @UiThreadTest
  public void completeMixed() {
    final GeckoResult<Integer> deferred = new GeckoResult<>();
    deferred.complete(42);
    deferred.completeExceptionally(new Exception("boom again"));
  }

  @Test(expected = IllegalArgumentException.class)
  @UiThreadTest
  public void completeExceptionallyNull() {
    new GeckoResult<Integer>().completeExceptionally(null);
  }

  @Test
  @UiThreadTest
  public void completeThreaded() {
    final GeckoResult<Integer> deferred = new GeckoResult<>();
    final Thread thread = new Thread(() -> deferred.complete(42));

    deferred.accept(
        value -> {
          assertThat("Value should match", value, equalTo(42));
          ThreadUtils.assertOnUiThread();
          done();
        });

    thread.start();
    waitUntilDone();
  }

  @Test
  @UiThreadTest
  public void dispatchOnInitialThread() throws InterruptedException {
    final Thread thread =
        new Thread(
            () -> {
              Looper.prepare();
              final Thread dispatchThread = Thread.currentThread();

              GeckoResult.fromValue(42)
                  .accept(
                      value -> {
                        assertThat(
                            "Thread should match", Thread.currentThread(), equalTo(dispatchThread));
                        Looper.myLooper().quit();
                      });

              Looper.loop();
            });

    thread.start();
    thread.join();
  }

  @Test
  @UiThreadTest
  public void completeExceptionallyThreaded() {
    final GeckoResult<Integer> deferred = new GeckoResult<>();
    final Throwable boom = new Exception("boom");
    final Thread thread = new Thread(() -> deferred.completeExceptionally(boom));

    deferred.exceptionally(
        error -> {
          assertThat("Exception should match", error, equalTo(boom));
          ThreadUtils.assertOnUiThread();
          done();
          return null;
        });

    thread.start();
    waitUntilDone();
  }

  @Test
  @UiThreadTest
  public void testFinallyException() {
    final GeckoResult<Integer> subject = new GeckoResult<>();
    final Throwable boom = new Exception("boom");

    subject
        .map(
            value -> {
              assertThat("This should not be called", true, equalTo(false));
              return null;
            },
            error -> {
              assertThat("Error matches", error, equalTo(boom));
              return error;
            })
        .finally_(() -> done());

    subject.completeExceptionally(boom);
    waitUntilDone();
  }

  @Test
  @UiThreadTest
  public void testFinallySuccessful() {
    final GeckoResult<Integer> subject = new GeckoResult<>();

    subject.accept(value -> assertThat("Value matches", value, equalTo(42))).finally_(() -> done());

    subject.complete(42);
    waitUntilDone();
  }

  @UiThreadTest
  @Test
  public void resultMapChaining() {
    assertThat(
        "We're on the UI thread",
        Thread.currentThread(),
        equalTo(Looper.getMainLooper().getThread()));

    GeckoResult.fromValue(42)
        .map(
            value -> {
              assertThat("Value should match", value, equalTo(42));
              return "hello";
            })
        .map(
            value -> {
              assertThat("Value should match", value, equalTo("hello"));
              return 42.0f;
            })
        .map(
            value -> {
              assertThat("Value should match", value, equalTo(42.0f));
              throw new Exception("boom");
            })
        .map(
            null,
            error -> {
              assertThat("Error message should match", error.getMessage(), equalTo("boom"));
              return new MockException();
            })
        .accept(
            null,
            exception -> {
              assertThat(
                  "Exception should be MockException", exception, instanceOf(MockException.class));
              done();
            });

    waitUntilDone();
  }

  @UiThreadTest
  @Test
  public void resultChaining() {
    assertThat(
        "We're on the UI thread",
        Thread.currentThread(),
        equalTo(Looper.getMainLooper().getThread()));

    GeckoResult.fromValue(42)
        .then(
            value -> {
              assertThat("Value should match", value, equalTo(42));
              return GeckoResult.fromValue("hello");
            })
        .then(
            value -> {
              assertThat("Value should match", value, equalTo("hello"));
              return GeckoResult.fromValue(42.0f);
            })
        .then(
            value -> {
              assertThat("Value should match", value, equalTo(42.0f));
              return GeckoResult.fromException(new Exception("boom"));
            })
        .exceptionally(
            error -> {
              assertThat("Error message should match", error.getMessage(), equalTo("boom"));
              throw new MockException();
            })
        .accept(
            null,
            exception -> {
              assertThat(
                  "Exception should be MockException", exception, instanceOf(MockException.class));
              done();
            });

    waitUntilDone();
  }

  @UiThreadTest
  @Test
  public void then_propagatedValue() {
    // The first GeckoResult only has an exception listener, so when the value 42 is
    // propagated to subsequent GeckoResult instances, the propagated value is coerced to null.
    GeckoResult.fromValue(42)
        .exceptionally(error -> null)
        .accept(
            value -> {
              assertThat("Propagated value is null", value, nullValue());
              done();
            });

    waitUntilDone();
  }

  @UiThreadTest
  @Test(expected = GeckoResult.UncaughtException.class)
  public void then_uncaughtException() {
    GeckoResult.fromValue(42)
        .then(
            value -> {
              throw new MockException();
            });

    waitUntilDone();
  }

  @UiThreadTest
  @Test(expected = GeckoResult.UncaughtException.class)
  public void then_propagatedUncaughtException() {
    GeckoResult.fromValue(42)
        .then(
            value -> {
              throw new MockException();
            })
        .accept(value -> {});

    waitUntilDone();
  }

  @UiThreadTest
  @Test
  public void then_caughtException() {
    GeckoResult.fromValue(42)
        .then(
            value -> {
              throw new MockException();
            })
        .accept(value -> {})
        .exceptionally(
            exception -> {
              assertThat(
                  "Exception should be expected", exception, instanceOf(MockException.class));
              done();
              return null;
            });

    waitUntilDone();
  }

  @Test(expected = IllegalThreadStateException.class)
  public void noLooperThenThrows() {
    assertThat("We shouldn't have a Looper", Looper.myLooper(), nullValue());
    GeckoResult.fromValue(42).then(value -> null);
  }

  @Test
  public void noLooperPoll() throws Throwable {
    assertThat("We shouldn't have a Looper", Looper.myLooper(), nullValue());
    assertThat("Value should match", GeckoResult.fromValue(42).poll(0), equalTo(42));
  }

  @Test
  public void withHandler() {

    final SynchronousQueue<Handler> queue = new SynchronousQueue<>();
    final Thread thread =
        new Thread(
            () -> {
              Looper.prepare();

              try {
                queue.put(new Handler(Looper.myLooper()));
              } catch (final InterruptedException e) {
                throw new RuntimeException(e);
              }

              Looper.loop();
            });

    thread.start();

    final GeckoResult<Integer> result = GeckoResult.fromValue(42);
    assertThat("We shouldn't have a Looper", result.getLooper(), nullValue());

    try {
      result
          .withHandler(queue.take())
          .accept(
              value -> {
                assertThat("Thread should match", Thread.currentThread(), equalTo(thread));
                assertThat("Value should match", value, equalTo(42));
                Looper.myLooper().quit();
              });

      thread.join();
    } catch (final InterruptedException e) {
      throw new RuntimeException(e);
    }
  }

  @Test
  public void pollCompleteWithValue() throws Throwable {
    assertThat("Value should match", GeckoResult.fromValue(42).poll(0), equalTo(42));
  }

  @Test(expected = MockException.class)
  public void pollCompleteWithError() throws Throwable {
    GeckoResult.fromException(new MockException()).poll(0);
  }

  @Test(expected = TimeoutException.class)
  public void pollTimeout() throws Throwable {
    new GeckoResult<Void>().poll(1);
  }

  @UiThreadTest
  @Test(expected = TimeoutException.class)
  public void pollTimeoutWithLooper() throws Throwable {
    new GeckoResult<Void>().poll(1);
  }

  @UiThreadTest
  @Test(expected = IllegalThreadStateException.class)
  public void pollWithLooper() throws Throwable {
    new GeckoResult<Void>().poll();
  }

  @UiThreadTest
  @Test
  public void cancelNoDelegate() {
    final GeckoResult<Void> result = new GeckoResult<Void>();
    result
        .cancel()
        .accept(
            value -> {
              assertThat("Cancellation should fail", value, equalTo(false));
              done();
            });
    waitUntilDone();
  }

  private GeckoResult<Integer> createCancellableResult() {
    final GeckoResult<Integer> result = new GeckoResult<>();
    result.setCancellationDelegate(
        new GeckoResult.CancellationDelegate() {
          @Override
          public GeckoResult<Boolean> cancel() {
            return GeckoResult.fromValue(true);
          }
        });

    return result;
  }

  @UiThreadTest
  @Test
  public void cancelSuccess() {
    final GeckoResult<Integer> result = createCancellableResult();

    result
        .cancel()
        .accept(
            value -> {
              assertThat("Cancel should succeed", value, equalTo(true));
              result.exceptionally(
                  exception -> {
                    assertThat(
                        "Exception should match",
                        exception,
                        instanceOf(CancellationException.class));
                    done();

                    return null;
                  });
            });

    waitUntilDone();
  }

  @UiThreadTest
  @Test
  public void cancelCompleted() {
    final GeckoResult<Integer> result = createCancellableResult();
    result.complete(42);
    result
        .cancel()
        .accept(
            value -> {
              assertThat("Cancel should fail", value, equalTo(false));
              done();
            });

    waitUntilDone();
  }

  @UiThreadTest
  @Test
  public void cancelParent() {
    final GeckoResult<Integer> result = createCancellableResult();
    final GeckoResult<Integer> result2 = result.then(value -> GeckoResult.fromValue(42));

    result
        .cancel()
        .accept(
            value -> {
              assertThat("Cancel should succeed", value, equalTo(true));
              result2.exceptionally(
                  exception -> {
                    assertThat(
                        "Exception should match",
                        exception,
                        instanceOf(CancellationException.class));
                    done();

                    return null;
                  });
            });

    waitUntilDone();
  }

  @UiThreadTest
  @Test
  public void cancelChildParentNotComplete() {
    final GeckoResult<Integer> result =
        new GeckoResult<Integer>()
            .then(value -> createCancellableResult())
            .then(value -> new GeckoResult<Integer>());

    result
        .cancel()
        .accept(
            value -> {
              assertThat("Cancel should fail", value, equalTo(false));
              done();
            });

    waitUntilDone();
  }

  @UiThreadTest
  @Test
  public void cancelChildParentComplete() {
    final GeckoResult<Integer> result =
        GeckoResult.fromValue(42)
            .then(value -> createCancellableResult())
            .then(value -> new GeckoResult<Integer>());

    final Handler handler = new Handler(Looper.myLooper());
    handler.post(
        () -> {
          result
              .cancel()
              .accept(
                  value -> {
                    assertThat("Cancel should succeed", value, equalTo(true));
                    done();
                  });
        });

    waitUntilDone();
  }

  @UiThreadTest
  @Test
  public void getOrAccept()
      throws NoSuchMethodException, InvocationTargetException, IllegalAccessException {
    final Method ai =
        GeckoResult.class.getDeclaredMethod("getOrAccept", GeckoResult.Consumer.class);
    ai.setAccessible(true);

    final AtomicBoolean ran = new AtomicBoolean(false);
    ai.invoke(GeckoResult.fromValue(42), (GeckoResult.Consumer<Integer>) o -> ran.set(true));
    assertThat("Should've ran", ran.get(), equalTo(true));
  }
}
