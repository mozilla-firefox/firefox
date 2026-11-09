/* Any copyright is dedicated to the Public Domain.
http://creativecommons.org/publicdomain/zero/1.0/ */

package org.mozilla.geckoview.test;

import static org.hamcrest.CoreMatchers.is;
import static org.hamcrest.CoreMatchers.nullValue;
import static org.hamcrest.MatcherAssert.assertThat;

import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.filters.MediumTest;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.Set;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mozilla.gecko.MultiMap;

@RunWith(AndroidJUnit4.class)
@MediumTest
public class MultiMapTest {
  @Test
  public void emptyMap() {
    final MultiMap<String, String> map = new MultiMap<>();

    assertThat(map.get("not-present").isEmpty(), is(true));
    assertThat(map.containsKey("not-present"), is(false));
    assertThat(map.containsEntry("not-present", "nope"), is(false));
    assertThat(map.size(), is(0));
    assertThat(map.asMap().size(), is(0));
    assertThat(map.remove("not-present"), nullValue());
    assertThat(map.remove("not-present", "nope"), is(false));
    assertThat(map.keySet().size(), is(0));

    map.clear();
  }

  @Test
  public void emptyMapWithCapacity() {
    final MultiMap<String, String> map = new MultiMap<>(10);

    assertThat(map.get("not-present").isEmpty(), is(true));
    assertThat(map.containsKey("not-present"), is(false));
    assertThat(map.containsEntry("not-present", "nope"), is(false));
    assertThat(map.size(), is(0));
    assertThat(map.asMap().size(), is(0));
    assertThat(map.remove("not-present"), nullValue());
    assertThat(map.remove("not-present", "nope"), is(false));
    assertThat(map.keySet().size(), is(0));

    map.clear();
  }

  @Test
  public void addMultipleValues() {
    final MultiMap<String, String> map = new MultiMap<>();
    map.add("test", "value1");
    map.add("test", "value2");
    map.add("test2", "value3");

    assertThat(map.containsEntry("test", "value1"), is(true));
    assertThat(map.containsEntry("test", "value2"), is(true));
    assertThat(map.containsEntry("test2", "value3"), is(true));

    assertThat(map.containsEntry("test3", "value1"), is(false));
    assertThat(map.containsEntry("test", "value3"), is(false));

    final List<String> values = map.get("test");
    assertThat(values.contains("value1"), is(true));
    assertThat(values.contains("value2"), is(true));
    assertThat(values.contains("value3"), is(false));
    assertThat(values.size(), is(2));

    final List<String> values2 = map.get("test2");
    assertThat(values2.contains("value1"), is(false));
    assertThat(values2.contains("value2"), is(false));
    assertThat(values2.contains("value3"), is(true));
    assertThat(values2.size(), is(1));

    assertThat(map.size(), is(2));
  }

  @Test
  public void remove() {
    final MultiMap<String, String> map = new MultiMap<>();
    map.add("test", "value1");
    map.add("test", "value2");
    map.add("test2", "value3");

    assertThat(map.size(), is(2));

    final List<String> values = map.remove("test");

    assertThat(values.size(), is(2));
    assertThat(values.contains("value1"), is(true));
    assertThat(values.contains("value2"), is(true));

    assertThat(map.size(), is(1));

    assertThat(map.containsKey("test"), is(false));
    assertThat(map.containsEntry("test", "value1"), is(false));
    assertThat(map.containsEntry("test", "value2"), is(false));
    assertThat(map.get("test").size(), is(0));

    assertThat(map.get("test2").size(), is(1));
    assertThat(map.get("test2").contains("value3"), is(true));
    assertThat(map.containsEntry("test2", "value3"), is(true));
  }

  @Test
  public void removeAllValuesRemovesKey() {
    final MultiMap<String, String> map = new MultiMap<>();
    map.add("test", "value1");
    map.add("test", "value2");
    map.add("test2", "value3");

    assertThat(map.remove("test", "value1"), is(true));
    assertThat(map.containsEntry("test", "value1"), is(false));
    assertThat(map.containsEntry("test", "value2"), is(true));
    assertThat(map.get("test").size(), is(1));
    assertThat(map.get("test").contains("value2"), is(true));

    assertThat(map.remove("test", "value2"), is(true));

    assertThat(map.remove("test", "value3"), is(false));
    assertThat(map.remove("test2", "value4"), is(false));

    assertThat(map.containsKey("test"), is(false));
    assertThat(map.containsKey("test2"), is(true));
  }

  @Test
  public void keySet() {
    final MultiMap<String, String> map = new MultiMap<>();
    map.add("test", "value1");
    map.add("test", "value2");
    map.add("test2", "value3");

    final Set<String> keys = map.keySet();

    assertThat(keys.size(), is(2));
    assertThat(keys.contains("test"), is(true));
    assertThat(keys.contains("test2"), is(true));
  }

  @Test
  public void clear() {
    final MultiMap<String, String> map = new MultiMap<>();
    map.add("test", "value1");
    map.add("test", "value2");
    map.add("test2", "value3");

    assertThat(map.size(), is(2));

    map.clear();

    assertThat(map.size(), is(0));
    assertThat(map.containsKey("test"), is(false));
    assertThat(map.containsKey("test2"), is(false));
    assertThat(map.containsEntry("test", "value1"), is(false));
    assertThat(map.containsEntry("test", "value2"), is(false));
    assertThat(map.containsEntry("test2", "value3"), is(false));
  }

  @Test
  public void asMap() {
    final MultiMap<String, String> map = new MultiMap<>();
    map.add("test", "value1");
    map.add("test", "value2");
    map.add("test2", "value3");

    final Map<String, List<String>> asMap = map.asMap();

    assertThat(asMap.size(), is(2));

    assertThat(asMap.get("test").size(), is(2));
    assertThat(asMap.get("test").contains("value1"), is(true));
    assertThat(asMap.get("test").contains("value2"), is(true));

    assertThat(asMap.get("test2").size(), is(1));
    assertThat(asMap.get("test2").contains("value3"), is(true));
  }

  @Test
  public void addAll() {
    final MultiMap<String, String> map = new MultiMap<>();
    map.add("test", "value1");

    assertThat(map.get("test").size(), is(1));

    // Existing key test
    final List<String> values = map.addAll("test", Arrays.asList("value2", "value3"));

    assertThat(values.size(), is(3));
    assertThat(values.contains("value1"), is(true));
    assertThat(values.contains("value2"), is(true));
    assertThat(values.contains("value3"), is(true));

    assertThat(map.containsEntry("test", "value1"), is(true));
    assertThat(map.containsEntry("test", "value2"), is(true));
    assertThat(map.containsEntry("test", "value3"), is(true));

    // New key test
    final List<String> values2 = map.addAll("test2", Arrays.asList("value4", "value5"));
    assertThat(values2.size(), is(2));
    assertThat(values2.contains("value4"), is(true));
    assertThat(values2.contains("value5"), is(true));

    assertThat(map.containsEntry("test2", "value4"), is(true));
    assertThat(map.containsEntry("test2", "value5"), is(true));
  }
}
