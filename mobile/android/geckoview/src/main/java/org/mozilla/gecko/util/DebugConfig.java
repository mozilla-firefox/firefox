/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko.util;

import android.os.Bundle;
import android.util.Log;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import java.io.Closeable;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import org.mozilla.gecko.annotation.ReflectionTarget;
import org.yaml.snakeyaml.LoaderOptions;
import org.yaml.snakeyaml.TypeDescription;
import org.yaml.snakeyaml.Yaml;
import org.yaml.snakeyaml.constructor.Constructor;
import org.yaml.snakeyaml.error.YAMLException;

// Raptor writes a *-config.yaml file to specify Gecko runtime settings (e.g.
// the profile dir). This file gets deserialized into a DebugConfig object.
// Yaml uses reflection to create this class so we have to tell PG to keep it.
@ReflectionTarget
public class DebugConfig {
  private static final String LOGTAG = "GeckoDebugConfig";

  protected Map<String, Object> prefs;
  protected Map<String, String> env;
  protected List<String> args;

  public static class ConfigException extends RuntimeException {
    private static final long serialVersionUID = 1L;

    public ConfigException(final String message) {
      super(message);
    }
  }

  public static @NonNull DebugConfig fromFile(final @NonNull File configFile)
      throws FileNotFoundException {
    final LoaderOptions options = new LoaderOptions();
    final Constructor constructor = new Constructor(DebugConfig.class, options);
    final TypeDescription description = new TypeDescription(DebugConfig.class);
    description.putMapPropertyType("prefs", String.class, Object.class);
    description.putMapPropertyType("env", String.class, String.class);
    description.putListPropertyType("args", String.class);

    final Yaml yaml = new Yaml(constructor);
    yaml.addTypeDescription(description);

    final FileInputStream fileInputStream = new FileInputStream(configFile);
    try {
      return yaml.load(fileInputStream);
    } catch (final YAMLException e) {
      throw new ConfigException(e.getMessage());
    } finally {
      try {
        if (fileInputStream != null) {
          ((Closeable) fileInputStream).close();
        }
      } catch (final IOException e) {
      }
    }
  }

  @Nullable
  public Bundle mergeIntoExtras(final @Nullable Bundle extras) {
    if (env == null) {
      return extras;
    }

    Log.d(LOGTAG, "Adding environment variables from debug config: " + env);

    final Bundle result = extras != null ? extras : new Bundle();

    int c = 0;
    while (result.getString("env" + c) != null) {
      c += 1;
    }

    for (final Map.Entry<String, String> entry : env.entrySet()) {
      result.putString("env" + c, entry.getKey() + "=" + entry.getValue());
      c += 1;
    }

    return result;
  }

  @Nullable
  public String[] mergeIntoArgs(final @Nullable String[] initArgs) {
    if (args == null) {
      return initArgs;
    }

    Log.d(LOGTAG, "Adding arguments from debug config: " + args);

    final ArrayList<String> combinedArgs = new ArrayList<>();
    if (initArgs != null) {
      combinedArgs.addAll(Arrays.asList(initArgs));
    }
    combinedArgs.addAll(args);

    return combinedArgs.toArray(new String[combinedArgs.size()]);
  }

  @Nullable
  public Map<String, Object> mergeIntoPrefs(final @Nullable Map<String, Object> initPrefs) {
    if (prefs == null) {
      return initPrefs;
    }

    Log.d(LOGTAG, "Adding prefs from debug config: " + prefs);

    final Map<String, Object> combinedPrefs = new HashMap<>();
    if (initPrefs != null) {
      combinedPrefs.putAll(initPrefs);
    }
    combinedPrefs.putAll(prefs);

    return Collections.unmodifiableMap(combinedPrefs);
  }
}
