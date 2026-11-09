/* Any copyright is dedicated to the Public Domain.
http://creativecommons.org/publicdomain/zero/1.0/ */

package org.mozilla.gecko.util;

import static org.junit.Assert.*;

import android.os.Parcel;
import java.util.Arrays;
import java.util.List;
import org.json.JSONException;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;

@RunWith(RobolectricTestRunner.class)
public class GeckoBundleTest {
  private static final int INNER_BUNDLE_SIZE = 28;
  private static final int OUTER_BUNDLE_SIZE = INNER_BUNDLE_SIZE + 6;

  private static GeckoBundle createInnerBundle() {
    final GeckoBundle bundle = new GeckoBundle();

    bundle.putBoolean("boolean", true);
    bundle.putBooleanArray("booleanArray", new boolean[] {false, true});

    bundle.putInt("int", 1);
    bundle.putIntArray("intArray", new int[] {2, 3});

    bundle.putDouble("double", 0.5);
    bundle.putDoubleArray("doubleArray", new double[] {1.5, 2.5});

    bundle.putLong("long", 1L);
    bundle.putLongArray("longArray", new long[] {2L, 3L});

    bundle.putString("string", "foo");
    bundle.putString("nullString", null);
    bundle.putString("emptyString", "");
    bundle.putStringArray("stringArray", new String[] {"bar", "baz"});
    bundle.putStringArray("stringArrayOfNull", new String[2]);

    bundle.putBooleanArray("emptyBooleanArray", new boolean[0]);
    bundle.putIntArray("emptyIntArray", new int[0]);
    bundle.putDoubleArray("emptyDoubleArray", new double[0]);
    bundle.putLongArray("emptyLongArray", new long[0]);
    bundle.putStringArray("emptyStringArray", new String[0]);

    bundle.putBooleanArray("nullBooleanArray", (boolean[]) null);
    bundle.putIntArray("nullIntArray", (int[]) null);
    bundle.putDoubleArray("nullDoubleArray", (double[]) null);
    bundle.putLongArray("nullLongArray", (long[]) null);
    bundle.putStringArray("nullStringArray", (String[]) null);

    bundle.putDoubleArray("mixedArray", new double[] {1.0, 1.5});

    bundle.putInt("byte", 1);
    bundle.putInt("short", 1);
    bundle.putDouble("float", 0.5);
    bundle.putString("char", "f");

    return bundle;
  }

  private static GeckoBundle createBundle() {
    final GeckoBundle outer = createInnerBundle();
    final GeckoBundle inner = createInnerBundle();

    outer.putBundle("object", inner);
    outer.putBundle("nullObject", null);
    outer.putBundleArray("objectArray", new GeckoBundle[] {null, inner});
    outer.putBundleArray("objectArrayOfNull", new GeckoBundle[2]);
    outer.putBundleArray("emptyObjectArray", new GeckoBundle[0]);
    outer.putBundleArray("nullObjectArray", (GeckoBundle[]) null);

    return outer;
  }

