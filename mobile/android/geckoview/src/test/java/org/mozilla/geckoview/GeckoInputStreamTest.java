/* Any copyright is dedicated to the Public Domain.
http://creativecommons.org/publicdomain/zero/1.0/ */

package org.mozilla.geckoview;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;

import java.io.IOException;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;

@RunWith(RobolectricTestRunner.class)
public class GeckoInputStreamTest {
  private static final int MAX = (int) GeckoInputStream.MAX_BUFFERED_BYTES;
  private static final int RESUME = (int) GeckoInputStream.RESUME_BUFFERED_BYTES;

  private static class TestStream extends GeckoInputStream {
    int resumeCount;

    TestStream() {
      super(null);
    }

    @Override
    void resumeRequest() {
      resumeCount++;
    }
  }

  private static void startReading(final TestStream stream) {
    // The first read on an empty stream resumes the initially suspended request.
    stream.setReadTimeoutMillis(1);
    try {
      stream.read(new byte[1]);
      fail("Expected the read to time out");
    } catch (final IOException e) {
      // Expected.
    }
    stream.setReadTimeoutMillis(0);
    assertEquals(1, stream.resumeCount);
  }

  @Test
  public void suspendsAndResumes() throws IOException {
    final TestStream stream = new TestStream();
    startReading(stream);

    stream.appendBuffer(new byte[MAX - 1]);
    assertFalse("Below the buffer limit, don't suspend", stream.shouldSuspend());

    stream.appendBuffer(new byte[1]);
    assertTrue("Should suspend once the buffer limit is reached", stream.shouldSuspend());
    assertFalse("Only ask to suspend once until resumed", stream.shouldSuspend());

    final byte[] dest = new byte[(MAX - RESUME) / 2];
    stream.read(dest);
    assertEquals("Should not resume before the buffer is drained", 1, stream.resumeCount);
    stream.read(dest);
    assertEquals("Should resume once the buffer is drained", 2, stream.resumeCount);

    stream.appendBuffer(new byte[MAX]);
    assertTrue(stream.shouldSuspend());
  }

  @Test
  public void doesNotSuspendToStart() throws IOException {
    final TestStream stream = new TestStream();
    stream.appendBuffer(new byte[2 * MAX]);
    assertFalse(stream.shouldSuspend());
  }

  @Test
  public void doesNotSuspendWhenClosed() throws IOException {
    final TestStream stream = new TestStream();
    startReading(stream);

    stream.appendBuffer(new byte[MAX]);
    stream.close();
    assertFalse(stream.shouldSuspend());
  }
}
