/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko.util;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;

import android.os.Bundle;
import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.Map;
import org.junit.After;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;

@RunWith(RobolectricTestRunner.class)
public class DebugConfigTest {
  @Rule public TemporaryFolder mTemp = new TemporaryFolder();

  private String mRuntimeName;

  // snakeyaml reads fields directly only when it detects the Android runtime by this property.
  @Before
  public void setUp() {
    mRuntimeName = System.setProperty("java.runtime.name", "Android Runtime");
  }

  @After
  public void tearDown() {
    System.setProperty("java.runtime.name", mRuntimeName);
  }

  private DebugConfig load(final String yaml) throws Exception {
    final File file = mTemp.newFile("config.yaml");
    Files.write(file.toPath(), yaml.getBytes(StandardCharsets.UTF_8));
    return DebugConfig.fromFile(file);
  }

  @Test
  public void propertyTypes() throws Exception {
    final DebugConfig config =
        load("prefs:\n  a: 1\n  b: true\n  c: x\nenv:\n  NUM: 1\nargs:\n  - 2\n  - -foo\n");

    final Map<String, Object> prefs = config.mergeIntoPrefs(null);
    assertEquals(1, prefs.get("a"));
    assertEquals(true, prefs.get("b"));
    assertEquals("x", prefs.get("c"));

    final Bundle extras = config.mergeIntoExtras(null);
    assertEquals("NUM=1", extras.getString("env0"));

    assertArrayEquals(new String[] {"2", "-foo"}, config.mergeIntoArgs(null));
  }
}