  private static void checkInnerBundle(final GeckoBundle bundle, final int expectedSize) {
    assertEquals(expectedSize, bundle.size());

    assertEquals(true, bundle.getBoolean("boolean"));
    assertArrayEquals(new boolean[] {false, true}, bundle.getBooleanArray("booleanArray"));

    assertEquals(1, bundle.getInt("int"));
    assertArrayEquals(new int[] {2, 3}, bundle.getIntArray("intArray"));

    assertEquals(0.5, bundle.getDouble("double"), 0.0);
    assertArrayEquals(new double[] {1.5, 2.5}, bundle.getDoubleArray("doubleArray"), 0.0);

    assertEquals(1L, bundle.getLong("long"));
    assertArrayEquals(new long[] {2L, 3L}, bundle.getLongArray("longArray"));

    assertEquals("foo", bundle.getString("string"));
    assertEquals(null, bundle.getString("nullString"));
    assertEquals("", bundle.getString("emptyString"));
    assertArrayEquals(new String[] {"bar", "baz"}, bundle.getStringArray("stringArray"));
    assertArrayEquals(new String[2], bundle.getStringArray("stringArrayOfNull"));

    assertArrayEquals(new boolean[0], bundle.getBooleanArray("emptyBooleanArray"));
    assertArrayEquals(new int[0], bundle.getIntArray("emptyIntArray"));
    assertArrayEquals(new double[0], bundle.getDoubleArray("emptyDoubleArray"), 0.0);
    assertArrayEquals(new long[0], bundle.getLongArray("emptyLongArray"));
    assertArrayEquals(new String[0], bundle.getStringArray("emptyStringArray"));

    assertArrayEquals(null, bundle.getBooleanArray("nullBooleanArray"));
    assertArrayEquals(null, bundle.getIntArray("nullIntArray"));
    assertArrayEquals(null, bundle.getDoubleArray("nullDoubleArray"), 0.0);
    assertArrayEquals(null, bundle.getLongArray("nullLongArray"));
    assertArrayEquals(null, bundle.getStringArray("nullStringArray"));

    assertArrayEquals(new double[] {1.0, 1.5}, bundle.getDoubleArray("mixedArray"), 0.0);

    assertEquals(1, bundle.getInt("byte"));
    assertEquals(1, bundle.getInt("short"));
    assertEquals(0.5, bundle.getDouble("float"), 0.0);
    assertEquals("f", bundle.getString("char"));
  }

  private static void checkBundle(final GeckoBundle bundle) {
    checkInnerBundle(bundle, OUTER_BUNDLE_SIZE);

    checkInnerBundle(bundle.getBundle("object"), INNER_BUNDLE_SIZE);
    assertEquals(null, bundle.getBundle("nullObject"));

    final GeckoBundle[] array = bundle.getBundleArray("objectArray");
    assertNotNull(array);
    assertEquals(2, array.length);
    assertEquals(null, array[0]);
    checkInnerBundle(array[1], INNER_BUNDLE_SIZE);

    assertArrayEquals(new GeckoBundle[2], bundle.getBundleArray("objectArrayOfNull"));
    assertArrayEquals(new GeckoBundle[0], bundle.getBundleArray("emptyObjectArray"));
    assertArrayEquals(null, bundle.getBundleArray("nullObjectArray"));
  }

  private GeckoBundle reference;

  @Before
  public void prepareReference() {
    reference = createBundle();
  }

  @Test
  public void canConstructWithCapacity() {
    new GeckoBundle(0);
    new GeckoBundle(1);
    new GeckoBundle(42);

    try {
      new GeckoBundle(-1);
      fail("Should throw with -1 capacity");
    } catch (final Exception e) {
      assertTrue(true);
    }
  }

  @Test
  public void canConstructWithBundle() {
    assertEquals(reference, new GeckoBundle(reference));

    try {
      new GeckoBundle(null);
      fail("Should throw with null bundle");
    } catch (final Exception e) {
      assertTrue(true);
    }
  }

  @Test
  public void referenceShouldBeCorrect() {
    checkBundle(reference);
  }

  @Test
  public void equalsShouldReturnCorrectResult() {
    assertTrue(reference.equals(reference));
    assertFalse(reference.equals(null));

    assertTrue(reference.equals(new GeckoBundle(reference)));
    assertFalse(reference.equals(new GeckoBundle()));
  }

  @Test
  public void toStringShouldNotReturnEmptyString() {
    assertNotNull(reference.toString());
    assertNotEquals("", reference.toString());
  }

  @Test
  public void hashCodeShouldNotReturnZero() {
    assertNotEquals(0, reference.hashCode());
  }

  private static void testRemove(final GeckoBundle bundle, final String key) {
    if (bundle.get(key) != null) {
      assertTrue(String.format("%s should exist", key), bundle.containsKey(key));
    } else {
      assertFalse(String.format("%s should not exist", key), bundle.containsKey(key));
    }
    bundle.remove(key);
    assertFalse(String.format("%s should not exist", key), bundle.containsKey(key));
  }

  @Test
  public void containsKeyAndRemoveShouldWork() {
    final GeckoBundle test = new GeckoBundle(reference);

    testRemove(test, "nonexistent");
    testRemove(test, "boolean");
    testRemove(test, "booleanArray");
    testRemove(test, "int");
    testRemove(test, "intArray");
    testRemove(test, "double");
    testRemove(test, "doubleArray");
    testRemove(test, "long");
    testRemove(test, "longArray");
    testRemove(test, "string");
    testRemove(test, "nullString");
    testRemove(test, "emptyString");
    testRemove(test, "stringArray");
    testRemove(test, "stringArrayOfNull");
    testRemove(test, "emptyBooleanArray");
    testRemove(test, "emptyIntArray");
    testRemove(test, "emptyDoubleArray");
    testRemove(test, "emptyLongArray");
    testRemove(test, "emptyStringArray");
    testRemove(test, "nullBooleanArray");
    testRemove(test, "nullIntArray");
    testRemove(test, "nullDoubleArray");
    testRemove(test, "nullLongArray");
    testRemove(test, "nullStringArray");
    testRemove(test, "mixedArray");
    testRemove(test, "byte");
    testRemove(test, "short");
    testRemove(test, "float");
    testRemove(test, "char");
    testRemove(test, "object");
    testRemove(test, "nullObject");
    testRemove(test, "objectArray");
    testRemove(test, "objectArrayOfNull");
    testRemove(test, "emptyObjectArray");
    testRemove(test, "nullObjectArray");

    assertEquals(0, test.size());
  }

  @Test
  public void clearShouldWork() {
    final GeckoBundle test = new GeckoBundle(reference);
    assertNotEquals(0, test.size());
    test.clear();
    assertEquals(0, test.size());
  }

  @Test
  public void keysShouldReturnCorrectResult() {
    final String[] actual = reference.keys();
    final String[] expected =
        new String[] {
          "boolean",
          "booleanArray",
          "int",
          "intArray",
          "double",
          "doubleArray",
          "long",
          "longArray",
          "string",
          "nullString",
          "emptyString",
          "stringArray",
          "stringArrayOfNull",
          "emptyBooleanArray",
          "emptyIntArray",
          "emptyDoubleArray",
          "emptyLongArray",
          "emptyStringArray",
          "nullBooleanArray",
          "nullIntArray",
          "nullDoubleArray",
          "nullLongArray",
          "nullStringArray",
          "mixedArray",
          "byte",
          "short",
          "float",
          "char",
          "object",
          "nullObject",
          "objectArray",
          "objectArrayOfNull",
          "emptyObjectArray",
          "nullObjectArray"
        };

    Arrays.sort(expected);
    Arrays.sort(actual);

    assertArrayEquals(expected, actual);
  }

  @Test
  public void isEmptyShouldReturnCorrectResult() {
    assertFalse(reference.isEmpty());
    assertTrue(new GeckoBundle().isEmpty());
  }

  @Test
  public void getExistentKeysShouldNotReturnDefaultValues() {
    assertNotEquals(false, reference.getBoolean("boolean", false));
    assertNotEquals(0, reference.getInt("int", 0));
    assertNotEquals(0.0, reference.getDouble("double", 0.0), 0.0);
    assertNotEquals(0L, reference.getLong("long", 0L));
    assertNotEquals("", reference.getString("string", ""));
  }

  private static void testDefaultValueForNull(final GeckoBundle bundle, final String key) {
    // We return default values for null values.
    assertEquals(true, bundle.getBoolean(key, true));
    assertEquals(1, bundle.getInt(key, 1));
    assertEquals(0.5, bundle.getDouble(key, 0.5), 0.0);
    assertEquals("foo", bundle.getString(key, "foo"));
  }

  @Test
  public void getNonexistentKeysShouldReturnDefaultValues() {
    assertEquals(null, reference.get("nonexistent"));

    assertEquals(false, reference.getBoolean("nonexistent"));
    assertEquals(true, reference.getBoolean("nonexistent", true));
    assertEquals(0, reference.getInt("nonexistent"));
    assertEquals(1, reference.getInt("nonexistent", 1));
    assertEquals(0.0, reference.getDouble("nonexistent"), 0.0);
    assertEquals(0.5, reference.getDouble("nonexistent", 0.5), 0.0);
    assertEquals(null, reference.getString("nonexistent"));
    assertEquals("foo", reference.getString("nonexistent", "foo"));
    assertEquals(null, reference.getBundle("nonexistent"));

    assertArrayEquals(null, reference.getBooleanArray("nonexistent"));
    assertArrayEquals(null, reference.getIntArray("nonexistent"));
    assertArrayEquals(null, reference.getDoubleArray("nonexistent"), 0.0);
    assertArrayEquals(null, reference.getLongArray("nonexistent"));
    assertArrayEquals(null, reference.getStringArray("nonexistent"));
    assertArrayEquals(null, reference.getBundleArray("nonexistent"));

    // We return default values for null values.
    testDefaultValueForNull(reference, "nullObject");
    testDefaultValueForNull(reference, "nullString");
    testDefaultValueForNull(reference, "nullBooleanArray");
    testDefaultValueForNull(reference, "nullIntArray");
    testDefaultValueForNull(reference, "nullDoubleArray");
    testDefaultValueForNull(reference, "nullLongArray");
    testDefaultValueForNull(reference, "nullStringArray");
    testDefaultValueForNull(reference, "nullObjectArray");
  }

  @Test
  public void bundleConversionShouldWork() {
    assertEquals(reference, GeckoBundle.fromBundle(reference.toBundle()));
  }

  @Test
  public void jsonConversionShouldWork() throws JSONException {
    assertEquals(reference, GeckoBundle.fromJSONObject(reference.toJSONObject()));
  }

  @Test
  public void parcelConversionShouldWork() {
    final Parcel parcel = Parcel.obtain();

    reference.writeToParcel(parcel, 0);
    reference.writeToParcel(parcel, 0);
    parcel.setDataPosition(0);

    assertEquals(reference, GeckoBundle.CREATOR.createFromParcel(parcel));

    final GeckoBundle test = new GeckoBundle();
    test.readFromParcel(parcel);
    assertEquals(reference, test);

    parcel.recycle();
  }

  private static void testInvalidCoercions(
      final GeckoBundle bundle, final String key, final String... exceptions) {
    final List<String> allowed;
    if (exceptions == null) {
      allowed = Arrays.asList(key);
    } else {
      allowed = Arrays.asList(Arrays.copyOf(exceptions, exceptions.length + 1));
      allowed.set(exceptions.length, key);
    }

    if (!allowed.contains("boolean")) {
      try {
        bundle.getBoolean(key);
        fail(String.format("%s should not coerce to boolean", key));
      } catch (final Exception e) {
        assertTrue(true);
      }
    }

    if (!allowed.contains("booleanArray")
        && !allowed.contains("emptyBooleanArray")
        && !allowed.contains("nullBooleanArray")) {
      try {
        bundle.getBooleanArray(key);
        fail(String.format("%s should not coerce to boolean array", key));
      } catch (final Exception e) {
        assertTrue(true);
      }
    }

    if (!allowed.contains("int")) {
      try {
        bundle.getInt(key);
        fail(String.format("%s should not coerce to int", key));
      } catch (final Exception e) {
        assertTrue(true);
      }
    }

    if (!allowed.contains("intArray")
        && !allowed.contains("emptyIntArray")
        && !allowed.contains("nullIntArray")) {
      try {
        bundle.getIntArray(key);
        fail(String.format("%s should not coerce to int array", key));
      } catch (final Exception e) {
        assertTrue(true);
      }
    }

    if (!allowed.contains("double")) {
      try {
        bundle.getDouble(key);
        fail(String.format("%s should not coerce to double", key));
      } catch (final Exception e) {
        assertTrue(true);
      }
    }

    if (!allowed.contains("doubleArray")
        && !allowed.contains("emptyDoubleArray")
        && !allowed.contains("nullDoubleArray")) {
      try {
        bundle.getDoubleArray(key);
        fail(String.format("%s should not coerce to double array", key));
      } catch (final Exception e) {
        assertTrue(true);
      }
    }

    if (!allowed.contains("long")) {
      try {
        bundle.getLong(key);
        fail(String.format("%s should not coerce to long", key));
      } catch (final Exception e) {
        assertTrue(true);
      }
    }

    if (!allowed.contains("longArray")
        && !allowed.contains("emptyLongArray")
        && !allowed.contains("nullLongArray")) {
      try {
        bundle.getLongArray(key);
        fail(String.format("%s should not coerce to long array", key));
      } catch (final Exception e) {
        assertTrue(true);
      }
    }

    if (!allowed.contains("string") && !allowed.contains("nullString")) {
      try {
        bundle.getString(key);
        fail(String.format("%s should not coerce to string", key));
      } catch (final Exception e) {
        assertTrue(true);
      }
    }

    if (!allowed.contains("stringArray")
        && !allowed.contains("emptyStringArray")
        && !allowed.contains("nullStringArray")
        && !allowed.contains("stringArrayOfNull")) {
      try {
        bundle.getStringArray(key);
        fail(String.format("%s should not coerce to string array", key));
      } catch (final Exception e) {
        assertTrue(true);
      }
    }

    if (!allowed.contains("object") && !allowed.contains("nullObject")) {
      try {
        bundle.getBundle(key);
        fail(String.format("%s should not coerce to bundle", key));
      } catch (final Exception e) {
        assertTrue(true);
      }
    }

    if (!allowed.contains("objectArray")
        && !allowed.contains("emptyObjectArray")
        && !allowed.contains("nullObjectArray")
        && !allowed.contains("objectArrayOfNull")) {
      try {
        bundle.getBundleArray(key);
        fail(String.format("%s should not coerce to bundle array", key));
      } catch (final Exception e) {
        assertTrue(true);
      }
    }
  }

  @Test
  public void booleanShouldNotCoerceToOtherTypes() {
    testInvalidCoercions(reference, "boolean");
  }

  @Test
  public void booleanArrayShouldNotCoerceToOtherTypes() {
    testInvalidCoercions(reference, "booleanArray");
  }

  @Test
  public void intShouldCoerceToDouble() {
    assertEquals(1.0, reference.getDouble("int"), 0.0);
    assertArrayEquals(new double[] {2.0, 3.0}, reference.getDoubleArray("intArray"), 0.0);
  }

  @Test
  public void intShouldCoerceToLong() {
    assertEquals(1L, reference.getLong("int"));
    assertArrayEquals(new long[] {2L, 3L}, reference.getLongArray("intArray"));
  }

  @Test
  public void intShouldNotCoerceToOtherTypes() {
    testInvalidCoercions(reference, "int", /* except */ "double", "long");
    testInvalidCoercions(reference, "intArray", /* except */ "doubleArray", "longArray");
  }

  @Test
  public void doubleShouldCoerceToInt() {
    assertEquals(0, reference.getInt("double"));
    assertArrayEquals(new int[] {1, 2}, reference.getIntArray("doubleArray"));
  }

  @Test
  public void doubleShouldCoerceToLong() {
    assertEquals(0L, reference.getLong("double"));
    assertArrayEquals(new long[] {1L, 2L}, reference.getLongArray("doubleArray"));
  }

  @Test
  public void doubleShouldNotCoerceToOtherTypes() {
    testInvalidCoercions(reference, "double", /* except */ "int", "long");
    testInvalidCoercions(reference, "doubleArray", /* except */ "intArray", "longArray");
  }

  @Test
  public void longShouldCoerceToInt() {
    assertEquals(1, reference.getInt("long"));
    assertArrayEquals(new int[] {2, 3}, reference.getIntArray("longArray"));
  }

  @Test
  public void longShouldCoerceToDouble() {
    assertEquals(1.0, reference.getDouble("long"), 0.0);
    assertArrayEquals(new double[] {2.0, 3.0}, reference.getDoubleArray("longArray"), 0.0);
  }

  @Test
  public void longShouldNotCoerceToOtherTypes() {
    testInvalidCoercions(reference, "long", /* except */ "int", "double");
    testInvalidCoercions(reference, "longArray", /* except */ "intArray", "doubleArray");
  }

  @Test
  public void nullStringShouldCoerceToBundle() {
    assertEquals(null, reference.getBundle("nullString"));
    assertArrayEquals(new GeckoBundle[2], reference.getBundleArray("stringArrayOfNull"));
  }

  @Test
  public void nullStringShouldNotCoerceToOtherTypes() {
    testInvalidCoercions(reference, "stringArrayOfNull", /* except */ "objectArrayOfNull");
  }

  @Test
  public void nonNullStringShouldNotCoerceToOtherTypes() {
    testInvalidCoercions(reference, "string");
  }

  @Test
  public void nullBundleShouldCoerceToString() {
    assertEquals(null, reference.getString("nullObject"));
    assertArrayEquals(new String[2], reference.getStringArray("objectArrayOfNull"));
  }

  @Test
  public void nullBundleShouldNotCoerceToOtherTypes() {
    testInvalidCoercions(reference, "objectArrayOfNull", /* except */ "stringArrayOfNull");
  }

  @Test
  public void nonNullBundleShouldNotCoerceToOtherTypes() {
    testInvalidCoercions(reference, "object");
  }

  @Test
  public void emptyArrayShouldCoerceToAnyArray() {
    assertArrayEquals(new int[0], reference.getIntArray("emptyBooleanArray"));
    assertArrayEquals(new double[0], reference.getDoubleArray("emptyBooleanArray"), 0.0);
    assertArrayEquals(new long[0], reference.getLongArray("emptyBooleanArray"));
    assertArrayEquals(new String[0], reference.getStringArray("emptyBooleanArray"));
    assertArrayEquals(new GeckoBundle[0], reference.getBundleArray("emptyBooleanArray"));

    assertArrayEquals(new boolean[0], reference.getBooleanArray("emptyIntArray"));
    assertArrayEquals(new double[0], reference.getDoubleArray("emptyIntArray"), 0.0);
    assertArrayEquals(new long[0], reference.getLongArray("emptyIntArray"));
    assertArrayEquals(new String[0], reference.getStringArray("emptyIntArray"));
    assertArrayEquals(new GeckoBundle[0], reference.getBundleArray("emptyIntArray"));

    assertArrayEquals(new boolean[0], reference.getBooleanArray("emptyDoubleArray"));
    assertArrayEquals(new int[0], reference.getIntArray("emptyDoubleArray"));
    assertArrayEquals(new long[0], reference.getLongArray("emptyDoubleArray"));
    assertArrayEquals(new String[0], reference.getStringArray("emptyDoubleArray"));
    assertArrayEquals(new GeckoBundle[0], reference.getBundleArray("emptyDoubleArray"));

    assertArrayEquals(new boolean[0], reference.getBooleanArray("emptyLongArray"));
    assertArrayEquals(new int[0], reference.getIntArray("emptyLongArray"));
    assertArrayEquals(new double[0], reference.getDoubleArray("emptyLongArray"), 0.0);
    assertArrayEquals(new String[0], reference.getStringArray("emptyLongArray"));
    assertArrayEquals(new GeckoBundle[0], reference.getBundleArray("emptyLongArray"));

    assertArrayEquals(new boolean[0], reference.getBooleanArray("emptyStringArray"));
    assertArrayEquals(new int[0], reference.getIntArray("emptyStringArray"));
    assertArrayEquals(new double[0], reference.getDoubleArray("emptyStringArray"), 0.0);
    assertArrayEquals(new long[0], reference.getLongArray("emptyStringArray"));
    assertArrayEquals(new GeckoBundle[0], reference.getBundleArray("emptyStringArray"));

    assertArrayEquals(new boolean[0], reference.getBooleanArray("emptyObjectArray"));
    assertArrayEquals(new int[0], reference.getIntArray("emptyObjectArray"));
    assertArrayEquals(new double[0], reference.getDoubleArray("emptyObjectArray"), 0.0);
    assertArrayEquals(new long[0], reference.getLongArray("emptyObjectArray"));
    assertArrayEquals(new String[0], reference.getStringArray("emptyObjectArray"));
  }

  @Test
  public void emptyArrayShouldNotCoerceToOtherTypes() {
    testInvalidCoercions(
        reference,
        "emptyBooleanArray", /* except */
        "intArray",
        "doubleArray",
        "longArray",
        "stringArray",
        "objectArray");
    testInvalidCoercions(
        reference,
        "emptyIntArray", /* except */
        "booleanArray",
        "doubleArray",
        "longArray",
        "stringArray",
        "objectArray");
    testInvalidCoercions(
        reference,
        "emptyDoubleArray", /* except */
        "booleanArray",
        "intArray",
        "longArray",
        "stringArray",
        "objectArray");
    testInvalidCoercions(
        reference,
        "emptyLongArray", /* except */
        "booleanArray",
        "intArray",
        "doubleArray",
        "stringArray",
        "objectArray");
    testInvalidCoercions(
        reference,
        "emptyStringArray", /* except */
        "booleanArray",
        "intArray",
        "doubleArray",
        "longArray",
        "objectArray");
    testInvalidCoercions(
        reference,
        "emptyObjectArray", /* except */
        "booleanArray",
        "intArray",
        "doubleArray",
        "longArray",
        "stringArray");
  }

  @Test
  public void nullArrayShouldCoerceToAnyArray() {
    assertArrayEquals(null, reference.getIntArray("nullBooleanArray"));
    assertArrayEquals(null, reference.getDoubleArray("nullBooleanArray"), 0.0);
    assertArrayEquals(null, reference.getLongArray("nullBooleanArray"));
    assertArrayEquals(null, reference.getStringArray("nullBooleanArray"));
    assertArrayEquals(null, reference.getBundleArray("nullBooleanArray"));

    assertArrayEquals(null, reference.getBooleanArray("nullIntArray"));
    assertArrayEquals(null, reference.getDoubleArray("nullIntArray"), 0.0);
    assertArrayEquals(null, reference.getLongArray("nullIntArray"));
    assertArrayEquals(null, reference.getStringArray("nullIntArray"));
    assertArrayEquals(null, reference.getBundleArray("nullIntArray"));

    assertArrayEquals(null, reference.getBooleanArray("nullDoubleArray"));
    assertArrayEquals(null, reference.getIntArray("nullDoubleArray"));
    assertArrayEquals(null, reference.getLongArray("nullDoubleArray"));
    assertArrayEquals(null, reference.getStringArray("nullDoubleArray"));
    assertArrayEquals(null, reference.getBundleArray("nullDoubleArray"));

    assertArrayEquals(null, reference.getBooleanArray("nullLongArray"));
    assertArrayEquals(null, reference.getIntArray("nullLongArray"));
    assertArrayEquals(null, reference.getDoubleArray("nullLongArray"), 0.0);
    assertArrayEquals(null, reference.getStringArray("nullLongArray"));
    assertArrayEquals(null, reference.getBundleArray("nullLongArray"));

    assertArrayEquals(null, reference.getBooleanArray("nullStringArray"));
    assertArrayEquals(null, reference.getIntArray("nullStringArray"));
    assertArrayEquals(null, reference.getDoubleArray("nullStringArray"), 0.0);
    assertArrayEquals(null, reference.getLongArray("nullStringArray"));
    assertArrayEquals(null, reference.getBundleArray("nullStringArray"));

    assertArrayEquals(null, reference.getBooleanArray("nullObjectArray"));
    assertArrayEquals(null, reference.getIntArray("nullObjectArray"));
    assertArrayEquals(null, reference.getDoubleArray("nullObjectArray"), 0.0);
    assertArrayEquals(null, reference.getLongArray("nullObjectArray"));
    assertArrayEquals(null, reference.getStringArray("nullObjectArray"));
  }
}
